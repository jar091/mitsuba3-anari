#!/usr/bin/env bash
# Environment for building/running mitsuba-anari on the IT4I Karolina GPU
# cluster (accelerated nodes: 8x NVIDIA A100-SXM4-40GB, AMD EPYC 7763 2x64c).
#
# Source it (do not execute) on an *allocated GPU node*, never on a login node:
#
#   source scripts/env-karolina.sh
#
# It loads the pinned module set, creates/activates the project venv (.venv),
# and exports the loader paths for the build tree. Everything is idempotent.

# --- modules ---------------------------------------------------------------
# Note: load them one per `ml` call. A single combined `ml A B C` invocation
# silently loads nothing on this Lmod deployment (8.7.37).
ml GCC/14.3.0
ml CUDA/13.0.0
ml CMake/4.0.3-GCCcore-14.3.0
# PyNARI is a pybind11 extension and needs Python development headers; the
# system pythons on the compute nodes ship none (only /usr/include/python3.6m).
# 3.13.5 is the GCCcore/14.3.0-matched build, so it mixes with the toolchain
# above without pulling a second GCCcore.
ml Python/3.13.5-GCCcore-14.3.0

# --- repo root -------------------------------------------------------------
MITSUBA_ANARI_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export MITSUBA_ANARI_ROOT
export MITSUBA_ANARI_PRESET="${MITSUBA_ANARI_PRESET:-linux-gcc-release}"
export MITSUBA_ANARI_BUILD="${MITSUBA_ANARI_ROOT}/build/${MITSUBA_ANARI_PRESET}"

# --- project venv ----------------------------------------------------------
# Nothing is ever installed into the module's site-packages (read-only anyway).
if [ ! -x "${MITSUBA_ANARI_ROOT}/.venv/bin/python" ]; then
  echo "mitsuba-anari: creating ${MITSUBA_ANARI_ROOT}/.venv (python $(python3 --version))"
  python3 -m venv "${MITSUBA_ANARI_ROOT}/.venv"
  "${MITSUBA_ANARI_ROOT}/.venv/bin/python" -m pip install --quiet --upgrade pip
  # pybind11 is pinned to the 2.x line: the documented PyNARI patch targets
  # 2.13's overload resolution, and PyNARI's CMake predates pybind11 3.x.
  "${MITSUBA_ANARI_ROOT}/.venv/bin/python" -m pip install --quiet numpy pillow pytest 'pybind11<3'
fi
# shellcheck disable=SC1091
source "${MITSUBA_ANARI_ROOT}/.venv/bin/activate"

# --- runtime loader paths (build tree) -------------------------------------
# The provisioned prefixes use GNUInstallDirs, which is lib64 on this RHEL-8
# based cluster and lib elsewhere — add whichever exists.
_ma_libdirs="${MITSUBA_ANARI_BUILD}/src"
for _ma_prefix in "${MITSUBA_ANARI_BUILD}/_deps/anari-sdk-install" \
                  "${MITSUBA_ANARI_BUILD}/_deps/mitsuba-install"; do
  for _ma_sub in lib64 lib; do
    [ -d "${_ma_prefix}/${_ma_sub}" ] && _ma_libdirs="${_ma_libdirs}:${_ma_prefix}/${_ma_sub}" && break
  done
done
unset _ma_prefix _ma_sub
export LD_LIBRARY_PATH="${_ma_libdirs}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset _ma_libdirs
export PYTHONPATH="${MITSUBA_ANARI_BUILD}/_deps/pynari-build${PYTHONPATH:+:$PYTHONPATH}"

# Dr.Jit writes its kernel cache to $HOME by default; the shared scratch of a
# batch job is a better place and keeps repeat runs warm.
export DRJIT_CACHE_DIR="${DRJIT_CACHE_DIR:-${MITSUBA_ANARI_ROOT}/.drjit-cache}"

echo "mitsuba-anari: env ready on $(hostname -s)"
echo "  preset : ${MITSUBA_ANARI_PRESET}"
echo "  build  : ${MITSUBA_ANARI_BUILD}"
echo "  python : $(python --version 2>&1) ($(command -v python))"
