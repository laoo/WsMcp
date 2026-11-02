#include "ws/server/TcpLineServer.hpp"

#include "ws/server/JsonLinesServer.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <atomic>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace ws::server
{

namespace
{

// The few calls that differ between Winsock and POSIX sockets.
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket NO_SOCKET = INVALID_SOCKET;
constexpr int SHUT_BOTH = SD_BOTH;
constexpr int SEND_FLAGS = 0;

void closeSocket( Socket socket )
{
  closesocket( socket );
}

int pollOne( Socket socket, int milliseconds )
{
  WSAPOLLFD entry{ .fd = socket, .events = POLLRDNORM, .revents = 0 };
  return WSAPoll( &entry, 1, milliseconds );
}

/// Winsock counts bytes in an int.
int byteCount( std::size_t size )
{
  return static_cast<int>( size );
}

/// Winsock is started for as long as a server exists.
struct Network
{
  Network()
  {
    WSADATA data{};
    if ( WSAStartup( MAKEWORD( 2, 2 ), &data ) != 0 )
    {
      throw std::runtime_error( "cannot start Winsock" );
    }
  }

  ~Network()
  {
    WSACleanup();
  }

  Network( Network const& ) = delete;
  Network& operator=( Network const& ) = delete;
  Network( Network&& ) = delete;
  Network& operator=( Network&& ) = delete;
};
#else
using Socket = int;
constexpr Socket NO_SOCKET = -1;
constexpr int SHUT_BOTH = SHUT_RDWR;
#ifdef MSG_NOSIGNAL
// A write to a connection the client has closed must fail, not raise SIGPIPE.
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SEND_FLAGS = 0;
#endif

void closeSocket( Socket socket )
{
  close( socket );
}

int pollOne( Socket socket, int milliseconds )
{
  pollfd entry{ .fd = socket, .events = POLLIN, .revents = 0 };
  return poll( &entry, 1, milliseconds );
}

std::size_t byteCount( std::size_t size )
{
  return size;
}

struct Network
{
};
#endif

/// How often the thread that accepts looks whether it should stop.
constexpr int ACCEPT_POLL_MILLISECONDS = 100;

bool sendAll( Socket socket, std::string_view bytes )
{
  while ( !bytes.empty() )
  {
    auto const sent = send( socket, bytes.data(), byteCount( bytes.size() ), SEND_FLAGS );
    if ( sent <= 0 )
    {
      return false;
    }
    bytes.remove_prefix( static_cast<std::size_t>( sent ) );
  }
  return true;
}

} // namespace

struct TcpLineServer::State
{
  struct Connection
  {
    Socket socket{ NO_SOCKET };
    bool closed{};
    std::thread thread;
  };

  explicit State( RequestHandler handler ) : lines{ std::move( handler ) } {}

  void accept();
  void serve( Connection& connection );

  Network network;
  JsonLinesServer lines;
  Socket listener{ NO_SOCKET };
  std::uint16_t port{};
  std::atomic<bool> stopping{};
  std::mutex mutex;
  std::list<Connection> connections;
  std::thread acceptor;
};

void TcpLineServer::State::accept()
{
  while ( !stopping )
  {
    if ( pollOne( listener, ACCEPT_POLL_MILLISECONDS ) <= 0 )
    {
      continue;
    }
    Socket const client = ::accept( listener, nullptr, nullptr );
    if ( client == NO_SOCKET )
    {
      continue;
    }
#ifdef SO_NOSIGPIPE
    int const on = 1;
    setsockopt( client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof( on ) );
#endif
    std::scoped_lock const lock{ mutex };
    // Connections that have ended are let go of here, so that a long session
    // of short connections does not pile them up.
    for ( auto at = connections.begin(); at != connections.end(); )
    {
      if ( at->closed )
      {
        at->thread.join();
        at = connections.erase( at );
      }
      else
      {
        ++at;
      }
    }
    Connection& connection = connections.emplace_back();
    connection.socket = client;
    connection.thread = std::thread{ [this, &connection] { serve( connection ); } };
  }
}

void TcpLineServer::State::serve( Connection& connection )
{
  std::string pending;
  std::array<char, 4096> buffer{};
  for ( ;; )
  {
    auto const received = recv( connection.socket, buffer.data(), byteCount( buffer.size() ), 0 );
    if ( received <= 0 )
    {
      break;
    }
    pending.append( buffer.data(), static_cast<std::size_t>( received ) );
    std::size_t end = 0;
    bool open = true;
    while ( open && ( end = pending.find( '\n' ) ) != std::string::npos )
    {
      std::string line = pending.substr( 0, end );
      pending.erase( 0, end + 1 );
      if ( !line.empty() && line.back() == '\r' )
      {
        line.pop_back();
      }
      if ( line.find_first_not_of( " \t" ) == std::string::npos )
      {
        continue;
      }
      open = sendAll( connection.socket, lines.handleLine( line ) + '\n' );
    }
    if ( !open )
    {
      break;
    }
  }
  std::scoped_lock const lock{ mutex };
  closeSocket( connection.socket );
  connection.closed = true;
}

TcpLineServer::TcpLineServer( std::uint16_t port, RequestHandler handler )
    : mState{ std::make_unique<State>( std::move( handler ) ) }
{
  State& state = *mState;
  state.listener = socket( AF_INET, SOCK_STREAM, 0 );
  if ( state.listener == NO_SOCKET )
  {
    throw std::runtime_error( "cannot open a socket" );
  }
  int const on = 1;
  setsockopt( state.listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>( &on ), sizeof( on ) );

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons( port );
  address.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  socklen_t length = sizeof( address );
  if ( bind( state.listener, reinterpret_cast<sockaddr*>( &address ), sizeof( address ) ) != 0 ||
       listen( state.listener, SOMAXCONN ) != 0 ||
       getsockname( state.listener, reinterpret_cast<sockaddr*>( &address ), &length ) != 0 )
  {
    closeSocket( state.listener );
    throw std::runtime_error( "cannot listen on 127.0.0.1:" + std::to_string( port ) );
  }
  state.port = ntohs( address.sin_port );
  state.acceptor = std::thread{ [&state] { state.accept(); } };
}

TcpLineServer::~TcpLineServer()
{
  State& state = *mState;
  state.stopping = true;
  state.acceptor.join();
  closeSocket( state.listener );
  {
    // A connection's thread waits in recv(); shutting its socket down wakes it.
    std::scoped_lock const lock{ state.mutex };
    for ( auto& connection : state.connections )
    {
      if ( !connection.closed )
      {
        shutdown( connection.socket, SHUT_BOTH );
      }
    }
  }
  for ( auto& connection : state.connections )
  {
    connection.thread.join();
  }
}

std::uint16_t TcpLineServer::port() const
{
  return mState->port;
}

} // namespace ws::server
