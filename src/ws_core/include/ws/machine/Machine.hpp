#pragma once

#include "ws/machine/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ws::machine
{

/// What a machine is powered up with. The ROMs are the caller's to find
/// (ws::Emulator reads them from files); the machine only runs them.
struct Setup
{
  Model model{ Model::COLOR };
  /// The cartridge's ROM, at least 64 KB, its header in its last 16 bytes.
  std::vector<std::uint8_t> rom;
  /// The IPL, 4 KB for the WonderSwan and 8 KB for the Color and the
  /// SwanCrystal; empty to start the cartridge as the IPL would leave it.
  std::vector<std::uint8_t> bootRom;
  /// The console's EEPROM as it was saved; empty for one as from the factory.
  std::vector<std::uint8_t> internalEeprom;
  /// The cartridge's SRAM or EEPROM as it was saved; empty for a blank one.
  std::vector<std::uint8_t> save;
  /// The cartridge's real-time clock as Machine::rtc() gave it when it was
  /// saved, and the seconds since: the clock runs on by them, as on its
  /// battery. Empty for a clock whose battery was just put in. Only a Bandai
  /// 2003 cartridge has a clock.
  std::vector<std::uint8_t> rtc;
  std::uint64_t rtcSecondsAway{};
  /// A WonderWitch's flash as it was saved, which takes the place of `rom`'s
  /// contents; empty for them as given. Only a Bandai 2003 cartridge of
  /// 512 KB has a flash chip.
  std::vector<std::uint8_t> flash;
  AudioMode audioMode{ AudioMode::HEADPHONES };
  /// Whether the picture carries the LCD's icons beside or below it.
  bool lcdShowIcons{};
};

enum class StopReason : std::uint8_t
{
  COMPLETED,
  CONDITION,
  TIMEOUT,
  BREAKPOINT,
  WATCHPOINT
};

/// The accesses a watchpoint watches.
enum class Access : std::uint8_t
{
  READ = 1,
  WRITE = 2,
  BOTH = 3
};

/// A range of addresses whose data accesses stop a run: of the 20-bit
/// address space, or of the I/O ports.
struct Watchpoint
{
  std::uint32_t address{};
  std::uint32_t size{ 1 };
  Access access{ Access::WRITE };
  bool port{};
};

/// The access that set a watchpoint off, and the instruction that made it.
struct WatchHit
{
  std::uint32_t address{};
  bool port{};
  bool write{};
  /// A word access carries 16 bits.
  bool word{};
  std::uint16_t value{};
  /// The instruction's CS:IP.
  std::uint16_t cs{};
  std::uint16_t ip{};
};

struct RunResult
{
  StopReason reason{};
  std::uint64_t cycles{};
  std::uint64_t frames{};
  /// With BREAKPOINT, the linear address; with WATCHPOINT, the access.
  std::uint32_t breakpoint{};
  WatchHit watchpoint{};
};

/// One stereo sample of the sound, at Machine::AUDIO_RATE.
struct AudioFrame
{
  std::int16_t left{};
  std::int16_t right{};
};

/// An instruction the V30MZ began: in which cycle, the registers before it,
/// and its bytes as they were when it ran.
struct TracedInstruction
{
  std::uint64_t cycle{};
  std::uint16_t cs{};
  std::uint16_t ip{};
  std::uint16_t ax{};
  std::uint16_t bx{};
  std::uint16_t cx{};
  std::uint16_t dx{};
  std::uint16_t sp{};
  std::uint16_t bp{};
  std::uint16_t si{};
  std::uint16_t di{};
  std::uint16_t ds{};
  std::uint16_t es{};
  std::uint16_t ss{};
  std::uint16_t flags{};
  std::array<std::uint8_t, 8> bytes{};
};

/// The memories a debugger reads and writes, by name.
enum class Memory : std::uint8_t
{
  WORK_RAM,
  ROM,
  SAVE_RAM,
  BOOT_ROM,
  INTERNAL_EEPROM,
  CART_EEPROM
};

/// A WonderSwan, a WonderSwan Color or a SwanCrystal with a cartridge in
/// it, stepped a cycle of its 3.072 MHz clock at a time
/// (docs/decisions/0008-stepped-by-the-cpus-cycles.md). Every run ends at an
/// instruction boundary, where the registers are whole.
class Machine
{
public:
  /// A machine powered up with `setup`: every part as after power-up, and
  /// the IPL about to run, or the cartridge where the IPL would leave it.
  /// The ROM must be at least 64 KB.
  explicit Machine( Setup setup );
  ~Machine();

  Machine( Machine const& ) = delete;
  Machine& operator=( Machine const& ) = delete;
  Machine( Machine&& ) = delete;
  Machine& operator=( Machine&& ) = delete;

  static constexpr unsigned AUDIO_RATE = 24000;
  static constexpr std::uint64_t CLOCK_RATE = CPU_CLOCK_RATE;

  [[nodiscard]] Model model() const;

  /// Runs at least `count` cycles, to the next instruction boundary.
  RunResult runCycles( std::uint64_t count );
  /// Runs through `count` frame boundaries, where the PPU hands a finished
  /// picture over, to the next instruction boundary.
  RunResult runFrames( std::uint64_t count );
  /// Runs until `condition` holds, checked after each instruction, each
  /// iteration of a REP and each cycle of a halted CPU, or for
  /// `timeoutCycles`.
  RunResult runUntil( std::function<bool( Machine const& )> const& condition, std::uint64_t timeoutCycles );
  /// Runs `count` instructions, stopping before at a breakpoint or after a
  /// watched access. A halted CPU's idle cycles are not instructions, and a
  /// string instruction under REP is one, however many times it repeats.
  RunResult step( std::uint64_t count );

  /// Breakpoints, by linear address (CS * 16 + IP, 20 bits): a run stops
  /// before the instruction there. The instruction the machine stands at
  /// runs when it runs on.
  void addBreakpoint( std::uint32_t address );
  bool removeBreakpoint( std::uint32_t address );
  [[nodiscard]] std::vector<std::uint32_t> breakpoints() const;

  /// Watchpoints, one per address and space: a run stops after the
  /// instruction whose data access, or a DMA's, falls in one's range.
  void addWatchpoint( Watchpoint watchpoint );
  bool removeWatchpoint( std::uint32_t address, bool port );
  [[nodiscard]] std::vector<Watchpoint> watchpoints() const;

  /// The history of execution: every instruction begun in the last
  /// traceCycles() cycles. trace() answers `count` of them from the
  /// `start`th, 0 being the oldest kept.
  static constexpr std::uint64_t DEFAULT_TRACE_CYCLES = CLOCK_RATE / 2;
  void setTraceCycles( std::uint64_t cycles );
  [[nodiscard]] std::uint64_t traceCycles() const;
  [[nodiscard]] std::size_t traceLength() const;
  [[nodiscard]] std::vector<TracedInstruction> trace( std::size_t start, std::size_t count ) const;
  /// The `index`th kept, 0 being the oldest; it must be below traceLength().
  [[nodiscard]] TracedInstruction tracedAt( std::size_t index ) const;

  /// Called at every instruction boundary from now on, before the
  /// instruction runs; empty to stop. For traces; it slows the run.
  void setInstructionHook( std::function<void( Machine const& )> hook );

  [[nodiscard]] RunResult const& lastRun() const;

  /// Cycles since power-up, and frame boundaries passed.
  [[nodiscard]] std::uint64_t now() const;
  [[nodiscard]] std::uint64_t frame() const;

  [[nodiscard]] CpuState cpu() const;
  /// The linear address of the instruction at CS:IP.
  [[nodiscard]] std::uint32_t pc() const;
  /// Every part's registers, as the debugger shows them.
  [[nodiscard]] State state() const;

  /// A byte as the CPU would read it at linear `address`, without a bus
  /// cycle; an address nothing answers reads 0.
  [[nodiscard]] std::uint8_t peek( std::uint32_t address ) const;
  /// Writes RAM or SRAM as a debugger does; false where nothing writable
  /// is mapped at `address`.
  bool poke( std::uint32_t address, std::uint8_t value );
  /// A port as the CPU would read it, without its side effects where it has
  /// any ($B1, $B3 and $B5 read 0).
  [[nodiscard]] std::uint8_t peekPort( std::uint16_t port ) const;
  /// Writes a port as the CPU would, but beside the bus: no cycle passes.
  void pokePort( std::uint16_t port, std::uint8_t value );

  [[nodiscard]] std::span<std::uint8_t const> memory( Memory which ) const;
  [[nodiscard]] std::span<std::uint8_t> memory( Memory which );

  /// What the cartridge's header says.
  [[nodiscard]] std::uint8_t publisher() const;
  [[nodiscard]] std::uint8_t gameId() const;
  [[nodiscard]] bool colorSupport() const;
  /// Whether the cartridge asks to be held upright, by its header or, once
  /// it has run, by the LCD's icons.
  [[nodiscard]] bool vertical() const;
  /// The CRC-32 of the cartridge's ROM as given and of the boot ROM.
  [[nodiscard]] std::uint32_t romCrc() const;
  [[nodiscard]] std::uint32_t bootRomCrc() const;
  /// Whether the cartridge has a real-time clock: a Bandai 2003's.
  [[nodiscard]] bool hasRtc() const;
  /// What the clock's battery keeps, to be given back as Setup::rtc; empty
  /// without a clock.
  [[nodiscard]] std::vector<std::uint8_t> rtc() const;
  /// Whether the cartridge's ROM is a flash chip, and whether it has been
  /// programmed or erased since power-up: its contents, memory(Memory::ROM),
  /// are then to be kept, and given back as Setup::flash.
  [[nodiscard]] bool hasFlash() const;
  [[nodiscard]] bool flashWritten() const;

  /// The buttons the protocol holds, Button bits.
  void setButtons( std::uint16_t buttons );
  [[nodiscard]] std::uint16_t buttons() const;
  /// The buttons the host's keyboard or pad holds, seen with those above.
  void setHostButtons( std::uint16_t buttons );

  /// The last complete picture, pictureWidth() by pictureHeight() RGBA, and
  /// how many have been completed since power-up.
  [[nodiscard]] std::span<std::uint8_t const> picture() const;
  [[nodiscard]] std::uint32_t pictureWidth() const;
  [[nodiscard]] std::uint32_t pictureHeight() const;
  [[nodiscard]] std::uint64_t picturesDrawn() const;

  /// The sound the last run made.
  [[nodiscard]] std::span<AudioFrame const> audio() const;
  /// Keeps every frame of sound made from now on, until stopCapture() hands
  /// them over.
  void startCapture();
  [[nodiscard]] bool capturing() const;
  std::vector<AudioFrame> stopCapture();
  /// Called with the sound of every run from now on, as it ends; empty to
  /// stop.
  void setAudioListener( std::function<void( std::span<AudioFrame const> )> listener );

  /// The bytes the cartridge sent to the serial port since the last call.
  std::vector<std::uint8_t> takeSerialOutput();

  /// The whole machine's state, all a run changes, without the ROMs or the
  /// debugging aids.
  [[nodiscard]] std::vector<std::uint8_t> saveState() const;
  /// Restores a state saveState() made of a machine of this model, with this
  /// cartridge, by this build. On failure the machine is as it was.
  std::expected<void, std::string> loadState( std::span<std::uint8_t const> state );

private:
  struct Impl;
  std::unique_ptr<Impl> mImpl;
};

} // namespace ws::machine
