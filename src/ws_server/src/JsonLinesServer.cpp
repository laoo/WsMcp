#include "ws/server/JsonLinesServer.hpp"

#include <istream>
#include <ostream>
#include <string>
#include <utility>

namespace ws::server
{

JsonLinesServer::JsonLinesServer( RequestHandler handler ) : mHandler{ std::move( handler ) } {}

JsonLinesServer::JsonLinesServer( control::Dispatcher const& dispatcher )
    : JsonLinesServer{ directHandler( dispatcher ) }
{
}

void JsonLinesServer::serve( std::istream& in, std::ostream& out ) const
{
  std::string line;
  while ( std::getline( in, line ) )
  {
    if ( line.find_first_not_of( " \t\r" ) == std::string::npos )
    {
      continue;
    }
    out << handleLine( line ) << '\n' << std::flush;
  }
}

std::string JsonLinesServer::handleLine( std::string_view line ) const
{
  // Parsed without exceptions: a malformed line is an ordinary answer, not a
  // failure of the server, and the next line is served as if it had not been.
  auto const request = control::Json::parse( line, nullptr, false );
  if ( request.is_discarded() )
  {
    auto const response =
        control::Json{ { "id", 0 },
                       { "ok", false },
                       { "error", { { "code", "bad_request" }, { "message", "Request is not valid JSON" } } } };
    return response.dump();
  }
  return mHandler( request ).dump();
}

} // namespace ws::server
