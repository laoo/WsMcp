# 0004: One control API

**Status:** accepted

## Context

The emulator is driven by AI agent sessions as much as by a person: running cartridges, pressing
buttons, reading memory, registers and the picture, and stopping at breakpoints. PetMcp answers
this with one dispatcher behind several transports (its record 0005), and the owner asked for the
same interface here.

## Decision

PetMcp's control API is taken whole: every capability is a method of one dispatcher in the core
library, taking JSON `params` and returning a JSON `result`, reached by JSON-lines on stdio and
TCP and by MCP on stdio and HTTP. The wire format, the error conventions, the transcripts and the
batch scripts are PetMcp's; the methods are the WonderSwan's
([spec/control-protocol.md](../spec/control-protocol.md)).

## Consequences

- The owner's tools and agent skills for PetMcp carry over with the method names changed.
- The same dispatcher is what the WebAssembly build exposes to JavaScript
  ([0010](0010-the-core-compiles-to-webassembly.md)).
