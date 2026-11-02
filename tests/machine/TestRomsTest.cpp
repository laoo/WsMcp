#include <catch2/catch_test_macros.hpp>

#include "ws/Emulator.hpp"
#include "ws/machine/Machine.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The hardware test ROMs scripts/fetch-test-roms.sh puts in
// build/tools/testroms: FluBBaOfWard's WSCpuTest, WSHWTest and WSTimingTest,
// each made on and passing on the hardware. They run with the owner's boot
// ROMs, as on the hardware, and skip without the ROMs. What the emulator
// still does otherwise is docs/hardware/departures.md.

namespace
{

using ws::machine::Button;
using ws::machine::Machine;
using ws::machine::Model;
using ws::machine::StopReason;

constexpr char const* TEST_ROM_DIR = WS_TEST_ROM_TOOLS_DIR;
constexpr char const* BIOS_DIR = WS_TEST_BIOS_DIR;

std::filesystem::path testRom( std::string_view relative )
{
  return std::filesystem::path{ TEST_ROM_DIR } / relative;
}

/// The emulator running `rom` as `model`, with its boot ROM; nothing when
/// either is not there.
std::unique_ptr<ws::Emulator> running( std::filesystem::path const& rom, Model model )
{
  if ( !std::filesystem::exists( rom ) || !std::filesystem::exists( BIOS_DIR ) )
  {
    return nullptr;
  }
  ws::Settings settings;
  settings.biosDirectory = BIOS_DIR;
  settings.model = model;
  settings.cartridge = rom;
  auto emulator = std::make_unique<ws::Emulator>( std::move( settings ) );
  if ( emulator->machine() == nullptr || !emulator->bootRomUsed() )
  {
    return nullptr;
  }
  return emulator;
}

std::uint16_t bit( Button button )
{
  return static_cast<std::uint16_t>( 1U << static_cast<unsigned>( button ) );
}

/// Holds `button` for as long as the test programs' menus take to see it,
/// then lets it go for as long again.
void press( Machine& machine, Button button )
{
  machine.setButtons( bit( button ) );
  machine.runFrames( 12 );
  machine.setButtons( 0 );
  machine.runFrames( 12 );
}

/// Where a label of a NASM listing is: the offset of the first byte after
/// it, in its section.
std::optional<std::uint32_t> labelOffset( std::filesystem::path const& listing, std::string_view label )
{
  std::ifstream file{ listing };
  std::string line;
  bool found = false;
  std::string const wanted = std::string{ label } + ":";
  while ( std::getline( file, line ) )
  {
    // "  254 0000015A C1E306   sal bx, 6": a line number, then the offset.
    if ( !found )
    {
      found = line.contains( wanted ) && !line.contains( ';' );
      continue;
    }
    std::string_view const text{ line };
    std::size_t const start = text.find_first_not_of( ' ' );
    std::size_t const offsetAt = start == std::string_view::npos ? start : text.find( ' ', start );
    if ( offsetAt == std::string_view::npos || offsetAt + 9 > text.size() || text[offsetAt + 1] == ' ' )
    {
      continue;
    }
    std::uint32_t offset = 0;
    auto const [end, error] = std::from_chars( text.data() + offsetAt + 1, text.data() + offsetAt + 9, offset, 16 );
    if ( error == std::errc{} && end == text.data() + offsetAt + 9 )
    {
      return offset;
    }
  }
  return std::nullopt;
}

/// What WSHWTest prints through INT 10h once `start` is pressed, a character
/// a call to the handler it installed, until it prints `until`, its menu's
/// title, again.
std::string printed( Machine& machine, Button start, std::string_view until, std::uint64_t timeoutFrames )
{
  std::uint32_t const vector =
      static_cast<std::uint32_t>( machine.peek( 0x40 ) | ( machine.peek( 0x41 ) << 8U ) ) |
      ( static_cast<std::uint32_t>( machine.peek( 0x42 ) | ( machine.peek( 0x43 ) << 8U ) ) << 4U );
  machine.addBreakpoint( vector & 0xFFFFFU );
  machine.setButtons( bit( start ) );
  std::uint64_t const released = machine.frame() + 12;
  std::uint64_t const end = machine.frame() + timeoutFrames;
  std::string text;
  while ( !text.contains( until ) && machine.frame() < end )
  {
    if ( machine.frame() >= released )
    {
      machine.setButtons( 0 );
    }
    if ( machine.runFrames( 1 ).reason == StopReason::BREAKPOINT )
    {
      text += static_cast<char>( machine.cpu().ax & 0xFFU );
    }
  }
  machine.setButtons( 0 );
  machine.removeBreakpoint( vector & 0xFFFFFU );
  return text;
}

std::size_t count( std::string_view text, std::string_view what )
{
  std::size_t found = 0;
  for ( std::size_t at = text.find( what ); at != std::string_view::npos; at = text.find( what, at + 1 ) )
  {
    ++found;
  }
  return found;
}

} // namespace

