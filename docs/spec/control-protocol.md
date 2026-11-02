# The control protocol

This document specifies how a client controls the emulator: what it sends, and what it gets back.
Why there is one protocol is [0004](../decisions/0004-one-control-api.md). It is the protocol
the owner's emulators share, with the WonderSwan's methods.

There is one set of methods (§5). Two protocols carry them:

- **JSON lines**, in the format of §2 and §3, on stdio or TCP.
- **MCP** (Model Context Protocol), where each method is a tool, on stdio or HTTP.

A method takes the same parameters and gives the same result in both.

## 0. Quick start

Start a server on TCP, put a cartridge in, then send JSON lines from a shell. All connections
control the same emulator.

```sh
wsmcp-cli --server tcp:7701 --bios-dir ~/Documents/WS/BIOS --save-dir build/saves &
printf '%s\n' '{"id":1,"method":"cart.load","params":{"path":"game.wsc"}}' \
              '{"id":2,"method":"emu.run_frames","params":{"count":600}}' \
              '{"id":3,"method":"video.screenshot","params":{"path":"shot.png"}}' | nc -w 5 127.0.0.1 7701
```

```json
{"id":1,"ok":true,"result":{"path":"game.wsc","model":"wsc","publisher":1,"game_id":20,"color_support":true,"vertical":false,"rom_size":4194304,"rom_crc32":"...","boot_rom":true,"...":"..."}}
{"id":2,"ok":true,"result":{"reason":"completed","cycles_executed":24422401,"frames_executed":600}}
{"id":3,"ok":true,"result":{"width":224,"height":144,"scale":1,"frame":600,"vertical":false,"path":"shot.png"}}
```

`scripts/wsmcp.py` sends one request per call. It prints the `result` as indented JSON. On an
error, it prints the error on stderr and exits with status 1.

```sh
scripts/wsmcp.py --port 7701 emu.run_frames count=75
scripts/wsmcp.py --port 7701 memory.read address=F000:FFF0 size=16
```

For an AI agent, use MCP. On stdio, the client starts the server itself. The desktop application
serves MCP on HTTP, and the client connects to it.

```sh
claude mcp add wsmcp -- /path/to/wsmcp-cli --mcp --bios-dir /path/to/bios --save-dir /path/to/saves
wsmcp --mcp-http 7702 &                                    # the desktop application, then
claude mcp add --transport http wsmcp http://127.0.0.1:7702/mcp
```

Other MCP clients take a configuration like this one:

```json
{"mcpServers":{"wsmcp":{"command":"/path/to/wsmcp-cli","args":["--mcp","--bios-dir","/path/to/bios"]}}}
```

## 1. Transports

### JSON lines on stdio

`wsmcp-cli --server` reads requests on stdin and writes responses on stdout.

- Each request is one line with one JSON object. Each response is one line with one JSON object.
- Responses are written in the order of the requests. Each response is flushed.
- Blank lines are skipped. They get no response.
- A line can end with `\n` or `\r\n`. There is no limit on its length. A newline inside a string
  must be escaped, as JSON requires.
- stdout carries only responses. Logs go to stderr. The server writes one line about itself to
  stderr when it starts.
- The server stops when stdin ends.

### JSON lines on TCP

`wsmcp-cli --server tcp:PORT` serves the same lines on TCP. It listens on 127.0.0.1 only. It runs
until the process is stopped.

- Any number of connections can be open.
- Each connection has its own stream of lines. Its responses come in the order of its requests.
- A connection ends when the client closes it.
- All connections control **the same** emulator. A cartridge that one client puts in is the
  cartridge that all clients see.
- Requests from all connections are answered one at a time. Each request is finished before the
  next one starts.

This lets a script or an agent keep one emulator across many short commands, as
`scripts/wsmcp.py` does.

There is no authentication. Anyone who can open a connection on this machine controls the
emulator. They can also make it read and write files, by the paths they give to methods such as
`cart.load`, `emu.set_bios_directory`, `video.screenshot`, `audio.capture_stop` and `state.save`.

### JSON lines from the desktop application

`wsmcp --server tcp:PORT` serves the same lines from the desktop application, while its window is
open ([0011](../decisions/0011-network-transports.md)).

- Requests from all connections, and what is done in the window, are handled in turn on the
  emulation thread, between two frames. They act on the machine on screen.
- The window runs the machine by itself, a frame at a time. `emu.pause` stops that; requests are
  still answered. A breakpoint or a watchpoint that the window's own run reaches pauses it too.
- The sound of a run made on request is not played.

### MCP on stdio

