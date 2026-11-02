# Where the emulated machine departs from Mesen2

Mesen2 decides how the machine behaves unless a hardware test ROM says otherwise
([0002](../decisions/0002-references.md)). These are the places the port knowingly differs from
Mesen2 at `b9fa69ddc6d0a331fb103fdb5eef6904305703c2`.

## The prefetch's second byte

`WsCpuPrefetch::Prefetch` masks the address of the second byte of a word fetch after reading it
(`InternalRead((_fetchCs << 4) + _fetchIp) & 0xFFFFF`), so a fetch across the top of the address
space indexes its page table past its end. The port masks the address before reading, wrapping to
the bottom as the 20-bit bus does.

## Channel 3's sweep is restarted by a write

Mesen2 lets a write to the sweep's time register ($8D) change the period and nothing else. On
the hardware it also restarts the count to the next step: WSHWTest's "Write resets Timer", which
writes the register once a line, sees the frequency never change. The port restarts both the
prescaler and the period's count on that write.

## The V30MZ's timing

FluBBaOfWard's WSTimingTest measures each of 407 instructions and forms on the hardware (a
WonderSwan Color), as scanlines per 1000 runs, about four a cycle. Its README allows a result to
differ by 1 either way, as the hardware's do from run to run. Where the port differs by more:

- **Fixed.** An effective address of two registers (`[BX+SI]`, `[BP+DI]`, with or without a
  displacement) costs two cycles, one more than Mesen2 takes, but in LEA, LDS, LES and POP r/m,
  which take one, as NEC's manual says. `POP r/m` of a register (`8F C0`) takes a cycle less than
  of memory. `BOUND` of a register (`62 C0` and the like) reads its bounds from memory, addressed
  as LEA addresses in that mode; Mesen2 read them from the register, and raised INT 5 where the
  hardware raises nothing.
- **Kept as Mesen2 has them.**
  - `IN AL, DX` and `IN AX, DX` read DX's port and take a cycle more than the hardware does
    (78 against 74). Mesen2 adds the cycle on purpose, for asiekierka's `gdma_timing` test, which
    is not run here yet.
  - A single-register address after an instruction that changed its register costs the hardware
    a cycle more ("Adr EA04" after `ADD SI, 2`: 66 against 62); the port does not track which
    registers the instruction before changed.
  - Taking an interrupt (289 against 293), the trap (285 against 261) and the shortest loop
    (27 against 31).

`tests/machine/TestRomsTest.cpp` holds the emulator to the hardware's values within 1, and to
the values above where it departs, so that any change shows.

## The colours

The picture is converted to RGBA as Mesen2's default video filter does with its frontend's
defaults: the LCD's colour response on, and no frame blending. Mesen2's core alone defaults to
the response off.

## What Mesen2's debugger and frontend did

The history of execution, breakpoints and watchpoints are this project's own, built on Mesen2's
hooks: a watchpoint sees the CPU's data accesses, the DMA's, and the APU's reads of its wave
tables, as Mesen2's debugger does; not the prefetch's fetches. The save states hold the APU's
queued samples and the PPU's row buffers, which Mesen2's do not, so that a loaded state sounds
and draws exactly as the machine it was saved from.
