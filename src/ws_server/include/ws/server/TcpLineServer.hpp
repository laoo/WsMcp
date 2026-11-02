#pragma once

#include "ws/server/RequestHandler.hpp"

#include <cstdint>
#include <memory>

namespace ws::server
{

/// The line protocol of JsonLinesServer over TCP: each connection a session of
/// its own, a request per line in and a response per line out. It listens on
/// the loopback interface only, as whoever connects drives the emulator. A
/// thread accepts, and each connection is served on a thread of its own.
class TcpLineServer
{
public:
  /// Listens on 127.0.0.1:`port`, or a port the system picks when it is 0.
  /// Throws std::runtime_error when it cannot.
  TcpLineServer( std::uint16_t port, RequestHandler handler );

  /// Stops listening, closes every connection and waits for their threads.
  ~TcpLineServer();

  TcpLineServer( TcpLineServer const& ) = delete;
  TcpLineServer& operator=( TcpLineServer const& ) = delete;
  TcpLineServer( TcpLineServer&& ) = delete;
  TcpLineServer& operator=( TcpLineServer&& ) = delete;

  /// The port it listens on.
  [[nodiscard]] std::uint16_t port() const;

private:
  struct State;
  std::unique_ptr<State> mState;
};

} // namespace ws::server
