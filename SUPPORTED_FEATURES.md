# Supported Features

Status values: `SUPPORTED` (implemented + automated test), `PARTIAL`,
`PLANNED`, `UNSUPPORTED`. Nothing is marked `SUPPORTED` without an automated
test, and runtime introspection (`json/MitsubaDefinitions.json` →
`anariGetObjectSubtypes`/`anariGetDeviceExtensions`) must match this table
(verified by `anari.introspection` and `anari.load_library`).

**Current state (after Milestone 7):** triangle/quad/sphere geometry with
normals, UVs and vertex colors; matte + physicallyBased materials; image2D
textures; four light types; perspective/orthographic cameras; color + depth
channels; retained-mode updates; opt-in `cuda_ad_rgb`/`llvm_ad_rgb` rendering.
Windows-validated; Linux/macOS validation pending local machines.

| ANARI object | Subtype | Status | Mitsuba mapping | Tests | Notes |
|--------------|---------|--------|-----------------|-------|-------|
| Device | `default` | SUPPORTED | process-global runtime, per-device state | `anari.create_device`, `install.load_library`, `sdk.anari_info` | properties: `version*`, `extension`, `mitsuba`, `mitsuba.variant` |
| Array | `array1D` (data + object) | SUPPORTED | copied into Mitsuba buffers at translation | `anari.object_lifetime` | capture/shared semantics + deleter via helium |
| Array | `array2D`/`array3D` | PARTIAL | — | `anari.object_lifetime` (lifetime only) | object model only; no consumer yet |
| Camera | `perspective` | SUPPORTED | `perspective` sensor (`fov_axis=y`, look_at); `thinlens` when `apertureRadius` > 0 | `anari.triangle_matte`, `anari.scene_update`, `pynari.triangle`, pynari `camera-depth-of-field` | position/direction/up/fovy/aspect, `apertureRadius`/`focusDistance` (KHR_CAMERA_DEPTH_OF_FIELD) |
| Camera | `orthographic` | SUPPORTED | `orthographic` sensor (look_at x scale) | `anari.lights_depth` | `height`, `aspect` |
| Geometry | `triangle` | SUPPORTED | `Mesh` (indexed + soup; world-space baked per instance) | same as camera | `vertex.position`, `primitive.index`, `vertex.normal`, `vertex.*`/`primitive.*` `color`/`attribute0..3` (any float / normalized fixed-point / sRGB type) as `vertex_color`/`face_color` mesh attributes or sampler texture coordinates |
| Geometry | `quad` | SUPPORTED | `Mesh` (triangulated) | `anari.geometry` | indexed (UINT32_VEC4) + soup |
| Geometry | `sphere` | SUPPORTED | one `sphere` shape per primitive; > 1024 spheres or per-sphere colors: one `ellipsoids` shape + per-primitive color texture | `anari.geometry`, pynari `geometry-spheres-with-sampler1D` (1M spheres) | per-vertex/uniform radius, `primitive.index`; `ellipsoids` culls back faces, so transmissive and emissive spheres stay individual shapes |
| Geometry | `curve` | PARTIAL | `cylinder` segments + `sphere` joints (mean segment radius); with bound color attributes tessellated cone frustums + sphere joints | pynari `sample04`, `sample07` | Mitsuba curve plugins are file-based |
| Geometry | `cylinder` | SUPPORTED | `cylinder` shapes + `disk` caps; tessellated (64 sides) with bound color attributes | pynari `geometry-cylinders*` | `primitive.radius`/`radius`, `caps`, `vertex.cap`, vertex/primitive colors |
| Geometry | `cone` | SUPPORTED | tessellated cone frustums (64 sides, smooth normals) | pynari `geometry-cones*` | `vertex.radius`, `caps`, `vertex.cap`, vertex/primitive colors |
| Geometry | `isosurface` | PARTIAL | marching tetrahedra on the (upsampled) `structuredRegular` field -> mesh | pynari `sample03-isosurface` | `isovalue` float or array; other field types warn and are skipped |
| Material | `matte` | SUPPORTED | `diffuse` BSDF | `anari.triangle_matte`, `anari.pbr_texture`, `anari.geometry` | `color` as constant, image1D/image2D/image3D sampler, or attribute binding (`color`, `attribute0..3`); `opacity` not yet |
| Material | `physicallyBased` | SUPPORTED | `roughplastic`+`diffuse` dielectric, `principled` metal, `roughdielectric` glass, `mask` opacity | `anari.pbr_texture`, pynari samples | baseColor (constant/sampler/attribute)/metallic/roughness/ior/opacity/transmission/specular/specularColor/emissive (area emitters); normal map, clearcoat, sheen, iridescence warn |
| Sampler | `image2D` | SUPPORTED | `bitmap` texture (linear float texels) | `anari.pbr_texture` | FLOAT32/UFIXED8/UFIXED16 1-4 channels, sRGB decode; in/out transforms + offsets; RGBA alpha dropped; single wrap mode |
| Sampler | `image1D` | SUPPORTED | 1-row `bitmap` texture via mesh texture coordinates; CPU-evaluated per-primitive colors on other geometry | pynari `geometry-spheres-with-sampler1D` | same parameters as image2D (`wrapMode`) |
| Sampler | `image3D` | PARTIAL | `volume` texture over an RGB `gridvolume` | pynari `sampler-image3d*` | `inAttribute` `worldPosition` or `objectPosition` only (both looked up in world space; other attributes warn and use the constant color); in/out transforms + offsets; one wrap mode for all axes; `clampToBorder` clamps to the edge |
| Sampler | `primitive`, `transform` | UNSUPPORTED | — | — | warning; materials fall back to their constant color |
| Light | `directional` | SUPPORTED | `directional` emitter (irradiance convention matches) | `anari.triangle_matte`, `anari.scene_update` | |
| Light | `point` | SUPPORTED | `point` emitter | `anari.lights_depth` | intensity, power/(4pi) |
| Light | `quad` | SUPPORTED | `rectangle` + `area` emitter | `anari.lights_depth` | radiance / intensity/A / power/(pi*A); front side only |
| Light | `hdri` | SUPPORTED | `envmap` (in-memory bitmap) | `anari.lights_depth` | tint*scale baked into data; orientation approximation |
| Surface | — | SUPPORTED | shape+BSDF binding | `anari.scene_update` | `geometry`, `material`; `visible`: later |
| Group | — | SUPPORTED | per-instance mesh emission | `anari.scene_update` | surface, volume and light arrays |
| Instance | `transform` | SUPPORTED | transform baked into mesh (Mitsuba-native instancing: Milestone 11) | `anari.scene_update` | shared groups across instances tested |
| World | — | SUPPORTED | scene assembly; `bounds` property | `anari.triangle_matte`, `pynari.triangle` | surfaces/lights (zero instance) + instance array |
| Renderer | `default` | SUPPORTED | `path` (`volpath` with volumes) integrator, max_depth = maxRayDepth + 1, ambient = hidden constant emitter, background composited | `anari.triangle_matte`, pynari samples | `pixelSamples`, `maxRayDepth`, `background` (color, or ARRAY2D image stretched over the frame), `ambientColor`, `ambientRadiance` (replaced by an hdri light when present: Mitsuba scenes hold one environment emitter; matches barney/Cycles) |
| Frame | `channel.color` | SUPPORTED | hdrfilm RGBA float32 → converted | `anari.triangle_matte` (FLOAT32_VEC4 + UFIXED8_RGBA_SRGB) | `size`, `renderer`, `camera`, `world`, `duration` |
| Frame | `channel.depth` | SUPPORTED | `aov` integrator (`dd:depth`) | `anari.lights_depth`, `06_depth.py` | primary-hit distance; misses = +inf |
| Frame | id/normal/albedo channels | PLANNED | AOVs | — | Milestone 8+ |
| SpatialField | `structuredRegular` | SUPPORTED | resampled (>= 64 cells on the longest axis) for volumes / isosurfaces | pynari `sample03` | FLOAT32/FLOAT64/UFIXED8/UFIXED16 data |
| SpatialField | `unstructured` | PARTIAL | cells split into tetrahedra, rasterized into a regular grid (128-256 cells) | pynari `unstructured-*` | tet/hex/wedge/pyramid, vertex or cell data |
| Volume | `transferFunction1D` | SUPPORTED | `heterogeneous` medium in a `cube` (null BSDF) with classified extinction + albedo `gridvolume`s | pynari `sample03`, `unstructured-*` | color/opacity arrays (opacity from RGBA alpha without an opacity array), `valueRange`, `unitDistance` (extinction = opacity / unitDistance) |

