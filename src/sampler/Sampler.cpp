// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "Sampler.h"

#include <anari/frontend/type_utility.h>
// std
#include <algorithm>
#include <cmath>

namespace mitsuba_anari {

Sampler::Sampler(MitsubaGlobalState *s) : Object(ANARI_SAMPLER, s) {}

Sampler *Sampler::createInstance(
    std::string_view subtype, MitsubaGlobalState *s)
{
  if (subtype == "image1D")
    return new Image1D(s);
  if (subtype == "image2D")
    return new Image2D(s);
  if (subtype == "image3D" || subtype == "primitive"
      || subtype == "transform")
    return new UnsupportedSampler(s, subtype);
  return (Sampler *)new UnknownObject(ANARI_SAMPLER, subtype, s);
}

// ImageSampler ///////////////////////////////////////////////////////////////

static float srgbToLinear(float c)
{
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

ImageSampler::ImageSampler(MitsubaGlobalState *s) : Sampler(s) {}

void ImageSampler::commitParameters()
{
  m_inAttribute = getParamString("inAttribute", "attribute0");
  m_filter = getParamString("filter", "linear");
  // 'wrapMode' is the image1D name; accepted as the default for image2D's
  // per-axis modes as well (some applications set it).
  const std::string wrap = getParamString("wrapMode", "clampToEdge");
  m_wrapMode[0] = getParamString("wrapMode1", wrap);
  m_wrapMode[1] = getParamString("wrapMode2", wrap);
  m_inTransform = getParam<mat4>("inTransform", mat4(linalg::identity));
  m_inOffset = getParam<float4>("inOffset", float4(0.f));
  m_outTransform = getParam<mat4>("outTransform", mat4(linalg::identity));
  m_outOffset = getParam<float4>("outOffset", float4(0.f));
}

bool ImageSampler::isValid() const
{
  return m_valid;
}

float4 ImageSampler::transformInput(const float4 &a) const
{
  return linalg::mul(m_inTransform, a) + m_inOffset;
}

float4 ImageSampler::transformOutput(const float4 &t) const
{
  return linalg::mul(m_outTransform, t) + m_outOffset;
}

bool ImageSampler::hasOutputTransform() const
{
  return m_outTransform != mat4(linalg::identity)
      || m_outOffset != float4(0.f);
}

bool ImageSampler::loadTexels(
    const void *data, ANARIDataType type, size_t count)
{
  m_texels.assign(count, float4(0.f, 0.f, 0.f, 1.f));
  const int n = int(anari::componentsOf(type));
  bool srgb = false;
  switch (type) {
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
    srgb = true;
    [[fallthrough]];
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4: {
    const auto *src = (const uint8_t *)data;
    for (size_t i = 0; i < count; ++i) {
      for (int c = 0; c < n; ++c) {
        float v = src[i * n + c] / 255.f;
        const bool alpha = (n == 2 && c == 1) || c == 3;
        if (srgb && !alpha)
          v = srgbToLinear(v);
        m_texels[i][c] = v;
      }
    }
    break;
  }
  case ANARI_UFIXED16:
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED16_VEC4: {
    const auto *src = (const uint16_t *)data;
    for (size_t i = 0; i < count; ++i)
      for (int c = 0; c < n; ++c)
        m_texels[i][c] = src[i * n + c] / 65535.f;
    break;
  }
  case ANARI_FLOAT32:
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4: {
    const auto *src = (const float *)data;
    for (size_t i = 0; i < count; ++i)
      for (int c = 0; c < n; ++c)
        m_texels[i][c] = src[i * n + c];
    break;
  }
  default:
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] sampler 'image' element type %s is not supported "
        "(supported: FLOAT32[_VEC2/3/4], UFIXED8[_VEC2/3/4], UFIXED16[...], "
        "UFIXED8_*_SRGB)",
        anari::toString(type));
    m_texels.clear();
    return false;
  }
  // Single- and two-channel images replicate their first channel into RGB
  // (luminance), keeping alpha in the last component.
  if (n <= 2) {
    for (auto &t : m_texels) {
      const float a = n == 2 ? t.y : 1.f;
      t = float4(t.x, t.x, t.x, a);
    }
  }
  return true;
}

