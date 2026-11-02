# WsMcp

WsMcp is an emulator of the Bandai WonderSwan, WonderSwan Color and SwanCrystal, that people and
AI agents drive alike.

It is a port of the WonderSwan core of **Mesen2**, by Sour, the most accurate emulator of these
machines, to a core of its own with the owner's control protocol, which their emulators share,
around it. An LLM ported it; all credit for understanding the machine goes to Mesen2's authors.

- The NEC V30MZ runs with its prefetch queue and wait states, and every cycle steps the PPU a dot
  and the APU a clock, as in Mesen2.
- All three models, the boot ROM's splash and the internal EEPROM, cartridges with SRAM or
  EEPROM or the Bandai 2003's real-time clock, the sound DMA and the HyperVoice of the Color.
- Other programs and AI agents control it over JSON lines or MCP: run it, press its buttons, read
  its memory, registers and picture, stop it at breakpoints and watchpoints, search the history
  of execution, save and load states.
- The core compiles to WebAssembly, to run in a browser.

## The ROMs

**WsMcp includes no ROM.** The boot ROMs are optional; with them the machine starts as the
hardware does, splash and all. It reads them from a directory under Mesen2's names
(`bootrom.ws`, `bootrom.wsc`, `bootrom_sc.wsc`) or No-Intro's
(`[BIOS] WonderSwan Color Boot ROM (Japan) (En).wsc` and the like). Without them the cartridge
starts where the boot ROM would leave it.

WonderSwan is a trademark of Bandai. WsMcp is not affiliated with Bandai, nor with Mesen2.

## Building

You need CMake 3.25 or later, Ninja and a C++23 compiler: Apple clang, GCC 14 or MSVC.

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release
```

CMake downloads every dependency at a pinned version when it first configures. So the first
configure needs the network.

### WebAssembly

With [Emscripten](https://emscripten.org) installed:

```sh
emcmake cmake --preset wasm && cmake --build --preset wasm
```

`build/wasm/web` is then a site of static files: the page in `web/`, with the core beside it.
Serve it and open it:

```sh
python3 -m http.server -d build/wasm/web 8000      # then http://localhost:8000
```

- Open a cartridge with Open cartridge, or drop one on the screen. The boot ROMs are optional;
  Boot ROMs takes them.
- The page keeps the cartridges, the boot ROMs, the saves and the states in the browser, and
  puts the last cartridge in again when it is opened. `?rom=URL` fetches a cartridge instead.
- The keys are the window's (below), rebound in Keys; gamepads work too. Sound starts at the
  first click or key.
- Control protocol takes a request as one line of JSON, and shows the answer.

`wsmcp.js` and `wsmcp.wasm` are an ES module that another page can use too. It writes the
cartridge into the module's file system and drives it:

```js
import createWsMcp from "./wsmcp.js";
const module = await createWsMcp();
module.FS.writeFile("/game.wsc", new Uint8Array(await (await fetch("game.wsc")).arrayBuffer()));
const ws = new module.WsMcp();
ws.load("/game.wsc", "");          // "" lets the cartridge choose the model; or "ws", "wsc", "sc"
ws.runFrame();                     // 75.47 a second
const rgba = ws.picture();         // ws.width() by ws.height(), RGBA
const sound = ws.audio();          // interleaved stereo, 16-bit, 24 kHz
ws.setButtons(1 << 11);            // A; the bits are in src/ws_core/include/ws/machine/Types.hpp
ws.handle('{"id":1,"method":"cpu.get_state"}');   // the control protocol
```

## Running

There are two programs. Both print their options with `--help`.

### The window: `wsmcp`

```sh
wsmcp
wsmcp --bios-dir ~/Documents/WS/BIOS game.wsc
wsmcp --server tcp:7701 --mcp-http 7702 game.wsc     # and serve it to other programs and agents
```

- A cartridge given after the options, opened with File > Open cartridge or dropped on the window
  is put in and powered up. File > Open recent remembers the last eight.
- File > Choose boot ROM directory sets the boot ROMs' folder, and the window remembers it.
- The cartridges' saves and the save states are kept beside the window's settings, unless
  `--save-dir` and `--state-dir` say otherwise. Emulation > Save state and Load state keep one
  state for each cartridge.
- Emulation > Model puts the cartridge in a WonderSwan, a Color or a SwanCrystal; otherwise it
  chooses.
- A game held upright is shown upright, and its keys turn with it (Display > Turn upright games
  upright).

The keys, while the screen has the focus, rebound in View > Input:

| Button | Held flat | Held upright |
|---|---|---|
| X1-X4 (up, right, down, left) | the arrows | A, W, D, S |
| Y1-Y4 | W, D, S, A | Left, Up, Right, Down |
| A, B | X, Z | Z, X |
| Start, Sound | Return, Q | Return, Q |

A gamepad's pad and left stick are the X pad when flat and the Y pad when upright, its directions
turned with the console.

### Headless: `wsmcp-cli`

`wsmcp-cli` has no window. It runs only on request.

```sh
wsmcp-cli --server tcp:7701 --bios-dir ~/Documents/WS/BIOS --save-dir saves game.wsc   # JSON lines on TCP
wsmcp-cli --mcp --bios-dir ~/Documents/WS/BIOS                                          # MCP on stdio
wsmcp-cli --batch tests/regression/boot.json --bios-dir ~/Documents/WS/BIOS             # check a batch script
```

- The cartridge is named after the options, or loaded later with `cart.load`.
- `--model ws|wsc|sc` builds that machine; otherwise the cartridge chooses: a Color for one that
  supports colour.
- `--no-boot-rom` starts the cartridge without the boot ROM.
- `--save-dir DIR` keeps the cartridge's SRAM or EEPROM, and the console's EEPROM, between runs.
- `--state-dir DIR` is where `state.save` and `state.load` keep save states.

## Remote control

There is one set of methods, specified in
[docs/spec/control-protocol.md](docs/spec/control-protocol.md). It is available in two ways:

- **JSON lines**, on stdio or TCP. Each request is one line of JSON. Each answer is one line.
- **MCP** (Model Context Protocol), on stdio from `wsmcp-cli --mcp` and on HTTP from
  `wsmcp --mcp-http PORT`. Each method is an MCP tool, so an AI agent can use the emulator
  directly, on the machine the window shows. Screenshots come back as images.

```sh
printf '%s\n' '{"id":1,"method":"emu.run_frames","params":{"count":600}}' | nc -w 5 127.0.0.1 7701
scripts/wsmcp.py input.press buttons='["start"]'
scripts/wsmcp.py video.screenshot path=shot.png
```

To add it to Claude Code:

```sh
claude mcp add wsmcp -- /path/to/wsmcp-cli --mcp --bios-dir /path/to/BIOS --save-dir /path/to/saves
```

The server listens on 127.0.0.1 only and has no authentication. A client can make the emulator
read and write files, by the paths it gives to methods such as `cart.load`, `video.screenshot`
and `state.save`. Do not expose it to a network.

## Licence

Copyright (C) 2026 Waldemar Pawlaszek.

WsMcp is released under the GNU General Public License, version 3 only ([LICENSE](LICENSE)), as
Mesen2 is; [docs/decisions/0003-licence-gpl-3.md](docs/decisions/0003-licence-gpl-3.md) says
why. [THIRD_PARTY.md](THIRD_PARTY.md) lists the work it is derived from and the libraries it is
built with.
