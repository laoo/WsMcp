#include "ws/machine/Machine.hpp"

#include "ControlManager.hpp"
#include "MemoryManager.hpp"
#include "Picture.hpp"
#include "Ppu.hpp"
#include "Serial.hpp"
#include "StateArchive.hpp"
#include "System.hpp"
#include "TraceHistory.hpp"

#include "cpu/Cpu.hpp"

#include <miniz.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace ws::machine
{

namespace
{

constexpr std::array<char, 8> STATE_MAGIC{ 'W', 'S', 'M', 'C', 'P', 'S', 'T', 'A' };
/// The layout of a state as a whole; each part's chunk has its own.
constexpr std::uint32_t STATE_VERSION = 1;

std::uint32_t crc32( std::span<std::uint8_t const> bytes )
{
  return static_cast<std::uint32_t>( mz_crc32( MZ_CRC32_INIT, bytes.data(), bytes.size() ) );
}

MemoryType memoryType( Memory which )
{
  switch ( which )
  {
  case Memory::WORK_RAM:
    return MemoryType::WORK_RAM;
  case Memory::ROM:
    return MemoryType::PRG_ROM;
  case Memory::SAVE_RAM:
    return MemoryType::CART_RAM;
  case Memory::BOOT_ROM:
    return MemoryType::BOOT_ROM;
  case Memory::INTERNAL_EEPROM:
    return MemoryType::INTERNAL_EEPROM;
  case Memory::CART_EEPROM:
    return MemoryType::CART_EEPROM;
  }
  return MemoryType::NONE;
}

/// Whether [address, address + size) and the watched range meet.
bool overlaps( Watchpoint const& watchpoint, std::uint32_t address, std::uint32_t size )
{
  return address < watchpoint.address + watchpoint.size && watchpoint.address < address + size;
}

} // namespace

struct Machine::Impl final : SystemListener, BusObserver
{
  /// Whether a run ends: asked after each instruction, with true, and after
  /// each cycle a halted CPU idles and each iteration of a REP string
  /// instruction but its last, with false.
  using Done = std::function<bool( bool instruction )>;

  explicit Impl( Setup setup )
      : info{ readRomInfo( setup.rom ) }, romCrc{ crc32( setup.rom ) }, bootRomCrc{ crc32( setup.bootRom ) }
  {
    Config const config{ .model = setup.model, .audioMode = setup.audioMode, .lcdShowIcons = setup.lcdShowIcons };
    std::vector<std::uint8_t> const save = std::move( setup.save );
    system = std::make_unique<System>(
        config, std::move( setup.rom ), std::move( setup.bootRom ), std::move( setup.internalEeprom ), *this );

    // The battery-backed memory as it was saved, as much of it as fits.
    for ( MemoryType const type : { MemoryType::CART_RAM, MemoryType::CART_EEPROM } )
    {
      std::span<std::uint8_t> const memory = system->getMemory( type );
      if ( !memory.empty() && !save.empty() )
      {
        std::copy_n( save.begin(), std::min( save.size(), memory.size() ), memory.begin() );
      }
    }

    picture.assign( static_cast<std::size_t>( SCREEN_WIDTH ) * SCREEN_HEIGHT * 4, 0 );
    pictureWidth = SCREEN_WIDTH;
    pictureHeight = SCREEN_HEIGHT;
  }

  Impl( Impl const& ) = delete;
  Impl& operator=( Impl const& ) = delete;
  Impl( Impl&& ) = delete;
  Impl& operator=( Impl&& ) = delete;
  ~Impl() override = default;

  [[nodiscard]] Cpu& cpu() const
  {
    return *system->getCpu();
  }

  [[nodiscard]] std::uint64_t now() const
  {
    return cpu().getCycleCount();
  }

  void frameDone( std::span<std::uint16_t const> pixels, std::uint32_t width, std::uint32_t height ) override
  {
    pictureWidth = width;
    pictureHeight = height;
    picture.resize( static_cast<std::size_t>( width ) * height * 4 );
    toRgba( pixels.first( static_cast<std::size_t>( width ) * height ), system->getModel(), picture );
    ++frames;
    trace.forgetBefore( now() > traceCycles ? now() - traceCycles : 0 );
  }

  void audioDone( std::span<std::int16_t const> samples ) override
  {
    for ( std::size_t i = 0; i + 1 < samples.size(); i += 2 )
    {
      sound.push_back( AudioFrame{ .left = samples[i], .right = samples[i + 1] } );
    }
  }

  void memoryAccess( std::uint32_t address, std::uint16_t value, bool word, MemoryOperationType type ) override
  {
    if ( type == MemoryOperationType::EXEC_OP_CODE || type == MemoryOperationType::EXEC_OPERAND )
    {
      return;
    }
    bool const write = type == MemoryOperationType::WRITE || type == MemoryOperationType::DMA_WRITE;
    watched( address, value, word, write, false );
  }

  void portAccess( std::uint16_t port, std::uint16_t value, bool word, bool write ) override
  {
    watched( port, value, word, write, true );
  }

  void watched( std::uint32_t address, std::uint16_t value, bool word, bool write, bool port )
  {
    if ( watchHit )
    {
      return;
    }
    Access const access = write ? Access::WRITE : Access::READ;
    for ( Watchpoint const& watchpoint : watchpoints )
    {
      if ( watchpoint.port == port &&
           ( static_cast<unsigned>( watchpoint.access ) & static_cast<unsigned>( access ) ) != 0 &&
           overlaps( watchpoint, address, word ? 2 : 1 ) )
      {
        watchHit = WatchHit{ .address = address,
                             .port = port,
                             .write = write,
                             .word = word,
                             .value = value,
                             .cs = instructionCs,
                             .ip = instructionIp };
        return;
      }
    }
  }

  void note()
  {
    CpuState const& state = cpu().getState();
    TracedInstruction instruction{ .cycle = state.cycleCount,
                                   .cs = state.cs,
                                   .ip = state.ip,
                                   .ax = state.ax,
                                   .bx = state.bx,
                                   .cx = state.cx,
                                   .dx = state.dx,
                                   .sp = state.sp,
                                   .bp = state.bp,
                                   .si = state.si,
                                   .di = state.di,
                                   .ds = state.ds,
                                   .es = state.es,
                                   .ss = state.ss,
                                   .flags = state.flags.get() };
    MemoryManager const& memory = *system->getMemoryManager();
    for ( std::size_t i = 0; i < instruction.bytes.size(); i++ )
    {
      instruction.bytes[i] = memory.debugCpuRead<std::uint8_t>( state.cs, static_cast<std::uint16_t>( state.ip + i ) );
    }
    trace.push( instruction );
  }

  RunResult run( Done const& done, std::uint64_t limit )
  {
    std::uint64_t const start = now();
    std::uint64_t const startFrame = frames;
    sound.clear();
    watchHit.reset();
    RunResult result{ .reason = StopReason::TIMEOUT };
    Cpu& v30 = cpu();
    for ( ;; )
    {
      if ( !prepared )
      {
        if ( !v30.prepareInstruction() )
        {
          // A halted CPU spent a cycle waiting for an interrupt.
          if ( done( false ) )
          {
            result.reason = StopReason::CONDITION;
            break;
          }
          if ( now() - start >= limit )
          {
            break;
          }
          continue;
        }
        // An instruction boundary, unless a REP runs its next iteration.
        if ( !v30.isRepeating() )
        {
          std::uint32_t const pc = v30.getProgramCounter();
          if ( !breakpoints.empty() && std::ranges::binary_search( breakpoints, pc ) )
          {
            prepared = true;
            result.reason = StopReason::BREAKPOINT;
            result.breakpoint = pc;
            break;
          }
          if ( traceCycles > 0 )
          {
            note();
          }
          if ( hook )
          {
            hook( *owner );
          }
        }
      }
      prepared = false;
      instructionCs = v30.getState().cs;
      instructionIp = v30.getState().ip;
      v30.execOpCode();

      if ( watchHit )
      {
        result.reason = StopReason::WATCHPOINT;
        result.watchpoint = *watchHit;
        watchHit.reset();
        break;
      }
      // An instruction is done when a REP has no iteration left to run.
      if ( done( !v30.isRepeating() ) )
      {
        result.reason = StopReason::CONDITION;
        break;
      }
      if ( now() - start >= limit )
      {
        break;
      }
    }
    result.cycles = now() - start;
    result.frames = frames - startFrame;
    lastRun = result;
    trace.forgetBefore( now() > traceCycles ? now() - traceCycles : 0 );
    if ( capturing )
    {
      captured.insert( captured.end(), sound.begin(), sound.end() );
    }
    if ( audioListener )
    {
      audioListener( sound );
    }
    return result;
  }

  /// A run that went the whole way it was asked to, by its count or its
  /// limit, is completed; one a breakpoint or a watchpoint stopped says so.
  RunResult completed( RunResult result )
  {
    if ( result.reason == StopReason::CONDITION || result.reason == StopReason::TIMEOUT )
    {
      result.reason = StopReason::COMPLETED;
      lastRun = result;
    }
    return result;
  }

  void observe()
  {
    system->setBusObserver( watchpoints.empty() ? nullptr : this );
  }

  void applyButtons() const
  {
    system->setButtons( static_cast<std::uint16_t>( buttons | hostButtons ) );
  }

  /// The machine's own chunk: what it keeps beside the parts.
  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( frames );
    archive( prepared );
  }

  Machine* owner{};
  RomInfo info;
  std::uint32_t romCrc;
  std::uint32_t bootRomCrc;
  std::unique_ptr<System> system;

  std::uint16_t buttons{};
  std::uint16_t hostButtons{};

  std::vector<std::uint8_t> picture;
  std::uint32_t pictureWidth{};
  std::uint32_t pictureHeight{};
  std::uint64_t frames{};

  std::vector<AudioFrame> sound;
  std::vector<AudioFrame> captured;
  bool capturing{};
  std::function<void( std::span<AudioFrame const> )> audioListener;

  /// Sorted.
  std::vector<std::uint32_t> breakpoints;
  std::vector<Watchpoint> watchpoints;
  std::optional<WatchHit> watchHit;
  std::uint16_t instructionCs{};
  std::uint16_t instructionIp{};
  /// Whether the last run stopped at a breakpoint, after the CPU took what
  /// interrupt it would: the next one begins with the instruction.
  bool prepared{};

  TraceHistory trace;
  std::uint64_t traceCycles{ DEFAULT_TRACE_CYCLES };
  std::function<void( Machine const& )> hook;
  RunResult lastRun{};
};

