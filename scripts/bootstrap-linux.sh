#!/usr/bin/env bash
# Verifies the Linux development environment for mitsuba-anari.
# Dependencies (ANARI-SDK) are fetched by CMake (MITSUBA_ANARI_FETCH_DEPENDENCIES=ON).
set -u

fail=0

check() {
  local name="$1" cmd="$2" hint="$3"
  if command -v "$cmd" >/dev/null 2>&1; then
    echo "  [ok] $name: $(command -v "$cmd")"
  else
    echo "  [MISSING] $name - $hint" >&2
    fail=1
  fi
}

echo "mitsuba-anari Linux bootstrap check"
echo "-----------------------------------"
check "CMake" cmake "install cmake >= 3.24 (apt: cmake, or cmake.org)"
check "C++ compiler" g++ "apt install build-essential (or use clang++)"
check "Git" git "apt install git"
check "Python 3" python3 "apt install python3 python3-venv"

if command -v cmake >/dev/null 2>&1; then
  ver="$(cmake --version | head -n1 | sed 's/cmake version //')"
  major="${ver%%.*}"; rest="${ver#*.}"; minor="${rest%%.*}"
  if [ "$major" -lt 3 ] || { [ "$major" -eq 3 ] && [ "$minor" -lt 24 ]; }; then
    echo "  [MISSING] CMake $ver too old; >= 3.24 required" >&2
    fail=1
  fi
fi

if [ "$fail" -ne 0 ]; then
  echo "Bootstrap FAILED" >&2
  exit 1
fi

echo ""
echo "Bootstrap OK. Next steps:"
echo "  cmake --preset linux-gcc-debug"
echo "  cmake --build --preset linux-gcc-debug"
echo "  ctest --preset linux-gcc-debug --output-on-failure"
