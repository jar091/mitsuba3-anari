#!/usr/bin/env bash
# Environment for building/running mitsuba-anari on the IT4I Complementary
# Systems partition p12 ("Turin" node p12-turin01: AMD EPYC 9555 64c,
# 2x NVIDIA RTX PRO 6000 Blackwell Server Edition 96GB, Rocky Linux 10).
#
# Source it (do not execute):
#
#   source scripts/env-cs-p12.sh
#
# It loads the CUDA module, creates/activates the project venv (.venv), and
# exports the loader paths for the build tree. Everything is idempotent, and
# nothing is ever written to $HOME (pip cache and the Dr.Jit kernel cache are
# both redirected into the repo).

# --- modules ---------------------------------------------------------------
# The p12 module tree is minimal: no CMake or Python modules exist. CUDA is
# the only module we take; note its nvcc has mode rwxr-xr-- (not executable
# for "others" — an easybuild packaging slip). That is harmless here: the
# project has no build-time CUDA dependency (Dr.Jit talks to the driver and
# OptiX at runtime), the module is loaded for its toolkit libraries.
# One module per `ml` call, as on Karolina.
if command -v ml >/dev/null 2>&1 || type module >/dev/null 2>&1; then
  ml CUDA/12.9.1
fi

# Compiler and Python come from the OS: Rocky 10 ships GCC 14.3.1 and
# python3.12 *with* development headers (/usr/include/python3.12/Python.h),
# so no module is needed for either.

# --- repo root -------------------------------------------------------------
MITSUBA_ANARI_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export MITSUBA_ANARI_ROOT
export MITSUBA_ANARI_PRESET="${MITSUBA_ANARI_PRESET:-linux-gcc-release}"
export MITSUBA_ANARI_BUILD="${MITSUBA_ANARI_ROOT}/build/${MITSUBA_ANARI_PRESET}"

# --- keep $HOME clean ------------------------------------------------------
export PIP_CACHE_DIR="${MITSUBA_ANARI_ROOT}/.pip-cache"
export DRJIT_CACHE_DIR="${DRJIT_CACHE_DIR:-${MITSUBA_ANARI_ROOT}/.drjit-cache}"

# --- project venv ----------------------------------------------------------
# Created from the system python3.12. CMake has no module and no system
# package on p12, so it is pip-installed *into the venv* (repo-local, not
# $HOME). pybind11 is pinned to 2.x as documented in PYNARI.md.
if [ ! -x "${MITSUBA_ANARI_ROOT}/.venv/bin/python" ]; then
  echo "mitsuba-anari: creating ${MITSUBA_ANARI_ROOT}/.venv (python $(python3 --version))"
  python3 -m venv "${MITSUBA_ANARI_ROOT}/.venv"
fi
"${MITSUBA_ANARI_ROOT}/.venv/bin/python" -m pip install --quiet --upgrade pip
"${MITSUBA_ANARI_ROOT}/.venv/bin/python" -m pip install --quiet \
  numpy pillow pytest 'pybind11<3' cmake
# shellcheck disable=SC1091
source "${MITSUBA_ANARI_ROOT}/.venv/bin/activate"

# --- LLVM for the llvm_ad_rgb variant --------------------------------------
# Dr.Jit dlopens libLLVM; point it at the distro build (LLVM 21).
if [ -e /usr/lib64/libLLVM.so.21.1 ]; then
  export DRJIT_LIBLLVM_PATH="${DRJIT_LIBLLVM_PATH:-/usr/lib64/libLLVM.so.21.1}"
fi

# --- runtime loader paths (build tree) -------------------------------------
# The provisioned prefixes use GNUInstallDirs => lib64 on RHEL-family distros.
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

echo "mitsuba-anari: env ready on $(hostname -s)"
echo "  preset : ${MITSUBA_ANARI_PRESET}"
echo "  build  : ${MITSUBA_ANARI_BUILD}"
echo "  python : $(python --version 2>&1) ($(command -v python))"
echo "  cmake  : $(cmake --version 2>/dev/null | head -1 || echo missing)"
