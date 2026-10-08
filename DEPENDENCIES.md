# Dependencies

All dependency versions are pinned. The ANARI-SDK and PyNARI pins live in
`cmake/Dependencies.cmake`; Mitsuba is pinned by the commit of the
`ext/mitsuba3` git submodule. Never build against floating branches.

Verified against upstream on **2026-08-14** (`git ls-remote`); the Mitsuba
commit was re-checked in the fork on **2026-10-08**.

| Dependency | Version / commit | Source | Role |
|------------|------------------|--------|------|
| ANARI-SDK | `v0.16.0` (`7534bd263d6ff97764eda93d0e1bd6bd2f108c32`) | https://github.com/KhronosGroup/ANARI-SDK | ANARI frontend, `helium` device foundation, tools (`anariInfo`, `anariCat`, render tests), debug layer |
| Mitsuba 3 | `v3.9.1` (`478e193a183c21723f4a8251afc3ad29a8da4c5e`) | git submodule `ext/mitsuba3` → https://github.com/jar091/mitsuba3, branch `master` (fork of [mitsuba-renderer/mitsuba3](https://github.com/mitsuba-renderer/mitsuba3)) | Rendering backend |
| PyNARI | branch `devel`, commit `a860d0b68d08096f552d158265a90d040bd4498d` | https://github.com/jar091/pynari | Python-facing ANARI client used for examples/tests (not a build dependency of the device) |
| ANARI spec | 1.1 | https://registry.khronos.org/ANARI/ | Target specification |

Reference-only repositories (never linked, never fetched by the build):

| Repository | Ref | Used for |
|------------|-----|----------|
| VisRTX | `next_release` | Device architecture reference |
| anari-cycles | `main` | ANARI→existing-renderer adapter reference |
| Barney | `main` | Secondary ANARI pattern reference |

## Dependency modes

1. **Reproducible developer mode (default)** — `MITSUBA_ANARI_FETCH_DEPENDENCIES=ON`:
   at configure time, CMake clones the pinned ANARI-SDK tag (verifying the
   commit hash), builds it, and installs it under `<build>/_deps/`, then
   consumes it via `find_package`. FetchContent/`add_subdirectory` is not an
   option: the SDK's package config explicitly forbids build-tree consumption.
2. **System/developer-installed mode** — `MITSUBA_ANARI_FETCH_DEPENDENCIES=OFF`:
   `find_package(anari CONFIG REQUIRED)` against an existing install; point
   `CMAKE_PREFIX_PATH` at the ANARI-SDK install prefix.

Mitsuba is built from the `ext/mitsuba3` submodule, so the sources must be
checked out before configuring (Mitsuba vendors its own dependencies as
nested submodules, hence `--recursive`):

```bash
git submodule update --init --recursive
```

