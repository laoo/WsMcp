# 0014: The page is plain modules on ws_web, and keeps its files in IndexedDB

**Status:** accepted

## Context

The core runs in a browser through `ws_web` ([0010](0010-the-core-compiles-to-webassembly.md)),
which leaves the canvas, the sound and the keys to the page. A page has no directory to read
boot ROMs from or keep saves in: it gets files from the person, one by one, and forgets them when
it is closed, unless it stores them itself.

## Decision

- **The page is `web/`:** HTML, CSS and ES modules, with no framework, no bundler and no build
  step of their own. The `wasm` preset copies them beside `wsmcp.js` and `wsmcp.wasm`, so
  `build/wasm/web` is the whole site, served as static files.
- **What it is given, it keeps.** `/cartridges`, `/bios`, `/saves` and `/states` are mounted on
  Emscripten's IDBFS, and the page writes them back to IndexedDB:
  - after each change,
  - every 30 seconds while a game runs,
  - when the page is hidden.

  The page puts the last cartridge in again when it is opened. Its settings and keys are in
  `localStorage`.
- **A boot ROM is told apart by its size**, since the core looks for it by name
  (`Emulator::bootRomPath`). A file with one of the names the core knows is kept as it is; any
  other is taken as a boot ROM when it is 4 KB (a WonderSwan's) or 8 KB, or is named like one.
  An 8 KB one is the Color's unless its name says SwanCrystal.
- **The frontend is the desktop application's**
  ([0013](0013-the-desktop-application-is-petmcps.md)):
  - the host's clock paces the frames at 75.47 Hz;
  - an upright cartridge is turned a quarter anticlockwise;
  - the keys are bound for each way the console is held, with Mesen2's defaults, and gamepads are
    turned with it.

  Keys are bound by their place on the keyboard (`KeyboardEvent.code`) rather than their symbol,
  so that WASD is where it is on any layout.
- **The sound is played by an AudioWorklet.** It resamples the 24 kHz frames to the device's
  rate itself, and runs up to half a percent fast or slow to keep its queue near its target, as
  the desktop application's stream does.
- `?rom=URL` fetches a cartridge, or a boot ROM, and takes it as if it were opened.

## Consequences

- The page needs a server, even a local one: browsers do not run modules or worklets from
  `file:`.
- Files kept in one browser are not in another; the page's Forget buttons delete them.
- The page has no touch controls; a phone shows the game, but plays it only with a gamepad or a
  keyboard.
