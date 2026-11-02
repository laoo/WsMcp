#include <catch2/catch_test_macros.hpp>

#include "support/Expect.hpp"

#include "ws/Emulator.hpp"
#include "ws/control/Dispatcher.hpp"
#include "ws/server/McpServer.hpp"

#include <sstream>
#include <string>

using ws::control::Json;
using ws::server::McpServer;

namespace
{

Json request( int id, std::string const& method, Json params = Json::object() )
{
  return Json{ { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", std::move( params ) } };
}

Json call( McpServer& server, std::string const& tool, Json arguments = Json::object() )
{
  return ws::test::valueOf( server.handle( request(
                                7, "tools/call", { { "name", tool }, { "arguments", std::move( arguments ) } } ) ) )
      .at( "result" );
}

} // namespace

TEST_CASE( "initialize answers the protocol revision asked for, and the server's name", "[server][mcp]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  ws::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };

  auto const answer =
      ws::test::valueOf( server.handle( request( 1, "initialize", { { "protocolVersion", "2025-06-18" } } ) ) );
  REQUIRE( answer.at( "id" ) == 1 );
  REQUIRE( answer.at( "result" ).at( "protocolVersion" ) == "2025-06-18" );
  REQUIRE( answer.at( "result" ).at( "serverInfo" ).at( "name" ) == "wsmcp" );
  REQUIRE( answer.at( "result" ).at( "capabilities" ).contains( "tools" ) );

  auto const unknown =
      ws::test::valueOf( server.handle( request( 2, "initialize", { { "protocolVersion", "1999-01-01" } } ) ) );
  REQUIRE( unknown.at( "result" ).at( "protocolVersion" ) == "2025-11-25" );

  // A notification is answered by nothing.
  REQUIRE_FALSE( server.handle( Json{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } } ) );
}

TEST_CASE( "the tools are the dispatcher's methods, with their schemas", "[server][mcp]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  ws::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };

  auto const tools = ws::test::valueOf( server.handle( request( 1, "tools/list" ) ) ).at( "result" ).at( "tools" );
  REQUIRE( tools.size() == dispatcher.methods().size() );
  bool found = false;
  for ( auto const& tool : tools )
  {
    REQUIRE( tool.at( "inputSchema" ).at( "type" ) == "object" );
    found |= tool.at( "name" ) == "emu_status";
  }
  REQUIRE( found );
}

TEST_CASE( "a tool call runs its method; a failure is a flagged result, an unknown tool an error", "[server][mcp]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  ws::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };

  REQUIRE( call( server, "emu_pause" ).at( "isError" ) == false );
  auto const status = call( server, "emu_status" );
  REQUIRE( Json::parse( status.at( "content" ).at( 0 ).at( "text" ).get<std::string>() ).at( "paused" ) == true );

  auto const unknown = ws::test::valueOf(
      server.handle( request( 3, "tools/call", { { "name", "emu_fly" }, { "arguments", Json::object() } } ) ) );
  REQUIRE( unknown.at( "error" ).at( "code" ) == -32602 );
}

TEST_CASE( "the stdio transport answers a line per request, and nothing to a notification", "[server][mcp]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  ws::control::Dispatcher const dispatcher{ emulator };
  McpServer server{ dispatcher };
  std::istringstream in{ R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
                         "\n"
                         R"({"jsonrpc":"2.0","id":"a","method":"ping"})"
                         "\nnot json\n" };
  std::ostringstream out;

  server.serve( in, out );

  std::istringstream lines{ out.str() };
  std::string line;
  REQUIRE( std::getline( lines, line ) );
  REQUIRE( Json::parse( line ).at( "id" ) == "a" );
  REQUIRE( std::getline( lines, line ) );
  REQUIRE( Json::parse( line ).at( "error" ).at( "code" ) == -32700 );
  REQUIRE_FALSE( std::getline( lines, line ) );
}