`wsmcp-cli --mcp` serves the Model Context Protocol on stdio. Messages are JSON-RPC 2.0, one per
line, framed like the JSON lines above.

- The server answers `initialize`, `ping`, `tools/list` and `tools/call`.
- `initialize` gives the protocol revision the client asks for, if it is one of `2025-11-25`,
  `2025-06-18`, `2025-03-26` or `2024-11-05`. Otherwise it gives `2025-11-25`. The only capability
  is `tools`. The server's name is `wsmcp`, and its version is the one `emu.status` gives. The
  `instructions` are a short paragraph on how to begin.
- A message without an `id` is a notification. It gets no answer. `notifications/initialized` is
  one of these.
- Batches (JSON arrays) are not supported.
- Each method in §5 is a tool. The tool's name is the method's name with `_` in place of `.`:
  `emu.run_frames` becomes `emu_run_frames`. The tool's description and `inputSchema` are the
  method's. The tool's `arguments` are the method's `params`. A parameter that is an address has
  the type `["integer", "string"]`.
- A tool's answer is a `content` array. It holds the method's `result` as JSON text. When the
  result holds a PNG (`video.screenshot` without a `path`), the PNG comes first, as an `image` of
  type `image/png`, and it is removed from the text.
- When a method fails, the tool result has `isError` set. Its one text is `CODE: message`, with
  the codes of §4.

Only these cases are JSON-RPC errors:

| Code | When |
|---|---|
| -32700 | The line is not JSON. The answer has `id` `null`. |
| -32600 | Not a JSON-RPC 2.0 request: not an object, `jsonrpc` is not `"2.0"`, or `method` is missing or not a string. |
| -32601 | A method other than the four above. |
| -32602 | `tools/call` without a `name`, or with a name that is not a tool. |

### MCP on HTTP, from the desktop application

`wsmcp --mcp-http PORT` serves MCP's Streamable HTTP transport from the desktop application, at
`http://127.0.0.1:PORT/mcp`.

- Each message is the body of a POST.
- The answer to a request is the body of the response, as `application/json`.
- A notification gets `202 Accepted`, with no body.
- A body that is not JSON gets `400`, with the -32700 error.
- The server does not stream. A GET gets `405`.
- The server does not issue an `Mcp-Session-Id`. All clients control the same emulator, as on TCP.
- A request with an `Origin` header that is not a page on `localhost` or `127.0.0.1` gets `403`. A
  request without `Origin` (from a program, not a browser) is served.

```sh
curl -s http://127.0.0.1:7702/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"emu_status","arguments":{}}}'
```

The application and its servers act on the machine on screen. What a client loads or runs is
what the window shows, and what is dropped onto the window is what the clients see.

### Transcripts

`wsmcp-cli --transcript FILE` answers the requests in FILE, one per line, and prints the answers
on stdout. Blank lines and lines that start with `#` are skipped. With `--mcp`, the lines are MCP
messages, and a notification has no answer.

With `--expect ANSWERS`, the answers are compared line by line with the lines of ANSWERS, instead
of being printed. The first difference is printed. The exit status is 0 when all lines are equal,
and 1 otherwise.

An answer holds the build's version as `devel`. So a recorded transcript reads the same whatever
build recorded it, and any build checks it in the same way. `tests/control/` holds the
transcripts CTest checks; they need no cartridge.

## 2. Requests

```json
{"id":1,"method":"emu.status","params":{}}
```

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | A non-negative integer, echoed in the response. The protocol does not require ids to be unique or increasing; a client that wants to match responses keeps them so. |
| `method` | yes | The method's name. |
| `params` | no | An object. A method that takes no parameters is given `{}` when it is left out. |

Other fields are ignored. Parameters that a method does not use are ignored.

Names are case-sensitive. So are parameter values that are names (`region`, `access`, a button).

A parameter that is a non-negative integer (a size, a count, a port) can be a JSON integer, or a
string of hex digits after `0x` or `$`: `"0x400"`, `"$400"`. A parameter that is an **address**
(`memory.read` and `memory.write` without a region, `cpu.disassemble`, breakpoints, memory
watchpoints, `emu.run_until`'s `pc` and `memory_equals`, `debug.trace.find`'s `pc`) is a linear
address in the V30MZ's 20-bit space, 0 to `$FFFFF`, written as such an integer, or a segment and
an offset, `"SSSS:OOOO"`, one to four hex digits each, either with `$` or `0x` before it or not:
`"F000:FFF0"` and `"$F000:$FFF0"` are `$FFFF0`, as `segment * 16 + offset` wrapped to 20 bits.
Answers always give integers; where they give a segment and an offset as well, it is as
`cs_ip`, `"F000:FFF0"`. The `id` must be a JSON integer.

