# Building mitsuba-anari

## Prerequisites

All platforms:

- CMake >= 3.24
- Git
- Python 3.x (build tooling and, later, PyNARI tests)
- Internet access on first configure (the pinned ANARI-SDK is fetched by CMake)
  — or a pre-installed ANARI-SDK (see "System dependency mode")
- The Mitsuba sources: the `ext/mitsuba3` git submodule with its nested
  submodules (`git clone --recurse-submodules`, or
  `git submodule update --init --recursive` in an existing clone)

Platform toolchains:

- **Windows (primary):** Windows 11, Visual Studio 2022 with
  "Desktop development with C++" (MSVC x64)
- **Linux:** GCC (or Clang), `build-essential`
- **macOS:** Xcode command-line tools (Apple Clang), Apple Silicon supported first

Run the bootstrap script for your platform to verify the environment:

```powershell
.\scripts\bootstrap-windows.ps1     # Windows
```

```bash
./scripts/bootstrap-linux.sh        # Linux
./scripts/bootstrap-macos.sh        # macOS
```

## Quick build (Windows, primary workflow)

```powershell
git clone --recurse-submodules https://github.com/jar091/mitsuba3-anari
cd mitsuba3-anari

.\scripts\bootstrap-windows.ps1

cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug

ctest --preset windows-msvc-debug --output-on-failure
```

Release builds: substitute `windows-msvc-release` (or `windows-msvc-relwithdebinfo`).

## Quick build (Linux / macOS)

```bash
cmake --preset linux-gcc-release        # or macos-clang-release
cmake --build --preset linux-gcc-release
ctest --preset linux-gcc-release --output-on-failure
```

### Linux notes

- The first configure clones and builds the ANARI-SDK and builds Mitsuba (variants
  `scalar_rgb,llvm_ad_rgb,cuda_ad_rgb`). Subsequent configures reuse them.
- `cuda_ad_rgb` needs a CUDA toolkit at build time and an NVIDIA driver at
  run time; without a GPU the device falls back to `scalar_rgb` with a
  warning, so the CPU-only path never requires CUDA.
- Mitsuba's Unix install layout puts `plugins/` under `bin/` and data under
  `share/`, while the runtime resolves both relative to the directory holding
  `libmitsuba.so` — the configure step colocates copies under `lib/`
  (see `cmake/Dependencies.cmake`), exactly as on macOS.

#### HPC clusters (IT4I Karolina, Complementary Systems p12)

Karolina's GPU partition is the reference Linux + CUDA environment. The
module set and build steps are captured in the helper scripts
(`scripts/env-karolina.sh`, `scripts/build-karolina.sh`,
`scripts/build-pynari-linux.sh`); measured CPU/GPU numbers are in
[PERFORMANCE.md](PERFORMANCE.md).

The Complementary Systems p12 node (Zen 5 + RTX PRO 6000 Blackwell) has its
own scripts, `scripts/env-cs-p12.sh` / `scripts/build-cs-p12.sh` — note its
mandatory `-march=x86-64-v2` workaround for the Rocky 10 compiler baseline.

### macOS notes (validated on Apple Silicon)

