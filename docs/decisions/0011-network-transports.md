# 0011: MCP over HTTP is served by cpp-httplib, JSON-lines over TCP by sockets of our own

**Status:** accepted

## Context

The transports the owner's emulators share are taken unchanged, with their server library.

## Decision

- MCP over HTTP is served by cpp-httplib, a single header fetched at a pinned tag.
- JSON-lines over TCP runs on sockets of our own (`ws::server::TcpLineServer`).
- Both listen on 127.0.0.1 only, and the HTTP server turns away a browser's request whose
  `Origin` is not a local page.

## Consequences

- Serving beyond the loopback interface, or TLS, would need this revisited.
