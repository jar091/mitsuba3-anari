// mitsuba-anari: central include + variant type aliases for the embedded
// Mitsuba renderer. All Mitsuba includes are confined to mitsuba_backend/ and
// the scene translation layer — ANARI object headers must never include
// Mitsuba.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_MSC_VER) && defined(_DEBUG)
#error \
    "The mitsuba-anari device links the Release-built Mitsuba runtime; Debug \
device builds are not supported with MSVC yet (CRT/ABI mismatch). Use the \
windows-msvc-release or windows-msvc-relwithdebinfo preset (see BUILDING.md)."
#endif

#ifdef _MSC_VER
#pragma warning(push)
// Third-party headers are not held to the project warning level.
#pragma warning(disable : 4100 4127 4244 4251 4324 4458 4459 4702)
#endif

#include <mitsuba/core/argparser.h>
#include <mitsuba/core/bitmap.h>
#include <mitsuba/core/fwd.h>
#include <mitsuba/core/logger.h>
#include <mitsuba/core/object.h>
#include <mitsuba/core/plugin.h>
#include <mitsuba/core/properties.h>
#include <mitsuba/core/rfilter.h>
#include <mitsuba/core/spectrum.h>
#include <mitsuba/core/thread.h>
#include <mitsuba/core/transform.h>
#include <mitsuba/core/util.h>
#include <mitsuba/render/bsdf.h>
#include <mitsuba/render/emitter.h>
#include <mitsuba/render/film.h>
#include <mitsuba/render/integrator.h>
#include <mitsuba/render/mesh.h>
#include <mitsuba/render/sampler.h>
#include <mitsuba/render/scene.h>
#include <mitsuba/render/sensor.h>

#if defined(MI_ENABLE_JIT)
#include <drjit-core/jit.h>
#endif

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace mitsuba_anari {

// The variants this device can render with must match the variants compiled
// into the linked Mitsuba build (ADR 0004). scalar_rgb is the mandatory
// cross-platform baseline; JIT variants are selected at runtime through the
// vendor extension ANARI_MITSUBA_DEVICE_VARIANT ('mitsuba.variant' device
// parameter) and probed for availability before use.
namespace mi {

// Baseline scalar variant types (used by variant-independent helpers).
using ScalarFloat = float;
using ScalarSpectrum = mitsuba::Color<ScalarFloat, 3>;

inline constexpr const char *scalarVariantName = "scalar_rgb";
#if defined(MI_ENABLE_LLVM)
inline constexpr const char *llvmVariantName = "llvm_ad_rgb";
#endif
#if defined(MI_ENABLE_CUDA)
inline constexpr const char *cudaVariantName = "cuda_ad_rgb";
#endif
#if defined(MI_ENABLE_METAL)
inline constexpr const char *metalVariantName = "metal_ad_rgb";
#endif

} // namespace mi

} // namespace mitsuba_anari
