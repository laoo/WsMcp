# Where the emulated machine departs from Mesen2

Mesen2 decides how the machine behaves unless a hardware test ROM says otherwise
([0002](../decisions/0002-references.md)). These are the places the port knowingly differs from
Mesen2 at `b9fa69ddc6d0a331fb103fdb5eef6904305703c2`.

## The prefetch's second byte

`WsCpuPrefetch::Prefetch` masks the address of the second byte of a word fetch after reading it
(`InternalRead((_fetchCs << 4) + _fetchIp) & 0xFFFFF`), so a fetch across the top of the address
space indexes its page table past its end. The port masks the address before reading, wrapping to
the bottom as the 20-bit bus does.

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
