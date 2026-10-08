#!/usr/bin/env bash
# Run the mitsuba-anari test suite on macOS.
# Usage: scripts/test-macos.sh [preset] [ctest-label]
set -euo pipefail

preset="${1:-macos-clang-debug}"
label="${2:-}"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

args=(--preset "$preset" --output-on-failure)
if [ -n "$label" ]; then
  args+=(-L "$label")
fi
ctest "${args[@]}"
