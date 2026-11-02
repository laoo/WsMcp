# 0003: Licence is GPL-3.0

**Status:** accepted

## Context

The machine is ported from Mesen2 ([0002](0002-references.md)), which is licensed under the GNU
General Public License, version 3. A port of its code is a work based on it.

## Decision

WsMcp is released under the GNU General Public License, version 3 only (`LICENSE`).
`THIRD_PARTY.md` credits Mesen2 and lists the libraries the programs are built with, with their
terms.

## Consequences

- Code cannot move from WsMcp into the owner's projects that are GPL-2.0-only. Code may move
  from them into WsMcp, as the infrastructure did.
- Every library linked must be compatible with GPL-3.0; the ones fetched are MIT, BSD, zlib,
  Boost and the like.
