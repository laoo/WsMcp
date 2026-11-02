# Architecture

This document covers what the components are, how emulated time and data flow through them,
and why the boundaries sit where they do. The reasoning behind individual choices is in
[the decision records](decisions/). Facts about the WonderSwan are in Mesen2's `Core/WS`
(`../Mesen2`) and the [WSdev wiki](https://ws.nesdev.org/wiki); neither is repeated here
([0002](decisions/0002-references.md)).

## The shape

```
                  ┌────────────────────────────── ws_core (library) ──────────────────────────────┐
 cartridge, boot ─▶│ Emulator: files, model, saves                                                  │
 ROM, saves        │   Machine: runs, breakpoints, watchpoints, history, states, RGBA, sound        │
                   │     System (Mesen2's WsConsole)                                                │
                   │      ├─ Cpu (V30MZ) + prefetch  ├─ Ppu ─ Timer   ├─ Apu (4 ch., HyperVoice)    │
                   │      ├─ MemoryManager: map, ports, IRQs          ├─ DmaController              │
                   │      └─ Cart (banks), Eeprom ×2, Serial, ControlManager (buttons)              │
                   │                                                                                │
                   │   control::Dispatcher (every capability, JSON in and out)                      │
                   └────────────▲──────────────────▲──────────────────▲─────────────────▲───────────┘
                                │                  │                  │                 │
                     ws_server transports     wsmcp-cli          wsmcp (GUI)        ws_web (WASM)
                   JSON-lines stdio/TCP, MCP  --server, --mcp    SDL3 + ImGui       embind, a page
```

The core is a library with no SDL, no ImGui, no threads, no sockets and no file dialogs, and it is
deterministic: the same cartridge, boot ROM, saves and inputs always give the same frames, sound
and memory. Everything else is a client of it. The boundary is drawn for four reasons:

- **Testability.** The suite links the core and nothing else.
- **Determinism.** It is what makes golden frames and batch scripts possible.
- **Agent use.** An agent reaches the same machine the GUI shows, through the same dispatcher.
- **The browser.** The core compiles to WebAssembly as it is
  ([0010](decisions/0010-the-core-compiles-to-webassembly.md)).

## Emulated time

One clock: the V30MZ's 3.072 MHz (the 12.288 MHz crystal divided by four). Time is the CPU's
cycle count since power-up. Every bus cycle the CPU makes steps the PPU a dot and the APU a clock
([0008](decisions/0008-stepped-by-the-cpus-cycles.md)); a halted CPU spends cycles idle, which
step them as well.

| Part | Rate | Source |
|---|---|---|
| V30MZ | its instructions' cycles, prefetch and wait states included | Mesen2's `WsCpu`, `WsCpuPrefetch` |
| PPU | a dot a cycle; 256 a line, 159 lines a frame: 40704 cycles, 75.47 Hz | Mesen2's `WsPpu` |
| Timers | at the end of each line and frame | Mesen2's `WsTimer` |
| APU, sound DMA | a clock a cycle; samples at 24 kHz | Mesen2's `WsApu`, `WsDmaController` |

A frame boundary is where the PPU hands a finished picture over. A run ends at the instruction
boundary after what it was asked for; a breakpoint stops after the interrupt the CPU takes and
before the instruction.

## The machine

The parts are Mesen2's, part for part, under its names
([0007](decisions/0007-the-parts-are-mesen2s.md)); `System` stands for its `WsConsole`. Three
models: the WonderSwan (16 KB of RAM, four shades), the WonderSwan Color and the SwanCrystal (64
KB, 4096 colours; the SwanCrystal another LCD). The model is chosen by the cartridge unless asked
for. The boot ROM runs where there is one; otherwise the cartridge starts where the IPL would
leave it ([0009](decisions/0009-boot-roms-are-the-owners.md)).

The cartridge's SRAM or EEPROM, and the console's EEPROM, are kept in the save directory between
runs, written when the machine is powered off, replaced or the emulator ends. So is a Bandai 2003
cartridge's real-time clock, which counts the machine's seconds while it runs and the host's
while it is off ([0015](decisions/0015-the-rtc-follows-the-wsdev-wiki.md)), and a WonderWitch's
flash, once it has been written ([0016](decisions/0016-the-flash-follows-its-datasheet.md)).

## The control API

Every capability is a method of `control::Dispatcher` ([0004](decisions/0004-one-control-api.md)),
in groups: `emu.*` (status, power, runs), `cart.*`, `memory.*`, `io.*`, `cpu.*`, `debug.*`,
`video.*`, `input.*`, `audio.*`, `state.*`, `serial.*`. The transports are thin and none adds a
capability of its own: JSON-lines on stdio and TCP, MCP on stdio and HTTP, and the WebAssembly
binding's `handle`.

## The desktop frontend

`wsmcp` is SDL3 with Dear ImGui (docking), the frontend the owner's emulators share
([0013](decisions/0013-the-desktop-application.md)).

**Threads:**

- The core runs on its own emulation thread (`EmulationThread`). It runs frames at the
  WonderSwan's pace, and between two frames it answers the requests queued for it. It hands each
  finished picture over under a lock, and its sound straight to the audio stream.
- The GUI thread never touches the core. It takes pictures and sends requests to the dispatcher
  through the queue, the same path the network transports use. So the person at the window
  watches what an agent does on `--server` or `--mcp-http`.

**Pacing:** the host's monotonic clock paces the emulation at 75.47 frames a second, whatever the
display's refresh rate. The sound goes to an SDL audio stream at 24 kHz, with dynamic rate
control.

**Buttons and screen:**

- The buttons come from the keyboard, while the screen window has the focus, and from gamepads.
  They are held beside the buttons the protocol holds (`setHostButtons`).
- An upright cartridge's picture is turned a quarter, and the keys and the pads turn with it.
- The debugger's windows (CPU, history, memory, I/O, sound) ask through the protocol, as an agent
  would. A frame of the window's own run that stops at a breakpoint or a watchpoint pauses it.

## The page

`web/` is a page on the WebAssembly build: plain modules, a canvas, an AudioWorklet, the keys and
gamepads as the desktop application has them, and the files it is given kept in IndexedDB
([0014](decisions/0014-the-page-keeps-its-files-in-indexeddb.md)). It has one thread: it runs the
frames the clock owes from `requestAnimationFrame`, and answers the control protocol, typed into
its panel, between them.

## Save states

Each part writes a tagged chunk (`FOURCC`, version, size). A state is a header,
which names the layout's version, the model and the cartridge's CRC-32, the machine's own chunk
and the parts' chunks, then the memories. The pictures are not kept: the next frame redraws them.

## The history of execution

The machine notes every instruction it begins, with its cycle, the registers before it and its
bytes as they were when it ran, and keeps those of the last half second of the machine's time by
default: some 500,000 instructions, 48 bytes each.

## Testing

1. **Unit tests** of the parts that stand alone: the picture's colours, the APU's channels, the
   disassembler.
2. **Machine tests** with the owner's public-domain cartridges and boot ROMs (`~/Documents/WS`):
   frame timing, the IPL's hand-over, breakpoints, save states.
3. **The hardware test ROMs** (`scripts/fetch-test-roms.sh`): FluBBaOfWard's WSCpuTest, every
   instruction's results and flags; WSHWTest, the interrupt controller, the timers, the I/O
   registers, the noise and the sweep; WSTimingTest, every instruction's cycles against the
   hardware's; asiekierka's ws-test-suite, the CPU's quirks and interrupt timing, the display,
   the EEPROMs, the DMAs, the sound and the Bandai 2003's RTC
   ([hardware/departures.md](hardware/departures.md)); and `tests/roms/flash`, which AthenaOS
   runs with its flash driver.
4. **Transcripts and batch scripts:** scripted runs whose pictures, memory and sound are summed
   at checkpoints, to catch regressions.
