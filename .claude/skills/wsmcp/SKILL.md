---
name: wsmcp
description: Load, run, play, inspect and debug WonderSwan, WonderSwan Color and SwanCrystal cartridges in WsMcp, the emulator in this repository. Use it to run a game headless, press its buttons, take screenshots, read and write memory, ports and registers, disassemble V30MZ code, stop at breakpoints or watchpoints, search the history of execution, read what a cartridge sends to the serial port, capture sound, save and load states, or run and record batch scripts.
---

# Driving WsMcp

WsMcp answers a JSON control protocol (`docs/spec/control-protocol.md`, every method with its
parameters). Run it headless as a server that keeps its state, and send it one request per
command with `scripts/wsmcp.py`. Work from the repository root.

## Start it

Build once, then start the server in the background (Bash with `run_in_background`):

```sh
cmake --build --preset release
build/release/src/ws_cli/wsmcp-cli --server tcp:7701 --bios-dir ~/Documents/WS/BIOS --save-dir build/saves --state-dir build
```

The boot ROMs are the owner's, in `~/Documents/WS/BIOS`; without the model's, the cartridge starts
where the boot ROM would leave it. `--save-dir` keeps the cartridges' SRAM and EEPROM and the
console's EEPROM between runs. When done, stop the one you started, by its port:
`pkill -f "wsmcp-cli --server tcp:7701"`.

If 7701 is taken, pick another port and pass it to every request as `--port PORT`, written out on
each call (zsh does not split a command kept in a variable).

A person may have the desktop application open instead, started with `--server tcp:7701`: then
the same requests drive the machine on their screen, and you must not start a second server. The
window runs the machine by itself between your requests: read what you need right after the run
that stops where you want it, or `emu.pause` it first (`emu.resume` when done). A breakpoint or a
watchpoint its own run reaches pauses it, and `emu.status` then says where it stopped. Do not
change what the person is running without being asked: try things on a headless server of your
own.

To have the emulator as MCP tools in your own sessions instead, register it for yourself (it is
not committed, as each session would then run the binary of its start):
`claude mcp add --scope local wsmcp -- "$PWD/build/release/src/ws_cli/wsmcp-cli" --mcp --bios-dir "$HOME/Documents/WS/BIOS"`.
Reconnect it (`/mcp`) after a rebuild.

## Put a cartridge in

Nothing runs until a cartridge is in. The ROMs are in `~/Documents/WS/ROM`, `.ws` and `.wsc`
files; write the path with `$HOME`, which the shell expands where it may leave `~` after `=` alone:

```sh
scripts/wsmcp.py cart.load path="$HOME/Documents/WS/ROM/Blue Wing Blitz (J).wsc"
scripts/wsmcp.py cart.load path="$HOME/Documents/WS/ROM/Anchor Field Z (J) [M].ws" model=wsc
```

A cartridge that supports colour, or a `.wsc` file, runs on a WonderSwan Color, any other on a
WonderSwan; `model` (`ws`, `wsc`, `sc`) chooses. The answer is `cart.info`'s: the header, the
CRC-32s, the save sizes, whether it is vertical, whether the boot ROM ran.

## Send requests

```sh
scripts/wsmcp.py emu.run_frames count=300
scripts/wsmcp.py video.screenshot path=build/shot.png scale=2     # then Read build/shot.png to see it
scripts/wsmcp.py input.press buttons='["start"]'
scripts/wsmcp.py cpu.get_state
scripts/wsmcp.py memory.read address=F000:FFF0 size=16
```

Parameters are `key=value` (JSON values; `0x...` is hex) or one JSON object. An address is linear,
0 to `$FFFFF` (`0x12345`, or `"$12345"` in raw JSON), or a segment and offset `"SSSS:OOOO"`
(`F000:FFF0`, `$F000:$0100`, as the disassembly writes them). The answer is the result as JSON; an
error goes to stderr with exit status 1. Numbers in answers are decimal; `cs_ip` fields give the
segmented form.

## What to ask for

