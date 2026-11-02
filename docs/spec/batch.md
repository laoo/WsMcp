# Batch scripts

A batch script is a run of the emulator written down: requests of the control protocol
([control-protocol.md](control-protocol.md)), and checkpoints at which the machine is summed.
`wsmcp-cli --batch FILE` runs one headless and unthrottled. The regression suite is such
scripts, each holding the sums it was recorded with: `tests/regression/*.json`.

## The file

A JSON object:

| Field | Meaning |
|---|---|
| `description` | What the script runs, for people. Optional. |
| `boot_rom` | `true` when the script needs the boot ROM to run: without the model's in the BIOS directory, the script is skipped at its first checkpoint rather than summed. `false` when left out. |
| `steps` | The steps, in order. |
| `golden` | The sums each checkpoint was recorded with, by checkpoint name. Written by `--record`. |

A step is one of:

- a request: `{"method": "emu.run_frames", "params": {"count": 600}}`, as the protocol has it,
  without an `id`;
- a checkpoint: `{"checkpoint": "title"}`.

The machine runs from the start when the command line names a cartridge; a script may put one in
with `cart.load` instead. Paths in requests (`cart.load`, `state.load` ...) are relative to the
working directory.

```json
{
  "description": "Power-up through the IPL to the title screen.",
  "boot_rom": true,
  "steps": [
    { "method": "cart.load", "params": { "path": "game.wsc" } },
    { "method": "emu.run_frames", "params": { "count": 600 } },
    { "checkpoint": "title" },
    { "method": "input.press", "params": { "buttons": ["start"] } },
    { "checkpoint": "menu" }
  ],
  "golden": {
    "title": { "frame": 600, "picture": "xxxxxxxx", "work_ram": "xxxxxxxx", "sound": "xxxxxxxx" },
    "menu": { "frame": 612, "picture": "xxxxxxxx", "work_ram": "xxxxxxxx", "sound": "xxxxxxxx" }
  }
}
```

## Checkpoints

At a checkpoint the machine is summed:

| Sum | Of |
|---|---|
| `frame` | Frame boundaries passed since power-up. |
| `picture` | The last complete picture, RGBA, as `video.screenshot` draws it at scale 1. |
| `work_ram` | The console's RAM, the region `memory.read` names `work_ram`. |
| `sound` | Every stereo frame made since the checkpoint before (or since power-up): left, then right, 16-bit little-endian. |

Each sum is a CRC-32 as eight lowercase hex digits. The emulator is deterministic, so a script's
sums change only when what it emulates does; the cartridge, the boot ROM and the saves a script
reads are part of what it emulates. A power-up (`cart.load`, `emu.reset`,
`emu.set_bios_directory`) starts the sound afresh.

## Running

```sh
wsmcp-cli --batch FILE --bios-dir ~/Documents/WS/BIOS            # check against golden
wsmcp-cli --batch FILE --bios-dir ~/Documents/WS/BIOS --record   # write golden into FILE
wsmcp-cli --batch FILE game.wsc --bios-dir ~/Documents/WS/BIOS   # with the cartridge named here
```

It prints a line per checkpoint that differs from its golden sums, or one that says they all
match, and exits:

| Status | When |
|---|---|
| 0 | Every checkpoint matches. |
| 1 | A checkpoint differs from its golden sums, has none, or a golden one was not reached. |
| 2 | A step failed, or the file is not a script. |
| 77 | The cartridge, the boot ROM the script needs, or a file a step names, is not there: a step answered `not_found` or `no_machine`, or a checkpoint came with no machine running, or without the boot ROM `boot_rom` asks for. CTest's code for a skipped test. |

`--record` overwrites the file's `golden` with the sums of this run. Golden sums are this
emulator's own, so they catch a change, not an error: before recording new ones, see that the run
does what its description says.

## The regression suite

`ctest -L regression` runs every script in `tests/regression/` with the cartridges' directory,
`WS_TEST_ROM_DIR` (`~/Documents/WS/ROM` by default), as the working directory, and the boot ROMs
from `WS_TEST_BIOS_DIR` (`~/Documents/WS/BIOS`): a script's `cart.load` names a cartridge by its
file name there. No cartridge or boot ROM is carried in the tree; a script whose cartridge or boot
ROM is not there is skipped.
