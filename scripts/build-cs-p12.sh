#!/usr/bin/env bash
# Configure + build mitsuba-anari on the IT4I Complementary Systems p12 node
# (p12-turin01). Unlike Karolina, p12 is a single directly-accessible node,
# so no Slurm allocation is required.
#
#   ./scripts/build-cs-p12.sh [preset]          # default: linux-gcc-release
#
# The first run provisions the ANARI-SDK and Mitsuba (variants
# scalar_rgb,llvm_ad_rgb,cuda_ad_rgb) inside build/<preset>/_deps — expect
# ~1 h on 64 cores. Re-running resumes incrementally, so an interrupted
# build is simply restarted.
set -euo pipefail

preset="${1:-linux-gcc-release}"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

case "$(hostname -s)" in
  p12-*) ;;
  *) echo "warning: $(hostname -s) does not look like a CS p12 node" >&2 ;;
esac

export MITSUBA_ANARI_PRESET="$preset"
# shellcheck disable=SC1091
source "$repo_root/scripts/env-cs-p12.sh"

log="$repo_root/build-${preset}.log"
echo "mitsuba-anari: logging to $log"

{
  echo "=== configure $(date -Is) on $(hostname -s) ==="
  # Rocky 10's GCC defaults to -march=x86-64-v3 (AVX2 baseline), which breaks
  # Embree's lowest-ISA link inside the Mitsuba superbuild (BVHN<4> is never
  # instantiated). Pin the superbuild base to x86-64-v2; Embree's own
  # per-target flags still raise AVX/AVX2/AVX-512 where intended.
  cmake --preset "$preset" \
    -DPython3_EXECUTABLE="$repo_root/.venv/bin/python" \
    -DMITSUBA_ANARI_MI_EXTRA_CXX_FLAGS=-march=x86-64-v2
  echo "=== build $(date -Is) ==="
  cmake --build --preset "$preset" --parallel "$(nproc)"
  echo "=== done $(date -Is) ==="
} 2>&1 | tee -a "$log"
