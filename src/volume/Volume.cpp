// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#include "Volume.h"
// std
#include <algorithm>
#include <cmath>

namespace mitsuba_anari {

Volume::Volume(MitsubaGlobalState *s) : Object(ANARI_VOLUME, s) {}

Volume *Volume::createInstance(
    std::string_view subtype, MitsubaGlobalState *s)
{
  if (subtype == "transferFunction1D")
    return new TransferFunction1D(s);
  return (Volume *)new UnknownObject(ANARI_VOLUME, subtype, s);
}

void Volume::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
}

// TransferFunction1D /////////////////////////////////////////////////////////

TransferFunction1D::TransferFunction1D(MitsubaGlobalState *s)
    : Volume(s), m_field(this), m_colorArray(this), m_opacityArray(this)
{}

void TransferFunction1D::commitParameters()
{
  Volume::commitParameters();
  m_field = getParamObject<SpatialField>("value");
  if (!getParam("valueRange", ANARI_FLOAT32_BOX1, &m_valueRange))
    m_valueRange = getParam<float2>("valueRange", float2(0.f, 1.f));
  m_colorArray = getParamObject<Array1D>("color");
  m_color = getParam<float3>("color", float3(1.f, 1.f, 1.f));
  m_opacityArray = getParamObject<Array1D>("opacity");
  m_opacity = getParam<float>("opacity", 1.f);
  m_unitDistance = getParam<float>("unitDistance", 1.f);
}

void TransferFunction1D::finalize()
{
  m_colors.clear();
  m_opacities.clear();

  bool alphaFromColor = false;
  if (m_colorArray && m_colorArray->size() > 0) {
    const ANARIDataType type = m_colorArray->elementType();
    if (type == ANARI_FLOAT32_VEC3 || type == ANARI_FLOAT32_VEC4) {
      const int stride = type == ANARI_FLOAT32_VEC4 ? 4 : 3;
      const auto *data = (const float *)m_colorArray->data();
      const size_t n = m_colorArray->size();
      m_colors.resize(n);
      for (size_t i = 0; i < n; ++i)
        m_colors[i] = float3(
            data[i * stride], data[i * stride + 1], data[i * stride + 2]);
      // Without an opacity array, the alpha of RGBA colors is the opacity
      // (ANARI spec).
      if (type == ANARI_FLOAT32_VEC4 && !m_opacityArray) {
        alphaFromColor = true;
        m_opacities.resize(n);
        for (size_t i = 0; i < n; ++i)
          m_opacities[i] = data[i * 4 + 3];
      }
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] transferFunction1D 'color' array must be "
          "FLOAT32_VEC3/VEC4; using the constant color");
    }
  }
  if (m_colors.empty())
    m_colors.push_back(m_color);

  if (!alphaFromColor) {
    if (m_opacityArray && m_opacityArray->size() > 0) {
      if (m_opacityArray->elementType() == ANARI_FLOAT32) {
        const auto *data = (const float *)m_opacityArray->data();
        m_opacities.assign(data, data + m_opacityArray->size());
      } else {
        reportMessage(ANARI_SEVERITY_WARNING,
            "[mitsuba] transferFunction1D 'opacity' array must be FLOAT32; "
            "using the constant opacity");
      }
    }
    if (m_opacities.empty())
      m_opacities.push_back(m_opacity);
  }

  if (!(m_unitDistance > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] transferFunction1D 'unitDistance' must be positive; using "
        "1");
    m_unitDistance = 1.f;
  }

  if (m_field && !m_field->isSampleable()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] transferFunction1D 'value' field type is not supported; "
        "the volume is not rendered");
  }
}

bool TransferFunction1D::isValid() const
{
  return m_field && m_field->isValid() && m_field->isSampleable();
}

const StructuredRegular *TransferFunction1D::field() const
{
  return dynamic_cast<const StructuredRegular *>(m_field.get());
}

template <typename T>
static T lookupLinear(const std::vector<T> &table, float t)
{
  if (table.size() == 1)
    return table[0];
  const float x = std::clamp(t, 0.f, 1.f) * float(table.size() - 1);
  const size_t i = std::min(size_t(x), table.size() - 2);
  const float a = x - float(i);
  return table[i] * (1.f - a) + table[i + 1] * a;
}

float4 TransferFunction1D::classify(float value) const
{
  const float range = m_valueRange.y - m_valueRange.x;
  const float t = range != 0.f ? (value - m_valueRange.x) / range : 0.f;
  const float3 c = lookupLinear(m_colors, t);
  const float o = lookupLinear(m_opacities, t);
  return float4(c, std::clamp(o, 0.f, 1.f));
}

bool TransferFunction1D::classifyGrid(ClassifiedVolume &out) const
{
  const SpatialField *f = m_field.get();
  if (!f || !f->isValid() || !f->isSampleable())
    return false;

  const box3 bounds = f->bounds();
  const float3 extent = bounds.upper - bounds.lower;
  if (!(extent.x > 0.f) || !(extent.y > 0.f) || !(extent.z > 0.f))
    return false;

  // Resolution: at least the field's own, at least ~64 cells along the
  // longest axis (coarse fields are upsampled so the transfer function sees
  // interpolated values), at most ~16M voxels.
  const uint3 native = f->sampleResolution();
  const float maxExtent = std::max({extent.x, extent.y, extent.z});
  float3 res;
  for (int a = 0; a < 3; ++a) {
    const float minCells = std::ceil(64.f * extent[a] / maxExtent);
    res[a] = std::max({float(native[a]), minCells, 2.f});
  }
  const double voxels = double(res.x) * res.y * res.z;
  const double maxVoxels = 16.0 * 1024 * 1024;
  if (voxels > maxVoxels) {
    const float s = float(std::cbrt(maxVoxels / voxels));
    for (int a = 0; a < 3; ++a)
      res[a] = std::max(2.f, std::floor(res[a] * s));
  }
  out.resolution = uint3(uint32_t(res.x), uint32_t(res.y), uint32_t(res.z));
  out.lower = bounds.lower;
  out.upper = bounds.upper;

  const size_t count =
      size_t(out.resolution.x) * out.resolution.y * out.resolution.z;
  out.sigmaT.assign(count, 0.f);
  out.albedo.assign(count * 3, 0.f);
  out.maxSigmaT = 0.f;
  const float invUnit = 1.f / m_unitDistance;

  std::vector<float> values;
  std::vector<uint8_t> inside;
  f->resample(bounds, out.resolution, values, inside);
  for (size_t i = 0; i < count; ++i) {
    if (!inside[i])
      continue;
    // The albedo is set even where the opacity vanishes, so interpolation
    // towards empty space keeps the transfer function's color.
    const float4 c = classify(values[i]);
    const float sigma = c.w * invUnit;
    out.sigmaT[i] = sigma;
    out.albedo[i * 3 + 0] = std::clamp(c.x, 0.f, 1.f);
    out.albedo[i * 3 + 1] = std::clamp(c.y, 0.f, 1.f);
    out.albedo[i * 3 + 2] = std::clamp(c.z, 0.f, 1.f);
    out.maxSigmaT = std::max(out.maxSigmaT, sigma);
  }
  return out.maxSigmaT > 0.f;
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Volume *);
