# Working in this repository

## Language

Discussion with the user is in Polish. **All code, comments, documentation,
commit messages and identifiers are in English.**

## Build and test

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
emcmake cmake --preset wasm && cmake --build --preset wasm      # the page and the core, build/wasm/web: python3 -m http.server -d build/wasm/web
```

Presets: `debug`, `release`, `asan`, `wasm`. Never create build directories by hand;
the presets own `build/<preset>`. The test presets run tests in parallel on
every core. The desktop application, `build/<preset>/src/ws_app/wsmcp`, is built by
default; `-DWS_BUILD_APP=OFF` leaves SDL3 and ImGui out. A build directory configured before it was
the default keeps `OFF` in its cache: configure it again with `-DWS_BUILD_APP=ON`.

```sh
build/debug/src/ws_cli/wsmcp-cli --server tcp:7701 --bios-dir ~/Documents/WS/BIOS --save-dir build/saves   # JSON lines on TCP
build/debug/src/ws_cli/wsmcp-cli --mcp --bios-dir ~/Documents/WS/BIOS "game.wsc"                              # MCP for an agent
build/debug/src/ws_app/wsmcp --server tcp:7701 --mcp-http 7702 "game.wsc"                                      # the window, served to agents
ctest --preset debug -L control                     # the protocol's recorded transcripts, tests/control/*.jsonl
ctest --preset debug -L regression                  # the batch scripts, tests/regression/*.json, against their golden sums
(cd tests && ../build/debug/src/ws_cli/wsmcp-cli --transcript control/lines.jsonl > control/lines.expect)   # re-record one, after a deliberate change of answers
```

```sh
./scripts/fetch-test-roms.sh                        # the hardware test ROMs and the flash test, into build/tools/testroms (brew install nasm make)
build/release/tests/ws_tests "[test-roms]"          # them all: ws-test-suite, the flash test, WSHWTest, WSTimingTest, and WSCpuTest, about 9 minutes
```

The [test-roms] tests skip without the test ROMs or the boot ROMs; WSCpuTest's,
tagged [cpu-test] as well, is hidden from the default run. The script installs
the Wonderful toolchain, which builds ws-test-suite, AthenaOS and `tests/roms/flash`, into
build/tools/wonderful, and Pillow, which AthenaOS's build needs, into build/tools/venv.

```sh
./scripts/format.sh          # format in place
./scripts/format.sh --check  # fail on deviation
./scripts/tidy.sh            # clang-tidy against build/debug
./scripts/compile-shaders.sh # the screen's shaders, after a change to src/ws_app/shaders (brew install glslang spirv-cross)
```

```sh
./scripts/publish-site.sh             # build the WebAssembly page into build/site, and stop
./scripts/publish-site.sh --publish   # the same, folded into gh-pages' one commit, force-pushed
```

The page is `laoo.github.io/WsMcp`, served from the **orphan `gh-pages` branch**, cloned in
`build/site` and never worked in. Publishing it is the owner's to ask for, as pushing `main` is.
The branch must never meet `main`: no source on it, no built page on `main`.

Homebrew keeps LLVM keg-only, so `clang-tidy` is not on `PATH` on macOS, and it
does not know where Apple's SDK keeps the standard library. `scripts/tidy.sh`
handles both; do not invoke `clang-tidy` directly.

An agent drives the emulator with the project skill, `.claude/skills/wsmcp/SKILL.md`.

## Layout

| Path | Contents |
|---|---|
| `src/ws_core/` | The emulated machine and the control dispatcher. All logic; no SDL, ImGui, threads, sockets or file dialogs; deterministic; compiles to WebAssembly. |
| `src/ws_core/src/machine/`, `src/ws_core/src/cpu/` | The parts, ported from Mesen2's `Core/WS`, one class per Mesen2 part. |
| `src/ws_server/` | Transports onto the dispatcher: JSON-lines (stdio, TCP) and MCP. |
| `src/ws_cli/` | `wsmcp-cli`: headless runs, `--server`, `--mcp`, `--transcript`, `--batch`. Argument parsing and I/O only. |
| `src/ws_app/` | `wsmcp`: the SDL3 + Dear ImGui window, its emulation thread, sound, keys and gamepads, and the debugger's windows; `shaders/` and their compiled forms (`scripts/compile-shaders.sh`). |
| `src/ws_web/` | The WebAssembly binding (embind), built by the `wasm` preset alone. |
| `web/` | The page on it: HTML, CSS and ES modules, copied into `build/wasm/web` beside `wsmcp.js`. |
| `tests/` | Catch2 v3 tests, linked against `ws_core`; `control/` also holds the protocol's recorded transcripts, `roms/` the sources of test ROMs of our own. |
| `.claude/skills/wsmcp/` | The project skill an agent drives the emulator with. |
| `cmake/` | `Warnings.cmake`, `Dependencies.cmake`, `Version.cmake`, `Install.cmake`. |
| `docs/` | See `docs/README.md`; it is the index and the rulebook. |

## The workspace around it

The repository builds and tests on its own. These are optional and read-only;
the tests that need one skip without it:

- `../Mesen2` — Mesen2 at `b9fa69ddc6d0a331fb103fdb5eef6904305703c2`. Its
  `Core/WS` is the reference for the machine; port from it
  ([0002](docs/decisions/0002-references.md)).
- `~/Documents/WS/BIOS` — the owner's boot ROM dumps (WonderSwan, Color,
  SwanCrystal), under No-Intro's names.
- `~/Documents/WS/ROM` — the owner's cartridges; the tests run the public-domain
  ones, `(PD)` in their names.
- https://github.com/wonderfultoolchain/awesome-wsdev — the index of the
  WonderSwan's documentation, test ROMs and tools. The `context-awesome` MCP
  server in `.mcp.json` searches it.

## Code style

Enforced by `.clang-format` and `.clang-tidy`; the summary below is for
orientation, and the config files are the authority.

- Allman braces, 2-space indent, no tabs, 120-column limit.
- Spaces inside parentheses, but not empty ones: `foo( a, b )`, `if ( x )`, `bar()`.
- East const, pointer on the left: `int* p`, `int const* p`.
- Types are `CamelCase`; functions and variables are `camelBack`; namespaces are
  `lower_case` (`ws`, `ws::machine`, `ws::control`, ...).
- Class members are `mMember` (private or protected); struct members are `member` (public).
- Constants are `UPPER_CASE` at namespace and class scope, and `camelBack` inside functions.
- Enum names follow types; enum constants follow constants.
- File names are `CamelCase.hpp` / `CamelCase.cpp`.
- Emulated registers, buses and bit fields are held in unsigned fixed-width
  types (`std::uint8_t` ... `std::uint64_t`).
- A name ported from Mesen2 keeps its words: `WsPpu::ProcessEndOfScanline`
  becomes `Ppu::processEndOfScanline`, `_state.ScrollX` becomes `mState.scrollX`.
- Every ported file's header comment names the Mesen2 file(s) it ports and the
  commit they were read at.

C++23, but only the subset Apple clang, GCC, MSVC and Emscripten all implement.
`src/ws_core/src/PortabilityChecks.cpp` pins that subset down. Prefer `fmt` (via
spdlog) over `std::format`, and avoid `std::print`, `<stacktrace>` and
`std::flat_map`.

## Documentation discipline

The rules are in `docs/README.md` and they are not optional. In short:

- Documentation carries **why** and **contracts**; code carries **how**.
- One fact, one place; everywhere else links to it. Facts about the WonderSwan
  stay in Mesen2 and the WSdev wiki and are linked, not copied.
- No status, progress or session-note files.
- Documentation is updated **in the same commit** as the change that invalidated it.
- A new document requires an entry in `docs/README.md`, or it does not get written.

Non-obvious reasoning belongs in code comments; do not comment the obvious.

## Never

- Commit ROMs, boot ROMs or save files.
