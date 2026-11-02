#include <catch2/catch_test_macros.hpp>

#include "machine/Cart.hpp"
#include "machine/MemoryManager.hpp"
#include "machine/System.hpp"

#include <cstdint>
#include <span>
#include <vector>

// The WonderWitch's flash chip, an MBM29DL400TC, on a cartridge of its own:
// a Bandai 2003 and 512 KB whose code is a jump to itself. The chip is
// written as the CPU writes it, through the 2003's $CE window, and held to
// its datasheet's command definitions (table 8), status flags (table 9) and
// sectors. AthenaOS's driver, which runs on the hardware, is held to it by
// tests/roms/flash ([test-roms]).

namespace
{

using ws::machine::Config;
using ws::machine::Model;
using ws::machine::System;

constexpr std::uint32_t FLASH_SIZE = 512 * 1024;
constexpr std::uint32_t WINDOW = 0x10000;

class Silent final : public ws::machine::SystemListener
{
public:
  void
  frameDone( std::span<std::uint16_t const> /*pixels*/, std::uint32_t /*width*/, std::uint32_t /*height*/ ) override
  {
  }

  void audioDone( std::span<std::int16_t const> /*samples*/ ) override {}
};

std::vector<std::uint8_t> wonderWitch( std::uint32_t size )
{
  std::vector<std::uint8_t> rom( size, 0x00 );
  std::size_t const header = rom.size() - 16;
  // FFFF:0000, where the console starts the cartridge: JMP $.
  rom[header] = 0xEB;
  rom[header + 1] = 0xFE;
  rom[header + 13] = 0x01; // the 2003
  return rom;
}

class Board
{
public:
  explicit Board( std::uint32_t size = FLASH_SIZE )
      : mSystem{ Config{ .model = Model::COLOR }, wonderWitch( size ), {}, {}, mListener }
  {
    // The ROM where the SRAM is, its 64 KB bank 1 first.
    select( 1 );
    mSystem.getCart()->writePort( 0xCE, 0x01 );
  }

  /// Shows the chip's 64 KB `bank` in the window.
  void select( std::uint8_t bank )
  {
    mSystem.getCart()->writePort( 0xC1, bank );
  }

  void write( std::uint32_t offset, std::uint8_t value )
  {
    mSystem.getMemoryManager()->internalWrite( WINDOW + offset, value );
  }

  std::uint8_t read( std::uint32_t offset )
  {
    return mSystem.getMemoryManager()->internalRead( WINDOW + offset );
  }

  /// A command of table 8: the two unlock writes, then `command` at $AAA.
  void command( std::uint8_t command )
  {
    write( 0xAAA, 0xAA );
    write( 0x555, 0x55 );
    write( 0xAAA, command );
  }

  void run( std::uint64_t cycles )
  {
    std::uint64_t const end = mSystem.getMasterClock() + cycles;
    while ( mSystem.getMasterClock() < end )
    {
      mSystem.exec();
    }
  }

  [[nodiscard]] std::span<std::uint8_t const> rom() const
  {
    return std::as_const( mSystem ).getMemory( ws::machine::MemoryType::PRG_ROM );
  }

  [[nodiscard]] bool hasFlash()
  {
    return mSystem.getCart()->getFlash() != nullptr;
  }

private:
  Silent mListener;
  System mSystem;
};

} // namespace

TEST_CASE( "only a Bandai 2003 of 512 KB has the flash chip", "[machine][flash]" )
{
  REQUIRE( Board{}.hasFlash() );
  Board maskRom{ 1024 * 1024 };
  REQUIRE( !maskRom.hasFlash() );
  maskRom.command( 0xA0 );
  maskRom.write( 0x10, 0x00 );
  maskRom.run( 100 );
  REQUIRE( maskRom.read( 0x10 ) == 0x00 );
  REQUIRE( maskRom.rom()[0x10010] == 0x00 );
}

TEST_CASE( "autoselect gives Fujitsu's and the MBM29DL400TC's codes", "[machine][flash]" )
{
  Board board;
  board.command( 0x90 );
  REQUIRE( board.read( 0x00 ) == 0x04 );
  REQUIRE( board.read( 0x01 ) == 0x00 );
  REQUIRE( board.read( 0x02 ) == 0x0C );
  REQUIRE( board.read( 0x03 ) == 0x22 );
  REQUIRE( board.read( 0x04 ) == 0x00 ); // the sector is not protected
  board.write( 0x00, 0xF0 );
  REQUIRE( board.read( 0x02 ) == 0x00 );
}

