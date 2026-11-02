#include <catch2/catch_test_macros.hpp>

#include "control/Methods.hpp"
#include "ws/Version.hpp"
#include "ws/control/Dispatcher.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using ws::control::Dispatcher;
using ws::control::Json;

namespace
{

std::string errorCode( Json const& response )
{
  return response.at( "error" ).at( "code" ).get<std::string>();
}

/// Every method of docs/spec/control-protocol.md §5, in the order methodNames()
/// gives them.
constexpr std::array<std::string_view, 41> METHODS{
  "audio.capture_start",
  "audio.capture_stop",
  "cart.info",
  "cart.load",
  "cart.save",
  "cpu.disassemble",
  "cpu.get_state",
  "debug.breakpoint.add",
  "debug.breakpoint.list",
  "debug.breakpoint.remove",
  "debug.trace",
  "debug.trace.configure",
  "debug.trace.find",
  "debug.watchpoint.add",
  "debug.watchpoint.list",
  "debug.watchpoint.remove",
  "emu.pause",
  "emu.reset",
  "emu.resume",
  "emu.run_cycles",
  "emu.run_frames",
  "emu.run_until",
  "emu.set_bios_directory",
  "emu.status",
  "emu.step",
  "input.clear",
  "input.get",
  "input.hold",
  "input.press",
  "input.set",
  "io.get_state",
  "io.read",
  "io.write",
  "memory.list_regions",
  "memory.read",
  "memory.write",
  "serial.read",
  "state.list",
  "state.load",
  "state.save",
  "video.screenshot",
};

/// The methods that answer without a machine (docs/spec/control-protocol.md §5).
constexpr std::array<std::string_view, 9> WITHOUT_MACHINE{ "emu.status",
                                                           "emu.pause",
                                                           "emu.resume",
                                                           "emu.reset",
                                                           "emu.set_bios_directory",
                                                           "cart.load",
                                                           "memory.list_regions",
                                                           "debug.trace.configure",
                                                           "state.list" };

std::optional<std::uint32_t> linearOf( Json const& value )
{
  auto const address = ws::control::parseAddress( value );
  return address ? std::optional<std::uint32_t>{ address->linear } : std::nullopt;
}

} // namespace

TEST_CASE( "a request is answered under its id with its result", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };

  auto const response = dispatcher.handle( Json::parse( R"({"id":7,"method":"emu.status","params":{}})" ) );

  REQUIRE( response.at( "id" ) == 7 );
  REQUIRE( response.at( "ok" ) == true );
  REQUIRE( response.at( "result" ).at( "version" ) == std::string{ ws::versionString() } );
}

TEST_CASE( "a request built in C++ is answered as a parsed one is", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };

  // `1` here is a signed int, which the JSON library stores differently from
  // the unsigned it parses `1` into.
  auto const response = dispatcher.handle( Json{ { "id", 1 }, { "method", "emu.status" } } );

  REQUIRE( response.at( "id" ) == 1 );
  REQUIRE( response.at( "ok" ) == true );
}

TEST_CASE( "params may be left out", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };

  auto const response = dispatcher.handle( Json::parse( R"({"id":1,"method":"emu.status"})" ) );

  REQUIRE( response.at( "ok" ) == true );
}

TEST_CASE( "an unknown method is answered with unknown_method under the request's id", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };

  auto const response = dispatcher.handle( Json::parse( R"({"id":3,"method":"emu.nonsense"})" ) );

  REQUIRE( response.at( "id" ) == 3 );
  REQUIRE( response.at( "ok" ) == false );
  REQUIRE( errorCode( response ) == "unknown_method" );
}

TEST_CASE( "a malformed request is answered with bad_request, not refused", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };

  SECTION( "not an object: answered under id 0" )
  {
    auto const response = dispatcher.handle( Json::parse( "[1,2]" ) );
    REQUIRE( response.at( "id" ) == 0 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "no id: answered under id 0" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"method":"emu.status"})" ) );
    REQUIRE( response.at( "id" ) == 0 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "a negative id is not an id" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"id":-1,"method":"emu.status"})" ) );
    REQUIRE( response.at( "id" ) == 0 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "a fractional id is not an id" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"id":1.5,"method":"emu.status"})" ) );
    REQUIRE( response.at( "id" ) == 0 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "no method: answered under the id that was given" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"id":4})" ) );
    REQUIRE( response.at( "id" ) == 4 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "params that are not an object" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"id":5,"method":"emu.status","params":[]})" ) );
    REQUIRE( response.at( "id" ) == 5 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }
}

