#pragma once

#include "AudioOutput.hpp"

#include "ws/Emulator.hpp"
#include "ws/control/Dispatcher.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace ws::app
{

/// A complete picture as the machine drew it.
struct Picture
{
  std::vector<std::uint8_t> rgba;
  std::uint32_t width{};
  std::uint32_t height{};
  /// Whether the cartridge is held upright, as Machine::vertical() says.
  bool vertical{};
};

/// The emulator on a thread of its own (docs/architecture.md). It runs the
/// machine at the WonderSwan's speed, paced by the host's clock, and answers
/// requests between two frames, so that the interface and every transport
/// attached to it drive the one machine without touching it. Nothing outside
/// reaches the emulator but through handle().
class EmulationThread
{
public:
  /// Starts the thread. `audio`, if not null, is fed the sound, and must
  /// outlive the thread.
  EmulationThread( Settings settings, AudioOutput* audio );
  ~EmulationThread();

  EmulationThread( EmulationThread const& ) = delete;
  EmulationThread& operator=( EmulationThread const& ) = delete;
  EmulationThread( EmulationThread&& ) = delete;
  EmulationThread& operator=( EmulationThread&& ) = delete;

  /// Answers a request of the control protocol, on the emulation thread
  /// between two frames; waits for the answer. Safe from any thread.
  [[nodiscard]] control::Json handle( control::Json const& request );

  /// The dispatcher's methods, which do not change once it is made.
  [[nodiscard]] std::vector<control::Method> const& methods() const;

  /// Whether frames are not being run: the emulator's paused(), which
  /// `emu.pause` and `emu.resume` set. Requests are answered either way.
  [[nodiscard]] bool paused() const;

  /// The buttons the host's keyboard and gamepads hold, Button bits, applied
  /// before each frame.
  void setHostButtons( std::uint16_t buttons );

  /// Copies the last complete picture into `out` if it is newer than
  /// `picturesSeen`, and updates that; false if there is none newer.
  bool takePicture( std::int64_t& picturesSeen, Picture& out );

private:
  struct Pending
  {
    control::Json request;
    std::promise<control::Json> answer;
  };

  void loop();
  /// Answers every request waiting, on the emulation thread.
  void serveRequests();
  /// Hands the machine's picture on to takePicture(), if it has a new one.
  void publishPicture();
  /// Runs a frame, and pauses where a breakpoint or a watchpoint stopped it.
  void runFrame( machine::Machine& machine );

  Emulator mEmulator;
  control::Dispatcher mDispatcher;
  std::vector<control::Method> const mMethods;
  AudioOutput* mAudio;

  std::mutex mMutex;
  std::condition_variable mWake;
  std::deque<Pending> mPending;
  bool mStop{};

  /// The emulator's paused(), as of the last time the thread looked.
  std::atomic<bool> mPaused{};

  std::atomic<std::uint16_t> mHostButtons{};

  std::mutex mPictureMutex;
  Picture mPicture;
  std::int64_t mPicturesDrawn{ -1 };
  /// The machine's count of pictures when one was last handed on, and the
  /// power-up it was of: a power-up makes another machine, which counts
  /// afresh.
  std::uint64_t mMachinePictures{};
  std::uint64_t mPicturePowerUp{};

  std::thread mThread;
};

} // namespace ws::app
