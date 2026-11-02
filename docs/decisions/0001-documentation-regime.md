# 0001: Documentation regime

**Status:** accepted

## Context

This project is written largely by AI agent sessions. Left alone, such sessions append to
whatever document looks plausible, restate signatures that rot on the first refactor, and leave
status files that duplicate git. The owner's PetMcp, which took the regime from PGMEmu and NGA,
solved this.

## Decision

PetMcp's regime is adopted whole, as written in
`../PetMcp/docs/decisions/0001-documentation-regime.md` and its `docs/README.md`:

- Every document answers exactly one question and has an explicit prohibition. The buckets are
  listed in [docs/README.md](../README.md), and a document must be indexed there before it may
  exist.
- Documentation carries *why* and *contracts*; code carries *how*.
- One fact, one place.
- No status files.
- Documentation changes in the same commit as the code that invalidated it.

`docs/hardware/` holds only where the emulator knowingly departs from its references
([0002](0002-references.md)), with the evidence.

## Consequences

- A plan is a queue in `docs/plans/` that only shrinks; it never says what is done.
- Facts about the WonderSwan are not copied from Mesen2 or the WSdev wiki into this
  repository's documents; documents link to them.
