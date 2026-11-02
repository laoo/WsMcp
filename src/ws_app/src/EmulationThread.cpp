#include "EmulationThread.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

namespace ws::app
{

namespace
{

/// How long the thread sleeps at most while it has nothing to run, so that it
/// notices a pause lifted.
constexpr std::chrono::milliseconds IDLE_WAIT{ 20 };

/// The WonderSwan's frame rate, some 75.47 Hz.
constexpr double FRAMES_PER_SECOND =
    static_cast<double>( machine::CPU_CLOCK_RATE ) / ( machine::CLOCKS_PER_SCANLINE * machine::SCANLINE_COUNT );

/// The most frames run to catch up: after a stall, such as a long request, the
/// rest of the backlog is dropped rather than run at full speed.
constexpr double MAX_FRAMES_OWED = 4.0;

control::Json shuttingDown()
{
  return control::Json{ { "id", 0 },
                        { "ok", false },
                        { "error", { { "code", "shutting_down" }, { "message", "The emulator is closing" } } } };
}

} // namespace

EmulationThread::EmulationThread( Settings settings, AudioOutput* audio )
    : mEmulator{ std::move( settings ) }, mDispatcher{ mEmulator }, mMethods{ mDispatcher.methods() }, mAudio{ audio },
      mThread{ [this] { loop(); } }
{
}

EmulationThread::~EmulationThread()
{
  {
    std::scoped_lock const lock{ mMutex };
    mStop = true;
  }
  mWake.notify_all();
  mThread.join();
}

std::vector<control::Method> const& EmulationThread::methods() const
{
  return mMethods;
}

control::Json EmulationThread::handle( control::Json const& request )
{
  std::future<control::Json> answer;
  {
    std::scoped_lock const lock{ mMutex };
    if ( mStop )
    {
      return shuttingDown();
    }
    mPending.push_back( Pending{ .request = request, .answer = {} } );
    answer = mPending.back().answer.get_future();
  }
  mWake.notify_all();
  return answer.get();
}

bool EmulationThread::paused() const
{
  return mPaused;
}

void EmulationThread::setHostButtons( std::uint16_t buttons )
{
  mHostButtons = buttons;
}

void EmulationThread::runFrame( machine::Machine& machine )
{
  machine.setHostButtons( mHostButtons );
  machine::RunResult const run = machine.runFrames( 1 );
  if ( mAudio != nullptr )
  {
    mAudio->push( machine.audio(), machine::Machine::AUDIO_RATE );
  }
  if ( run.reason == machine::StopReason::BREAKPOINT || run.reason == machine::StopReason::WATCHPOINT )
  {
    // The window stops where the debugger asked, as a client's run would;
    // emu.resume or the menu runs it on.
    mEmulator.setPaused( true );
  }
}

void EmulationThread::publishPicture()
{
  machine::Machine const* const machine = mEmulator.machine();
  if ( machine == nullptr ||
       ( mEmulator.powerUps() == mPicturePowerUp && machine->picturesDrawn() == mMachinePictures ) )
  {
    return;
  }
  mPicturePowerUp = mEmulator.powerUps();
  mMachinePictures = machine->picturesDrawn();
  auto const picture = machine->picture();
  std::scoped_lock const lock{ mPictureMutex };
  mPicture.rgba.assign( picture.begin(), picture.end() );
  mPicture.width = machine->pictureWidth();
  mPicture.height = machine->pictureHeight();
  mPicture.vertical = machine->vertical();
  ++mPicturesDrawn;
}

bool EmulationThread::takePicture( std::int64_t& picturesSeen, Picture& out )
{
  std::scoped_lock const lock{ mPictureMutex };
  if ( mPicturesDrawn == picturesSeen || mPicture.rgba.empty() )
  {
    return false;
  }
  out = mPicture;
  picturesSeen = mPicturesDrawn;
  return true;
}

void EmulationThread::serveRequests()
{
  for ( ;; )
  {
    Pending pending;
    {
      std::scoped_lock const lock{ mMutex };
      if ( mPending.empty() )
      {
        return;
      }
      pending = std::move( mPending.front() );
      mPending.pop_front();
    }
    pending.answer.set_value( mDispatcher.handle( pending.request ) );
  }
}

void EmulationThread::loop()
{
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();
  double owed = 0.0;
  for ( ;; )
  {
    serveRequests();
    // A request may have run the machine, or powered a new one up.
    publishPicture();
    {
      std::scoped_lock const lock{ mMutex };
      if ( mStop )
      {
        break;
      }
    }
    bool const paused = mEmulator.paused();
    if ( paused && !mPaused.exchange( true ) && mAudio != nullptr )
    {
      // So that sound resumes at once, not after what was queued.
      mAudio->clear();
    }
    mPaused = paused;

    // The host's clock paces the emulation, so that it runs at the
    // WonderSwan's speed whatever the display's refresh rate.
    auto const now = Clock::now();
    double const elapsed = std::chrono::duration<double>( now - last ).count();
    last = now;
    machine::Machine* const machine = mEmulator.machine();
    auto wakeAt = now + IDLE_WAIT;
    if ( machine == nullptr || paused )
    {
      owed = 0.0;
    }
    else
    {
      owed = std::min( owed + ( elapsed * FRAMES_PER_SECOND ), MAX_FRAMES_OWED );
      if ( owed >= 1.0 )
      {
        owed -= 1.0;
        runFrame( *machine );
        publishPicture();
        continue;
      }
      wakeAt = now + std::chrono::duration_cast<Clock::duration>(
                         std::chrono::duration<double>( ( 1.0 - owed ) / FRAMES_PER_SECOND ) );
    }

    std::unique_lock lock{ mMutex };
    mWake.wait_until( lock, wakeAt, [this] { return mStop || !mPending.empty(); } );
  }

  // Whoever is still waiting is told why no answer comes.
  std::scoped_lock const lock{ mMutex };
  for ( Pending& pending : mPending )
  {
    pending.answer.set_value( shuttingDown() );
  }
  mPending.clear();
}

} // namespace ws::app