## 3. Responses

A request that succeeded:

```json
{"id":1,"ok":true,"result":{"version":"devel"}}
```

A request that failed:

```json
{"id":1,"ok":false,"error":{"code":"unknown_method","message":"Unknown method: emu.nonsense"}}
```

- Keys appear in the order shown.
- `error.code` is stable and meant for programs; `error.message` is for people and may change.
- A request whose `id` cannot be read is answered under `id` 0. That covers text that is not
  JSON, JSON that is not an object, and an `id` that is missing, negative or not an integer.
- Every request is answered. A malformed one never ends the session.

## 4. Error codes

| Code | Meaning |
|---|---|
| `bad_request` | The request is malformed: not JSON, not an object, `id` or `method` missing or of the wrong type, `params` not an object, or a parameter missing, of the wrong type or out of its bounds. |
| `unknown_method` | No method of that name exists. |
| `no_machine` | The method needs a machine running, and none is: no cartridge is in, or it could not be read. The message says why. |
| `not_found` | `cart.load` was given a path that is not a file. |
| `load_failed` | `cart.load` could not read the cartridge, or it is not one; what ran runs on. |
| `power_up_failed` | `emu.reset` or `emu.set_bios_directory` could not power the machine up; what ran runs on. |
| `save_failed` | `cart.save` could not write a save. |
| `invalid_input` | An `input.` method was given a name that is no button. |
| `invalid_region` | No memory region of that name, or `memory.write` was given one it cannot write: a ROM. |
| `invalid_range` | The bytes asked for, or given to `memory.write`, run past the end of the region; or, in the CPU's address space, a byte to write falls where no RAM or SRAM is mapped. |
| `screenshot_failed` | The picture could not be encoded or written. |
| `state_failed` | A state file could not be written or read. |
| `state_mismatch` | A state file is not a state, is of another build's layout, or of another model or cartridge. |
| `capture_running` | `audio.capture_start` while a capture is running. |
| `capture_not_running` | `audio.capture_stop` with no capture running. |
| `capture_failed` | The capture's WAV could not be written. |
| `shutting_down` | The desktop application is closing, and the request was not run. |

## 5. Methods

A machine runs once a cartridge is in: given on the command line, or put in with `cart.load`.
Every method but `emu.status`, `emu.pause`, `emu.resume`, `emu.reset`, `emu.set_bios_directory`,
`cart.load`, `memory.list_regions`, `debug.trace.configure` and `state.list` answers `no_machine`
until then.
The machine is a WonderSwan (`ws`), a WonderSwan Color (`wsc`) or a SwanCrystal (`sc`); the boot
ROM (the IPL) runs first when the BIOS directory (`--bios-dir`, `emu.set_bios_directory`) has the
model's, and the cartridge starts where the IPL would leave it otherwise.

A run ends at an instruction boundary, where the V30MZ's registers are whole, never inside an
instruction or an interrupt's entry. Time is counted in cycles of the 3.072 MHz clock from
power-up; a frame is 159 lines of 256 cycles, 75.47 a second.

### `emu.status`

Takes no parameters.

```json
{"id":1,"ok":true,"result":{"version":"devel","paused":false,"running":true,"model":"wsc","cartridge":"game.wsc","boot_rom":true,"cycles":24422401,"frame":600}}
{"id":1,"ok":true,"result":{"version":"devel","paused":false,"running":false,"error":"no cartridge is in"}}
```

| Field | Meaning |
|---|---|
| `version` | The emulator's version: the release tag it was built from, or `devel`. |
| `paused` | Whether the desktop application's own run is paused (`emu.pause`). Always present; `wsmcp-cli` has no run of its own. |
| `running` | Whether a machine is powered up. |
| `model`, `cartridge`, `boot_rom` | With `running`: `ws`, `wsc` or `sc`; the cartridge's path; whether the boot ROM ran. |
| `cycles`, `frame` | With `running`: cycles since power-up, and frame boundaries passed. |
| `breakpoint`, `watchpoint` | Present when the last run stopped at one: the same object as in that run's answer. So a client that attaches to a window that stopped by itself sees where. |
| `error` | Without `running`: why not. |

### `emu.pause`, `emu.resume`

`emu.pause` stops the desktop application's own run. `emu.resume` starts it again. Both take no
parameters and answer `{}`.

`wsmcp-cli` has no run of its own. It runs only on request. There, these only set what
`emu.status` says.

### `emu.set_bios_directory`

