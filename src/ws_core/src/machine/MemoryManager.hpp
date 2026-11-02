#pragma once

// The 20-bit address space, the I/O ports and the interrupt controller.
// Ported from Mesen2's Core/WS/WsMemoryManager.h and WsMemoryManager.cpp at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2.
//
// Every bus cycle the CPU makes goes through read(), write(), readPort() or
// writePort(), and each of those first runs exec(): the cycle's PPU dot and
// APU clock. That is how the machine is stepped a cycle at a time
// (docs/decisions/0008-stepped-by-the-cpus-cycles.md).

#include "Apu.hpp"
#include "Ppu.hpp"
#include "System.hpp"

#include "cpu/Cpu.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

namespace ws::machine
{

class MemoryManager
{
public:
  explicit MemoryManager( System& system );

  /// Takes the other parts and maps the memories in, once every part exists.
  void init();

  MemoryManagerState& getState()
  {
    return mState;
  }

  /// Maps the memories in again, after a bank, the colour mode or the boot
  /// ROM's switch changed.
  void refreshMappings();

  [[nodiscard]] std::uint8_t getUnmappedPort() const;

  void map( std::uint32_t start, std::uint32_t end, MemoryType type, std::uint32_t offset, bool readonly );
  void unmap( std::uint32_t start, std::uint32_t end );

  /// A cycle of the bus: the CPU's clock, a PPU dot, an APU clock.
  void exec()
  {
    mCpu->incCycleCount();
    mPpu->exec();
    mApu->run();
  }

  [[nodiscard]] std::uint8_t internalRead( std::uint32_t addr ) const
  {
    std::uint8_t const* handler = mReads[( addr >> 12U ) & 0xFFU];
    std::uint8_t value = 0x90;
    if ( handler != nullptr )
    {
      value = handler[addr & 0xFFFU];
    }
    return value;
  }

  void internalWrite( std::uint32_t addr, std::uint8_t value )
  {
    std::uint8_t* handler = mWrites[( addr >> 12U ) & 0xFFU];
    if ( handler != nullptr )
    {
      handler[addr & 0xFFFU] = value;
    }
  }

  /// A byte as the CPU would read it, without a bus cycle.
  [[nodiscard]] std::uint8_t debugRead( std::uint32_t addr ) const;
  /// Writes whatever is mapped writable at `addr`, without a bus cycle.
  void debugWrite( std::uint32_t addr, std::uint8_t value );

  template <typename T>
  [[nodiscard]] T debugCpuRead( std::uint16_t seg, std::uint16_t offset ) const;

  template <typename T>
  T read( std::uint16_t seg, std::uint16_t offset, MemoryOperationType opType = MemoryOperationType::READ )
  {
    std::uint32_t const addr = ( ( static_cast<std::uint32_t>( seg ) << 4U ) + offset ) & 0xFFFFFU;
    if constexpr ( std::is_same_v<T, std::uint16_t> )
    {
      bool const splitReads = !isWordBus( addr ) || ( addr & 0x01U ) != 0;
      if ( splitReads )
      {
        auto const lo = read<std::uint8_t>( seg, offset, opType );
        auto const hi = read<std::uint8_t>( seg, static_cast<std::uint16_t>( offset + 1 ), opType );
        return static_cast<std::uint16_t>( lo | ( hi << 8U ) );
      }
      exec();
      std::uint8_t const lo = internalRead( addr );
      std::uint8_t const hi = internalRead(
          ( ( static_cast<std::uint32_t>( seg ) << 4U ) + static_cast<std::uint16_t>( offset + 1 ) ) & 0xFFFFFU );
      auto const value = static_cast<std::uint16_t>( lo | ( hi << 8U ) );
      mSystem->processMemoryAccess( addr, value, true, opType );
      return value;
    }
    else
    {
      exec();
      std::uint8_t const value = internalRead( addr );
      mSystem->processMemoryAccess( addr, value, false, opType );
      return value;
    }
  }

  template <typename T>
  void
  write( std::uint16_t seg, std::uint16_t offset, T value, MemoryOperationType opType = MemoryOperationType::WRITE )
  {
    std::uint32_t const addr = ( ( static_cast<std::uint32_t>( seg ) << 4U ) + offset ) & 0xFFFFFU;
    if constexpr ( std::is_same_v<T, std::uint16_t> )
    {
      bool const splitWrites = !isWordBus( addr ) || ( addr & 0x01U ) != 0;
      if ( splitWrites )
      {
        write<std::uint8_t>( seg, offset, static_cast<std::uint8_t>( value ), opType );
        write<std::uint8_t>(
            seg, static_cast<std::uint16_t>( offset + 1 ), static_cast<std::uint8_t>( value >> 8U ), opType );
      }
      else
      {
        exec();
        mSystem->processMemoryAccess( addr, value, true, opType );
        internalWrite( addr, static_cast<std::uint8_t>( value ) );
        internalWrite( ( ( static_cast<std::uint32_t>( seg ) << 4U ) + static_cast<std::uint16_t>( offset + 1 ) ) &
                           0xFFFFFU,
                       static_cast<std::uint8_t>( value >> 8U ) );
      }
    }
    else
    {
      exec();
      mSystem->processMemoryAccess( addr, value, false, opType );
      internalWrite( addr, value );
    }
  }

  template <typename T>
  T readPort( std::uint16_t port );
  template <typename T>
  void writePort( std::uint16_t port, T value );
  std::uint8_t internalReadPort( std::uint16_t port, bool isWordAccess );
  void internalWritePort( std::uint16_t port, std::uint8_t value, bool isWordAccess );

  /// A port as the CPU would read it, without its side effects.
  template <typename T>
  [[nodiscard]] T debugReadPort( std::uint16_t port );

  [[nodiscard]] bool isPowerOffRequested() const
  {
    return mState.powerOffRequested;
  }

  [[nodiscard]] bool isColorEnabled() const
  {
    return mState.colorEnabled;
  }

  [[nodiscard]] bool isWordBus( std::uint32_t addr ) const;
  [[nodiscard]] std::uint8_t getWaitStates( std::uint32_t addr ) const;

  void setIrqSource( IrqSource src );
  void clearIrqSource( IrqSource src );
  std::uint8_t getActiveIrqs();
  std::uint8_t getIrqVector();

  bool hasPendingIrq()
  {
    return getActiveIrqs() != 0;
  }

  /// Brings the EEPROMs' pending commands to their end, before a debugger
  /// looks at them.
  void onBeforeBreak();

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
    if constexpr ( Archive::READING )
    {
      refreshMappings();
    }
  }

private:
  [[nodiscard]] static bool isWordPort( std::uint16_t port );
  [[nodiscard]] std::uint8_t getPortWaitStates( std::uint16_t port ) const;
  [[nodiscard]] static bool isUnmappedPort( std::uint16_t port );

  System* mSystem;
  Cpu* mCpu{};
  Ppu* mPpu{};
  Apu* mApu{};

  MemoryManagerState mState{};

  std::array<std::uint8_t*, 256> mReads{};
  std::array<std::uint8_t*, 256> mWrites{};
};

} // namespace ws::machine
