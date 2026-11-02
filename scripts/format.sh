#!/usr/bin/env bash
# Formats every source file in place. Pass --check to fail instead of rewriting.
# Kept free of bash-4 features: macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
fmt="$( "$root/scripts/find-llvm-tool.sh" clang-format )"

if [[ "${1:-}" == "--check" ]]; then
  set -- --dry-run --Werror
else
  set -- -i
fi

find "$root/src" "$root/tests" -type f \( -name '*.cpp' -o -name '*.hpp' \) \
  -exec "$fmt" "$@" {} +