| Param | Meaning |
|---|---|
| `path` | A directory holding the boot ROMs, as Mesen2 names them (`bootrom.ws`, `bootrom.wsc`, `bootrom_sc.wsc`) or as No-Intro does. |

Reads the boot ROMs from `path` from now on, and powers the machine up again with the cartridge it
runs, as `emu.reset` does. Answers `{"boot_rom"}`, whether the boot ROM ran. On failure, the
directory and the machine stay as they were; with no cartridge in, that is always.

Errors: `power_up_failed`, `bad_request`.

### `emu.reset`

Takes no parameters. The WonderSwan has no reset button: this switches it off and on. The saves
are written back, the cartridge, the boot ROM and the saves are read again, every part starts as
at power-up, and time from 0. Breakpoints, watchpoints and the trace belong to the machine, and
start empty. Answers `{"boot_rom"}`. On failure the machine runs on.

Errors: `power_up_failed`.

### `cart.load`

| Param | Meaning |
|---|---|
| `path` | The cartridge's ROM, a `.ws` or `.wsc` file. |
| `model` | `ws`, `wsc` or `sc`. Left out, a cartridge whose header supports colour, or a `.wsc` file, runs on a WonderSwan Color, and any other on a WonderSwan. |

Writes the saves of the cartridge that ran back, puts this one in, and powers up, reading its
SRAM or EEPROM and the model's console EEPROM from the save directory (`--save-dir`) if there is
one. Answers as `cart.info`. On failure what ran runs on.

Errors: `not_found`, `load_failed`, `bad_request`.

### `cart.info`

Takes no parameters.

```json
{"id":2,"ok":true,"result":{"path":"game.wsc","model":"wsc","publisher":1,"game_id":20,"color_support":true,"vertical":false,"rom_size":4194304,"rom_crc32":"0f1e2d3c","game_version":0,"checksum":40128,"checksum_ok":true,"header_hex":"ea00c000f00001011400030441000cb0","save_ram_size":32768,"cart_eeprom_size":0,"internal_eeprom_size":2048,"boot_rom":true,"boot_rom_crc32":"..."}}
```

| Field | Meaning |
|---|---|
| `path`, `model` | The cartridge's file, and the model running it. |
| `publisher`, `game_id`, `game_version`, `color_support` | What its header says. |
| `vertical` | Whether it is held upright: by its header's flag, or, once it has run, by the LCD's icons it lights. |
| `rom_size`, `rom_crc32` | The ROM as given, and its CRC-32 as eight hex digits. |
| `checksum`, `checksum_ok` | The header's checksum, and whether the 16-bit sum of every other byte of the ROM is it. |
| `header_hex` | The header whole: the last 16 bytes of the ROM. |
| `save_ram_size`, `cart_eeprom_size`, `internal_eeprom_size` | The cartridge's SRAM and EEPROM (0 for none) and the console's EEPROM, in bytes. |
| `boot_rom`, `boot_rom_crc32` | Whether the boot ROM ran, and when it did, its CRC-32. |

Errors: `no_machine`.

### `cart.save`

Takes no parameters. Writes the cartridge's SRAM or EEPROM and the console's EEPROM to the save
directory now, as a power-up and the emulator's end do: the cartridge's as its file name with
`.sav`, the console's as `ws.ieeprom`, `wsc.ieeprom` or `sc.ieeprom`. Answers
`{"written", "save_directory"}`; without a save directory nothing is written.

Errors: `no_machine`, `save_failed`.

### `emu.run_frames`, `emu.run_cycles`

| Param | Meaning |
|---|---|
| `count` | `emu.run_frames`: frame boundaries to run through, where the PPU hands a finished picture over, 75.47 a second. `emu.run_cycles`: cycles to run at least. |

```json
{"id":4,"ok":true,"result":{"reason":"completed","cycles_executed":3052801,"frames_executed":75}}
```

| Field | Meaning |
|---|---|
| `reason` | `completed`; `breakpoint` when it stopped before an instruction with a breakpoint; `watchpoint` when it stopped after an instruction that made a watched access. |
| `cycles_executed`, `frames_executed` | Cycles the run took, to the instruction boundary it stopped at, and frame boundaries it passed. |
| `breakpoint` | With `reason` `breakpoint`: `{"address"}`, linear. |
| `watchpoint` | With `reason` `watchpoint`: `{"address", "port", "access", "word", "value", "cs", "ip", "pc"}`: the address or port accessed, `read` or `write`, whether it was a 16-bit access and the value it carried, and the instruction that made it, as CS:IP and linear. |

Every run, `emu.run_until`, `input.press` and `emu.step` among them, stops at a breakpoint or a
watchpoint and answers so.

Errors: `no_machine`, `bad_request`.

