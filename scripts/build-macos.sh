#!/usr/bin/env bash
# Configure and build mitsuba-anari on macOS.
# Usage: scripts/build-macos.sh [preset]   (default: macos-clang-debug)
set -euo pipefail

preset="${1:-macos-clang-debug}"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

cmake --preset "$preset"
cmake --build --preset "$preset"
