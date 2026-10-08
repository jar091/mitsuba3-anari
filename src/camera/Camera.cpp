// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "Camera.h"
// std
#include <algorithm>

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

void Perspective::finalize()
{
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

void Orthographic::finalize()
{
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