### `emu.run_until`

| Param | Meaning |
|---|---|
| `pc` | Stop before the instruction at this address. |
| `memory_equals` | `{"address", "value"}`: stop when the byte at the address holds the value. |
| `serial_contains` | Stop when the bytes sent to the serial port and not yet read by `serial.read` hold this text. |
| `timeout_cycles` | Cycles after which to give up; 61,440,000 (20 s) when left out. |

Give one or more; any of them stops it. Each is checked at every instruction boundary, and at
every cycle of a CPU halted by HLT. Answers as `emu.run_frames` does, with `reason`
`condition_met` and `condition`, `pc`, `memory_equals` or `serial_contains`; or with `timeout`
when the time ran out first.

Errors: `no_machine`, `bad_request`.

### `emu.step`

| Param | Meaning |
|---|---|
| `count` | Instructions to run, 1 to 1,000,000; 1 when left out. |

Answers as `emu.run_frames` does. An interrupt's entry is no instruction: it runs on the way to
the next one. The cycles a CPU halted by HLT waits are no instructions either: a step from HLT
runs until an interrupt wakes it.

Errors: `no_machine`, `bad_request`.

### `memory.list_regions`

Takes no parameters. Answers `regions`, each `{"name", "size", "writable", "holds"}`; `size` is
the running machine's, `null` when none runs.

| Region | Holds |
|---|---|
| `cpu` | The V30MZ's 20-bit address space, 1 MB, as it is mapped now: work RAM from `$00000`, SRAM at `$10000`, the ROM's banks at `$20000`-`$FFFFF`, and the boot ROM over the top while it is enabled. The region when none is named. |
| `work_ram` | The console's RAM: 16 KB on a WonderSwan, 64 KB on a Color or SwanCrystal. |
| `rom` | The cartridge's ROM, whole. |
| `save_ram` | The cartridge's SRAM; size 0 without one. |
| `boot_rom` | The IPL: 4 KB on a WonderSwan, 8 KB on a Color or SwanCrystal; size 0 when it is not run. |
| `internal_eeprom` | The console's EEPROM: the owner's name and settings. |
| `cart_eeprom` | The cartridge's EEPROM; size 0 without one. |

### `memory.read`

| Param | Meaning |
|---|---|
| `address` | In `cpu`, an address (§2); in another region, the first byte's offset. |
| `size` | How many bytes, at most `$100000`. |
| `region` | A name from `memory.list_regions`; `cpu` when left out. |

```json
{"id":5,"method":"memory.read","params":{"address":"F000:FFF0","size":5}}
{"id":5,"ok":true,"result":{"region":"cpu","address":1048560,"data_hex":"ea000000ff"}}
```

Reads without the side effects a read by the V30MZ has. `address` in the answer is linear in
`cpu`. `data_hex` holds two lowercase hex digits per byte, in address order. In `cpu` an address
nothing answers reads 0.

Errors: `no_machine`, `invalid_region`, `invalid_range`, `bad_request`.

### `memory.write`

| Param | Meaning |
|---|---|
| `address` | As `memory.read` takes it. |
| `data_hex` | The bytes, two hex digits each, upper or lower case. |
| `region` | `cpu`, `work_ram`, `save_ram`, `internal_eeprom` or `cart_eeprom`; `cpu` when left out. |

Writes as a debugger does: no bus cycle is made and no device runs. In `cpu` every byte must fall
where RAM or SRAM is mapped now. The whole range is checked first, so the write happens
completely or not at all. Answers `{"region", "address", "size"}`.

Errors: `no_machine`, `invalid_region`, `invalid_range`, `bad_request`.

### `io.read`

| Param | Meaning |
|---|---|
| `port` | The first port, 0 to `$FFFF`. |
| `count` | How many consecutive ports, 1 to 256; 1 when left out. |

Reads ports as `IN` would, without a read's side effects: `$B1` and `$B3`, the serial port's data
and status, and `$B5`, the keypad, read 0. The WonderSwan's ports are `$00`-`$FF`; the I/O space
above them mirrors them, or answers as nothing does. Answers `{"port", "values", "data_hex"}`.

Errors: `no_machine`, `bad_request`.

### `io.write`

| Param | Meaning |
|---|---|
| `port` | The port, 0 to `$FFFF`. |
| `value` | The byte. |

Writes a port as `OUT` would, with every effect the write has, but beside the bus: no cycle
passes. Answers `{}`.

Errors: `no_machine`, `bad_request`.

### `io.get_state`

Takes no parameters. Answers every device's registers as the debugger keeps them. A register the
PPU latches for the line it draws is `{"value", "latch"}`.

