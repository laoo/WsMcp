#pragma once

// The cartridge's mapper: the Bandai 2001's and 2003's banks, ports $C0-$C3,
// and the EEPROM's ports $C4-$C8, ported from Mesen2's Core/WS/Carts/WsCart.h
// and WsCart.cpp at b9fa69ddc6d0a331fb103fdb5eef6904305703c2. What only the
// 2003 has, which Mesen2 does not port, follows the WSdev wiki's "Bandai
// 2003" page (docs/decisions/0015-the-rtc-follows-the-wsdev-wiki.md): the RTC
// at $CA-$CB (Rtc), the general-purpose outputs at $CC-$CD, the ROM in the
// SRAM's window at $CE, and the banks' high bits at $CF-$D5, which ares's
// ares/ws/cartridge/io.cpp at 4307bf2eff6e4f9a277fdc6f9d4f4de177b3f932 (ISC)
// reads and writes as they are here. A WonderWitch's flash chip is Flash.

#include "Flash.hpp"
#include "Rtc.hpp"
#include "System.hpp"

#include <cstdint>
#include <optional>

namespace ws::machine
{

class Cart
{
public:
  /// `bandai2003` when the header names the 2003, which has an RTC;
  /// `flash` when its ROM is a WonderWitch's flash chip.
  Cart( System& system, bool bandai2003, bool flash );

  void refreshMappings();

  CartState& getState()
  {
    return mState;
  }

  [[nodiscard]] Eeprom* getEeprom() const;
  /// The RTC, or null on a 2001.
  [[nodiscard]] Rtc* getRtc();
  /// The flash chip, or null for a mask ROM.
  [[nodiscard]] Flash* getFlash();

  /// A read or a write the memory manager sends to the flash chip, at a
  /// CPU address in a window of the ROM; and the chip's contents there, as a
  /// debugger reads them.
  std::uint8_t readFlash( std::uint32_t address );
  void writeFlash( std::uint32_t address, std::uint8_t value );
  [[nodiscard]] std::uint8_t peekFlash( std::uint32_t address ) const;

  std::uint8_t readPort( std::uint16_t port );
  /// A port as the debugger reads it: the RTC's as it last stood, its clock
  /// not brought up to now and its command not moved on.
  [[nodiscard]] std::uint8_t peekPort( std::uint16_t port );
  void writePort( std::uint16_t port, std::uint8_t value );

  /// Once a frame: the RTC's clock brought up to now.
  void run();

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mState );
  }

private:
  /// A 2003 port, $CA-$D5, or nothing for one that answers nothing.
  [[nodiscard]] std::optional<std::uint8_t> read2003( Rtc& rtc, std::uint16_t port );
  void write2003( Rtc& rtc, std::uint16_t port, std::uint8_t value );
  /// Where in the ROM the CPU address `address` reaches, through the bank
  /// of its window.
  [[nodiscard]] std::uint32_t romOffset( std::uint32_t address ) const;

  System* mSystem;
  CartState mState{};
  std::optional<Rtc> mRtc;
  std::optional<Flash> mFlash;
};

} // namespace ws::machine
