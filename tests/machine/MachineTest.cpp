#include <catch2/catch_test_macros.hpp>

#include "ws/Emulator.hpp"
#include "ws/machine/Machine.hpp"

#include <algorithm>
#include <filesystem>
#include <set>

// A whole machine needs a cartridge, and the boot ROM for a start as the
// hardware makes it; none is carried in the tree. These tests run the owner's
// public-domain cartridges and skip without them.

namespace
{

using ws::machine::Machine;
using ws::machine::Model;

constexpr char const* ROM_DIR = WS_TEST_ROM_DIR;
constexpr char const* BIOS_DIR = WS_TEST_BIOS_DIR;

/// The emulator with `cartridge` in, or nothing when the file is not there.
std::unique_ptr<ws::Emulator> withCartridge( std::string const& cartridge, bool bootRom )
{
  std::filesystem::path const path = std::filesystem::path{ ROM_DIR } / cartridge;
  if ( !std::filesystem::exists( path ) || ( bootRom && !std::filesystem::exists( BIOS_DIR ) ) )
  {
    return nullptr;
  }
  ws::Settings settings;
  settings.biosDirectory = bootRom ? std::filesystem::path{ BIOS_DIR } : std::filesystem::path{};
  settings.useBootRom = bootRom;
  settings.cartridge = path;
  return std::make_unique<ws::Emulator>( std::move( settings ) );
}

/// How many colours the picture shows.
std::size_t coloursOf( Machine const& machine )
{
  std::set<std::uint32_t> colours;
  auto const picture = machine.picture();
  for ( std::size_t i = 0; i + 3 < picture.size(); i += 4 )
  {
    colours.insert( static_cast<std::uint32_t>( picture[i] | ( picture[i + 1] << 8U ) | ( picture[i + 2] << 16U ) ) );
  }
  return colours.size();
}

} // namespace

TEST_CASE( "a frame is 159 lines of 256 cycles", "[machine][roms]" )
{
  auto const emulator = withCartridge( "Hello World by Sir Dragoon (PD).ws", false );
  if ( !emulator )
  {
    SKIP( "the cartridge is not in " << ROM_DIR );
  }
  Machine& machine = *emulator->machine();
  machine.runFrames( 2 );
  auto const result = machine.runFrames( 10 );
  REQUIRE( result.frames == 10 );
  // Each run ends at the instruction boundary after its last frame boundary.
  REQUIRE( result.cycles >= ( 10 * 159 * 256 ) - 64 );
  REQUIRE( result.cycles <= ( 10 * 159 * 256 ) + 64 );
}

TEST_CASE( "a cartridge started without the boot ROM draws its picture", "[machine][roms]" )
{
  auto const emulator = withCartridge( "Hello World by Sir Dragoon (PD).ws", false );
  if ( !emulator )
  {
    SKIP( "the cartridge is not in " << ROM_DIR );
  }
  Machine& machine = *emulator->machine();
  machine.runFrames( 120 );
  REQUIRE( machine.pictureWidth() == ws::machine::SCREEN_WIDTH );
  REQUIRE( machine.pictureHeight() == ws::machine::SCREEN_HEIGHT );
  REQUIRE( coloursOf( machine ) > 1 );
}

TEST_CASE( "the boot ROM hands over to the cartridge", "[machine][roms]" )
{
  auto const emulator = withCartridge( "Swandriving (PD).wsc", true );
  if ( !emulator || !emulator->bootRomUsed() )
  {
    SKIP( "the cartridge or the Color's boot ROM is not there" );
  }
  Machine& machine = *emulator->machine();
  REQUIRE( machine.model() == Model::COLOR );
  // The IPL runs from the top of the address space, then switches itself out
  // through port $A0 and jumps to the cartridge's FFFF:0000.
  REQUIRE( ( machine.peekPort( 0xA0 ) & 0x01U ) == 0 );
  machine.runFrames( 450 );
  REQUIRE( ( machine.peekPort( 0xA0 ) & 0x01U ) != 0 );
  REQUIRE( coloursOf( machine ) > 1 );
}

TEST_CASE( "a state loaded runs as the machine it was saved from", "[machine][roms]" )
{
  auto const emulator = withCartridge( "WonderPong by LiraNuna (PD).wsc", false );
  if ( !emulator )
  {
    SKIP( "the cartridge is not in " << ROM_DIR );
  }
  Machine& machine = *emulator->machine();
  machine.runFrames( 30 );
  auto const state = machine.saveState();
  machine.runFrames( 30 );
  std::vector<std::uint8_t> const picture{ machine.picture().begin(), machine.picture().end() };
  auto const cpu = machine.cpu();

  REQUIRE( machine.loadState( state ) );
  machine.runFrames( 30 );
  REQUIRE( std::ranges::equal( machine.picture(), picture ) );
  REQUIRE( machine.cpu().cycleCount == cpu.cycleCount );
  REQUIRE( machine.cpu().ip == cpu.ip );
}

TEST_CASE( "a breakpoint stops before its instruction, and the run goes on from it", "[machine][roms]" )
{
  auto const emulator = withCartridge( "Hello World by Sir Dragoon (PD).ws", false );
  if ( !emulator )
  {
    SKIP( "the cartridge is not in " << ROM_DIR );
  }
  // Where the 10th instruction after power-up is, read from the history: in
  // the start-up code, which runs once and on, before the cartridge's last
  // loop, a jump to itself.
  emulator->machine()->step( 20 );
  auto const traced = emulator->machine()->tracedAt( 10 );
  std::uint32_t const address = ( ( traced.cs * 16U ) + traced.ip ) & 0xFFFFFU;

  REQUIRE( emulator->powerUp() );
  Machine& machine = *emulator->machine();
  machine.addBreakpoint( address );
  auto const stopped = machine.runFrames( 5 );
  REQUIRE( stopped.reason == ws::machine::StopReason::BREAKPOINT );
  REQUIRE( stopped.breakpoint == address );
  REQUIRE( machine.pc() == address );

  auto const stepped = machine.step( 1 );
  REQUIRE( stepped.reason == ws::machine::StopReason::COMPLETED );
  REQUIRE( machine.pc() != address );
}
