# Documentation map

This file is the index of every document in the project. **A document that is
not listed here must not exist.** If a new piece of writing cannot be classified
into one of the buckets below, that is the signal it should not be written. The
regime is PetMcp's, adopted whole: [0001](decisions/0001-documentation-regime.md).

## The rule

> **Documentation carries *why* and *contracts*. Code carries *how*.**

If a sentence can be read off a function signature, it does not belong in a
document. Non-obvious *reasoning* inside the code belongs in code comments, not
here.

Two supporting rules:

1. **One fact, one place.** Every other place links to it instead of repeating it.
2. **No status files.** No `PROGRESS.md`, `TODO.md`, `CHANGES.md`, session notes
   or similar. Git history covers that ground.

Documentation is updated in the **same commit** as the change that invalidated
it, never "at the end of the session".

## Buckets

| Location | Answers | Must not contain |
|---|---|---|
| `/README.md` | What WsMcp is, how to build it, how to run it | architecture, internal APIs |
| `/THIRD_PARTY.md` | What WsMcp is derived from and built with, and the terms that work asks to be carried | anything of ours beyond the credits |
| `/CLAUDE.md` | How to work in this repository: commands, conventions, where things live | domain knowledge; rules and pointers only |
| `docs/architecture.md` | What the components are, how time and data flow between them, **why** the boundaries sit where they do | class or function listings, signatures |
| `docs/decisions/NNNN-*.md` | Why a decision was made, and what it costs. Immutable once merged; a reversal is a new record that supersedes the old one | present-tense state of the system |
| `docs/spec/*.md` | External contracts: the control protocol, batch scripts | implementation detail |
| `docs/hardware/*.md` | Where the emulator knowingly departs from Mesen2 or the hardware, with the evidence | anything Mesen2 or the WSdev wiki already says; link to them |
| `docs/plans/*.md` | The order decided-but-unbuilt work is built in, one entry per session-sized task, each removed in the commit that lands it. A queue that only shrinks | status, progress, what was done, any decision the records do not hold |

## Index

### Overview
- [Architecture](architecture.md): the core, the control API, the frontends, and how emulated time is kept

### Specifications
- [The control protocol](spec/control-protocol.md): transports, requests, responses, error codes, transcripts, and every method
- [Batch scripts](spec/batch.md): runs written down, the checkpoints they sum, and the regression suite made of them

### Decision records
- [0001: Documentation regime](decisions/0001-documentation-regime.md)
- [0002: What decides how the machine behaves](decisions/0002-references.md)
- [0003: Licence is GPL-3.0](decisions/0003-licence-gpl-3.md)
- [0004: One control API](decisions/0004-one-control-api.md)
- [0005: Ported code is rewritten, tooling is fetched](decisions/0005-dependencies.md)
- [0006: Code style and toolchain are PetMcp's](decisions/0006-code-style-is-petmcps.md)
- [0007: The machine's parts are Mesen2's, and System stands for its console](decisions/0007-the-parts-are-mesen2s.md)
- [0008: The machine is stepped by the CPU's bus cycles](decisions/0008-stepped-by-the-cpus-cycles.md)
- [0009: The boot ROMs are the owner's dumps; without one the cartridge starts where the IPL leaves it](decisions/0009-boot-roms-are-the-owners.md)
- [0010: The core compiles to WebAssembly](decisions/0010-the-core-compiles-to-webassembly.md)
- [0011: MCP over HTTP is served by cpp-httplib, JSON-lines over TCP by sockets of our own](decisions/0011-network-transports.md)
- [0012: CI builds three platforms when asked, and a tag makes a release](decisions/0012-ci-runs-when-asked.md)

### Hardware
- [Departures](hardware/departures.md): where the emulated machine departs from Mesen2, and why

### Plans
- [The queue](plans/queue.md): what is decided and not built yet, in the order it is built