TEST_CASE( "a byte programs in 8 us, polled by DQ7 and DQ6, and only clears bits", "[machine][flash]" )
{
  Board board;
  board.command( 0xA0 );
  board.write( 0x1234, 0x5A );
  // While it programs: DQ7 the complement of the bit written, DQ6 toggling.
  std::uint8_t const first = board.read( 0x1234 );
  std::uint8_t const second = board.read( 0x1234 );
  REQUIRE( ( first & 0x80 ) == 0x80 );
  REQUIRE( ( first ^ second ) == 0x40 );
  board.run( 30 );
  REQUIRE( board.read( 0x1234 ) == 0x00 ); // 0x00 & 0x5A
  REQUIRE( board.rom()[0x11234] == 0x00 );

  // A 0 cannot be programmed back to a 1.
  board.command( 0x80 );
  board.command( 0x10 );
  board.run( std::uint64_t{ ws::machine::CPU_CLOCK_RATE } * 19 );
  board.command( 0xA0 );
  board.write( 0x1234, 0x5A );
  board.run( 30 );
  board.command( 0xA0 );
  board.write( 0x1234, 0xFF );
  board.run( 30 );
  REQUIRE( board.read( 0x1234 ) == 0x5A );
}

TEST_CASE( "a sector erases in a second, while the other bank reads as memory", "[machine][flash]" )
{
  Board board;
  board.command( 0x80 );
  board.write( 0xAAA, 0xAA );
  board.write( 0x555, 0x55 );
  board.write( 0x8000, 0x30 ); // in sector 1, $10000-$1FFFF

  // DQ3 is clear while more sectors may still be added, set once it erases.
  REQUIRE( ( board.read( 0x8000 ) & 0x08 ) == 0x00 );
  board.run( 200 );
  std::uint8_t const status = board.read( 0x8000 );
  REQUIRE( ( status & 0x88 ) == 0x08 );
  // DQ2 toggles in a sector being erased.
  REQUIRE( ( ( status ^ board.read( 0x8000 ) ) & 0x44 ) == 0x44 );

  // Bank 1, the top 128 KB, reads as memory meanwhile.
  board.select( 7 );
  REQUIRE( board.read( 0xFFF0 ) == 0xEB );
  board.select( 1 );

  board.run( ws::machine::CPU_CLOCK_RATE );
  REQUIRE( board.read( 0x8000 ) == 0xFF );
  REQUIRE( board.rom()[0x10000] == 0xFF );
  REQUIRE( board.rom()[0x1FFFF] == 0xFF );
  // The sectors either side are left as they were.
  REQUIRE( board.rom()[0x0FFFF] == 0x00 );
  REQUIRE( board.rom()[0x20000] == 0x00 );
}

TEST_CASE( "Fast Mode programs with two writes, until its reset", "[machine][flash]" )
{
  Board board;
  board.command( 0x80 );
  board.write( 0xAAA, 0xAA );
  board.write( 0x555, 0x55 );
  board.write( 0x0000, 0x30 );
  board.run( ws::machine::CPU_CLOCK_RATE + 200 );

  board.command( 0x20 );
  for ( std::uint8_t i = 0; i < 4; ++i )
  {
    board.write( i, 0xA0 );
    board.write( i, static_cast<std::uint8_t>( 0x10 + i ) );
    board.run( 30 );
  }
  board.write( 0x0000, 0x90 );
  board.write( 0x0000, 0xF0 );
  for ( std::uint8_t i = 0; i < 4; ++i )
  {
    REQUIRE( board.read( i ) == 0x10 + i );
  }
}

TEST_CASE( "a command out of sequence leaves the chip reading memory", "[machine][flash]" )
{
  Board board;
  board.write( 0xAAA, 0xAA );
  board.write( 0x554, 0x55 ); // the wrong address
  board.write( 0xAAA, 0xA0 );
  board.write( 0x0100, 0xFF );
  board.run( 30 );
  REQUIRE( board.read( 0x0100 ) == 0x00 );
}
