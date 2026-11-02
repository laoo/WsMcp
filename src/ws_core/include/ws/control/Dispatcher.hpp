#pragma once

#include "ws/Emulator.hpp"

#include <nlohmann/json.hpp>

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ws::control
{

/// Keys keep the order they were written in, so a response reads `id`, `ok`,
/// `result`, and a transcript of answers can be compared as text.
using Json = nlohmann::ordered_json;

/// Why a request failed, as the protocol reports it. `code` is one of the
/// stable identifiers listed in docs/spec/control-protocol.md; `message` is for
/// a person.
struct Error
{
  std::string code;
  std::string message;
};

/// What a method answers: the `result` object, or the error that replaces it.
using Outcome = std::expected<Json, Error>;

/// What a client is told about a method: what it does, and the JSON Schema of
/// its `params`. MCP's tool list is made from these.
struct MethodInfo
{
  std::string description;
  Json params = Json{ { "type", "object" } };
};

/// A method under its own name, with what is told about it.
struct Method
{
  std::string name;
  MethodInfo info;
};

/// The one place every capability of the emulator is reached through, whatever
/// the transport (docs/decisions/0004-one-control-api.md). It takes a request
/// object and answers a response object; framing, sockets and MCP belong to the
/// transports in ws_server. Every method is specified in
/// docs/spec/control-protocol.md.
class Dispatcher
{
public:
  using Handler = std::function<Outcome( Json const& params )>;

  /// Answers requests about `emulator`, which must outlive the dispatcher.
  explicit Dispatcher( Emulator& emulator );

  /// Answers one request. Never throws and always answers: a malformed request
  /// is answered with a `bad_request` error, not refused.
  [[nodiscard]] Json handle( Json const& request ) const;

  /// Every method name a request may use, aliases included, in a stable order.
  [[nodiscard]] std::vector<std::string> methodNames() const;

  /// Every method under its own name, aliases left out, ordered by name.
  [[nodiscard]] std::vector<Method> methods() const;

  /// Makes `name` a method answered by `handler`, described by `info`.
  void add( std::string_view name, MethodInfo info, Handler handler );

  /// Makes `alias` answer exactly as `name` does.
  void alias( std::string_view alias, std::string_view name );

private:
  std::unordered_map<std::string, Handler> mHandlers;
  std::unordered_map<std::string, MethodInfo> mInfo;
};

} // namespace ws::control
