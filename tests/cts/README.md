# ANARI Compliance Baseline (Milestone 8)

Tooling comes from the provisioned ANARI-SDK build (`BUILD_TESTING=ON`,
`BUILD_CTS=ON`, `INSTALL_CTS=ON` — see `cmake/Dependencies.cmake`):
`anariInfo`, `anariRenderTests`, `anariCts` in `<build>/_deps/anari-sdk-install/bin`.

Recorded on Windows, 2026-08-15, device at Milestones 0–7 + M11.

## `anariCts query-features mitsuba`

Reports exactly the advertised extension set (14 extensions — see
`SUPPORTED_FEATURES.md`); no undeclared feature appears.

## `anariCts check-properties mitsuba`

- **45 tests runnable** (all features the device advertises)
- **44 tests correctly skipped** with `missing: ANARI_KHR_*` reasons
  (cone/cylinder geometry, unimplemented frame channels, samplers other than
  image2D, volumes, …) — the introspection-honesty contract holds.

Baseline: `check-properties.txt` (regenerate with
`anariCts check-properties "mitsuba,<device-dir>/" > check-properties.txt`).

## `anariCts run mitsuba` (image scoring) — first baseline, 2026-08-15

Ground truth: `anariCts generate` with the SDK reference device `helide`
(built ad hoc with `-DBUILD_HELIDE_DEVICE=ON`; 206 images generated,
123 skipped by helide's own feature set).

First scoring run against `mitsuba`:

```text
329 cases: 18 passed, 81 failed, 230 skipped
  camera      0/14 failed=14   frame     0/4  failed=4
  geometry    6/52 failed=46   material 12/16 failed=4
  sampler     0/13 failed=13   (light/renderer/volume/instance: skipped)
```

Interpretation: the skips match the honesty contract (features not
advertised). The failure clusters are expected to be largely *systematic*
differences between helide's ray-caster ground truth and this device's path
tracer (global illumination, soft shadows, sampling noise, AA filter), plus
possibly camera/UV conventions — per-case image diffing is the next step and
each cluster must be dispositioned as either a real defect (fix) or a
documented renderer-difference (tolerance/upstream discussion). This file is
the monotonically-shrinking baseline.

Regenerate/refresh:

```powershell
anariCts generate --workdir gt              # uses helide
anariCts run "mitsuba,<device-dir>/" --workdir gt
anariCts report gt
```

## `anariRenderTests -l mitsuba`

- Scenes rendering successfully: `demo_cornell_box`,
  `demo_gravity_spheres_volume` (volume part skipped by validity rules),
  `perf_materials`, `perf_particles`, `perf_primitives`, `perf_surfaces`.
- Findings already fixed during this pass:
  - materials binding the `color` attribute on geometry without
    `vertex.color` used to abort the render — now falls back to the constant
    color with a warning;
  - non-positive `vertex.radius` sphere primitives (the `random_spheres`
    scene generates them intentionally) crashed the Mitsuba backend — now
    validated and skipped with a warning (§31).
- **`perf spheres` crash — upstream, not this device:** the scene
  (`random_spheres`) terminates with an access violation even when run
  against the SDK's own `sink` device
  (`anariRenderTests -l sink --scene perf spheres` → 0xC0000005), i.e. the
  fault is in the SDK test harness/scene generator, not in
  `anari_library_mitsuba`. Recorded 2026-08-15 against ANARI-SDK v0.16.0;
  worth reporting upstream. All other scenes pass against this device.

## Regeneration

```powershell
$env:PATH = "<sdk-install>\bin;<device-dir>;<mitsuba-install>\bin;$env:PATH"
anariCts query-features   "mitsuba,<device-dir>/"
anariCts check-properties "mitsuba,<device-dir>/"
anariRenderTests -l "mitsuba,<device-dir>/"
```
