# mitsuba-anari

A [Khronos ANARI](https://registry.khronos.org/ANARI/) device implementation
backed by the [Mitsuba 3](https://github.com/mitsuba-renderer/mitsuba3)
renderer. The project provides a dynamically loadable ANARI device library
named **`mitsuba`**, usable from standard ANARI C/C++ applications and from
[PyNARI](https://github.com/jar091/pynari).

**Project status: Milestones 0–8 and 11 complete (validated on Windows and
macOS/Apple Silicon).** The device renders real Mitsuba images through the
ANARI C API and through PyNARI: triangle/quad/sphere geometry (normals, UVs,
vertex colors), matte and physicallyBased materials, image2D textures, four
light types, perspective/orthographic cameras, color + depth channels,
retained-mode scene updates with a translation cache and Mitsuba-native
instancing, and opt-in accelerated rendering (CUDA/OptiX on Windows/Linux,
LLVM JIT everywhere, Metal on Apple Silicon) via the
`ANARI_MITSUBA_DEVICE_VARIANT` extension. Remaining work: volumes and Linux
validation.

## Supported platforms

- Windows 11 + Visual Studio 2022 (MSVC x64) — primary development platform
- macOS (Apple Silicon, Apple Clang) — validated (see the macOS walkthrough
  below)
- Linux x86-64 (GCC/Clang) — presets and scripts provided; validation pending
  access to such a machine

The mandatory build and test path is CPU-only: no CUDA/OptiX, no Metal, no
discrete GPU required. GPU rendering is an opt-in extra.

## Prerequisites (Windows)

- Visual Studio 2022 with "Desktop development with C++" (MSVC x64)
- CMake ≥ 3.24, Git, Python 3.x
- Internet access on first configure (pinned dependencies are fetched and
  built automatically; see [DEPENDENCIES.md](DEPENDENCIES.md))

Verify the environment with:

```powershell
.\scripts\bootstrap-windows.ps1
```

## Build

```powershell
git clone --recurse-submodules https://github.com/jar091/mitsuba3-anari
cd mitsuba3-anari

cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

Mitsuba is a git submodule (`ext/mitsuba3`, the
[jar091/mitsuba3](https://github.com/jar091/mitsuba3) fork pinned at
v3.9.1); in a clone made without `--recurse-submodules`, run
`git submodule update --init --recursive` first.

The **first configure takes long** (tens of minutes): it clones and builds
the pinned ANARI-SDK v0.16.0 and builds Mitsuba (variants
`scalar_rgb,llvm_ad_rgb,cuda_ad_rgb`) into `build/windows-msvc-release/_deps/`.
Subsequent configures reuse them. Note: use the **Release** (or
RelWithDebInfo) preset on Windows — the Debug device configuration is
unsupported while Mitsuba is provisioned Release-only (see
[BUILDING.md](BUILDING.md)).

Run the test suite:

```powershell
ctest --preset windows-msvc-release --output-on-failure
```

## Install

```powershell
cmake --install build\windows-msvc-release --config Release --prefix C:\path\to\install
```

The install tree is **self-contained**: `bin/` holds
`anari_library_mitsuba.dll`, the ANARI frontend (`anari.dll`), the Mitsuba
runtime (`mitsuba.dll`, Dr.Jit DLLs, `plugins/`, `data/`). Applications only
need `<prefix>\bin` on their DLL search path (`PATH`); the ANARI loader also
finds the device automatically when it sits next to `anari.dll`, or via the
explicit form `anariLoadLibrary("mitsuba,<prefix>/bin/")`.

Verify the installed tree:

```powershell
$env:MITSUBA_ANARI_INSTALL_PREFIX = "C:\path\to\install"
ctest --preset windows-msvc-release -L install --output-on-failure
```

## Usage from C/C++

```cpp
ANARILibrary lib = anariLoadLibrary("mitsuba", statusCallback, userPtr);
ANARIDevice device = anariNewDevice(lib, "default");
```

Optional GPU rendering (falls back to `scalar_rgb` with a warning when
CUDA/OptiX is unavailable at runtime):

```cpp
anariSetParameter(device, device, "mitsuba.variant", ANARI_STRING, "cuda_ad_rgb");
anariCommitParameters(device, device);
```

## Usage from PyNARI

### 1. Build PyNARI once

PyNARI (pinned commit, see [PYNARI.md](PYNARI.md)) is built against the same
ANARI-SDK by a helper script; it also applies one small documented upstream
patch needed for Python 3.13:

```powershell
.\scripts\build-pynari-windows.ps1 -Python python
```

The script prints the directory containing the built `pynari*.pyd` module.

### 2. Set up the environment

```powershell
# module import path (from the script output)
$env:PYTHONPATH = "F:\...\build\windows-msvc-release\_deps\pynari-build\Release"
# DLL search: anari.dll + device + Mitsuba runtime (or just <install>\bin
# when using an installed tree)
$b = "F:\...\build\windows-msvc-release"
$env:PATH = "$b\_deps\anari-sdk-install\bin;$b\src\Release;$b\_deps\mitsuba-install\bin;$env:PATH"
```

Note for Python ≥ 3.8 on Windows: extension-module DLL dependencies are not
resolved via `PATH`; either start python from a directory listed above, or
register it explicitly before the import:

```python
import os
os.add_dll_directory(r"F:\...\_deps\anari-sdk-install\bin")
```

### 3. Run the simplest example

```powershell
python examples\pynari\01_triangle.py
```

It creates the `mitsuba` device, renders one matte triangle lit by a
directional light, verifies the pixels, and writes `01_triangle.png`.
The core of the script:

```python
import pynari as anari

device = anari.newDevice("mitsuba")

camera = device.newCamera("perspective")
camera.setParameter("position", anari.FLOAT32_VEC3, (0.0, 0.0, 2.0))
camera.setParameter("direction", anari.FLOAT32_VEC3, (0.0, 0.0, -1.0))
camera.commitParameters()
# ... geometry / material / surface / light / world / renderer ...
frame.render()
color = frame.get("channel.color")   # numpy array (height, width, 4)
```

More examples in [examples/pynari/](examples/pynari/): textured quad,
instancing, dynamic updates, depth read-back
(`03_textured_quad.py`, `04_instancing.py`, `05_dynamic_updates.py`,
`06_depth.py`).

## macOS walkthrough (Apple Silicon)

```bash
./scripts/bootstrap-macos.sh                 # verify toolchain (Xcode CLT, CMake, Python)

cmake --preset macos-clang-release           # first run provisions SDK + Mitsuba (long)
cmake --build --preset macos-clang-release
ctest --preset macos-clang-release --output-on-failure
```

Mitsuba is provisioned with variants `scalar_rgb,llvm_ad_rgb,metal_ad_rgb`;
`llvm_ad_rgb` picks up a Homebrew LLVM automatically, `metal_ad_rgb` uses the
Apple Silicon GPU (both opt-in via `mitsuba.variant`, with scalar fallback).

PyNARI on macOS:

```bash
./scripts/build-pynari-macos.sh              # clones + patches + builds pynari against the project venv (.venv)

cmake --preset macos-clang-release \
  -DMITSUBA_ANARI_BUILD_PYNARI_TESTS=ON \
  -DMITSUBA_ANARI_PYNARI_PYTHONPATH=$PWD/build/macos-clang-release/_deps/pynari-build \
  -DPython3_EXECUTABLE=$PWD/.venv/bin/python
ctest --preset macos-clang-release -L pynari --output-on-failure

# run the examples directly
export PYTHONPATH=$PWD/build/macos-clang-release/_deps/pynari-build
export DYLD_LIBRARY_PATH=$PWD/build/macos-clang-release/src:$PWD/build/macos-clang-release/_deps/anari-sdk-install/lib
.venv/bin/python examples/pynari/01_triangle.py
```

Install works as on Windows (`cmake --install build/macos-clang-release
--prefix <prefix>`): `lib/` is self-contained (device, ANARI frontend,
Mitsuba runtime incl. `plugins/` and `data/`), verified by the
`install.load_library` test with `MITSUBA_ANARI_INSTALL_PREFIX` set.

## Feature status

See [SUPPORTED_FEATURES.md](SUPPORTED_FEATURES.md) — nothing is marked
supported without an automated test, and runtime introspection matches that
table (verified by the ANARI CTS `check-properties`, see
[tests/cts/README.md](tests/cts/README.md)).

## Documentation

- [BUILDING.md](BUILDING.md) — prerequisites, presets, options, install
- [TESTING.md](TESTING.md) — test layers, labels, install-tree smoke test
- [ARCHITECTURE.md](ARCHITECTURE.md) — ANARI→Mitsuba translation design
- [PYNARI.md](PYNARI.md) — PyNARI setup and integration contract
- [DEPENDENCIES.md](DEPENDENCIES.md) — pinned dependencies and how to update
  them
- [PERFORMANCE.md](PERFORMANCE.md) — measured render times

## Development

AI agent harnesses were used for selected development, testing, and optimization tasks

## License

Apache-2.0, see [LICENSE](LICENSE). Third-party components:
[THIRD_PARTY.md](THIRD_PARTY.md).
