#include <catch2/catch_test_macros.hpp>

#include "machine/Cart.hpp"
#include "machine/Rtc.hpp"
#include "machine/System.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

// The Bandai 2003's RTC and the S-3511A behind it, on a cartridge of its own:
// 64 KB whose code is a jump to itself, so that the machine's time passes
// and nothing else happens. The 2003's protocol is held to the hardware by
// ws-test-suite's rtc/mapper ([test-roms]); what is held here is the
// S-3511A's calendar and its checks, as the WSdev wiki's "Real-Time Clock"
// page describes them.

namespace
{

using ws::machine::Cart;
using ws::machine::Config;
using ws::machine::Model;
using ws::machine::System;

constexpr std::uint64_t SECOND = ws::machine::CPU_CLOCK_RATE;

class Silent final : public ws::machine::SystemListener
{
public:
  void
  frameDone( std::span<std::uint16_t const> /*pixels*/, std::uint32_t /*width*/, std::uint32_t /*height*/ ) override
  {
  }

  void audioDone( std::span<std::int16_t const> /*samples*/ ) override {}
};

std::vector<std::uint8_t> cartridge2003()
{
  std::vector<std::uint8_t> rom( 0x10000, 0xFF );
  std::size_t const header = rom.size() - 16;
  // FFFF:0000, where the console starts the cartridge: JMP $.
  rom[header] = 0xEB;
  rom[header + 1] = 0xFE;
  rom[header + 5] = 0x00;
  rom[header + 11] = 0x00;
  rom[header + 13] = 0x01; // the 2003
  return rom;
}

class Board
{
public:
  Board() : mSystem{ Config{ .model = Model::MONOCHROME }, cartridge2003(), {}, {}, mListener } {}

  Cart& cart()
  {
    return *mSystem.getCart();
  }

  void run( std::uint64_t cycles )
  {
    std::uint64_t const end = mSystem.getMasterClock() + cycles;
    while ( mSystem.getMasterClock() < end )
    {
      mSystem.exec();
    }
  }

