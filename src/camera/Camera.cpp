// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "Camera.h"
// std
#include <algorithm>
#include <cmath>

namespace mitsuba_anari {

Camera::Camera(MitsubaGlobalState *s) : Object(ANARI_CAMERA, s) {}

Camera::Camera(ANARIDataType subtype, MitsubaGlobalState *s) : Object(subtype, s)
{}

void Camera::commitParameters()
{
  m_position = getParam<float3>("position", float3(0.f, 0.f, 0.f));
  m_direction =
      linalg::normalize(getParam<float3>("direction", float3(0.f, 0.f, -1.f)));
  m_up = linalg::normalize(getParam<float3>("up", float3(0.f, 1.f, 0.f)));

  m_imageRegion = float4(0.f, 0.f, 1.f, 1.f);
  getParam("imageRegion", ANARI_FLOAT32_BOX2, &m_imageRegion);
  m_hasNear = getParam("near", ANARI_FLOAT32, &m_near);
  m_hasFar = getParam("far", ANARI_FLOAT32, &m_far);
}

bool Camera::hasFullImageRegion() const
{
  return m_imageRegion.x == 0.f && m_imageRegion.y == 0.f
      && m_imageRegion.z == 1.f && m_imageRegion.w == 1.f;
}

float Camera::nearClipDistance(float, float, float) const
{
  return 0.f;
}

void Camera::finalizeCommon()
{
  if (!(m_imageRegion.z > m_imageRegion.x)
      || !(m_imageRegion.w > m_imageRegion.y)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] camera 'imageRegion' is empty; using the full image");
    m_imageRegion = float4(0.f, 0.f, 1.f, 1.f);
  }

  // near <= 0 and an infinite far mean "no clipping": Mitsuba needs a
  // positive near and a finite far distance, so its default near plane and a
  // very far plane stand in for them.
  if (m_hasNear && !(m_near > 0.f))
    m_hasNear = false;
  if (m_hasFar && !(m_far < 1e30f))
    m_far = 1e30f;
  const float nearClip = m_hasNear ? m_near : 1e-2f; // Mitsuba's defaults
  const float farClip = m_hasFar ? m_far : 1e4f;
  if (!(nearClip < farClip)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] camera 'near' must be smaller than 'far'; ignoring both");
    m_hasNear = false;
    m_hasFar = false;
  }
}

Camera *Camera::createInstance(
    std::string_view type, MitsubaGlobalState *s)
{
  // "default" (advertised through KHR_CAMERA_DEPTH_OF_FIELD) is the
  // perspective camera.
  if (type == "perspective" || type == "default")
    return new Perspective(s);
  if (type == "orthographic")
    return new Orthographic(s);
  return (Camera *)new UnknownObject(ANARI_CAMERA, type, s);
}

// Perspective ////////////////////////////////////////////////////////////////

Perspective::Perspective(MitsubaGlobalState *s) : Camera(s) {}

void Perspective::commitParameters()
{
  Camera::commitParameters();
  // ANARI: fovy in radians (default 60 degrees), aspect = width/height.
  m_fovy = getParam<float>("fovy", 1.0471975512f);
  m_aspect = getParam<float>("aspect", 1.f);
  m_apertureRadius = getParam<float>("apertureRadius", 0.f);
  m_focusDistance = getParam<float>("focusDistance", 1.f);
}

float Perspective::nearClipDistance(float u, float v, float frameAspect) const
{
  if (!m_hasNear)
    return 0.f;
  // Point of the image plane at distance 1 (pixels are square: the horizontal
  // extent follows from the frame, see buildSensor()).
  const float4 &r = m_imageRegion;
  const float halfH = std::tan(0.5f * m_fovy);
  const float halfW = halfH * (r.w - r.y) * frameAspect / (r.z - r.x);
  const float px = (2.f * (r.x + u * (r.z - r.x)) - 1.f) * halfW;
  const float py = (2.f * (r.y + v * (r.w - r.y)) - 1.f) * halfH;
  return m_near * std::sqrt(1.f + px * px + py * py);
}

void Perspective::finalize()
{
  finalizeCommon();
  if (m_apertureRadius < 0.f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] perspective camera 'apertureRadius' must not be negative; "
        "using 0");
    m_apertureRadius = 0.f;
  }
  if (!(m_focusDistance > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] perspective camera 'focusDistance' must be positive; "
        "using 1");
    m_focusDistance = 1.f;
  }
  if (!(m_fovy > 0.f) || m_fovy >= 3.14159265f) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] perspective camera 'fovy' out of range; clamping");
    m_fovy = std::min(std::max(m_fovy, 0.01f), 3.13f);
  }
  if (!(m_aspect > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] perspective camera 'aspect' must be positive; using 1");
    m_aspect = 1.f;
  }
}

// Orthographic ///////////////////////////////////////////////////////////////

Orthographic::Orthographic(MitsubaGlobalState *s) : Camera(s) {}

void Orthographic::commitParameters()
{
  Camera::commitParameters();
  // ANARI: 'height' is the vertical extent of the view volume in world units.
  m_height = getParam<float>("height", 1.f);
  m_aspect = getParam<float>("aspect", 1.f);
}

float Orthographic::nearClipDistance(float, float, float) const
{
  return m_hasNear ? m_near : 0.f;
}

void Orthographic::finalize()
{
  finalizeCommon();
  if (!(m_height > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] orthographic camera 'height' must be positive; using 1");
    m_height = 1.f;
  }
  if (!(m_aspect > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] orthographic camera 'aspect' must be positive; using 1");
    m_aspect = 1.f;
  }
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Camera *);
