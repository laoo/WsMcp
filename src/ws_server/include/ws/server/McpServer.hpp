#pragma once

#include "ws/control/Dispatcher.hpp"
#include "ws/server/RequestHandler.hpp"

#include <iosfwd>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ws::server
{

/// The Model Context Protocol onto the dispatcher: JSON-RPC 2.0 messages, a
/// tool per method of the dispatcher, made from its method table
/// (docs/decisions/0004-one-control-api.md). A tool is named as its method with
/// underscores for the dots, emu_run_frames for emu.run_frames, as MCP allows
/// no dots in a name. A screenshot comes back as an image the agent can see.
class McpServer
{
public:
  /// Tools for `methods`, whose calls `handler` answers.
  McpServer( std::vector<control::Method> methods, RequestHandler handler );

  /// Tools for the dispatcher's methods, answered by it on the caller's thread.
  explicit McpServer( control::Dispatcher const& dispatcher );

  /// The stdio transport: a message per line in, a message per line out, until
  /// `in` ends.
  void serve( std::istream& in, std::ostream& out );

  /// Answers one message, or nothing for a notification.
  [[nodiscard]] std::optional<control::Json> handle( control::Json const& message );

  /// Answers one line of text, or nothing for a notification. Text that is not
  /// JSON is a parse error.
  [[nodiscard]] std::optional<std::string> handleLine( std::string const& line );

private:
  [[nodiscard]] control::Json listTools() const;
  [[nodiscard]] std::optional<control::Json> callTool( control::Json const& params, control::Json& error ) const;

  std::vector<control::Method> mMethods;
  RequestHandler mHandler;
  /// Tool name to method name.
  std::unordered_map<std::string, std::string> mTools;
};

} // namespace ws::server