| To | Methods |
|---|---|
| See where it is | `emu.status`, `video.screenshot {path, scale}`, `cart.info` |
| Cartridges | `cart.load {path, model}`, `cart.info`, `cart.save` (write the saves now) |
| Run | `emu.run_frames {count}` (75.47 a second), `emu.run_cycles {count}` (3.072 MHz), `emu.run_until {pc, memory_equals: {address, value}, serial_contains, timeout_cycles}`, `emu.step {count}` (instructions) |
| Power | `emu.reset` (off and on: there is no reset button), `emu.set_bios_directory {path}` |
| Buttons | `input.press {buttons, frames, release_frames}` (held 6 frames, then 6 released, by default), `input.hold {buttons}`, `input.set {button, down}`, `input.clear`, `input.get` |
| Stop at code | `debug.breakpoint.add/remove/list {address}`: a run stops before that instruction |
| Stop at data | `debug.watchpoint.add {address, size, access: read/write/access, port}`: a run stops after the instruction (or DMA) that touched it; `port: true` watches I/O ports |
| See what ran | `debug.trace {count, from}`: index 0 is the newest instruction; `debug.trace.find {pc, pc_start, pc_end, disasm_contains, ax ... ss, from, limit}`; `debug.trace.configure {seconds}` |
| CPU | `cpu.get_state` (registers, flags as ODITSZAPC, CS:IP, the instruction there), `cpu.disassemble {address, count}` |
| Memory | `memory.list_regions`, `memory.read {address, size, region}`, `memory.write {address, data_hex, region}`: without region the CPU's space, with it `work_ram`, `rom`, `save_ram`, `boot_rom`, `internal_eeprom`, `cart_eeprom` by offset |
| I/O | `io.read {port, count}`, `io.write {port, value}`, `io.get_state` (PPU, APU, timers, DMA, IRQs, EEPROMs, the cartridge's banks, serial) |
| Serial port | `serial.read`: what the cartridge sent since the last read, hex and text |
| Sound | `audio.capture_start`, `audio.capture_stop {path}` (a WAV, 24 kHz stereo) |
| State | `state.save {filename}`, `state.load {filename}`, `state.list` (`.wsstate`) |
| The window | `emu.pause`, `emu.resume`: the machine on screen stops running by itself |

A run answers `reason`: `completed`, `breakpoint`, `watchpoint`, `condition_met` (with
`condition`) or `timeout`, with the cycles and frames it took. After a breakpoint, the next run
executes that instruction before stopping again; a breakpoint in code that runs every frame (a
VBlank handler) stops every run within a frame, so remove it to run on.

## The WonderSwan's habits

- A frame is 159 lines of 256 cycles: 75.47 frames a second, so 75 frames is a second, 300 four.
- With the boot ROM, the IPL's logo and sound run first: some 250 frames, a little over three
  seconds, before it hands over at `FFFF:0000`. `emu.run_until {pc: "FFFF:0000"}` stops there.
- The buttons are the X pad (`x1` up, `x2` right, `x3` down, `x4` left; also `up`, `right`,
  `down`, `left`), the Y pad (`y1`-`y4`, likewise), `a`, `b`, `start` and `sound`. Games read the
  keypad in their frame loop: `input.press` holds for 6 frames and lets go for 6, so that both are
  seen; for a game that wants a longer hold, give `frames`.
- A vertical game (`cart.info`'s `vertical`) is played with the console held upright: its picture
  is drawn on its side, as the LCD is, and its directions are on whichever pad the game reads.
  Turn the screenshot a quarter in your head, and try the Y pad for its directions.
- The picture is 224 by 144; a screenshot at `scale=2` or `3` reads more easily.
- Code runs from ROM banked into `$20000`-`$FFFFF`; work RAM is `$00000`-`$03FFF` on a
  WonderSwan and to `$0FFFF` on a Color, SRAM at `$10000`. The I/O ports `$C0`-`$C3` are the
  cartridge's bank registers.

## Batch scripts

A run written down, with checkpoints summed against golden sums
(`docs/spec/batch.md`):

```sh
(cd ~/Documents/WS/ROM && "$OLDPWD/build/release/src/ws_cli/wsmcp-cli" --batch "$OLDPWD/tests/regression/SCRIPT.json" --bios-dir ~/Documents/WS/BIOS)
ctest --preset release -L regression
```
