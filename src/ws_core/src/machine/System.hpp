#pragma once

// The WonderSwan's parts and what they reach each other through: Mesen2's
// WsConsole, and the few services of its Emulator the parts call on (the
// settings, the memories, the debugger's hooks, the finished frame and the
// sound). Ported from Mesen2's Core/WS/WsConsole.h and WsConsole.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.
//
// Every part holds a System* where Mesen2's holds its WsConsole* and its
// Emulator*, and reaches the others through it. System is private to the
// core; machine::Machine is its face to the rest of the program.

#include "ws/machine/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ws::machine
{

class Apu;
class Cart;
class ControlManager;
class Cpu;
class DmaController;
class Eeprom;
class MemoryManager;
class Ppu;
class Serial;
class Timer;

/// The memories the address space is mapped from, as Mesen2's MemoryType
/// names the WonderSwan's.
enum class MemoryType : std::uint8_t
{
  NONE,
  WORK_RAM,
  PRG_ROM,
  CART_RAM,
  BOOT_ROM,
  INTERNAL_EEPROM,
  CART_EEPROM
};

/// What an access was for, as Mesen2's MemoryOperationType has it: the
/// debugger's watchpoints look at the data accesses alone.
enum class MemoryOperationType : std::uint8_t
{
  READ,
  WRITE,
  EXEC_OP_CODE,
  EXEC_OPERAND,
  DMA_READ,
  DMA_WRITE
};

/// What the machine is built as and how it is played: the part of Mesen2's
/// WsConfig that reaches the machine.
struct Config
{
  Model model{ Model::COLOR };
  AudioMode audioMode{ AudioMode::HEADPHONES };
  /// Whether the picture has the LCD's icons drawn beside it (vertical
  /// strip on the right) or below it.
  bool lcdShowIcons{};
};

/// What a cartridge's header, its last 16 bytes, says about it.
struct RomInfo
{
  /// The ROM as the machine maps it: padded at its start to a power of two.
  std::size_t romSize{};
  std::uint32_t saveRamSize{};
  std::uint32_t cartEepromSize{};
  bool colorSupport{};
  /// Upright, as the header's orientation bit says.
  bool vertical{};
  /// 0 the Bandai 2001 or KARNAK, 1 the 2003.
  std::uint8_t mapper{};
  std::uint8_t publisher{};
  std::uint8_t gameId{};
  std::uint16_t checksum{};
};

/// Reads a cartridge's header; the ROM must be at least 64 KB.
[[nodiscard]] RomInfo readRomInfo( std::span<std::uint8_t const> rom );

/// Who watches the bus: the Machine, while a watchpoint is set. Every data
/// access the CPU or a DMA makes, and every port access, is reported.
class BusObserver
{
public:
  virtual ~BusObserver() = default;
  virtual void memoryAccess( std::uint32_t address, std::uint16_t value, bool word, MemoryOperationType type ) = 0;
  virtual void portAccess( std::uint16_t port, std::uint16_t value, bool word, bool write ) = 0;

protected:
  BusObserver() = default;
  BusObserver( BusObserver const& ) = default;
  BusObserver& operator=( BusObserver const& ) = default;
  BusObserver( BusObserver&& ) = default;
  BusObserver& operator=( BusObserver&& ) = default;
};

/// Called with each frame the PPU finishes, and with the sound made.
class SystemListener
{
public:
  virtual ~SystemListener() = default;
  /// A frame: `width` by `height` pixels, each 0x0RGB, 4 bits a component.
  virtual void frameDone( std::span<std::uint16_t const> pixels, std::uint32_t width, std::uint32_t height ) = 0;
  /// Stereo samples at Apu::SAMPLE_RATE, left then right.
  virtual void audioDone( std::span<std::int16_t const> samples ) = 0;

protected:
  SystemListener() = default;
  SystemListener( SystemListener const& ) = default;
  SystemListener& operator=( SystemListener const& ) = default;
  SystemListener( SystemListener&& ) = default;
  SystemListener& operator=( SystemListener&& ) = default;
};

class System
{
public:
  /// A machine with a cartridge of `rom` in it, and `bootRom`, the IPL, or
  /// none: then the state the IPL would leave is set up instead (Mesen2's
  /// InitPostBootRomState). `internalEeprom`, empty for a blank one, is the
  /// console's own EEPROM: its owner's name and settings.
  System( Config const& config,
          std::vector<std::uint8_t> rom,
          std::vector<std::uint8_t> bootRom,
          std::vector<std::uint8_t> internalEeprom,
          SystemListener& listener );
  ~System();

  System( System const& ) = delete;
  System& operator=( System const& ) = delete;
  System( System&& ) = delete;
  System& operator=( System&& ) = delete;

  /// Runs one instruction, or a cycle of a halted CPU.
  void exec();

  [[nodiscard]] Model getModel() const;
  [[nodiscard]] Config const& getConfig() const;
  [[nodiscard]] bool isColorMode() const;
  [[nodiscard]] bool isPowerOff() const;
  /// Whether the last frames asked for the console to be held upright, by
  /// the LCD's icons.
  [[nodiscard]] bool isVerticalMode() const;
  [[nodiscard]] std::uint64_t getMasterClock() const;

  [[nodiscard]] Cpu* getCpu() const;
  [[nodiscard]] Ppu* getPpu() const;
  [[nodiscard]] Apu* getApu() const;
  [[nodiscard]] MemoryManager* getMemoryManager() const;
  [[nodiscard]] ControlManager* getControlManager() const;
  [[nodiscard]] DmaController* getDmaController() const;
  [[nodiscard]] Timer* getTimer() const;
  [[nodiscard]] Serial* getSerial() const;
  [[nodiscard]] Cart* getCart() const;
  [[nodiscard]] Eeprom* getInternalEeprom() const;
  /// The cartridge's EEPROM, or null where it has SRAM or nothing.
  [[nodiscard]] Eeprom* getCartEeprom() const;

  /// One of the memories; empty where the machine has none of it.
  [[nodiscard]] std::span<std::uint8_t> getMemory( MemoryType type );
  [[nodiscard]] std::span<std::uint8_t const> getMemory( MemoryType type ) const;

  [[nodiscard]] State getState() const;

  /// The buttons held, Button bits.
  void setButtons( std::uint16_t buttons );
  [[nodiscard]] std::uint16_t getButtons() const;

  /// The PPU's end of a frame: the picture goes to the listener, and the
  /// buttons are read for the next (Mesen2's SendFrame and ProcessEndOfFrame).
  void frameDone( std::span<std::uint16_t const> pixels, std::uint32_t width, std::uint32_t height );
  /// The APU's sound, at the end of each frame.
  void audioDone( std::span<std::int16_t const> samples );

  /// The debugger's hooks; each does nothing without an observer.
  void setBusObserver( BusObserver* observer );

  void processMemoryAccess( std::uint32_t address, std::uint16_t value, bool word, MemoryOperationType type )
  {
    if ( mObserver != nullptr )
    {
      mObserver->memoryAccess( address, value, word, type );
    }
  }

  void processPortAccess( std::uint16_t port, std::uint16_t value, bool word, bool write )
  {
    if ( mObserver != nullptr )
    {
      mObserver->portAccess( port, value, word, write );
    }
  }

  /// The machine's state for a save state, every part's, as Mesen2's
  /// WsConsole::Serialize lists them.
  template <class Archive>
  void serialize( Archive& archive );

private:
  void initPostBootRomState();

  Config mConfig;
  SystemListener* mListener;
  BusObserver* mObserver{};

  std::vector<std::uint8_t> mWorkRam;
  std::vector<std::uint8_t> mSaveRam;
  std::vector<std::uint8_t> mBootRom;
  std::vector<std::uint8_t> mPrgRom;
  std::vector<std::uint8_t> mInternalEepromData;
  std::vector<std::uint8_t> mCartEepromData;

  std::unique_ptr<Cpu> mCpu;
  std::unique_ptr<Ppu> mPpu;
  std::unique_ptr<Apu> mApu;
  std::unique_ptr<Cart> mCart;
  std::unique_ptr<Timer> mTimer;
  std::unique_ptr<Serial> mSerial;
  std::unique_ptr<MemoryManager> mMemoryManager;
  std::unique_ptr<ControlManager> mControlManager;
  std::unique_ptr<DmaController> mDmaController;
  std::unique_ptr<Eeprom> mInternalEeprom;
  std::unique_ptr<Eeprom> mCartEeprom;

  bool mVerticalMode{};
};

} // namespace ws::machine
