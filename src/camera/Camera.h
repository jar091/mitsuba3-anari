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

  // 'imageRegion': the part (x0, y0, x1, y1) of the image plane which the
  // frame shows, in normalized coordinates with the origin at the lower left.
  float4 imageRegion() const { return m_imageRegion; }
  bool hasFullImageRegion() const;

  // 'near'/'far': clip planes of the camera rays, perpendicular to the
  // camera direction (distances along the direction). Unset parameters keep
  // Mitsuba's defaults (near_clip 1e-2, far_clip 1e4).
  bool hasNearClip() const { return m_hasNear; }
  bool hasFarClip() const { return m_hasFar; }
  float nearClip() const { return m_near; }
  float farClip() const { return m_far; }

  // Mitsuba starts camera rays on the near clip plane and measures its depth
  // AOV from there. Returns the distance from the camera position to that
  // start for the camera ray through (u, v) in [0,1]^2 of the frame (origin
  // at the lower left), so depth stays the distance to the camera position.
  // 0 when 'near' is unset (depth as before the parameter existed).
  virtual float nearClipDistance(float u, float v, float frameAspect) const;

 protected:
  Camera(MitsubaGlobalState *s); // for subclasses

  // Checks the parameters shared by all subtypes; called from finalize().
  void finalizeCommon();

  float3 m_position{0.f, 0.f, 0.f};
  float3 m_direction{0.f, 0.f, -1.f};
  float3 m_up{0.f, 1.f, 0.f};
  float4 m_imageRegion{0.f, 0.f, 1.f, 1.f};
  bool m_hasNear{false};
  bool m_hasFar{false};
  float m_near{0.f};
  float m_far{0.f};
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

  float nearClipDistance(float u, float v, float frameAspect) const override;

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

  float nearClipDistance(float u, float v, float frameAspect) const override;

 private:
  float m_height{1.f};
  float m_aspect{1.f};
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Camera *, ANARI_CAMERA);
