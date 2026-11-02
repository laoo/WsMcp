# 0010: The core compiles to WebAssembly

**Status:** accepted

## Context

The owner wants the emulator to run in a browser as well. The core was already kept free of SDL,
threads, file dialogs and the network, so that the suite, the CLI and the application share it.

## Decision

- `ws_core` builds with Emscripten (`cmake --preset wasm`), with a thin binding, `ws_web`
  (`src/ws_web`), exported to JavaScript with embind: put a cartridge and a boot ROM in, run
  frames, take the RGBA picture and the sound, press buttons, and the dispatcher's
  `handle(json)`, so that the control protocol reaches a page as it reaches an agent.
- Files reach the core through Emscripten's in-memory file system: a page writes the
  cartridge's bytes there and loads it by path, as the CLI does.
- The core keeps to what Emscripten's libc++ has: no threads, no sockets, no `std::print`.
  `ws_server`, `ws_cli` and the desktop application are not part of the WebAssembly build.

## Consequences

- A change to the core that needs a thread or a socket belongs in `ws_server` or the
  application instead.
- The page's own frontend (canvas, Web Audio, keys) is the page's; the binding stays thin.
