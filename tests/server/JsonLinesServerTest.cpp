#include <catch2/catch_test_macros.hpp>

#include "ws/control/Dispatcher.hpp"
#include "ws/server/JsonLinesServer.hpp"

#include <sstream>
#include <string>
#include <vector>

using ws::control::Dispatcher;
using ws::control::Json;
using ws::server::JsonLinesServer;

namespace
{

std::vector<Json> responsesTo( std::string const& input )
{
  ws::Emulator emulator{ ws::Settings{} };
  Dispatcher const dispatcher{ emulator };
  JsonLinesServer const server{ dispatcher };
  std::istringstream in{ input };
  std::ostringstream out;

  server.serve( in, out );

  std::vector<Json> responses;
  std::istringstream lines{ out.str() };
  std::string line;
  while ( std::getline( lines, line ) )
  {
    responses.push_back( Json::parse( line ) );
  }
  return responses;
}

} // namespace

TEST_CASE( "each request line is answered by one response line, in order", "[server]" )
{
  auto const responses = responsesTo( "{\"id\":1,\"method\":\"emu.status\"}\n"
                                      "{\"id\":2,\"method\":\"emu.pause\"}\n" );

  REQUIRE( responses.size() == 2 );
  REQUIRE( responses[0].at( "id" ) == 1 );
  REQUIRE( responses[1].at( "id" ) == 2 );
}

TEST_CASE( "blank lines are skipped", "[server]" )
{
  auto const responses = responsesTo( "\n   \n{\"id\":1,\"method\":\"emu.status\"}\r\n\n" );

  REQUIRE( responses.size() == 1 );
}

TEST_CASE( "a line that is not JSON is answered with bad_request and serving goes on", "[server]" )
{
  auto const responses = responsesTo( "{not json\n{\"id\":9,\"method\":\"emu.status\"}\n" );

  REQUIRE( responses.size() == 2 );
  REQUIRE( responses[0].at( "id" ) == 0 );
  REQUIRE( responses[0].at( "error" ).at( "code" ) == "bad_request" );
  REQUIRE( responses[1].at( "ok" ) == true );
}
