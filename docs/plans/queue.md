# The queue

What is decided and not built yet, in the order it is built. Each entry is removed in the commit
that lands it.

1. **The desktop application**, `wsmcp`: PetMcp's SDL3 + Dear ImGui frontend, its emulation
   thread, pacing at 75.47 Hz, sound through an SDL stream, the buttons from the keyboard and
   gamepads, the screen rotated for upright cartridges, and the debugger's windows (CPU, history,
   memory, I/O) asking through the protocol; `--server tcp:PORT` and `--mcp-http PORT` on it.
2. **The WebAssembly page**: a page in `web/` on `ws_web`, its canvas, Web Audio and keys, and a
   file input for the cartridge and the boot ROM.
3. **The cartridge's RTC and flash**: the Bandai 2003's real-time clock (Seiko S-3511A) and the
   WonderWitch's flash, which Mesen2 has not ported yet. ws-test-suite's `rtc/mapper`, which
   the [test-roms] tests leave out until then, is the reference for the RTC's commands.
