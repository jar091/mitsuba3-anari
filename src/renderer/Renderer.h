// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#pragma once

#include "array/Array2D.h"
#include "core/Object.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"

// std
#include <limits>
#include <vector>

namespace mitsuba_anari {

// Data associated with one produced pixel (used by Frame extraction).
struct PixelSample
{
  float4 color{0.f, 0.f, 0.f, 1.f};
  float depth{std::numeric_limits<float>::max()};

  PixelSample(float4 c) : color(c) {}
};

// The 'default' renderer: maps to a Mitsuba path-tracing configuration.
struct Renderer : public Object
{
  Renderer(MitsubaGlobalState *s);
  ~Renderer() override = default;

  static Renderer *createInstance(
      std::string_view subtype, MitsubaGlobalState *d);

  void commitParameters() override;

  void finalize() override;

  float4 background() const { return m_background; }
  // Background color at normalized frame coordinates (0,0 = bottom left):
  // the constant color, or the 'background' image (ARRAY2D, stretched over
  // the frame, bilinear between texel centers).
  float4 backgroundAt(float u, float v) const;
  bool hasBackgroundImage() const { return !m_backgroundTexels.empty(); }
  int pixelSamples() const { return m_pixelSamples; }
  float3 ambientColor() const { return m_ambientColor; }
  float ambientRadiance() const { return m_ambientRadiance; }
  int maxRayDepth() const { return m_maxRayDepth; }

 private:
  helium::ChangeObserverPtr<Array2D> m_backgroundImage;
  std::vector<float4> m_backgroundTexels;
  uint2 m_backgroundSize{0u, 0u};
  float4 m_background{0.f, 0.f, 0.f, 1.f};
  int m_pixelSamples{16};
  float3 m_ambientColor{1.f, 1.f, 1.f};
  float m_ambientRadiance{0.f};
  int m_maxRayDepth{5};
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Renderer *, ANARI_RENDERER);
