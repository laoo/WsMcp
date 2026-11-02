# 0006: Code style and toolchain are PetMcp's

**Status:** accepted

## Context

The owner holds code quality and formatting to one standard across projects: NGA's, carried by
PGMEmu and PetMcp.

## Decision

- `.clang-format`, `.clang-tidy`, `scripts/format.sh`, `scripts/tidy.sh`,
  `scripts/find-llvm-tool.sh`, the presets, `cmake/Warnings.cmake` and Catch2 v3 are PetMcp's.
- C++23, restricted to the subset Apple clang, GCC and MSVC all implement, and Emscripten's
  clang ([0010](0010-the-core-compiles-to-webassembly.md)).
  `src/ws_core/src/PortabilityChecks.cpp` pins that subset down.

## Consequences

- A name ported from Mesen2 keeps its words in this style: `WsPpu::ProcessEndOfScanline`
  becomes `Ppu::processEndOfScanline`, `_state.ScrollX` becomes `mState.scrollX`, so that a
  grep across the trees still meets.
