# 0007: The machine's parts are Mesen2's, and System stands for its console

**Status:** accepted

## Context

Mesen2's `Core/WS` is a set of parts (`WsCpu`, `WsMemoryManager`, `WsPpu`, `WsApu`, `WsTimer`,
`WsDmaController`, `WsEeprom`, `WsSerial`, `WsControlManager`, `WsCart`) that reach each other
through `WsConsole` and through Mesen2's `Emulator`, which also carries its debugger, its
settings and its video and audio output. Porting them faithfully is easiest part for part;
porting Mesen2's `Emulator` is not wanted.

## Decision

- Each Mesen2 part becomes one class of `ws::machine`, under its name without `Ws`, with its
  state struct in `ws/machine/Types.hpp`.
- `System` (`src/ws_core/src/machine/System.hpp`) stands for `WsConsole` and for the few services
  of `Emulator` the parts call: the configuration, the memories, the debugger's bus hooks, the
  finished frame and the sound. Every part holds a `System*`.
- `Machine`, the core's public face, owns a `System` and adds what Mesen2's debugger and frontend
  did: runs, breakpoints, watchpoints, the history of execution, save states, the picture in RGBA.

## Consequences

- A part of WsMcp reads beside its Mesen2 file, which is what makes a later upstream fix a small
  port.
- The parts are coupled as Mesen2's are; a part is tested alone only where it does not need the
  others (the picture's colours, the APU's channels).
