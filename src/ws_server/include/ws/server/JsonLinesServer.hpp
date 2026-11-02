#pragma once

#include "ws/control/Dispatcher.hpp"
#include "ws/server/RequestHandler.hpp"

#include <iosfwd>
#include <string>
#include <string_view>

namespace ws::server
{

/// The line-framed transport of docs/spec/control-protocol.md: one JSON request
/// per line in, one JSON response per line out. It is the RTL simulator's
/// framing, so a script written against `./sim --server` drives this unchanged.
class JsonLinesServer
{
public:
  explicit JsonLinesServer( RequestHandler handler );
  /// Answers through `dispatcher` on the caller's thread.
  explicit JsonLinesServer( control::Dispatcher const& dispatcher );

  /// Answers every request read from `in` until it ends, flushing `out` after
  /// each response so that a client waiting on one is never left waiting.
  /// Blank lines are skipped, as the simulator skips them.
  void serve( std::istream& in, std::ostream& out ) const;

  /// Answers one line. Text that is not JSON is answered with `bad_request`.
  [[nodiscard]] std::string handleLine( std::string_view line ) const;

private:
  RequestHandler mHandler;
};

} // namespace ws::server
