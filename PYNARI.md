# PyNARI Integration

PyNARI (https://github.com/jar091/pynari, branch `devel`, pinned commit
`a860d0b68d08096f552d158265a90d040bd4498d`) is a first-class client of this
device. Target usage:

```python
import pynari as anari

device = anari.newDevice("mitsuba")
```

## How PyNARI loads the device (verified against the pinned commit)

- `pynari.newDevice(libName, devName="default")` calls the plain ANARI C API:
  `anariLoadLibrary(libName, statusFunc)` + `anariNewDevice(lib, devName)`.
  There is no PyNARI-specific loader logic. Our library therefore must register
  the device subtype **`default`**.
- `pynari.newDevice("default")` honors the `ANARI_LIBRARY` environment variable
  (falling back to `barney`). `ANARI_LIBRARY=mitsuba python sample.py` runs any
  bundled PyNARI sample against our device unmodified — used by our test suite.
- PyNARI links an **external** ANARI-SDK (`find_package(anari 0.15.0)`), plus
  `anari::helium`. We pin ANARI-SDK v0.16.0 and build both PyNARI and the
  device against the same SDK install to avoid loader/ABI mismatches.
- PyNARI itself performs **no** DLL/shared-library path handling (no rpath in
  the default build, no `os.add_dll_directory`). Discovery is delegated to the
  ANARI-SDK loader and OS rules:
  - Windows: `anari_library_mitsuba.dll` must be on `PATH` or next to
    `anari.dll` (the SDK loader's anchored fallback; note SDK 0.16.0 has no
    `ANARI_LIBRARY_PATH` variable — covered by our install-tree tests);
  - Linux: `LD_LIBRARY_PATH` or co-located with `libanari.so`;
  - macOS: `DYLD_LIBRARY_PATH` / `@loader_path` of the anari dylib.

## Device requirements imposed by PyNARI (from its binding code)

1. Device subtype `default` must exist.
2. `channel.color` as `ANARI_UFIXED8_RGBA_SRGB` is mandatory — PyNARI's Frame
   constructor presets it before user code runs. `ANARI_FLOAT32_VEC4` is also
   used by samples. `frame.get()` only reads `channel.color` on this commit.
3. Never emit `ANARI_SEVERITY_FATAL_ERROR` for recoverable conditions —
   PyNARI's status callback calls `std::exit(1)` on fatal.
4. `anariGetObjectInfo` / `anariGetParameterInfo` must return non-null
   `description` / `required` values — PyNARI dereferences them without null
   checks.
5. Implement the World `bounds` property (`ANARI_FLOAT32_BOX3`); PyNARI's
   viewer and `world.getBounds()` rely on it.
6. `frame.render()` maps to `anariRenderFrame` + `anariFrameReady(ANARI_WAIT)`
   (synchronous); PyNARI's array creation copies numpy data via map/unmap, so
   the device never borrows Python-owned memory.

## Setup (all platforms)

1. Build and install ANARI-SDK v0.16.0 (or use this project's fetched build).
2. Build and install this project (`BUILDING.md`).
3. Build PyNARI against the same SDK:

   ```bash
   git clone https://github.com/jar091/pynari
   cd pynari && git checkout a860d0b68d08096f552d158265a90d040bd4498d
   cmake -B build -Danari_DIR=<sdk-prefix>/lib/cmake/anari
   cmake --build build
   ```

4. Make the module and the device library findable:

   Windows (PowerShell):

   ```powershell
   $env:PYTHONPATH = "<pynari-build>\Release"
   $env:PATH = "<install-prefix>\bin;<sdk-prefix>\bin;$env:PATH"
   python -c "import pynari as anari; print(anari.newDevice('mitsuba'))"
   ```

   Linux/macOS:

   ```bash
   export PYTHONPATH=<pynari-build>
   export LD_LIBRARY_PATH=<install-prefix>/lib:<sdk-prefix>/lib   # DYLD_ on macOS
   python -c "import pynari as anari; print(anari.newDevice('mitsuba'))"
   ```

### macOS (validated on Apple Silicon)

Steps 1–3 are automated by `scripts/build-pynari-macos.sh`: it clones the
pinned commit, applies the documented patch, and builds against this
project's provisioned SDK and the project venv (`.venv`, created with a
Homebrew `python3.13` if missing — nothing is installed into user or system
site-packages). Then:

```bash
export PYTHONPATH=<repo>/build/macos-clang-release/_deps/pynari-build
export DYLD_LIBRARY_PATH=<repo>/build/macos-clang-release/src:<repo>/build/macos-clang-release/_deps/anari-sdk-install/lib
.venv/bin/python examples/pynari/01_triangle.py
```

(`libmitsuba.dylib` and the ANARI frontend resolve through the device's
rpath; `DYLD_LIBRARY_PATH` is only needed for the `dlopen` of
`libanari_library_mitsuba.dylib` itself. With an installed tree,
`<install-prefix>/lib` alone suffices.)

## Examples and tests

- `examples/pynari/` — project-owned examples (added milestone by milestone,
  starting with `01_triangle.py` in Milestone 3).
- `tests/pynari/` — pytest-based tests run through CTest
  (`MITSUBA_ANARI_BUILD_PYNARI_TESTS=ON`), each in a separate process to
  isolate dynamic-library and renderer state.
- PyNARI's own `testing/release_00..05.py` scripts (device create/destroy
  loops, simple render) will be wired in as an additional smoke gate using
  `ANARI_LIBRARY=mitsuba`.

## Policy and the one applied patch

PyNARI is not modified except where an upstream bug blocks correct use; such
fixes are isolated, documented, kept compatible with the ANARI C API, and
prepared for upstreaming.

**Applied patch:** `patches/pynari-pybind213-int-dispatch.patch`
(auto-applied by `scripts/build-pynari-windows.ps1` /
`scripts/build-pynari-macos.sh`).

- Why: Python 3.13 requires pybind11 ≥ 2.13 (the vendored 2.12.0.dev1 does
  not support it). pybind11 2.13's overload resolution routes Python ints and
  int-tuples into pynari's `set_float*` overloads, which reject integer
  declared types (`RuntimeError: ... unsupported type ANARI_INT32` /
  `ANARI_UINT32_VEC2` — e.g. `pixelSamples` and `size` from pynari's own
  samples).
- Fix: the `set_float*` setters honor explicitly declared integer parameter
  types by converting the values (INT32/UINT32/UINT64/DATA_TYPE and
  UINT32_VEC2/3/4). No behavior change for correctly-dispatched calls; the
  ANARI C API contract is untouched.
- The same patch also adds `frame.get("channel.depth")` (returns a
  `(height, width)` float32 numpy array) — upstream `Frame::get` only reads
  `channel.color`; the README documents depth reads that the code does not
  implement. Used by `examples/pynari/06_depth.py`.
- Upstream: the patch applies cleanly onto `devel` @ `a860d0b6` and is
  written to be submitted as a PR to `ingowald/pynari`.
