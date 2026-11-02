#!/usr/bin/env bash
# Puts the hardware test ROMs the tests tagged [test-roms] run into
# build/tools/testroms/, which those tests read and skip without.
#
#   scripts/fetch-test-roms.sh
#
# FluBBaOfWard's WSCpuTest is fetched as its release; his WSHWTest and
# WSTimingTest are assembled with NASM (brew install nasm) from fixed commits,
# so that a result can be reproduced and the listings name where their
# routines are. Downloads are checked against their SHA-256. Kept free of
# bash-4 features: macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
target="$root/build/tools/testroms"
mkdir -p "$target"

command -v nasm >/dev/null || { echo "fetch-test-roms.sh: NASM is needed (brew install nasm)" >&2; exit 1; }

# The SHA-256 of `file`, by whichever tool the platform has.
sha256() {
  if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# A release asset, unpacked into a directory of its own name.
release() {
  local repo="$1" tag="$2" asset="$3" sum="$4"
  local dir="$target/${asset%.zip}"
  [[ -d "$dir" ]] && return
  curl -sSL --fail -o "$target/$asset.part" "https://github.com/$repo/releases/download/$tag/$asset"
  if [[ "$( sha256 "$target/$asset.part" )" != "$sum" ]]; then
    echo "fetch-test-roms.sh: $asset is not the file it was pinned as" >&2
    exit 1
  fi
  mkdir -p "$dir.part"
  unzip -q "$target/$asset.part" -d "$dir.part"
  rm "$target/$asset.part"
  mv "$dir.part" "$dir"
}

# A program assembled from its source at a commit, with its listing.
assembled() {
  local repo="$1" commit="$2" source="$3" output="$4"
  local dir="$target/src/${repo#*/}"
  if [[ ! -d "$dir/.git" ]]; then
    git clone -q "https://github.com/$repo.git" "$dir"
  fi
  git -C "$dir" -c advice.detachedHead=false checkout -q "$commit"
  ( cd "$dir" && nasm -f bin -o "$output" "$source" -l "${output%.*}.lst" -w-all )
}

release FluBBaOfWard/WSCPUTest v0.7.1 WSCpuTest.zip d15b36ac5a36c8a44c458e5e262a41585751494f67b3c42c223fb81effeb06a4
assembled FluBBaOfWard/WSHWTest 34e33d5bfaaabb8b09fa5a557e409efe9fbfd979 WSHWTest.asm WSHWTest.wsc
assembled FluBBaOfWard/WSTimingTest 025e606fc3b6615160ffdae485d992ffcad0c024 timingtest.asm timingtest.ws

echo "test ROMs in $target"