Machine::Machine( Setup setup ) : mImpl{ std::make_unique<Impl>( std::move( setup ) ) }
{
  mImpl->owner = this;
}

Machine::~Machine() = default;

Model Machine::model() const
{
  return mImpl->system->getModel();
}

RunResult Machine::runCycles( std::uint64_t count )
{
  return mImpl->completed( mImpl->run( []( bool ) { return false; }, count ) );
}

RunResult Machine::runFrames( std::uint64_t count )
{
  std::uint64_t const target = mImpl->frames + count;
  return mImpl->completed( mImpl->run( [&]( bool ) { return mImpl->frames >= target; }, ~std::uint64_t{} ) );
}

RunResult Machine::runUntil( std::function<bool( Machine const& )> const& condition, std::uint64_t timeoutCycles )
{
  return mImpl->run( [&]( bool ) { return condition( *this ); }, timeoutCycles );
}

RunResult Machine::step( std::uint64_t count )
{
  std::uint64_t instructions = 0;
  return mImpl->completed(
      mImpl->run( [&]( bool instruction ) { return instruction && ++instructions >= count; }, ~std::uint64_t{} ) );
}

void Machine::addBreakpoint( std::uint32_t address )
{
  auto& breakpoints = mImpl->breakpoints;
  address &= 0xFFFFFU;
  auto const at = std::ranges::lower_bound( breakpoints, address );
  if ( at == breakpoints.end() || *at != address )
  {
    breakpoints.insert( at, address );
  }
}

