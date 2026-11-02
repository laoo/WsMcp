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

## Channel 4's LFSR runs in noise mode alone

Mesen2 shifts the noise LFSR at each step of channel 4 whenever the channel is on. On the
hardware it stays still with the channel on and noise mode off: ws-test-suite's
`sound/quirks`, "LFSR tick on bit 3+7". The port shifts it only when both are on.

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
  - `IN AL, DX` and `IN AX, DX` read DX's port and take a cycle more than WSTimingTest finds
    (78 against 74). Mesen2 adds the cycle on purpose: ws-test-suite's `dma/gdma_timing`, also
    made on the hardware, passes with it and fails without, so the two tests disagree on where
    the cycle falls, and the one that tests what a program sees is kept.
  - A single-register address after an instruction that changed its register costs the hardware
    a cycle more ("Adr EA04" after `ADD SI, 2`: 66 against 62); the port does not track which
    registers the instruction before changed.
  - Taking an interrupt (289 against 293), the trap (285 against 261) and the shortest loop
    (27 against 31).

`tests/machine/TestRomsTest.cpp` holds the emulator to the hardware's values within 1, and to
the values above where it departs, so that any change shows.

## The Bandai 2003

Mesen2 ports the 2001's and the 2003's banks at `$C0`-`$C3` and nothing more of the 2003. The
port adds its RTC at `$CA`-`$CB`, its outputs at `$CC`-`$CD`, the ROM's window at `$CE` and the
banks' high bits at `$CF`-`$D5`, after the WSdev wiki
([0015](../decisions/0015-the-rtc-follows-the-wsdev-wiki.md)); ws-test-suite's `rtc/mapper`
passes. The S-3511A's interrupt is sampled when its ports are used and once a frame, so the
frequency mode, up to 32 kHz on the hardware, is seen at most once a frame.

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
