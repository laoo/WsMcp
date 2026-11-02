# 0008: The machine is stepped by the CPU's bus cycles

**Status:** accepted

## Context

The WonderSwan runs from one clock: the V30MZ's 3.072 MHz, at which the PPU draws a dot (256 a
line, 159 lines a frame) and the APU is clocked. Mesen2 drives everything from the CPU: every bus
cycle the CPU makes, a memory or port access, an idle cycle or a prefetch, calls
`WsMemoryManager::Exec`, which counts the cycle, steps the PPU a dot and the APU a clock.

## Decision

- The machine is stepped as Mesen2 steps it: the CPU runs an instruction, and every cycle of it
  steps the other parts in Mesen2's order (`MemoryManager::exec`). Time is the CPU's cycle count
  since power-up.
- A run stops only at an instruction boundary, or after a cycle a halted CPU spends waiting.
  Mesen2's `Exec` is split in two (`Cpu::prepareInstruction`, `Cpu::execOpCode`) so that a
  breakpoint stops where Mesen2's debugger does: after the interrupt the CPU takes, before the
  instruction.
- A frame boundary is where the PPU hands a finished picture over.

## Consequences

- There is one clock and no catch-up, as in PetMcp.
- A REP string instruction runs an iteration per `execOpCode`, as in Mesen2, so interrupts are
  taken between iterations; the history and breakpoints see it once, at its first iteration.
