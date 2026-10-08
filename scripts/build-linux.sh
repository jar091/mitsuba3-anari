#!/usr/bin/env bash
# Configure and build mitsuba-anari on Linux.
# Usage: scripts/build-linux.sh [preset]   (default: linux-gcc-debug)
set -euo pipefail

preset="${1:-linux-gcc-debug}"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

cmake --preset "$preset"
cmake --build --preset "$preset"
