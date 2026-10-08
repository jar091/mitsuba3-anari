// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#pragma once

#include "core/Object.h"

namespace mitsuba_anari {

struct Camera : public Object
{
  Camera(ANARIDataType subtype, MitsubaGlobalState *s);
  ~Camera() override = default;
  static Camera *createInstance(
      std::string_view type, MitsubaGlobalState *state);

  void commitParameters() override;

  // Common ANARI camera parameters (committed state).
  float3 position() const { return m_position; }
  float3 direction() const { return m_direction; }
  float3 up() const { return m_up; }

 protected:
  Camera(MitsubaGlobalState *s); // for subclasses

  float3 m_position{0.f, 0.f, 0.f};
  float3 m_direction{0.f, 0.f, -1.f};
  float3 m_up{0.f, 1.f, 0.f};
};

struct Perspective : public Camera
{
  Perspective(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  float fovy() const { return m_fovy; }
  float aspect() const { return m_aspect; }
  // Depth of field (KHR_CAMERA_DEPTH_OF_FIELD): thin lens when > 0.
  float apertureRadius() const { return m_apertureRadius; }
  float focusDistance() const { return m_focusDistance; }

 private:
  float m_fovy{0.f};
  float m_aspect{1.f};
  float m_apertureRadius{0.f};
  float m_focusDistance{1.f};
};

struct Orthographic : public Camera
{
  Orthographic(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  float height() const { return m_height; }
  float aspect() const { return m_aspect; }

 private:
  float m_height{1.f};
  float m_aspect{1.f};
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Camera *, ANARI_CAMERA);