bool Machine::removeBreakpoint( std::uint32_t address )
{
  auto& breakpoints = mImpl->breakpoints;
  auto const at = std::ranges::lower_bound( breakpoints, address & 0xFFFFFU );
  if ( at == breakpoints.end() || *at != ( address & 0xFFFFFU ) )
  {
    return false;
  }
  breakpoints.erase( at );
  return true;
}

std::vector<std::uint32_t> Machine::breakpoints() const
{
  return mImpl->breakpoints;
}

void Machine::addWatchpoint( Watchpoint watchpoint )
{
  removeWatchpoint( watchpoint.address, watchpoint.port );
  mImpl->watchpoints.push_back( watchpoint );
  mImpl->observe();
}

bool Machine::removeWatchpoint( std::uint32_t address, bool port )
{
  auto const removed = std::erase_if( mImpl->watchpoints,
                                      [&]( Watchpoint const& watchpoint )
                                      { return watchpoint.address == address && watchpoint.port == port; } );
  mImpl->observe();
  return removed > 0;
}

std::vector<Watchpoint> Machine::watchpoints() const
{
  return mImpl->watchpoints;
}

void Machine::setTraceCycles( std::uint64_t cycles )
{
  mImpl->traceCycles = cycles;
  mImpl->trace.forgetBefore( now() > cycles ? now() - cycles : 0 );
}

