#!/usr/bin/env bash
# Locates an LLVM tool. Homebrew keeps llvm keg-only, so clang-tidy is not on
# PATH on macOS; CI and Linux have it on PATH. Usage: find-llvm-tool.sh clang-tidy
set -euo pipefail
tool="${1:?usage: find-llvm-tool.sh <tool-name>}"

# An unset LLVM_DIR must not leave "/bin/$tool" behind: that is /usr/bin on
# Linux, so the system LLVM would silently win over the one CI installs.
candidates="/opt/homebrew/opt/llvm/bin/$tool /usr/local/opt/llvm/bin/$tool"
if [[ -n "${LLVM_DIR:-}" ]]; then
  candidates="$LLVM_DIR/bin/$tool $candidates"
fi

for candidate in $candidates
do
  [[ -x "$candidate" ]] && { echo "$candidate"; exit 0; }
done

command -v "$tool" 2>/dev/null && exit 0

echo "error: $tool not found; try 'brew install llvm'" >&2
exit 1
