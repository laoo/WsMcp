# 0009: The boot ROMs are the owner's dumps; without one the cartridge starts where the IPL leaves it

**Status:** accepted

## Context

Each of the three machines has an IPL in its SoC: 4 KB on the WonderSwan, 8 KB on the Color and
the SwanCrystal. It draws the splash, keeps the owner's name in the internal EEPROM and checks the
cartridge, then switches itself out (port $A0) and jumps to the cartridge. The dumps are
copyrighted and are not part of the repository. The owner has all three.

## Decision

- The boot ROMs are read from a directory given at start (`--bios-dir`), under Mesen2's names
  (`bootrom.ws`, `bootrom.wsc`, `bootrom_sc.wsc`) or No-Intro's, and only at their sizes.
- Where there is none, or `--no-boot-rom` is given, the machine starts in the state Mesen2's
  `InitPostBootRomState` sets up: the registers, the ports and the internal EEPROM as the IPL
  leaves them.
- The tests that need the boot ROMs or the cartridges read them from the owner's
  `~/Documents/WS` and skip without them.

## Consequences

- CI runs none of the tests that need a ROM.
- Without the IPL, the splash, the owner's name prompt and the IPL's own timing are missing; a
  cartridge that relies on them is to be run with the boot ROM.