std::uint64_t Machine::traceCycles() const
{
  return mImpl->traceCycles;
}

std::size_t Machine::traceLength() const
{
  return mImpl->trace.size();
}

std::vector<TracedInstruction> Machine::trace( std::size_t start, std::size_t count ) const
{
  std::vector<TracedInstruction> instructions;
  std::size_t const end = std::min( mImpl->trace.size(), start + count );
  for ( std::size_t i = start; i < end; i++ )
  {
    instructions.push_back( mImpl->trace.at( i ) );
  }
  return instructions;
}

TracedInstruction Machine::tracedAt( std::size_t index ) const
{
  return mImpl->trace.at( index );
}

void Machine::setInstructionHook( std::function<void( Machine const& )> hook )
{
  mImpl->hook = std::move( hook );
}

RunResult const& Machine::lastRun() const
{
  return mImpl->lastRun;
}

std::uint64_t Machine::now() const
{
  return mImpl->now();
}

std::uint64_t Machine::frame() const
{
  return mImpl->frames;
}

CpuState Machine::cpu() const
{
  return mImpl->cpu().getState();
}

std::uint32_t Machine::pc() const
{
  return mImpl->cpu().getProgramCounter();
}

State Machine::state() const
{
  return mImpl->system->getState();
}

std::uint8_t Machine::peek( std::uint32_t address ) const
{
  return mImpl->system->getMemoryManager()->debugRead( address & 0xFFFFFU );
}

bool Machine::poke( std::uint32_t address, std::uint8_t value )
{
  MemoryManager& memory = *mImpl->system->getMemoryManager();
  address &= 0xFFFFFU;
  std::uint8_t const before = memory.debugRead( address );
  memory.debugWrite( address, static_cast<std::uint8_t>( before ^ 0xFFU ) );
  bool const writable = memory.debugRead( address ) != before;
  memory.debugWrite( address, value );
  return writable;
}

std::uint8_t Machine::peekPort( std::uint16_t port ) const
{
  return mImpl->system->getMemoryManager()->debugReadPort<std::uint8_t>( port );
}

void Machine::pokePort( std::uint16_t port, std::uint8_t value )
{
  mImpl->system->getMemoryManager()->internalWritePort( port, value, false );
}

std::span<std::uint8_t const> Machine::memory( Memory which ) const
{
  return std::as_const( *mImpl->system ).getMemory( memoryType( which ) );
}

