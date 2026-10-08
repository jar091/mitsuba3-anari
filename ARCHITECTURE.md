# Architecture

`mitsuba-anari` is a native C++ implementation of the Khronos ANARI API whose
rendering backend is Mitsuba 3. The core rule: **ANARI
objects are never Mitsuba objects.** The ANARI layer owns semantic scene
state; the Mitsuba layer is a translated, renderable representation of it.

## Top-level flow

```text
ANARI application (C/C++ or PyNARI)
      |
      v
Khronos ANARI frontend (anari.dll / libanari)
      |  anariLoadLibrary("mitsuba") -> anari_library_mitsuba_new_library()
      v
anari_library_mitsuba  (this project)
      |
      +--------------------------------------+
      | ANARI retained object model (helium) |
      |  Array  Camera  Geometry  Material   |
      |  Sampler Surface Group    Instance   |
      |  Light  World   Renderer  Frame      |
      +--------------------------------------+
                    |
             commit / finalize
                    |
                    v
          MitsubaSceneBuilder
                    |
          +---------+----------+
          |                    |
          v                    v
    Mitsuba object cache   dirty-state graph
          |                    |
          +---------+----------+
                    |
                    v
              Mitsuba Scene
                    |
                    v
         Integrator / Sensor / Film
                    |
                    v
            ANARIFrame channels
```

## Device foundation

The retained object model is built on ANARI-SDK 0.16.0 `helium`:

- `mitsuba_anari::Library : anari::LibraryImpl` — the loadable entry object
  ([src/library/Library.cpp](src/library/Library.cpp)).
- `MitsubaDevice : helium::BaseDevice` (Milestone 1) — object factories,
  introspection forwarding, device parameters/properties.
- `MitsubaGlobalState : helium::BaseGlobalDeviceState` — deferred commit
  buffer, status routing, backend handle.
- A project `Object` base between `helium::BaseObject` and concrete types
  centralizes validity checks and state access (helide pattern).
- Introspection is generated (`anari_generate_queries()` +
  `MitsubaDefinitions.json`); the definitions file is the single source of
  truth for advertised subtypes/parameters/extensions and must always match
  runtime behavior.

### Commit / staging model (helium 0.16)

`anariCommitParameters()` snapshots staged parameters into committed state and
enqueues the object in a priority-ordered deferred commit buffer
(Frame > World > Instance > Group > Surface/Volume > Material > others).
The buffer is flushed by our `Frame::renderFrame()` (helium does not flush it
implicitly). During flush each object runs `commitParameters()` (read
committed params) then `finalize()` (resolve dependencies). Uncommitted
changes are never visible to rendering.

## Translation layer

`MitsubaSceneBuilder` consumes committed ANARI state and maintains:

- **Mitsuba object cache** — per ANARI object, the translated Mitsuba object
  (`Mesh`, BSDF, emitter, sensor, film, integrator) plus the version it was
  translated from.
- **Dirty-state graph** — scene versions (parameter, committed, dependency,
  translated, rendered) determining the minimal Mitsuba update.

Update classification (from Mitsuba 3.9.1 investigation):

| ANARI change | Mitsuba action |
|--------------|----------------|
| Mesh vertex/index data, counts | in-place buffer update + `parameters_changed()` → accel rebuild |
| Instance transform | in-place + `parameters_changed()` |
| Material/sampler/light parameter values | in-place on cached object |
| Camera parameters, frame size | sensor/film update in place |
| Renderer parameters | integrator update or swap (no scene rebuild) |
| Surface/light **membership** in world | new `Scene` object, **reusing** all cached Mitsuba objects |
| Device variant change | refused / device-level policy (see "Variant handling") |

A render performs: flush pending commits → resolve dependencies → determine
minimal Mitsuba updates → update/rebuild Mitsuba objects → (if membership
changed) rebuild `Scene` → render → extract channels.

## Rendering and frames

- One render thread per device; `anariRenderFrame` enqueues work,
  `anariFrameReady(ANARI_NO_WAIT/WAIT)` polls/waits, `discardFrame` maps to
  `Integrator::cancel()` (block granularity on scalar variants, pass
  granularity on JIT variants — documented as best-effort there).
- Color extraction via `Film::bitmap()` (mutex-guarded in Mitsuba; safe for
  progressive readback), converted to the frame's channel format
  (`UFIXED8_RGBA_SRGB` mandatory for PyNARI, `FLOAT32_VEC4` next).
- Depth/normal/albedo/ID channels map to Mitsuba AOV integrator channels
  (Milestone 6+). ID channels are never synthesized.

## Variant handling

Mitsuba variants are compile-time template instantiations selected at runtime
by name via `MI_INVOKE_VARIANT`. Only the `src/mitsuba/` backend layer is
variant-templated; the ANARI object model is variant-agnostic. Baseline
variant: `scalar_rgb` (all platforms, no GPU/LLVM). One active variant per
device; variant-family global state (JIT init, thread pool, color management)
is initialized once per process.

## Error handling

All Mitsuba/Dr.Jit exceptions are caught at every ANARI boundary and reported
through ANARI status callbacks (the SDK frontend `std::terminate`s on escaped
exceptions). Fatal severity is reserved for truly
unrecoverable states (PyNARI's callback exits the process on fatal). Optional
logging is controlled by `ANARI_MITSUBA_LOG_LEVEL`; default output is quiet.

## Source layout

- `src/library/` — loadable library entry point
- `src/device/` — `MitsubaDevice` and the global device state
- `src/core/`, `src/array/` — object base class, math helpers, data arrays
- `src/camera/`, `src/light/`, `src/geometry/`, `src/material/`,
  `src/sampler/`, `src/spatial_field/`, `src/volume/`, `src/renderer/`,
  `src/frame/` — one directory per ANARI object type
- `src/scene/` — world, instance, group, surface and `MitsubaSceneBuilder`
  (the translation layer)
- `src/mitsuba_backend/` — Mitsuba backend initialization and variant dispatch
