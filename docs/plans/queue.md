# The queue

What is decided and not built yet, in the order it is built. Each entry is removed in the commit
that lands it.

1. **The cartridge's RTC and flash**: the Bandai 2003's real-time clock (Seiko S-3511A) and the
   WonderWitch's flash, which Mesen2 has not ported yet. ws-test-suite's `rtc/mapper`, which
   the [test-roms] tests leave out until then, is the reference for the RTC's commands.
