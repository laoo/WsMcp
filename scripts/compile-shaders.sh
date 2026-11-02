#!/usr/bin/env bash
# Compiles the desktop application's screen shaders, src/ws_app/shaders/*.vert
# and *.frag, into the bytecode and source SDL_GPU takes on each platform:
# SPIR-V for Vulkan, MSL for Metal (docs/decisions/0013-the-desktop-application.md).
# The results go into src/ws_app/shaders/compiled/ as C array initialisers,
# which are committed: building the application needs neither tool.
#
#   scripts/compile-shaders.sh
#
# Needs glslangValidator and spirv-cross (brew install glslang spirv-cross).
# Kept free of bash-4 features: macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
shaders="$root/src/ws_app/shaders"
out="$shaders/compiled"
work="$( mktemp -d )"
trap 'rm -rf "$work"' EXIT

for tool in glslangValidator spirv-cross; do
  command -v "$tool" > /dev/null || { echo "error: $tool is not on PATH" >&2; exit 1; }
done

# Bytes as a C array initialiser, sixteen to a line.
as_array() {
  od -An -v -tx1 "$1" | awk '{ for ( i = 1; i <= NF; ++i ) printf "0x%s,%s", $i, ( ++n % 16 == 0 ? "\n" : "" ) }
                              END { if ( n % 16 != 0 ) printf "\n" }'
}

mkdir -p "$out"
for source in "$shaders"/*.vert "$shaders"/*.frag; do
  name="$( basename "$source" )"
  stem="${name//./_}"
  glslangValidator -V --quiet -I"$shaders" -o "$work/$stem.spv" "$source"
  spirv-cross --msl --msl-version 20000 --msl-decoration-binding --output "$work/$stem.metal" "$work/$stem.spv"
  as_array "$work/$stem.spv" > "$out/$stem.spv.inc"
  as_array "$work/$stem.metal" > "$out/$stem.msl.inc"
  echo "$name"
done