| Field | Meaning |
|---|---|
| `model` | `ws`, `wsc` or `sc`. |
| `ppu` | The frame count, scanline and cycle; `control` (`$00`), `mode` (`monochrome`, `color_2bpp`, `color_4bpp`, `color_4bpp_packed`) and `next_mode`; the LCD (`lcd_enabled`, `lcd_control`, `high_contrast`, `sleep_enabled`); `screen_address` (`$07`), `bg_color`, `irq_scanline`, `last_scanline`, `back_porch_scanline`; `screen1` and `screen2`, each `enabled`, `map_address`, `scroll_x`, `scroll_y`; `screen2_window` and `sprite_window`, each `enabled`, `left`, `right`, `top`, `bottom`, and `draw_outside_screen2_window`; the sprites (`sprites_enabled`, `sprite_table_address`, `first_sprite`, `sprite_count`); `bw_shades` and `bw_palettes`; `icons`, the LCD's segments; `lcd_tft_config`, the SwanCrystal's. |
| `apu` | `ch1` to `ch4`, each `enabled`, `frequency`, `timer`, `left_volume`, `right_volume`, `sample_position`, `left_output`, `right_output`, with channel 2's PCM, channel 3's sweep and channel 4's noise; `hyper_voice`; `wave_table_address`, the speaker's and headphones' state, `master_volume`, `internal_master_volume`, `hold_channels`, `sound_test`. |
| `timer` | `control`; the horizontal and vertical blank timers, each the count, reload value, enabled and auto-reload. |
| `dma` | The general DMA's source, destination, length and control; the sound DMA's source, length, their reload values, control and its decoded bits, frequency and timer. |
| `memory_manager` | The interrupt controller (`active_irqs`, `enabled_irqs`, `irq_vector_offset`), the system control registers, colour and 4bpp modes, the boot ROM's lock, the cartridge's bus width and wait states, and whether a power-off was asked. |
| `keypad` | `input_select`, the rows `$B5` selects. |
| `serial` | `enabled`, `baud` (9600 or 38400), the send and receive buffers and flags. |
| `internal_eeprom` | The console's EEPROM: `size`, the command, control and buffers, its flags. |
| `cart` | `banks`, the four bank registers `$C0`-`$C3`; `eeprom`, as `internal_eeprom`, `size` `none` without one. |

Errors: `no_machine`.

### `cpu.get_state`

Takes no parameters.

```json
{"id":6,"ok":true,"result":{"cs":61440,"ip":65520,"pc":1048560,"cs_ip":"F000:FFF0","ax":0,"bx":0,"cx":0,"dx":0,"sp":0,"bp":0,"si":0,"di":0,"ds":0,"es":0,"ss":0,"flags":61442,"flags_text":"oditszapc","halted":false,"disasm":"JMP $FF00:$0000","bytes_hex":"ea000000ff","cycle":0}}
```

| Field | Meaning |
|---|---|
| `cs`, `ip`, `pc`, `cs_ip` | The instruction the V30MZ runs next: its segment and offset, its linear address, and both as text. |
| `ax` ... `ss` | The registers: `ax`, `bx`, `cx`, `dx`, `sp`, `bp`, `si`, `di`, `ds`, `es`, `ss`. |
| `flags`, `flags_text` | The flags register as PUSHF pushes it, and as `ODITSZAPC`, a capital for each flag set. |
| `halted` | Whether HLT has stopped it until an interrupt. |
| `disasm`, `bytes_hex` | The instruction at CS:IP, as `cpu.disassemble` gives it. |
| `cycle` | The cycle the instruction begins in. |

Errors: `no_machine`.

### `cpu.disassemble`

| Param | Meaning |
|---|---|
| `address` | Where to start, an address (§2). |
| `count` | How many instructions, 1 to 1000. |

Answers `instructions`, each `{"address", "cs_ip", "length", "bytes_hex", "text"}`, read as the
V30MZ would fetch them, without a read's side effects, in Mesen2's syntax:
`MOV AX, DS:[BX+SI+$04]`, `JMP $F000:$0100`; a relative jump shows the offset it lands at. Given
as `"SSSS:OOOO"`, the walk keeps to that segment, IP wrapping within it. Given linear, it is taken
in the current CS when CS reaches it, and as offset `address & $FFFF` of segment
`(address >> 16) * $1000` otherwise.

Errors: `no_machine`, `bad_request`.

### `debug.breakpoint.add`, `debug.breakpoint.remove`, `debug.breakpoint.list`