TEST_CASE( "the methods are the WonderSwan's, listed in a stable order", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };

  auto const names = dispatcher.methodNames();

  REQUIRE( std::ranges::is_sorted( names ) );
  REQUIRE( std::ranges::equal( names, METHODS ) );
}

TEST_CASE( "every method is described, with an object of params", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };

  for ( ws::control::Method const& method : dispatcher.methods() )
  {
    INFO( method.name );
    REQUIRE_FALSE( method.info.description.empty() );
    REQUIRE( method.info.params.at( "type" ) == "object" );
  }
}

TEST_CASE( "a parameter that is an address is an integer or a string to MCP", "[control]" )
{
  Json const schema =
      ws::control::paramsSchema( { { .name = "address", .type = ws::control::ADDRESS_TYPE, .description = "Where." },
                                   { .name = "buttons", .type = "array", .description = "Which." } } );

  REQUIRE( schema.at( "properties" ).at( "address" ).at( "type" ) == Json::array( { "integer", "string" } ) );
  REQUIRE( schema.at( "properties" ).at( "buttons" ).at( "items" ).at( "type" ) == "string" );
  REQUIRE( schema.at( "required" ) == Json::array( { "address", "buttons" } ) );
}

TEST_CASE( "without a cartridge, a method that needs a machine answers no_machine", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };

  for ( std::string const& name : dispatcher.methodNames() )
  {
    if ( std::ranges::find( WITHOUT_MACHINE, name ) != WITHOUT_MACHINE.end() )
    {
      continue;
    }
    INFO( name );
    auto const response = dispatcher.handle( Json{ { "id", 1 }, { "method", name } } );
    REQUIRE( errorCode( response ) == "no_machine" );
    REQUIRE( response.at( "error" ).at( "message" ).get<std::string>().starts_with( "No machine is running: " ) );
  }
}

TEST_CASE( "without a cartridge, the methods that need none answer", "[control]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };
  auto const call = [&dispatcher]( std::string_view method, Json params = Json::object() )
  { return dispatcher.handle( Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } ); };

  SECTION( "emu.status says why none runs" )
  {
    auto const result = call( "emu.status" ).at( "result" );
    REQUIRE( result.at( "running" ) == false );
    REQUIRE_FALSE( result.at( "error" ).get<std::string>().empty() );
    REQUIRE_FALSE( result.contains( "cycles" ) );
  }

  SECTION( "emu.reset has nothing to power up" )
  {
    REQUIRE( errorCode( call( "emu.reset" ) ) == "power_up_failed" );
  }

  SECTION( "cart.load checks its params and its file" )
  {
    REQUIRE( errorCode( call( "cart.load" ) ) == "bad_request" );
    REQUIRE( errorCode( call( "cart.load", { { "path", "x.ws" }, { "model", "gb" } } ) ) == "bad_request" );
    REQUIRE( errorCode( call( "cart.load", { { "path", "no/such/cartridge.ws" } } ) ) == "not_found" );
    REQUIRE( call( "emu.status" ).at( "result" ).at( "running" ) == false );
  }

  SECTION( "memory.list_regions names the regions, sized by no machine" )
  {
    auto const regions = call( "memory.list_regions" ).at( "result" ).at( "regions" );
    REQUIRE( regions.size() == 7 );
    REQUIRE( regions.at( 0 ).at( "name" ) == "cpu" );
    REQUIRE( regions.at( 0 ).at( "size" ).is_null() );
  }

  SECTION( "debug.trace.configure keeps to its bounds" )
  {
    REQUIRE( call( "debug.trace.configure", { { "seconds", 2 } } ).at( "result" ).at( "seconds" ) == 2.0 );
    REQUIRE( errorCode( call( "debug.trace.configure", { { "seconds", 11 } } ) ) == "bad_request" );
    REQUIRE( errorCode( call( "debug.trace.configure", { { "seconds", "1" } } ) ) == "bad_request" );
  }

  SECTION( "emu.pause is told by emu.status" )
  {
    REQUIRE( call( "emu.pause" ).at( "ok" ) == true );
    REQUIRE( call( "emu.status" ).at( "result" ).at( "paused" ) == true );
    REQUIRE( call( "emu.resume" ).at( "ok" ) == true );
    REQUIRE( call( "emu.status" ).at( "result" ).at( "paused" ) == false );
  }
}

