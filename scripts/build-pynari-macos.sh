#!/usr/bin/env bash
# Clone and build PyNARI (pinned commit) against this project's provisioned
# ANARI-SDK, for use by tests/pynari and examples/pynari — macOS counterpart
# of scripts/build-pynari-windows.ps1.
#
# - Clones jar091/pynari (fork of ingowald/pynari) and checks out the pinned commit from
#   DEPENDENCIES.md.
# - The vendored pybind11 (2.12.0.dev1) does not support Python >= 3.13, so
#   the build uses PYNARI_USE_INSTALLED_PYBIND=ON with a pip-installed
#   pybind11 (from the project's .venv — nothing is installed into the user
#   or system site-packages).
# - Applies the documented upstream fix (see PYNARI.md):
#   patches/pynari-pybind213-int-dispatch.patch.
#
# Usage: scripts/build-pynari-macos.sh [build-dir] [python]
#   build-dir: project build dir holding the provisioned SDK
#              (default: build/macos-clang-release)
#   python:    interpreter to build against; default: <repo>/.venv/bin/python
#              (created with `python3.13 -m venv .venv` if missing)
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${1:-build/macos-clang-release}"
pynari_commit='a860d0b68d08096f552d158265a90d040bd4498d'
pynari_src="$repo_root/$build_dir/_deps/pynari-src"
pynari_build="$repo_root/$build_dir/_deps/pynari-build"
anari_cmake_root="$repo_root/$build_dir/_deps/anari-sdk-install/lib/cmake"

python="${2:-$repo_root/.venv/bin/python}"
if [ ! -x "$python" ]; then
  echo "Creating project venv at $repo_root/.venv (python3.13)"
  python3.13 -m venv "$repo_root/.venv"
  python="$repo_root/.venv/bin/python"
fi

anari_config="$(find "$anari_cmake_root" -name anariConfig.cmake -print -quit 2>/dev/null || true)"
if [ -z "$anari_config" ]; then
  echo "ERROR: Provisioned ANARI-SDK not found under $anari_cmake_root." >&2
  echo "       Configure the project first (cmake --preset macos-clang-release)." >&2
  exit 1
fi
anari_cmake_dir="$(dirname "$anari_config")"

if [ ! -f "$pynari_src/CMakeLists.txt" ]; then
  git clone https://github.com/jar091/pynari "$pynari_src"
fi
git -C "$pynari_src" checkout "$pynari_commit"
git -C "$pynari_src" submodule update --init --recursive

# Idempotent patch application via --reverse --check (as on Windows).
patch="$repo_root/patches/pynari-pybind213-int-dispatch.patch"
if ! git -C "$pynari_src" apply --reverse --check "$patch" 2>/dev/null; then
  git -C "$pynari_src" apply "$patch"
fi

"$python" -m pip install --quiet pybind11 numpy pytest
pybind_dir="$("$python" -m pybind11 --cmakedir)"

cmake -S "$pynari_src" -B "$pynari_build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  "-Danari_DIR=$anari_cmake_dir" \
  -DPYNARI_USE_INSTALLED_PYBIND=ON \
  "-Dpybind11_DIR=$pybind_dir" \
  -DPYBIND11_FINDPYTHON=ON \
  "-DPython_EXECUTABLE=$python" \
  "-DPYTHON_EXECUTABLE=$python"
cmake --build "$pynari_build" --parallel

module="$(find "$pynari_build" -name "pynari*.so" -print -quit)"
if [ -z "$module" ]; then
  echo "ERROR: pynari module not found after build" >&2
  exit 1
fi
module_dir="$(dirname "$module")"
echo ""
echo "PyNARI module built: $module"
echo "Add its directory to PYTHONPATH, e.g.:"
echo "  export PYTHONPATH=$module_dir"
echo "and configure the project with:"
echo "  -DMITSUBA_ANARI_BUILD_PYNARI_TESTS=ON \\"
echo "  -DMITSUBA_ANARI_PYNARI_PYTHONPATH=$module_dir \\"
echo "  -DPython3_EXECUTABLE=$python"