`add` and `remove` take `address`, an instruction's address (§2). A breakpoint is by linear
address, whatever segment reaches it. `add` answers `{"address"}`, linear; `remove` answers
`{"removed"}`, whether there was one. A run stops before the instruction there; the instruction
the machine stands at runs when the run goes on, so a run never stops twice at once at the same
place. A breakpoint in code that runs every frame stops every run within a frame; remove it to
run on. `list` answers `{"breakpoints": [addresses]}`, ascending.

Errors: `no_machine`, `bad_request`.

### `debug.watchpoint.add`, `debug.watchpoint.remove`, `debug.watchpoint.list`

A watchpoint stops a run after the instruction that reads or writes data in its range of memory,
or of I/O ports: any access the instruction makes but fetching its own bytes, and a DMA's.

| Param | Meaning |
|---|---|
| `address` | The range's first address (§2); with `port`, its first port, 0 to `$FFFF`. One watchpoint per address and space, a second replacing the first. |
| `size` | Bytes or ports in the range, 1 if left out. |
| `access` | `read`, `write` (the default) or `access` for both. |
| `port` | `true` to watch I/O ports rather than memory; `false` when left out. |

`add` answers `{}`. `remove` takes `address` and `port`, and answers `{"removed"}`. `list` answers
`{"watchpoints":[{"address","size","access","port"}]}`.

Errors: `no_machine`, `bad_request`.

### `debug.trace`

The history of execution, counted back from now: index 0 is the newest instruction, the one the
machine stands at and runs next, 1 the one that ran before it, and so on, over the last seconds
`debug.trace.configure` sets, 0.5 by default. While the machine stands still an index keeps
naming the same instruction; as it runs on, the indices move with it.

| Param | Meaning |
|---|---|
| `from` | The newest one's index; 0 when left out. |
| `count` | How many, going back from `from`, 0 to 4096; 32 when left out. |

```json
{"id":8,"method":"debug.trace","params":{"from":1,"count":1}}
{"id":8,"ok":true,"result":{"total":512340,"seconds":0.5,"instructions":[
  {"index":1,"cycle":1534001,"pc":1048560,"cs_ip":"F000:FFF0","ax":0,"bx":0,"cx":0,"dx":0,"sp":0,"bp":0,"si":0,"di":0,"ds":0,"es":0,"ss":0,"flags":61442,"bytes_hex":"ea000000ff","disasm":"JMP $FF00:$0000"}]}}
```

| Field | Meaning |
|---|---|
| `total` | How many instructions are kept; the oldest has index `total` - 1. |
| `seconds` | The seconds kept. |
| `instructions` | Newest first, each with its `index`, the cycle it began in, its `pc` and `cs_ip`, the registers and flags before it, and its bytes and disassembly as they were when it ran. |

Errors: `no_machine`, `bad_request`.

### `debug.trace.find`

Searches the history back from index `from`, the newest first, for the instructions that meet
every condition given, on the emulator's side.

| Param | Meaning |
|---|---|
| `pc` | The instruction's address (§2). |
| `pc_start`, `pc_end` | Instead of `pc`, both: the linear address is at least `pc_start` and below `pc_end`, at most `$100000`. |
| `disasm_contains` | Its disassembly, as it ran, holds this text, case aside: `OUT`, `CALL`, `[$0400]`. |
| `ax`, `bx`, `cx`, `dx`, `sp`, `bp`, `si`, `di`, `cs`, `ds`, `es`, `ss` | The register held this before the instruction. |
| `from` | The index to search back from; 0, the newest, when left out. |
| `limit` | The most matches to answer, 1 to 4096; 32 when left out. |

Answers `{"total", "matches", "next_from"}`. `matches` are as `debug.trace` answers its
instructions, newest first. `next_from` is where to go on searching back, with the same
conditions, or `null` when the search reached the oldest kept. Indices count back from the
instruction the machine stands at, so they hold while it stands still.

Errors: `no_machine`, `bad_request`.

### `debug.trace.configure`

| Param | Meaning |
|---|---|
| `seconds` | The seconds of the machine's time the history keeps, 0.01 to 10. |

Applies to the machine running and to every one powered up after it. A second is at most
3,072,000 instructions, some 50 bytes each. Answers `{"seconds"}`.

Errors: `bad_request`.

### `video.screenshot`

The last complete picture: the LCD's 224 by 144 dots, with its icons beside or below them when
the emulator is set to show them, in the WonderSwan's shades of grey or the Color's colours. A
vertical game draws on its side, as the LCD is when the console is held upright; the picture is
not turned.

| Param | Meaning |
|---|---|
| `path` | Where to write it as a PNG. Left out, the PNG comes back in the answer. |
| `scale` | Each dot as `scale` by `scale` pixels, 1 to 8; 1 when left out. |

