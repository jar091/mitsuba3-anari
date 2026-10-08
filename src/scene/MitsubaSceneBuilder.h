// mitsuba-anari: translation of committed ANARI scene state into a renderable
// Mitsuba scene, templated over the Mitsuba variant. This is the only place
// (besides the backend) that touches Mitsuba types — ANARI objects stay
// renderer-agnostic (ADR 0003/0004).
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "core/Math.h"
#include "mitsuba_backend/MitsubaTypes.h"

#include <string>

namespace mitsuba_anari {

struct World;
struct Camera;
struct Renderer;
struct MitsubaGlobalState;

struct RenderStats {
  size_t shapeCount{0};
  size_t emitterCount{0};
  size_t cacheHits{0};
  size_t cacheMisses{0};
  bool sceneReused{false};
  double translateMs{0.0};
  double renderMs{0.0};
};

// Builds the Mitsuba scene from committed ANARI state with the given variant,
// renders it, and returns the developed image as an RGBA float32 host Bitmap
// (top row first — callers flip to the ANARI bottom-left origin).
//
// Throws (std::exception-derived) on failure — callers catch at the ANARI
// boundary and route to status callbacks.
//
// Milestone 3/4 status (documented early-milestone allowance, master prompt
// §9): the scene is rebuilt per render call; incremental updates via the
// object cache land in Milestone 11. Timings are returned in `stats`.
// When `wantDepth` is set, `depthOut` receives a single-channel float32
// Bitmap with the primary-hit ray distance (Mitsuba 'aov' integrator,
// ADR 0005); background pixels hold 0 after film normalization.
//
// Translated Mitsuba objects (BSDFs, shapes, emitters, shape groups and the
// scene itself) are cached in `state->translationCache` keyed by the source
// ANARI objects and their helium timestamps (Milestone 11): unchanged state
// reuses the previous Mitsuba scene; value changes rebuild only the affected
// objects. Instance transforms use Mitsuba-native shapegroup/instance
// plugins, so geometry is shared across instances in object space.
mitsuba::ref<mitsuba::Bitmap> renderMitsubaScene(const std::string &variant,
    MitsubaGlobalState *state,
    const World &world,
    const Camera &camera,
    const Renderer &renderer,
    uint2 frameSize,
    bool wantDepth,
    mitsuba::ref<mitsuba::Bitmap> &depthOut,
    RenderStats &stats,
    uint32_t seed = 0);

} // namespace mitsuba_anari
