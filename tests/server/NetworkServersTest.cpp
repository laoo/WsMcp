#include <catch2/catch_test_macros.hpp>

#include "ws/Emulator.hpp"
#include "ws/control/Dispatcher.hpp"
#include "ws/server/McpHttpServer.hpp"
#include "ws/server/McpServer.hpp"
#include "ws/server/TcpLineServer.hpp"

#include <httplib.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <string>

using ws::control::Json;

namespace
{

#ifndef _WIN32
/// A client of the line protocol over TCP, for the test's own thread.
class LineClient
{
public:
  explicit LineClient( std::uint16_t port ) : mSocket{ socket( AF_INET, SOCK_STREAM, 0 ) }
  {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons( port );
    address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    REQUIRE( connect( mSocket, reinterpret_cast<sockaddr*>( &address ), sizeof( address ) ) == 0 );
  }

  ~LineClient()
  {
    close( mSocket );
  }

  LineClient( LineClient const& ) = delete;
  LineClient& operator=( LineClient const& ) = delete;
  LineClient( LineClient&& ) = delete;
  LineClient& operator=( LineClient&& ) = delete;

  void send( std::string const& text ) const
  {
    REQUIRE( ::send( mSocket, text.data(), text.size(), 0 ) == static_cast<ssize_t>( text.size() ) );
  }

  /// The next line the server sends.
  std::string line()
  {
    for ( ;; )
    {
      if ( auto const end = mPending.find( '\n' ); end != std::string::npos )
      {
        std::string line = mPending.substr( 0, end );
        mPending.erase( 0, end + 1 );
        return line;
      }
      std::array<char, 1024> buffer{};
      auto const received = recv( mSocket, buffer.data(), buffer.size(), 0 );
      REQUIRE( received > 0 );
      mPending.append( buffer.data(), static_cast<std::size_t>( received ) );
    }
  }

private:
  int mSocket;
  std::string mPending;
};
#endif

} // namespace

TEST_CASE( "the line protocol over TCP answers a line per request, on each connection", "[server]" )
{
#ifdef _WIN32
  SKIP( "the test's client is POSIX sockets" );
#else
  ws::Emulator emulator{ ws::Settings{} };
  ws::control::Dispatcher const dispatcher{ emulator };
  ws::server::TcpLineServer const server{ 0, ws::server::directHandler( dispatcher ) };
  REQUIRE( server.port() != 0 );

  LineClient first{ server.port() };
  LineClient second{ server.port() };
  first.send( R"({"id":1,"method":"emu.status"})"
              "\r\n\n"
              R"({"id":2,"method":"emu.pause"})"
              "\n" );
  second.send( "not json\n" );

  REQUIRE( Json::parse( first.line() ).at( "id" ) == 1 );
  REQUIRE( Json::parse( first.line() ).at( "ok" ) == true );
  REQUIRE( Json::parse( second.line() ).at( "error" ).at( "code" ) == "bad_request" );
#endif
}

TEST_CASE( "MCP over HTTP answers a POST to /mcp, and accepts a notification with nothing", "[server][mcp]" )
{
  ws::Emulator emulator{ ws::Settings{} };
  ws::control::Dispatcher const dispatcher{ emulator };
  ws::server::McpServer mcp{ dispatcher };
  ws::server::McpHttpServer const server{ 0, mcp };
  httplib::Client client{ "127.0.0.1", server.port() };

  auto const initialized =
      client.Post( "/mcp",
                   R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})",
                   "application/json" );
  REQUIRE( initialized );
  REQUIRE( initialized->status == 200 );
  REQUIRE( Json::parse( initialized->body ).at( "result" ).at( "serverInfo" ).at( "name" ) == "wsmcp" );

  auto const notified =
      client.Post( "/mcp", R"({"jsonrpc":"2.0","method":"notifications/initialized"})", "application/json" );
  REQUIRE( notified );
  REQUIRE( notified->status == 202 );
  REQUIRE( notified->body.empty() );

  auto const garbled = client.Post( "/mcp", "{", "application/json" );
  REQUIRE( garbled );
  REQUIRE( garbled->status == 400 );

  auto const streamed = client.Get( "/mcp" );
  REQUIRE( streamed );
  REQUIRE( streamed->status == 405 );

  httplib::Headers const foreign{ { "Origin", "http://example.com" } };
  auto const refused =
      client.Post( "/mcp", foreign, R"({"jsonrpc":"2.0","id":2,"method":"ping"})", "application/json" );
  REQUIRE( refused );
  REQUIRE( refused->status == 403 );

  httplib::Headers const local{ { "Origin", "http://localhost:6274" } };
  auto const pinged = client.Post( "/mcp", local, R"({"jsonrpc":"2.0","id":3,"method":"ping"})", "application/json" );
  REQUIRE( pinged );
  REQUIRE( pinged->status == 200 );
}
