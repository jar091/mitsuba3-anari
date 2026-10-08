#!/usr/bin/env bash
# Configure + build mitsuba-anari on an allocated Karolina GPU node.
#
#   salloc -A <PROJECT> -p qgpu -N 1 -t 12:00:00
#   srun --jobid=<JOBID> --overlap --pty bash
#   ./scripts/build-karolina.sh [preset]        # default: linux-gcc-release
#
# The first run provisions the ANARI-SDK and Mitsuba (variants
# scalar_rgb,llvm_ad_rgb,cuda_ad_rgb) inside build/<preset>/_deps — expect
# ~1-2 h on 128 cores. Re-running resumes incrementally, so an interrupted
# build (job walltime) is simply restarted.
set -euo pipefail

preset="${1:-linux-gcc-release}"
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

if [ -z "${SLURM_JOB_ID:-}" ]; then
  echo "refusing to build on a login node: run inside a Slurm allocation" >&2
  exit 1
fi

export MITSUBA_ANARI_PRESET="$preset"
# shellcheck disable=SC1091
source "$repo_root/scripts/env-karolina.sh"

log="$repo_root/build-${preset}.log"
echo "mitsuba-anari: logging to $log"

{
  echo "=== configure $(date -Is) on $(hostname -s) ==="
  cmake --preset "$preset" -DPython3_EXECUTABLE="$repo_root/.venv/bin/python"
  echo "=== build $(date -Is) ==="
  cmake --build --preset "$preset" --parallel "$(nproc)"
  echo "=== done $(date -Is) ==="
} 2>&1 | tee -a "$log"