TEST_CASE( "WSHWTest passes its interrupt, timer, I/O register and noise tests", "[test-roms]" )
{
  for ( Model const model : { Model::MONOCHROME, Model::COLOR, Model::SWAN_CRYSTAL } )
  {
    CAPTURE( ws::modelName( model ) );
    auto const emulator = running( testRom( "src/WSHWTest/WSHWTest.wsc" ), model );
    if ( !emulator )
    {
      SKIP( "WSHWTest or the boot ROMs are not there: scripts/fetch-test-roms.sh" );
    }
    Machine& machine = *emulator->machine();
    machine.runFrames( 400 );
    press( machine, Button::DOWN ); // "Test All."
    // "Test All." waits for a key at its end: it is read until then.
    std::string const text = printed( machine, Button::A, "WonderSwan HW Test", 3000 );
    CAPTURE( text );
    REQUIRE( count( text, "Noise Mode:" ) == 8 );
    REQUIRE( count( text, "Ok!" ) == 17 );
    REQUIRE( count( text, "Failed" ) == 0 );
  }
}

TEST_CASE( "WSHWTest's channel 3 sweeps as the hardware does", "[test-roms]" )
{
  auto const emulator = running( testRom( "src/WSHWTest/WSHWTest.wsc" ), Model::COLOR );
  if ( !emulator )
  {
    SKIP( "WSHWTest or the boot ROMs are not there: scripts/fetch-test-roms.sh" );
  }
  Machine& machine = *emulator->machine();
  machine.runFrames( 400 );
  for ( int i = 0; i < 6; ++i )
  {
    press( machine, Button::DOWN ); // "Test Sound Sweep."
  }
  std::string const text = printed( machine, Button::A, "Done.", 3000 );
  CAPTURE( text );
  REQUIRE( text.contains( "Write resets Timer:" ) );
  REQUIRE( count( text, "Ok!" ) == 13 );
  REQUIRE( count( text, "Failed" ) == 0 );
}

