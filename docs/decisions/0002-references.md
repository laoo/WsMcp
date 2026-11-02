# 0002: What decides how the machine behaves

**Status:** accepted

## Context

Several sources describe the WonderSwan, the WonderSwan Color and the SwanCrystal, found through
the community's index, [awesome-wsdev](https://github.com/wonderfultoolchain/awesome-wsdev):

- **Mesen2** (`../Mesen2`, https://github.com/SourMesen/Mesen2), the most accurate emulator of
  the three machines, recommended by the homebrew community for development. Its V30MZ counts
  cycles, prefetch included, and its PPU draws a dot a cycle.
- **The hardware test ROMs**: asiekierka's ws-test-suite, FluBBaOfWard's WSCPUTest,
  WSTimingTest and WSHWTest, the MiSTer core's test ROMs. They were made on, and pass on, the
  hardware.
- **The documentation**: the WSdev wiki (https://ws.nesdev.org/wiki), STSWS, WSMan, NEC's V30MZ
  manual.
- ares and Mednafen, other emulators.

## Decision

- **A hardware test ROM decides**, where one tests the behaviour.
- **Otherwise Mesen2 decides**, at the commit its port names
  (`b9fa69ddc6d0a331fb103fdb5eef6904305703c2`, 2026-06-04): the V30MZ, the memory map and the
  ports, the PPU, the APU, the DMA, the timers, the EEPROMs and the post-IPL state. Its
  `Core/WS` is ported part for part ([0007](0007-the-parts-are-mesen2s.md)).
- Where the emulator knowingly departs from Mesen2, `docs/hardware/` says so, with the evidence.
- `../Mesen2` is a read-only reference, never edited from here.

## Consequences

- A difference from Mesen2 is a bug in the emulator unless a test ROM or a record says
  otherwise.
- A fix Mesen2 makes upstream is ported by reading its diff, and the port's header names the new
  commit.
