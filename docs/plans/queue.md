# The queue

What is decided and not built yet, in the order it is built. Each entry is removed in the commit
that lands it.

1. **The hardware test ROMs.** Build asiekierka's ws-test-suite and FluBBaOfWard's WSCPUTest,
   WSTimingTest and WSHWTest (Wonderful toolchain), run them headless, and make their results
   batch scripts of the regression suite; every failure becomes an entry in
   [hardware/departures.md](../hardware/departures.md) or a fix.
2. **The desktop application**, `wsmcp`: PetMcp's SDL3 + Dear ImGui frontend, its emulation
   thread, pacing at 75.47 Hz, sound through an SDL stream, the buttons from the keyboard and
   gamepads, the screen rotated for upright cartridges, and the debugger's windows (CPU, history,
   memory, I/O) asking through the protocol; `--server tcp:PORT` and `--mcp-http PORT` on it.
3. **The WebAssembly page**: a page in `web/` on `ws_web`, its canvas, Web Audio and keys, and a
   file input for the cartridge and the boot ROM.
4. **The cartridge's RTC and flash**: the Bandai 2003's real-time clock (Seiko S-3511A) and the
   WonderWitch's flash, which Mesen2 has not ported yet; decided when Mesen2 or a test ROM gives
   a reference.