TEST_CASE( "WSTimingTest's instructions take the hardware's cycles", "[test-roms]" )
{
  std::filesystem::path const rom = testRom( "src/WSTimingTest/timingtest.ws" );
  auto const emulator = running( rom, Model::COLOR );
  auto const okfail = labelOffset( testRom( "src/WSTimingTest/timingtest.lst" ), "okfail" );
  if ( !emulator || !okfail )
  {
    SKIP( "WSTimingTest or the boot ROMs are not there: scripts/fetch-test-roms.sh" );
  }

  // A result may differ from the hardware's by 1 either way, which
  // WSTimingTest's README says the hardware shows as well. Where the emulator
  // knowingly departs by more, by test: the hardware's value, then the
  // emulator's (docs/hardware/departures.md, "The V30MZ's timing").
  std::map<std::string, std::pair<unsigned, unsigned>, std::less<>> const departures{
    { "IN AL, DX:", { 74, 78 } },   { "EC OPIN B:", { 74, 78 } },    { "ED OPIN W:", { 74, 78 } },
    { "IRQ      :", { 293, 289 } }, { "Break/Trap:", { 261, 285 } }, { "Short Loo:", { 31, 27 } },
  };
  // "Adr EA04" runs twice, the second time after the instruction before it
  // changed SI, which costs the hardware a cycle the emulator does not take.
  std::map<std::string, std::pair<unsigned, unsigned>, std::less<>> const secondRun{
    { "Adr EA04 :", { 66, 62 } },
  };

  Machine& machine = *emulator->machine();
  std::uint32_t const breakpoint = 0xF0000U + *okfail;
  machine.addBreakpoint( breakpoint );

  std::map<std::string, unsigned, std::less<>> seen;
  std::size_t tests = 0;
  for ( unsigned page = 0; page < 40; ++page )
  {
    std::size_t const before = tests;
    // The menu's loop waits for eight lines a turn; X2 is held across one.
    if ( page > 0 )
    {
      machine.setButtons( bit( Button::RIGHT ) );
    }
    std::uint64_t const heldUntil = machine.frame() + 20;
    for ( ;; )
    {
      if ( machine.frame() >= heldUntil )
      {
        machine.setButtons( 0 );
      }
      auto const run = machine.runFrames( page == 0 && tests == 0 ? 2000 : 120 );
      if ( run.reason != StopReason::BREAKPOINT )
      {
        if ( machine.frame() < heldUntil + 120 )
        {
          continue;
        }
        break;
      }
      // At okfail: CX the hardware's value, DX the emulator's; the test's
      // name is runtest's third argument, on its frame at SS:BP + 4.
      auto const cpu = machine.cpu();
      std::uint32_t const frame = ( ( cpu.ss * 16U ) + cpu.bp + 4U ) & 0xFFFFFU;
      std::uint32_t const name =
          0xF0000U + static_cast<std::uint32_t>( machine.peek( frame ) | ( machine.peek( frame + 1 ) << 8U ) );
      std::string test;
      for ( std::uint32_t at = name; machine.peek( at ) != 0 && test.size() < 40; ++at )
      {
        test += static_cast<char>( machine.peek( at ) );
      }
      unsigned const runs = ++seen[test];
      auto const& known = runs > 1 ? secondRun : departures;
      auto const departure = known.find( test );
      CAPTURE( page, test, runs, cpu.cx, cpu.dx );
      if ( departure != known.end() )
      {
        REQUIRE( cpu.cx == departure->second.first );
        REQUIRE( cpu.dx == departure->second.second );
      }
      else
      {
        REQUIRE( cpu.dx + 1 >= cpu.cx );
        REQUIRE( cpu.dx <= cpu.cx + 1U );
      }
      ++tests;
    }
    if ( tests == before && page > 0 )
    {
      break;
    }
  }
  REQUIRE( tests == 407 );
}

TEST_CASE( "WSCpuTest passes every test of its own", "[.][test-roms][cpu-test]" )
{
  // The release's addresses: testAll's return from its last group, and the
  // three routines that print a failure (jmpFailed, printFailedResult,
  // printFailedResult32), found by their code.
  constexpr std::uint32_t passed = 0xF024A;
  constexpr std::array<std::uint32_t, 3> failedAt{ 0xF2F36, 0xF4499, 0xF4534 };

  for ( Model const model : { Model::MONOCHROME, Model::COLOR } )
  {
    CAPTURE( ws::modelName( model ) );
    auto const emulator = running( testRom( "WSCpuTest/WSCpuTest.wsc" ), model );
    if ( !emulator )
    {
      SKIP( "WSCpuTest or the boot ROMs are not there: scripts/fetch-test-roms.sh" );
    }
    Machine& machine = *emulator->machine();
    machine.runFrames( 400 );
    press( machine, Button::A ); // "Test All."
    machine.addBreakpoint( passed );
    for ( std::uint32_t const failed : failedAt )
    {
      machine.addBreakpoint( failed );
    }
    // Some 90 minutes of the machine's time.
    auto const run = machine.runFrames( 500'000 );
    REQUIRE( run.reason == StopReason::BREAKPOINT );
    REQUIRE( run.breakpoint == passed );
  }
}
