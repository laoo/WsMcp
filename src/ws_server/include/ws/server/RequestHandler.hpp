#pragma once

#include "ws/control/Dispatcher.hpp"

#include <functional>

namespace ws::server
{

/// What a transport hands each request to, for its response: the dispatcher
/// itself in a headless program, or the queue onto the emulation thread in the
/// desktop application. It is called from whichever thread the transport
/// serves on, so it must be safe to call from any.
using RequestHandler = std::function<control::Json( control::Json const& request )>;

/// A handler that answers through `dispatcher` on the caller's thread.
[[nodiscard]] inline RequestHandler directHandler( control::Dispatcher const& dispatcher )
{
  return [&dispatcher]( control::Json const& request ) { return dispatcher.handle( request ); };
}

} // namespace ws::server
