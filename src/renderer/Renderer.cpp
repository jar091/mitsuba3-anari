// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "Renderer.h"

// std
#include <algorithm>
#include <cmath>

namespace mitsuba_anari {

Renderer::Renderer(MitsubaGlobalState *s)
    : Object(ANARI_RENDERER, s), m_backgroundImage(this)
{}

Renderer *Renderer::createInstance(
    std::string_view subtype, MitsubaGlobalState *s)
{
  // Only "default" exists; unknown subtypes yield an invalid object so that
  // nothing unknown ever silently participates in rendering.
  if (subtype == "default")
    return new Renderer(s);
  return (Renderer *)new UnknownObject(ANARI_RENDERER, subtype, s);
}

void Renderer::commitParameters()
{
  m_backgroundImage = getParamObject<Array2D>("background");
  m_background = getParam<float4>("background", float4(float3(0.f), 1.f));
  m_pixelSamples = std::max(1, getParam<int>("pixelSamples", 16));
  m_ambientColor = getParam<float3>("ambientColor", float3(1.f, 1.f, 1.f));
  m_ambientRadiance = std::max(0.f, getParam<float>("ambientRadiance", 0.f));
  m_maxRayDepth = std::max(1, getParam<int>("maxRayDepth", 5));
}

static float srgbToLinear(float c)
{
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

void Renderer::finalize()
{
  m_backgroundTexels.clear();
  m_backgroundSize = uint2(0u, 0u);
  if (!m_backgroundImage)
    return;
  const uint32_t w = uint32_t(m_backgroundImage->size().x);
  const uint32_t h = uint32_t(m_backgroundImage->size().y);
  const ANARIDataType t = m_backgroundImage->elementType();
  const size_t n = size_t(w) * h;
  if (n == 0)
    return;
  std::vector<float4> texels(n, float4(0.f, 0.f, 0.f, 1.f));
  const void *data = m_backgroundImage->data();
  switch (t) {
  case ANARI_FLOAT32_VEC4:
    for (size_t i = 0; i < n; ++i)
      texels[i] = ((const float4 *)data)[i];
    break;
  case ANARI_FLOAT32_VEC3:
    for (size_t i = 0; i < n; ++i)
      texels[i] = float4(((const float3 *)data)[i], 1.f);
    break;
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_RGBA_SRGB:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_RGB_SRGB: {
    const int c = (t == ANARI_UFIXED8_VEC4 || t == ANARI_UFIXED8_RGBA_SRGB)
        ? 4
        : 3;
    const bool srgb =
        t == ANARI_UFIXED8_RGBA_SRGB || t == ANARI_UFIXED8_RGB_SRGB;
    const auto *src = (const uint8_t *)data;
    for (size_t i = 0; i < n; ++i) {
      float4 v(0.f, 0.f, 0.f, 1.f);
      for (int k = 0; k < c; ++k) {
        v[k] = src[i * c + k] / 255.f;
        if (srgb && k < 3)
          v[k] = srgbToLinear(v[k]);
      }
      texels[i] = v;
    }
    break;
  }
  default:
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] renderer 'background' image element type is not supported "
        "(supported: FLOAT32_VEC3/4, UFIXED8_VEC3/4, UFIXED8_RGB(A)_SRGB); "
        "using the background color");
    return;
  }
  m_backgroundTexels = std::move(texels);
  m_backgroundSize = uint2(w, h);
}

float4 Renderer::backgroundAt(float u, float v) const
{
  if (m_backgroundTexels.empty())
    return m_background;
  const int w = int(m_backgroundSize.x);
  const int h = int(m_backgroundSize.y);
  const float x = u * w - 0.5f;
  const float y = v * h - 0.5f;
  const int x0 = int(std::floor(x));
  const int y0 = int(std::floor(y));
  const float ax = x - x0;
  const float ay = y - y0;
  auto at = [&](int i, int j) {
    i = std::clamp(i, 0, w - 1);
    j = std::clamp(j, 0, h - 1);
    return m_backgroundTexels[size_t(j) * w + i];
  };
  return (at(x0, y0) * (1.f - ax) + at(x0 + 1, y0) * ax) * (1.f - ay)
      + (at(x0, y0 + 1) * (1.f - ax) + at(x0 + 1, y0 + 1) * ax) * ay;
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Renderer *);