std::span<std::uint8_t> Machine::memory( Memory which )
{
  return mImpl->system->getMemory( memoryType( which ) );
}

std::uint8_t Machine::publisher() const
{
  return mImpl->info.publisher;
}

std::uint8_t Machine::gameId() const
{
  return mImpl->info.gameId;
}

bool Machine::colorSupport() const
{
  return mImpl->info.colorSupport;
}

bool Machine::vertical() const
{
  return mImpl->info.vertical || mImpl->system->isVerticalMode();
}

std::uint32_t Machine::romCrc() const
{
  return mImpl->romCrc;
}

std::uint32_t Machine::bootRomCrc() const
{
  return mImpl->bootRomCrc;
}

void Machine::setButtons( std::uint16_t buttons )
{
  mImpl->buttons = buttons;
  mImpl->applyButtons();
}

std::uint16_t Machine::buttons() const
{
  return mImpl->buttons;
}

void Machine::setHostButtons( std::uint16_t buttons )
{
  mImpl->hostButtons = buttons;
  mImpl->applyButtons();
}

std::span<std::uint8_t const> Machine::picture() const
{
  return mImpl->picture;
}

std::uint32_t Machine::pictureWidth() const
{
  return mImpl->pictureWidth;
}

std::uint32_t Machine::pictureHeight() const
{
  return mImpl->pictureHeight;
}

std::uint64_t Machine::picturesDrawn() const
{
  return mImpl->frames;
}

std::span<AudioFrame const> Machine::audio() const
{
  return mImpl->sound;
}

void Machine::startCapture()
{
  mImpl->captured.clear();
  mImpl->capturing = true;
}

bool Machine::capturing() const
{
  return mImpl->capturing;
}

std::vector<AudioFrame> Machine::stopCapture()
{
  mImpl->capturing = false;
  return std::exchange( mImpl->captured, {} );
}

void Machine::setAudioListener( std::function<void( std::span<AudioFrame const> )> listener )
{
  mImpl->audioListener = std::move( listener );
}

std::vector<std::uint8_t> Machine::takeSerialOutput()
{
  return mImpl->system->getSerial()->takeSent();
}

std::vector<std::uint8_t> Machine::saveState() const
{
  StateWriter writer;
  writer( STATE_MAGIC );
  writer( STATE_VERSION );
  writer( mImpl->system->getModel() );
  writer( mImpl->romCrc );
  writer.chunk( fourCc( "MACH" ), 1, *mImpl );
  mImpl->system->serialize( writer );
  return writer.bytes();
}

std::expected<void, std::string> Machine::loadState( std::span<std::uint8_t const> state )
{
  StateReader header{ state };
  std::array<char, 8> magic{};
  std::uint32_t version = 0;
  Model model{};
  std::uint32_t crc = 0;
  header( magic );
  header( version );
  header( model );
  header( crc );
  if ( !header.good() || magic != STATE_MAGIC )
  {
    return std::unexpected( std::string{ "not a WsMcp state" } );
  }
  if ( version != STATE_VERSION )
  {
    return std::unexpected( std::string{ "a state of another build's layout" } );
  }
  if ( model != mImpl->system->getModel() )
  {
    return std::unexpected( std::string{ "a state of another model" } );
  }
  if ( crc != mImpl->romCrc )
  {
    return std::unexpected( std::string{ "a state of another cartridge" } );
  }
  // Read into the machine, and back out of what it was if the state proves
  // short or of another layout.
  std::vector<std::uint8_t> const before = saveState();
  auto const read = [&]( std::span<std::uint8_t const> bytes )
  {
    StateReader reader{ bytes };
    reader( magic );
    reader( version );
    reader( model );
    reader( crc );
    reader.chunk( fourCc( "MACH" ), 1, *mImpl );
    mImpl->system->serialize( reader );
    return reader.good() && reader.atEnd();
  };
  if ( read( state ) )
  {
    mImpl->applyButtons();
    return {};
  }
  read( before );
  return std::unexpected( std::string{ "a state of another build's layout, or cut short" } );
}

} // namespace ws::machine