```json
{"id":7,"ok":true,"result":{"width":448,"height":288,"scale":2,"frame":600,"vertical":false,"path":"shot.png"}}
{"id":8,"ok":true,"result":{"width":224,"height":144,"scale":1,"frame":600,"vertical":false,"png_base64":"iVBORw0..."}}
```

`width` and `height` are the PNG's. `frame` counts the pictures completed since power-up.
`vertical` is `cart.info`'s.

Errors: `no_machine`, `screenshot_failed`, `bad_request`.

### `input.press`

| Param | Meaning |
|---|---|
| `buttons` | The buttons' names, a list (a single name is taken too). |
| `frames` | Frames to hold them, 1 to 600; 6 when left out. |
| `release_frames` | Frames to run after letting them go, 0 to 600; 6 when left out. |

Holds the buttons, beside those `input.hold` holds, runs `frames`, lets them go, and runs
`release_frames`, so that a game that reads the keypad once a frame sees both the press and the
release. Answers as `emu.run_frames` does, the cycles and frames of both runs together. A run
that stops at a breakpoint or a watchpoint ends the press there, the buttons let go.

The names are the hardware's: `x1` to `x4`, the X pad, up, right, down and left as the console is
held flat, also named `up`, `right`, `down` and `left`; `y1` to `y4`, the Y pad, likewise; `a`,
`b`, `start`, `sound`. A vertical game is held turned and reads the pads turned with it; which
button is its up is the game's to say.

```json
{"id":9,"method":"input.press","params":{"buttons":["start"]}}
{"id":9,"ok":true,"result":{"reason":"completed","cycles_executed":488449,"frames_executed":12}}
```

Errors: `no_machine`, `invalid_input`, `bad_request`.

### `input.set`, `input.hold`, `input.clear`, `input.get`

`input.set` holds one `button` down (`down` true) or lets it go, until told otherwise.
`input.hold` holds exactly the `buttons` listed, letting go of any other; `[]` lets go of all.
Both answer `{"held"}`, the names held after. `input.clear` lets every button go and answers `{}`.
None of them runs the machine. `input.get` answers `{"held", "mask"}`: the names, as the hardware
gives them, and the mask of the `Button` bits (`ws/machine/Types.hpp`).

The desktop application's keyboard and pads hold buttons of their own beside these; the machine
sees both.

Errors: `no_machine`, `invalid_input`, `bad_request`.

### `audio.capture_start`, `audio.capture_stop`

`audio.capture_start` keeps every frame of the machine's sound from then on, made by any run, and
answers `{}`. `audio.capture_stop` takes `path`, ends the capture and writes what it kept as a
WAV, answering `{"path", "frames", "seconds"}`.

The sound is stereo at 24 kHz, 16 bits: the four channels and Hyper Voice as the headphones hear
them, or as the speaker does when the emulator is set so.

Errors: `no_machine`, `capture_running`, `capture_not_running`, `capture_failed`, `bad_request`.

### `state.save`, `state.load`, `state.list`

`state.save` writes the whole machine's state to `filename`, answering `{"filename", "size"}`;
`state.load` restores one and answers `{}`. A `filename` without a directory is in the state
directory (`--state-dir`, or the working directory). `state.list` answers the `.wsstate` files
there, `{"states":["a.wsstate"]}`; `state.save` adds no extension of its own.

A state holds everything a run changes: every part's registers, work RAM, the cartridge's SRAM
and EEPROM, and the console's EEPROM. It holds none of the ROMs, the breakpoints, the
watchpoints, the trace or the last complete picture, and the buttons held stay as they are. It is read back by the
build that wrote it, into a machine of the same model with the same cartridge; any other is
refused with `state_mismatch`, and the machine runs on as it was. Loading a state, then running,
gives exactly the frames, sound and memory that followed when it was saved.

Errors: `no_machine`, `state_failed`, `state_mismatch`, `bad_request`.

### `serial.read`

Takes no parameters. The serial port (the EXT connector's UART, 9600 or 38400 baud) has nothing
at the other end: what the cartridge sends is kept, and nothing is received. Answers the bytes
sent since the last `serial.read`, or since power-up, and forgets them:

| Field | Meaning |
|---|---|
| `size` | How many bytes. |
| `hex` | Two lowercase hex digits a byte. |
| `text` | A character a byte: printable ASCII, tab, line feed and carriage return as themselves, any other byte as `.`. |

`emu.run_until`'s `serial_contains` looks into the same bytes, and leaves them for
`serial.read`.

Errors: `no_machine`.
