# 0005: Ported code is rewritten, tooling is fetched

**Status:** accepted

## Context

The owner's emulators vendor the third-party code that decides emulated behaviour, such as a
CPU core, and fetch the rest. Here no third-party code decides emulated behaviour as it is:
Mesen2's is ported.

## Decision

- **Nothing is vendored.** Code ported from Mesen2 is rewritten as this project's C++, under its
  style, with the Mesen2 file and commit it ports named in its header.
- **Fetched** with `FetchContent`, each pinned to an exact tag, at the versions the owner's projects use: CLI11,
  spdlog, nlohmann/json, miniz, cpp-httplib, Catch2, and SDL3 and Dear ImGui for the desktop
  application.

## Consequences

- There is no `libextern/`.
- A clean configure needs the network once per build directory.
