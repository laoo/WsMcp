# 0016: The WonderWitch's flash follows its datasheet, and is held to AthenaOS's driver

**Status:** accepted

## Context

A WonderWitch cartridge has a Bandai 2003 mapper and, in place of a mask ROM, a Fujitsu
MBM29DL400TC flash chip of 512 KB. Its software writes the chip through the 2003's `$CE` window,
where the SRAM is otherwise.

Mesen2 has no flash. No hardware test ROM tests one, and the owner has no WonderWitch image. ares
emulates the chip, but its own notes call its reads in Fast Mode a guess, it leaves out the
timings, and only its game database says which cartridge has the chip.

AthenaOS, OpenWitch's open FreyaOS, is MIT-licensed and builds with the Wonderful toolchain. Its
BIOS has an MBM29DL400TC driver, used on the hardware:
- programs in Fast Mode, polling DQ6;
- erases a sector, polling DQ6 and DQ2;
- runs from RAM while the chip answers commands.

## Decision

- **The datasheet decides** ("MBM29DL400TC-55/-70/-90/-12"):
  - the command sequences of table 8, in the byte mode;
  - the top boot sectors, and the two banks;
  - the status flags of table 9;
  - the autoselect codes, $04 and $220C;
  - the typical times: 8 us a byte, 1 s a sector, and 18.2 s the whole chip, after the 50 us an
    erase waits for more sectors.
- Where the datasheet is silent, what a read gives in Fast Mode is ares's: the flags of a
  program that is done.
- Sector protection and Erase Suspend are not emulated.
- **A Bandai 2003 cartridge of exactly 512 KB has the chip.** A WonderWitch's header says no
  more than that. Dicing Knight and Judgement Silversword, games of WonderWitch origin, have the
  same header. A game on a mask ROM sends the chip no commands.
- **AthenaOS's driver holds it to the hardware.**
  - `tests/roms/flash` is a FreyaOS program. It writes a sector through the BIOS, erases it and
    writes it again, reading each back, and programs its verdict into the chip.
  - `scripts/fetch-test-roms.sh` builds it, and AthenaOS at a fixed commit, into a 512 KB
    cartridge. The [test-roms] tests run it.
- **The chip's contents are a save.**
  - Once the chip has been programmed or erased, they are written whole to the save directory,
    as `<cartridge>.flash`, and the next power-up takes them in place of the ROM.
  - A save state holds them.
  - The ROM's CRC stays the cartridge's as given.

## Consequences

- A program that times the chip sees the datasheet's typical times, not one chip's.
- A save state of a WonderWitch cartridge is 512 KB larger.
