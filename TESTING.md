# Testing

CTest is the single entry point for the whole suite:

```bash
ctest --preset <preset> --output-on-failure
```

or, from a build directory:

```bash
ctest --test-dir build/<preset> --output-on-failure
```

## Test layers and labels

| Layer | Label | Location | Runs |
|-------|-------|----------|------|
| Unit tests | `unit` | `tests/unit/` | always |
| Native ANARI integration | `integration` | `tests/integration/` | always |
| Render regression | `render` | `tests/render/` | from Milestone 3 |
| PyNARI tests | `pynari` | `tests/pynari/` | with `MITSUBA_ANARI_BUILD_PYNARI_TESTS=ON` |
| SDK tools (`anariInfo`, `anariCat`, render tests) | `sdk` | `tests/` | from Milestone 1 |
| ANARI CTS | `cts` | `tests/cts/` | with `MITSUBA_ANARI_BUILD_CTS=ON` |
| Install-tree smoke | `install` | `tests/integration/` | after `cmake --install`, opt-in (see below) |

Filter by label:

```bash
ctest -L unit
ctest -L pynari
ctest -LE slow
```

## Naming convention

```text
unit.<area>            e.g. unit.parameter_store
anari.<scenario>       e.g. anari.load_library, anari.triangle_matte
debug.<scenario>       ANARI debug-layer wrapped runs
pynari.<scenario>      e.g. pynari.triangle
sdk.<tool>             e.g. sdk.anari_info
cts.<group>
install.<scenario>     install-tree checks
```

## Integration tests are true out-of-process clients

Native integration tests link only the public ANARI frontend and load the device
with `anariLoadLibrary("mitsuba", ...)` — never against internal classes. This
keeps them honest proxies for real applications.

## Install-tree smoke test

The `install.load_library` test verifies the *installed* device library loads by
name, independent of the build tree:

```powershell
cmake --install build/windows-msvc-debug --config Debug --prefix _install
$env:MITSUBA_ANARI_INSTALL_PREFIX = (Resolve-Path _install)
ctest --preset windows-msvc-debug -L install --output-on-failure
```

The test is skipped (reported as such) when `MITSUBA_ANARI_INSTALL_PREFIX` is
not set; run it after every `cmake --install` as shown above (there is no
hosted CI — validation is local by project policy).

## Render regression policy (from Milestone 3)

No exact golden-byte comparisons across platforms. Tests assert:

- finite pixel values, expected dimensions and alpha range;
- non-background coverage and mean-luminance bounds;
- relational behavior for parameter changes (e.g. `roughness=0.05` must differ
  meaningfully from `roughness=0.8`);
- RMSE/MAE tolerances against reference images where justified.

Deterministic scene settings (fixed resolution, sample count, seed, camera) are
mandatory for every render test.

## Debug layer (from Milestone 2)

Applicable integration tests are additionally run through the ANARI-SDK debug
device wrapping the Mitsuba device; debug-layer errors are test failures.