- The first configure clones and builds the ANARI-SDK and builds Mitsuba (variants
  `scalar_rgb,llvm_ad_rgb,metal_ad_rgb` — CUDA does not exist on macOS, the
  GPU path is Dr.Jit's Metal backend). Subsequent configures reuse them.
- `llvm_ad_rgb` loads `libLLVM.dylib` at runtime; a Homebrew LLVM
  (`brew install llvm`) is found automatically via
  `/opt/homebrew/Cellar/llvm/*/lib/libLLVM.dylib` (override with
  `DRJIT_LIBLLVM_PATH`).
- Mitsuba's upstream Unix install layout puts `plugins/` under `bin/` and
  data files under `share/`, while the runtime resolves both relative to the
  directory containing `libmitsuba.dylib` — the configure step automatically
  colocates copies under `lib/` (see `cmake/Dependencies.cmake`).
- Building on an **exFAT** volume: clang's `#pragma once` can misfire under
  heavy parallel builds there (transient "redefinition" errors caused by
  unstable synthesized inode numbers). Re-running the build resumes
  incrementally and succeeds; APFS volumes are unaffected.

## CMake options

| Option | Default | Meaning |
|--------|---------|---------|
| `MITSUBA_ANARI_BUILD_TESTS` | `ON` | Build unit/integration tests, register CTest |
| `MITSUBA_ANARI_BUILD_EXAMPLES` | `ON` | Build C++ examples |
| `MITSUBA_ANARI_BUILD_PYNARI_TESTS` | `OFF` | Enable PyNARI-based tests (needs Python + PyNARI; from Milestone 3) |
| `MITSUBA_ANARI_BUILD_CTS` | `OFF` | Enable ANARI CTS integration (from Milestone 8) |
| `MITSUBA_ANARI_FETCH_DEPENDENCIES` | `ON` | Build+install the pinned ANARI-SDK and Mitsuba into the build dir at configure time; `OFF` = use installed SDK and Mitsuba |
| `MITSUBA_ANARI_MITSUBA_SOURCE_DIR` | `ext/mitsuba3` | Mitsuba source tree to build from (the submodule by default) |
| `MITSUBA_ANARI_ENABLE_ASAN` | `OFF` | AddressSanitizer (GCC/Clang only) |
| `MITSUBA_ANARI_ENABLE_UBSAN` | `OFF` | UndefinedBehaviorSanitizer (GCC/Clang only) |
| `MITSUBA_ANARI_ENABLE_COVERAGE` | `OFF` | Coverage instrumentation (GCC/Clang only) |
| `MITSUBA_ANARI_WARNINGS_AS_ERRORS` | `OFF` | Promote project warnings to errors (never applied to third-party code) |

## System dependency mode

To build against existing installs instead of provisioning them:

```bash
cmake --preset linux-gcc-release \
  -DMITSUBA_ANARI_FETCH_DEPENDENCIES=OFF \
  -DCMAKE_PREFIX_PATH=/path/to/anari-sdk-install \
  -DMITSUBA_ANARI_MITSUBA_ROOT=/path/to/mitsuba-install
```

The SDK install must provide `find_package(anari CONFIG)` with the `anari::anari`
and `anari::helium` targets (ANARI-SDK v0.16.0); the Mitsuba install must match
the pinned version/variants (see `DEPENDENCIES.md`).

## Windows configuration note (from Milestone 3)

The Mitsuba backend is provisioned as a **Release** build. Mixing a Debug
device with Mitsuba's Release C++ ABI is unsafe with MSVC, so use
`windows-msvc-release` (or `windows-msvc-relwithdebinfo`) on Windows. The
first configure of a preset builds Mitsuba (~large; cached in the
build directory afterwards).

## Install

```bash
cmake --install build/<preset> --config <Config> --prefix <install-prefix>
```

Device library discovery: ANARI-SDK 0.16.0 has **no** `ANARI_LIBRARY_PATH`
environment variable (verified against the SDK loader source). The loader
resolves `anariLoadLibrary("mitsuba")` in this order:

1. OS default search — `PATH` on Windows, `LD_LIBRARY_PATH`/ld cache on
   Linux, `DYLD_LIBRARY_PATH` on macOS;
2. anchored search next to the `anari` runtime library itself (on Windows the
   loader also temporarily switches CWD there so transitive DLLs resolve).

Applications can also pin an explicit directory with the SDK's comma syntax:
`anariLoadLibrary("mitsuba,<install-prefix>/bin/")` (trailing separator
required). The recommended deployment is to install the device library into
the same directory as the ANARI runtime (`bin/` on Windows, `lib/` on
Linux/macOS).

See `TESTING.md` for the install-tree smoke test.

## Local presets

Never commit machine-specific paths into `CMakePresets.json`. Put local
overrides into `CMakeUserPresets.json` (gitignored).
