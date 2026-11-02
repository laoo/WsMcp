# Credits and third-party licences

WsMcp is Copyright (C) 2026 Waldemar Pawlaszek, and is released under the GNU General Public
License, version 3 only ([LICENSE](LICENSE)). This file names the work it is derived from and the
code it is built with, and carries the terms that work asks to be carried. The binary packages
hold this file and the licence of every library linked into them, in `licenses/`.

## The WonderSwan: Mesen2

The emulated machine is ported from **Mesen2**, https://github.com/SourMesen/Mesen2, by Sour
(Copyright (C) 2014-2026 Sour and its contributors), released under the GNU General Public
License, version 3. Every ported file names the Mesen2 file it ports and the commit it was read
at, `b9fa69ddc6d0a331fb103fdb5eef6904305703c2`, in its header.

The control protocol, the transports, the batch scripts and the build are the owner's PetMcp's.

## Libraries

Fetched when the build is configured ([cmake/Dependencies.cmake](cmake/Dependencies.cmake)), and
linked into the programs:

| Library | Copyright | Licence |
|---|---|---|
| [SDL 3](https://github.com/libsdl-org/SDL) | Sam Lantinga | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | Omar Cornut | MIT |
| [spdlog](https://github.com/gabime/spdlog), with its bundled [{fmt}](https://github.com/fmtlib/fmt) | Gabi Melman and contributors; Victor Zverovich | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | Niels Lohmann | MIT |
| [miniz](https://github.com/richgel999/miniz) | RAD Game Tools and Valve Software, Rich Geldreich | MIT |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | yhirose | MIT |
| [CLI11](https://github.com/CLIUtils/CLI11) | University of Cincinnati | BSD-3-Clause |

Used by the test suite alone, and not distributed: [Catch2](https://github.com/catchorg/Catch2)
(Boost Software License 1.0), and Fredrik Ahlström's (FluBBaOfWard) and Robert Peip's
[WSCpuTest](https://github.com/FluBBaOfWard/WSCpuTest),
[WSHWTest](https://github.com/FluBBaOfWard/WSHWTest) and
[WSTimingTest](https://github.com/FluBBaOfWard/WSTimingTest), and Adrian Siekierka's (asie)
[ws-test-suite](https://github.com/asiekierka/ws-test-suite) (MIT), built with his
[Wonderful toolchain](https://wonderful.asie.pl), all fetched, assembled or built by
`scripts/fetch-test-roms.sh`.

## What is not here

No ROM of any kind is part of WsMcp. The boot ROMs are Bandai's; the emulator reads them from a
directory the user names. WonderSwan, WonderSwan Color and SwanCrystal are Bandai's trademarks;
WsMcp is not affiliated with Bandai, nor with Mesen2.
