#pragma once

// The WonderWitch's flash chip, a Fujitsu MBM29DL400TC of 512 KB, top boot,
// as the Bandai 2003 writes it through its $CE window. Mesen2 has none at
// b9fa69ddc6d0a331fb103fdb5eef6904305703c2
// (docs/decisions/0016-the-flash-follows-its-datasheet.md). Its commands,
// sectors, status flags and typical times are its datasheet's, "MBM29DL400TC
// -55/-70/-90/-12"; where the datasheet is silent, what a read gives in Fast
// Mode is ares's, ares/ws/cartridge/flash.cpp at
// 4307bf2eff6e4f9a277fdc6f9d4f4de177b3f932 (ISC). AthenaOS's driver, which
// runs on the hardware, is held to it by tests/roms/flash.

#include "ws/machine/Types.hpp"

#include <cstdint>
#include <span>

namespace ws::machine
{

class System;

class Flash
{
public:
  /// The chip's size: a cartridge of a Bandai 2003 and a ROM this large is
  /// taken to be a WonderWitch's.
  static constexpr std::uint32_t SIZE = 512 * 1024;

  Flash( System& system, FlashState& state );

  /// Whether reads give the contents, so that the ROM can be read without
  /// asking the chip.
  [[nodiscard]] bool readsArray() const;

  /// A read or a write at `offset` into the chip.
  std::uint8_t read( std::uint32_t offset );
  void write( std::uint32_t offset, std::uint8_t value );

  /// Ends a program or an erase whose time has come.
  void run();

private:
  /// Changes the mode, and the mappings with it where reads stop or start
  /// giving the contents.
  void setMode( FlashMode mode );
  void startProgram( std::uint32_t offset, std::uint8_t value );
  void startSectorErase( std::uint32_t offset );
  void startChipErase();
  /// What a read in the bank being programmed or erased gives: the status
  /// flags.
  [[nodiscard]] std::uint8_t status( std::uint32_t offset );
  [[nodiscard]] static std::uint8_t autoselect( std::uint32_t offset );
  /// Whether `offset` is in the bank being programmed or erased; the other
  /// bank reads as memory meanwhile.
  [[nodiscard]] bool busyBank( std::uint32_t offset ) const;
  /// The chip's contents: the machine's ROM, asked for each time, as a
  /// state loaded may have moved it.
  [[nodiscard]] std::span<std::uint8_t> contents() const;

  System* mSystem;
  FlashState* mState;
};

} // namespace ws::machine
