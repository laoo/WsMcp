#pragma once

#include "ws/server/McpServer.hpp"

#include <cstdint>
#include <memory>

namespace ws::server
{

/// MCP's Streamable HTTP transport: each JSON-RPC message is the body of a POST
/// to /mcp, and the answer, if it has one, the body of the response. The
/// server streams nothing, so a GET is refused. It listens on the loopback
/// interface only, and turns away a browser's request from any other origin.
class McpHttpServer
{
public:
  /// Serves `server`'s tools on 127.0.0.1:`port`, or a port the system picks
  /// when it is 0, on threads of its own. Throws std::runtime_error when it
  /// cannot listen.
  McpHttpServer( std::uint16_t port, McpServer& server );

  /// Stops serving, and waits for the requests being served.
  ~McpHttpServer();

  McpHttpServer( McpHttpServer const& ) = delete;
  McpHttpServer& operator=( McpHttpServer const& ) = delete;
  McpHttpServer( McpHttpServer&& ) = delete;
  McpHttpServer& operator=( McpHttpServer&& ) = delete;

  [[nodiscard]] std::uint16_t port() const;

private:
  struct State;
  std::unique_ptr<State> mState;
};

} // namespace ws::server
