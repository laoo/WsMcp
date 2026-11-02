# 0015: The 2003's RTC follows the WSdev wiki, and keeps the machine's time

**Status:** accepted

## Context

Some cartridges have the Bandai 2003 mapper, and a Seiko S-3511A real-time clock behind it on
ports `$CA` and `$CB`. Dokodemo Hamster 3 and Inu Yasha are among them. The 2003 also adds
general-purpose outputs, a window for its ROM where the SRAM is, and wider banks. Mesen2, which
decides how the machine behaves ([0002](0002-references.md)), ports none of it, at its commit or
upstream.

Three sources describe it:

- ws-test-suite's `rtc/mapper`, made on the hardware, holds the 2003's protocol: how many bytes
  each command moves, when Busy and Ready are set, an invalid command, and Ready cleared by a new
  one.
- The WSdev wiki's "Bandai 2003" and "Real-Time Clock" pages, written from those tests, describe
  the protocol, the S-3511A's registers, its power-on and reset states, and how it checks what is
  written to it.
- ares (ISC) emulates the 2003 and the S-3511A. It has no transfer time: a command is done when
  it is written, so `rtc/mapper` fails on it, and so does an invalid command.

## Decision

- **The test ROM decides, then the wiki, then ares.**
  - The 2003 moves a byte in 64 of the CPU's cycles: its 384 kHz serial clock, 8 bits.
  - The S-3511A's commands, checks, power-on state and reset are the wiki's.
  - Where the wiki is silent, ares is followed: the calendar's carries, the alarm's and the
    per-minute interrupts, and the high bits of the banks at `$D1`, `$D3` and `$D5`, which a
    write to `$C1`-`$C3` leaves alone.
- **Every 2003 cartridge has the clock.** The header names the mapper, not the clock, and a game
  without one does not touch `$CA`.
- **The clock keeps the machine's time**, a second for each 3,072,000 cycles, so that a run is
  deterministic.
  - It is brought up to date when its ports are used and once a frame. Its interrupt, the
    cartridge's, is sampled then.
  - Its registers are part of a save state.
- **Its battery is the save directory.** The clock is written beside the cartridge's save, as
  `<cartridge>.rtc`: its registers and the host's time. When the cartridge is next powered up,
  the clock runs on by the time that has passed since.
  - Without a save directory, or for a cartridge powered up for the first time, the clock is a
    new battery's: 2000-01-01 00:00:00, with its power-failure flag set, so the game asks for the
    time.
- The window at `$CE` maps the ROM where the SRAM is, to be read. Writing to a flash chip there
  is the WonderWitch's, still in the queue.

## Consequences

- The ROM-dependent [test-roms] tests run `rtc/mapper` again.
- A game that drives the clock's interrupt at its frequency, rather than per minute or by alarm,
  sees it sampled once a frame ([hardware/departures.md](../hardware/departures.md)).
- A cartridge's clock does not follow the host's clock while the machine runs; only while it is
  off.