int ImageSampler::wrapIndex(int i, int n, int axis) const
{
  const std::string &mode = m_wrapMode[axis];
  if (mode == "repeat") {
    i %= n;
    return i < 0 ? i + n : i;
  }
  if (mode == "mirrorRepeat") {
    const int period = 2 * n;
    i %= period;
    if (i < 0)
      i += period;
    return i < n ? i : period - 1 - i;
  }
  return std::clamp(i, 0, n - 1); // clampToEdge (and clampToBorder)
}

float4 ImageSampler::fetch(int x, int y) const
{
  const int w = int(m_resolution.x);
  const int h = int(m_resolution.y);
  x = wrapIndex(x, w, 0);
  y = h > 1 ? wrapIndex(y, h, 1) : 0;
  return m_texels[size_t(y) * w + x];
}

float4 ImageSampler::evaluate(const float4 &attribute) const
{
  if (m_texels.empty())
    return float4(0.f, 0.f, 0.f, 1.f);
  const float4 tc = transformInput(attribute);
  const float fx = tc.x * m_resolution.x;
  const float fy = m_resolution.y > 1 ? tc.y * m_resolution.y : 0.5f;
  float4 texel;
  if (nearestFilter()) {
    texel = fetch(int(std::floor(fx)), int(std::floor(fy)));
  } else {
    // Bilinear between texel centers.
    const float x = fx - 0.5f;
    const float y = fy - 0.5f;
    const int x0 = int(std::floor(x));
    const int y0 = int(std::floor(y));
    const float ax = x - x0;
    const float ay = y - y0;
    const float4 t00 = fetch(x0, y0);
    const float4 t10 = fetch(x0 + 1, y0);
    const float4 t01 = fetch(x0, y0 + 1);
    const float4 t11 = fetch(x0 + 1, y0 + 1);
    texel = (t00 * (1.f - ax) + t10 * ax) * (1.f - ay)
        + (t01 * (1.f - ax) + t11 * ax) * ay;
  }
  return transformOutput(texel);
}

// Image1D ////////////////////////////////////////////////////////////////////

Image1D::Image1D(MitsubaGlobalState *s) : ImageSampler(s), m_image(this) {}

void Image1D::commitParameters()
{
  ImageSampler::commitParameters();
  m_image = getParamObject<Array1D>("image");
}

void Image1D::finalize()
{
  m_valid = false;
  m_texels.clear();
  if (!m_image || m_image->size() == 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] image1D sampler is missing required 'image'");
    return;
  }
  if (!loadTexels(m_image->data(), m_image->elementType(), m_image->size()))
    return;
  m_resolution = uint3(uint32_t(m_image->size()), 1u, 1u);
  m_valid = true;
}

// Image2D ////////////////////////////////////////////////////////////////////

Image2D::Image2D(MitsubaGlobalState *s) : ImageSampler(s), m_image(this) {}

void Image2D::commitParameters()
{
  ImageSampler::commitParameters();
  m_image = getParamObject<Array2D>("image");
}

void Image2D::finalize()
{
  m_valid = false;
  m_texels.clear();

  if (!m_image) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] image2D sampler is missing required 'image'");
    return;
  }
  const size_t w = m_image->size().x;
  const size_t h = m_image->size().y;
  if (w == 0 || h == 0) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "[mitsuba] image2D sampler 'image' is empty");
    return;
  }
  if (!loadTexels(m_image->data(), m_image->elementType(), w * h))
    return;
  m_resolution = uint3(uint32_t(w), uint32_t(h), 1u);

  if (wrapMode(0) != wrapMode(1)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] image2D wrapMode1 != wrapMode2 is not supported by the "
        "Mitsuba bitmap texture; wrapMode1 is used for both axes");
  }

  m_valid = true;
}

// UnsupportedSampler /////////////////////////////////////////////////////////

UnsupportedSampler::UnsupportedSampler(
    MitsubaGlobalState *s, std::string_view subtype)
    : Sampler(s), m_subtype(subtype)
{
  reportMessage(ANARI_SEVERITY_WARNING,
      "[mitsuba] '%s' samplers are not supported; materials using one fall "
      "back to their constant value",
      m_subtype.c_str());
}

bool UnsupportedSampler::isValid() const
{
  return false;
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Sampler *);