TEST_CASE( "an address is linear or a segment and an offset", "[control]" )
{
  SECTION( "linear, as an integer or hex digits" )
  {
    REQUIRE( linearOf( Json( 0x12345 ) ) == 0x12345U );
    REQUIRE( linearOf( Json( 0 ) ) == 0U );
    REQUIRE( linearOf( Json( "$12345" ) ) == 0x12345U );
    REQUIRE( linearOf( Json( "0x12345" ) ) == 0x12345U );
    REQUIRE( linearOf( Json( "0XFFFFF" ) ) == 0xFFFFFU );
  }

  SECTION( "a segment and an offset" )
  {
    auto const address = ws::control::parseAddress( Json( "F000:FFF0" ) );
    REQUIRE( address );
    if ( address )
    {
      REQUIRE( address->linear == 0xFFFF0U );
      REQUIRE( address->segment == 0xF000 );
      REQUIRE( address->offset == 0xFFF0 );
    }
    REQUIRE( linearOf( Json( "$F000:$0100" ) ) == 0xF0100U );
    REQUIRE( linearOf( Json( "0x2000:0x10" ) ) == 0x20010U );
    REQUIRE( linearOf( Json( "1:2" ) ) == 0x12U );
    REQUIRE( linearOf( Json( "f000:fff0" ) ) == 0xFFFF0U );
  }

  SECTION( "a segment and an offset past the top wrap, as the bus does" )
  {
    REQUIRE( linearOf( Json( "FFFF:0010" ) ) == 0U );
    REQUIRE( linearOf( Json( "FFFF:FFFF" ) ) == 0x0FFEFU );
    REQUIRE( ws::control::linearOf( 0xFFFF, 0x0020 ) == 0x10U );
  }

  SECTION( "a linear address is not segmented" )
  {
    auto const address = ws::control::parseAddress( Json( 0x400 ) );
    REQUIRE( address );
    REQUIRE_FALSE( ( address && address->segment ) );
  }

  SECTION( "anything else is no address" )
  {
    for ( Json const& value : { Json( 0x100000 ),
                                Json( "$100000" ),
                                Json( -1 ),
                                Json( 1.5 ),
                                Json( "12345" ),
                                Json( "" ),
                                Json( "$" ),
                                Json( "G000:0000" ),
                                Json( "10000:0000" ),
                                Json( "F000:" ),
                                Json( ":FFF0" ),
                                Json( "F000:FFF0:0" ),
                                Json( "F000 :FFF0" ),
                                Json( true ),
                                Json( nullptr ),
                                Json::array( { 1 } ) } )
    {
      INFO( value.dump() );
      REQUIRE_FALSE( ws::control::parseAddress( value ) );
    }
  }

  SECTION( "a missing or bad one is a bad_request" )
  {
    REQUIRE( ws::control::requireAddress( Json{ { "pc", "F000:FFF0" } }, "pc" )->linear == 0xFFFF0U );
    REQUIRE( ws::control::requireAddress( Json::object(), "pc" ).error().code == "bad_request" );
    REQUIRE( ws::control::requireAddress( Json{ { "pc", "nowhere" } }, "pc" ).error().code == "bad_request" );
  }
}

TEST_CASE( "an unsigned parameter is an integer or hex digits", "[control]" )
{
  Json const params{ { "a", 7 }, { "b", "$1F" }, { "c", "0x20" }, { "d", -1 }, { "e", "20" }, { "f", 2.5 } };

  REQUIRE( ws::control::requireUnsigned( params, "a" ) == 7U );
  REQUIRE( ws::control::requireUnsigned( params, "b" ) == 0x1FU );
  REQUIRE( ws::control::requireUnsigned( params, "c" ) == 0x20U );
  for ( std::string_view const name : { "d", "e", "f", "g" } )
  {
    INFO( name );
    REQUIRE( ws::control::requireUnsigned( params, name ).error().code == "bad_request" );
  }
}

TEST_CASE( "bytes go to hex and back", "[control]" )
{
  std::vector<std::uint8_t> const bytes{ 0x00, 0xEA, 0x7F, 0xFF };

  REQUIRE( ws::control::hexOf( bytes ) == "00ea7fff" );
  REQUIRE( ws::control::bytesOfHex( "00EA7fff" ) == bytes );
  REQUIRE_FALSE( ws::control::bytesOfHex( "0" ) );
  REQUIRE_FALSE( ws::control::bytesOfHex( "0g" ) );
  auto const none = ws::control::bytesOfHex( "" );
  REQUIRE( ( none && none->empty() ) );
}

TEST_CASE( "flags and segments read as a debugger shows them", "[control]" )
{
  REQUIRE( ws::control::flagsText( 0xF002 ) == "oditszapc" );
  REQUIRE( ws::control::flagsText( 0x0FD5 ) == "ODITSZAPC" );
  REQUIRE( ws::control::flagsText( 0x0241 ) == "odItsZapC" );
  REQUIRE( ws::control::segmentedText( 0xF000, 0x00F0 ) == "F000:00F0" );
}
