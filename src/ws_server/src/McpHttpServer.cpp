#include "ws/server/McpHttpServer.hpp"

#include <httplib.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace ws::server
{

namespace
{

constexpr char const* ENDPOINT = "/mcp";
constexpr char const* JSON_TYPE = "application/json";

/// Whether a request comes from a page of this machine, or from no page at
/// all. A browser names the page's origin; another origin's page could
/// otherwise drive the emulator through a name that resolves to 127.0.0.1.
bool trustedOrigin( httplib::Request const& request )
{
  if ( !request.has_header( "Origin" ) )
  {
    return true;
  }
  std::string const origin = request.get_header_value( "Origin" );
  constexpr std::array<std::string_view, 4> local{
    "http://localhost", "http://127.0.0.1", "https://localhost", "https://127.0.0.1"
  };
  return std::ranges::any_of( local,
                              [&origin]( std::string_view prefix )
                              { return origin == prefix || origin.starts_with( std::string{ prefix } + ":" ); } );
}

} // namespace

struct McpHttpServer::State
{
  httplib::Server http;
  int port{};
  std::thread thread;
};

McpHttpServer::McpHttpServer( std::uint16_t port, McpServer& server ) : mState{ std::make_unique<State>() }
{
  httplib::Server& http = mState->http;
  http.Post( ENDPOINT,
             [&server]( httplib::Request const& request, httplib::Response& response )
             {
               if ( !trustedOrigin( request ) )
               {
                 response.status = httplib::StatusCode::Forbidden_403;
                 return;
               }
               auto const message = control::Json::parse( request.body, nullptr, false );
               if ( message.is_discarded() )
               {
                 response.status = httplib::StatusCode::BadRequest_400;
                 response.set_content(
                     R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"The message is not valid JSON"}})",
                     JSON_TYPE );
                 return;
               }
               auto const answer = server.handle( message );
               if ( !answer )
               {
                 // A notification, or a response to the server: accepted, and
                 // answered by nothing.
                 response.status = httplib::StatusCode::Accepted_202;
                 return;
               }
               response.set_content( answer->dump(), JSON_TYPE );
             } );
  http.Get( ENDPOINT,
            []( httplib::Request const& /*request*/, httplib::Response& response )
            { response.status = httplib::StatusCode::MethodNotAllowed_405; } );

  if ( port == 0 )
  {
    mState->port = http.bind_to_any_port( "127.0.0.1" );
  }
  else
  {
    mState->port = http.bind_to_port( "127.0.0.1", port ) ? port : -1;
  }
  if ( mState->port <= 0 )
  {
    throw std::runtime_error( "cannot listen on 127.0.0.1:" + std::to_string( port ) );
  }
  mState->thread = std::thread{ [&http] { http.listen_after_bind(); } };
}

McpHttpServer::~McpHttpServer()
{
  mState->http.stop();
  mState->thread.join();
}

std::uint16_t McpHttpServer::port() const
{
  return static_cast<std::uint16_t>( mState->port );
}

} // namespace ws::server