## Extensions (all advertised by introspection, all tested)

| Extension | Status | Tests | Notes |
|-----------|--------|-------|-------|
| `KHR_DEVICE_SYNCHRONIZATION` | SUPPORTED | `debug.object_lifetime` (helium per-object locking) | |
| `KHR_CAMERA_PERSPECTIVE` | SUPPORTED | `anari.triangle_matte` | |
| `KHR_GEOMETRY_TRIANGLE` | SUPPORTED | `anari.triangle_matte` | |
| `KHR_MATERIAL_MATTE` | SUPPORTED | `anari.triangle_matte` | |
| `KHR_LIGHT_DIRECTIONAL` | SUPPORTED | `anari.triangle_matte` | |
| `KHR_LIGHT_POINT` / `KHR_LIGHT_QUAD` / `KHR_LIGHT_HDRI` | SUPPORTED | `anari.lights_depth` | |
| `KHR_CAMERA_ORTHOGRAPHIC` | SUPPORTED | `anari.lights_depth` | |
| `KHR_GEOMETRY_QUAD` / `KHR_GEOMETRY_SPHERE` | SUPPORTED | `anari.geometry` | |
| `KHR_MATERIAL_PHYSICALLY_BASED` | SUPPORTED | `anari.pbr_texture` | mapping in the `physicallyBased` row above |
| `KHR_SAMPLER_IMAGE2D` | SUPPORTED | `anari.pbr_texture` | |
| `KHR_SAMPLER_IMAGE1D` | SUPPORTED | pynari `geometry-spheres-with-sampler1D` | |
| `KHR_SAMPLER_IMAGE3D` | PARTIAL | pynari `sampler-image3d*` | position-driven samplers only, see the `image3D` row above |
| `KHR_GEOMETRY_CONE` / `KHR_GEOMETRY_CYLINDER` / `KHR_GEOMETRY_ISOSURFACE` | SUPPORTED / PARTIAL | pynari geometry samples | see table above |
| `KHR_CAMERA_DEPTH_OF_FIELD` | SUPPORTED | pynari `camera-depth-of-field` | `thinlens` sensor |
| `KHR_RENDERER_AMBIENT_LIGHT` / `KHR_RENDERER_BACKGROUND_COLOR` / `KHR_RENDERER_BACKGROUND_IMAGE` | SUPPORTED | pynari samples | |
| `KHR_SPATIAL_FIELD_STRUCTURED_REGULAR` / `KHR_SPATIAL_FIELD_UNSTRUCTURED` / `KHR_VOLUME_TRANSFER_FUNCTION1D` | SUPPORTED / PARTIAL | pynari volume samples | |
| `ANARI_MITSUBA_DEVICE_VARIANT` | SUPPORTED | `gpu.triangle_matte`, `llvm.triangle_matte`, `metal.triangle_matte` | `mitsuba.variant` device parameter + property; scalar fallback with warning; one variant per process |

## Rendering variants

| Variant | Role | Validation |
|---------|------|------------|
| `scalar_rgb` | mandatory cross-platform baseline (all mandatory tests) | full suite (Windows + macOS arm64) |
| `cuda_ad_rgb` | opt-in GPU, Windows/Linux (NVIDIA driver + OptiX probed at runtime) | `gpu.triangle_matte` — PASS on RTX 2080 Ti; skips elsewhere |
| `llvm_ad_rgb` | opt-in JIT CPU (LLVM loaded at runtime) | `llvm.triangle_matte` — PASS on macOS arm64 (Homebrew LLVM 22); Windows: pynari matrix with `LLVM-C.dll` from the official LLVM 20.1.8 release staged next to the device (`MITSUBA_ANARI_LLVM_RUNTIME`); skips where LLVM absent |
| `metal_ad_rgb` | opt-in GPU, macOS/Apple Silicon (Dr.Jit Metal backend) | `metal.triangle_matte` — PASS on Apple M4; skips off-macOS |