  /// Runs until the 2003 has a byte ready, or the command is done.
  void waitReady()
  {
    for ( int i = 0; i < 10'000 && ( cart().readPort( 0xCA ) & 0x80U ) == 0; ++i )
    {
      run( 8 );
    }
  }

  /// Sends command `command` with `bytes`, as a game does: the first byte
  /// in $CB before the command, each next once the 2003 is ready for it.
  void write( std::uint8_t command, std::initializer_list<std::uint8_t> bytes )
  {
    auto byte = bytes.begin();
    cart().writePort( 0xCB, byte != bytes.end() ? *byte++ : 0 );
    cart().writePort( 0xCA, command );
    for ( ; byte != bytes.end(); ++byte )
    {
      waitReady();
      cart().writePort( 0xCB, *byte );
    }
    waitReady();
    REQUIRE( ( cart().readPort( 0xCA ) & 0x10U ) == 0 );
  }

  std::vector<std::uint8_t> read( std::uint8_t command, std::size_t count )
  {
    cart().writePort( 0xCA, command );
    std::vector<std::uint8_t> bytes;
    for ( std::size_t i = 0; i < count; ++i )
    {
      waitReady();
      bytes.push_back( cart().readPort( 0xCB ) );
    }
    REQUIRE( ( cart().readPort( 0xCA ) & 0x10U ) == 0 );
    return bytes;
  }

  std::vector<std::uint8_t> dateTime()
  {
    return read( 0x15, 7 );
  }

  /// The year, month and day of month.
  std::vector<std::uint8_t> date()
  {
    std::vector<std::uint8_t> bytes = dateTime();
    bytes.resize( 3 );
    return bytes;
  }

private:
  Silent mListener;
  System mSystem;
};

using Bytes = std::vector<std::uint8_t>;

} // namespace

TEST_CASE( "a new battery leaves the S-3511A as the wiki's power-on state", "[machine][rtc]" )
{
  Board board;
  // Reading the configuration clears its power failure bit.
  REQUIRE( board.read( 0x13, 1 ) == Bytes{ 0x82 } );
  REQUIRE( board.read( 0x13, 1 ) == Bytes{ 0x02 } );
  REQUIRE( board.dateTime() == Bytes{ 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 } );
  REQUIRE( board.read( 0x19, 2 ) == Bytes{ 0xFF, 0xFF } );
}

TEST_CASE( "the clock counts the machine's seconds across the century", "[machine][rtc]" )
{
  Board board;
  board.write( 0x12, { 0x40 } ); // 24 hours
  board.write( 0x14, { 0x99, 0x12, 0x31, 0x06, 0x23, 0x59, 0x59 } );
  REQUIRE( board.dateTime() == Bytes{ 0x99, 0x12, 0x31, 0x06, 0xA3, 0x59, 0x59 } );
  board.run( SECOND );
  REQUIRE( board.dateTime() == Bytes{ 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 } );
}

TEST_CASE( "February has a 29th every fourth year, 00 among them", "[machine][rtc]" )
{
  Board board;
  board.write( 0x12, { 0x40 } );
  for ( std::uint8_t const year : std::array<std::uint8_t, 2>{ 0x00, 0x24 } )
  {
    board.write( 0x14, { year, 0x02, 0x28, 0x03, 0x23, 0x59, 0x59 } );
    board.run( SECOND );
    REQUIRE( board.date() == Bytes{ year, 0x02, 0x29 } );
  }
  board.write( 0x14, { 0x23, 0x02, 0x28, 0x03, 0x23, 0x59, 0x59 } );
  board.run( SECOND );
  REQUIRE( board.date() == Bytes{ 0x23, 0x03, 0x01 } );
}

TEST_CASE( "the 12-hour clock turns at 11 to 0, and the day at 11 PM", "[machine][rtc]" )
{
  Board board;
  board.write( 0x12, { 0x00 } );
  board.write( 0x14, { 0x26, 0x05, 0x10, 0x00, 0x11, 0x59, 0x59 } );
  board.run( SECOND );
  REQUIRE( board.read( 0x17, 3 ) == Bytes{ 0x80, 0x00, 0x00 } );
  board.write( 0x16, { 0x91, 0x59, 0x59 } );
  board.run( SECOND );
  REQUIRE( board.dateTime() == Bytes{ 0x26, 0x05, 0x11, 0x01, 0x00, 0x00, 0x00 } );
}

TEST_CASE( "a value the S-3511A cannot hold is written as the wiki says", "[machine][rtc]" )
{
  Board board;
  board.write( 0x12, { 0x40 } );
  // Year, month, day of month, day of week, hour, minute: each its default.
  board.write( 0x14, { 0x9A, 0x13, 0x32, 0x07, 0x24, 0x60, 0x00 } );
  REQUIRE( board.dateTime() == Bytes{ 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 } );
  // The 31st of a month of 30 days moves on to the first of the next.
  board.write( 0x14, { 0x26, 0x04, 0x31, 0x02, 0x10, 0x00, 0x00 } );
  REQUIRE( board.date() == Bytes{ 0x26, 0x05, 0x01 } );
  // A second it cannot count is kept, and rolls over at the next.
  board.write( 0x16, { 0x10, 0x20, 0x7A } );
  REQUIRE( board.read( 0x17, 3 ) == Bytes{ 0x10, 0x20, 0x7A } );
  board.run( SECOND );
  REQUIRE( board.read( 0x17, 3 ) == Bytes{ 0x10, 0x21, 0x00 } );
}

TEST_CASE( "the reset command clears every register", "[machine][rtc]" )
{
  Board board;
  board.write( 0x14, { 0x26, 0x11, 0x02, 0x01, 0x09, 0x30, 0x00 } );
  board.write( 0x18, { 0x07, 0x15 } );
  board.write( 0x10, {} );
  REQUIRE( board.read( 0x13, 1 ) == Bytes{ 0x00 } );
  REQUIRE( board.dateTime() == Bytes{ 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00 } );
}

TEST_CASE( "a clock put back runs on by the time it was away", "[machine][rtc]" )
{
  Board board;
  board.write( 0x12, { 0x40 } );
  board.write( 0x14, { 0x26, 0x12, 0x30, 0x03, 0x22, 0x00, 0x00 } );
  auto const saved = board.cart().getRtc()->registers();

  Board later;
  // Two days, an hour, a minute and a second.
  later.cart().getRtc()->restore( saved, ( 2 * 86'400 ) + 3'661 );
  REQUIRE( later.dateTime() == Bytes{ 0x27, 0x01, 0x01, 0x05, 0xA3, 0x01, 0x01 } );
}