At configure time the submodule is configured with `MI_ENABLE_PYTHON=OFF` and
variants `scalar_rgb,llvm_ad_rgb,cuda_ad_rgb` (on macOS `cuda_ad_rgb` is
replaced by `metal_ad_rgb` — CUDA does not exist there), built Release,
installed under `<build>/_deps/`, and linked through a hand-written
`mitsuba::mitsuba` IMPORTED target (Mitsuba has no `find_package` support).
The build is redone whenever the submodule commit changes or files in its
working tree are edited, so Mitsuba can be modified in place (edits inside
Mitsuba's own nested submodules are only noticed the first time).
`-DMITSUBA_ANARI_MITSUBA_SOURCE_DIR=<dir>`
builds from a different Mitsuba checkout instead. In system mode point
`-DMITSUBA_ANARI_MITSUBA_ROOT=<prefix>` at an existing install (the submodule
is still needed for the vendored headers).

### Documented upstream workarounds (Mitsuba v3.9.1)

1. **CMake ≥ 4:** vendored `ext/` projects declare `cmake_minimum_required`
   < 3.5, which CMake 4 rejects → configured with
   `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`.
2. **`MI_ENABLE_PYTHON=OFF` include bug:** `mitsuba/core/object.h` includes
   nanobind's intrusive-ref headers unconditionally, but
   `NANOBIND_INCLUDE_DIRS` is only populated when Python bindings are enabled
   → the nanobind include dir is injected via `CMAKE_CXX_FLAGS`.
3. **Variant-set constraints:** `MI_DEFAULT_VARIANTS=scalar_rgb` alone trips a
   duplicate `add_subdirectory(nanothread)` when `MI_ENABLE_JIT=OFF`, and a
   non-ad JIT variant (`llvm_rgb`) fails to link — `librender` references
   `ad_*`/traversable symbols that live in `drjit-extra`, which upstream only
   builds for `ad` variants. The working set is `scalar_rgb,llvm_ad_rgb`
   (matching upstream defaults; LLVM is loaded at *runtime* only, so this
   adds no build dependency).

4. **ISA/ABI baseline:** Dr.Jit types in Mitsuba's C++ ABI change layout with
   the enabled SIMD instruction set (Mitsuba defaults to AVX2 on MSVC,
   `-march=native` elsewhere). A consumer compiled with a different ISA
   silently corrupts the heap — verified during Milestone 3 bring-up. Both
   sides are therefore pinned to a deterministic baseline (Haswell/AVX2 on
   x86-64, ARMv8/Apple-M1 on ARM): the Mitsuba build via `MI_NATIVE_FLAGS`,
   consumers via INTERFACE compile options on `mitsuba::mitsuba`.

5. **Unix install layout (macOS/Linux):** upstream installs `plugins/` under
   `bin/plugins` and data files (`srgb.coeff`, `ior/`, `sunsky/`) under
   `share/`, but the runtime resolves `plugins/...` and `data/...` relative
   to the directory containing the Mitsuba shared library (`lib/`). The
   provisioning step colocates copies under `lib/` so resolution works like
   the Windows `bin/` layout.
6. **`mitsuba.conf` caching:** `MI_DEFAULT_VARIANTS` only seeds
   `mitsuba.conf` when the file does not exist in the Mitsuba build dir.
   Changing the variant pin therefore also removes the cached conf during
   re-provisioning; otherwise the old variant set is silently kept.

All six are pure build-system workarounds — no Mitsuba sources are modified.

### Windows configuration note

The Mitsuba backend is built **Release**. On Windows (MSVC), mixing a Debug
device with the Release Mitsuba C++ ABI is not safe, so from Milestone 3 the
Windows development presets that link Mitsuba must be `windows-msvc-release`
or `windows-msvc-relwithdebinfo`. A Debug-Mitsuba provisioning mode can be
added later if debugging inside Mitsuba becomes necessary.

## Updating a pin

1. Change the version in `cmake/Dependencies.cmake` (ANARI-SDK, PyNARI) or
   move the `ext/mitsuba3` submodule to the new commit (Mitsuba).
2. Update this file.
3. The local test suite (`scripts/test-*.{ps1,sh}`) must be green before the
   pin change merges. There is no hosted CI by project policy; cross-platform
   validation is run locally on the target machines.

### Moving the Mitsuba submodule

```bash
git -C ext/mitsuba3 fetch origin
git -C ext/mitsuba3 checkout <commit>
git -C ext/mitsuba3 submodule update --init --recursive
git add ext/mitsuba3            # records the new pin in this repository
```

The next configure notices the change and rebuilds Mitsuba.

The submodule deliberately stays on `v3.9.1` although it tracks the fork's
`master`: the tip of `master` (`ab3e3b18` on 2026-10-08, 234 commits after
`v3.9.1`) changes C++ API this device uses — `Mesh` is constructed
differently and `Texture::mean()` is gone — so
`src/scene/MitsubaSceneBuilder.cpp` has to be ported before the pin can move
there. `git submodule update --remote` jumps to that tip; do not use it until
the port is done.
