// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "Light.h"
// std
#include <algorithm>
#include <cmath>

namespace mitsuba_anari {

Light::Light(MitsubaGlobalState *s) : Object(ANARI_LIGHT, s) {}

Light *Light::createInstance(std::string_view subtype, MitsubaGlobalState *s)
{
  if (subtype == "directional")
    return new Directional(s);
  if (subtype == "point")
    return new Point(s);
  if (subtype == "spot")
    return new Spot(s);
  if (subtype == "quad")
    return new QuadLight(s);
  if (subtype == "hdri")
    return new HDRI(s);
  return (Light *)new UnknownObject(ANARI_LIGHT, subtype, s);
}

// Directional ////////////////////////////////////////////////////////////////

Directional::Directional(MitsubaGlobalState *s) : Light(s) {}

void Directional::commitParameters()
{
  m_color = getParam<float3>("color", float3(1.f, 1.f, 1.f));
  m_direction =
      linalg::normalize(getParam<float3>("direction", float3(0.f, 0.f, -1.f)));
  // ANARI directional lights are specified by irradiance (W/m^2 on a surface
  // perpendicular to the light); Mitsuba's directional emitter uses the same
  // convention, so the value maps through without conversion constants.
  m_irradiance = getParam<float>("irradiance", 1.f);
}

// Point //////////////////////////////////////////////////////////////////////

Point::Point(MitsubaGlobalState *s) : Light(s) {}

void Point::commitParameters()
{
  m_color = getParam<float3>("color", float3(1.f, 1.f, 1.f));
  m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
  // ANARI point lights specify radiant intensity (W/sr) or total power (W);
  // intensity takes precedence. power = 4*pi*intensity for an isotropic
  // source — the conversion is derived, not a magic constant.
  if (auto intensity = getParam<float>("intensity", -1.f); intensity >= 0.f) {
    m_intensity = intensity;
  } else if (auto power = getParam<float>("power", -1.f); power >= 0.f) {
    m_intensity = power / (4.f * 3.14159265358979323846f);
  } else {
    m_intensity = 1.f;
  }
}

// Spot ///////////////////////////////////////////////////////////////////////

Spot::Spot(MitsubaGlobalState *s) : Light(s) {}

void Spot::commitParameters()
{
  m_color = getParam<float3>("color", float3(1.f, 1.f, 1.f));
  m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
  m_direction =
      linalg::normalize(getParam<float3>("direction", float3(0.f, 0.f, -1.f)));
  m_openingAngle = std::clamp(
      getParam<float>("openingAngle", 3.14159265358979323846f),
      1e-4f,
      3.14159265358979323846f);
  m_falloffAngle = std::max(getParam<float>("falloffAngle", 0.1f), 0.f);
  // Intensity takes precedence; power is spread over the cone solid angle.
  if (auto intensity = getParam<float>("intensity", -1.f); intensity >= 0.f) {
    m_intensity = intensity;
  } else if (auto power = getParam<float>("power", -1.f); power >= 0.f) {
    const float solidAngle = 2.f * 3.14159265358979323846f
        * (1.f - std::cos(0.5f * m_openingAngle));
    m_intensity = solidAngle > 0.f ? power / solidAngle : 0.f;
  } else {
    m_intensity = 1.f;
  }
}

// QuadLight //////////////////////////////////////////////////////////////////

QuadLight::QuadLight(MitsubaGlobalState *s) : Light(s) {}

void QuadLight::commitParameters()
{
  m_color = getParam<float3>("color", float3(1.f, 1.f, 1.f));
  m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
  m_edge1 = getParam<float3>("edge1", float3(1.f, 0.f, 0.f));
  m_edge2 = getParam<float3>("edge2", float3(0.f, 1.f, 0.f));

  const float area =
      linalg::length(linalg::cross(m_edge1, m_edge2)); // parallelogram
  // Precedence: radiance > intensity > power. For a one-sided Lambertian
  // emitter of area A: peak intensity I = L*A and power P = pi*L*A — the
  // conversions below are derived from those identities.
  if (auto radiance = getParam<float>("radiance", -1.f); radiance >= 0.f) {
    m_radiance = radiance;
  } else if (auto intensity = getParam<float>("intensity", -1.f);
             intensity >= 0.f && area > 0.f) {
    m_radiance = intensity / area;
  } else if (auto power = getParam<float>("power", -1.f);
             power >= 0.f && area > 0.f) {
    m_radiance = power / (3.14159265358979323846f * area);
  } else {
    m_radiance = 1.f;
  }

  if (getParamString("side", "front") != "front") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] quad light 'side' other than 'front' is not supported yet "
        "(Mitsuba area emitters emit from the front face)");
  }
}

// HDRI ///////////////////////////////////////////////////////////////////////

HDRI::HDRI(MitsubaGlobalState *s) : Light(s), m_radiance(this) {}

void HDRI::commitParameters()
{
  m_color = getParam<float3>("color", float3(1.f, 1.f, 1.f));
  m_up = getParam<float3>("up", float3(0.f, 0.f, 1.f));
  m_direction =
      linalg::normalize(getParam<float3>("direction", float3(1.f, 0.f, 0.f)));
  m_scale = getParam<float>("scale", 1.f);
  m_visible = getParam<bool>("visible", true);
  m_radiance = getParamObject<Array2D>("radiance");
}

void HDRI::finalize()
{
  m_valid = false;
  if (!m_radiance) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] hdri light is missing required 'radiance'");
    return;
  }
  if (m_radiance->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] hdri 'radiance' must be ANARI_FLOAT32_VEC3");
    return;
  }
  m_valid = true;
}

bool HDRI::isValid() const
{
  return m_valid;
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Light *);
