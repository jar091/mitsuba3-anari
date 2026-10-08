// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#pragma once

#include "array/Array2D.h"
#include "core/Object.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"

namespace mitsuba_anari {

struct Light : public Object
{
  Light(MitsubaGlobalState *s);
  ~Light() override = default;
  static Light *createInstance(
      std::string_view subtype, MitsubaGlobalState *state);

  float3 color() const { return m_color; }

 protected:
  float3 m_color{1.f, 1.f, 1.f};
};

// ANARI 'directional' light -> Mitsuba 'directional' emitter.
struct Directional : public Light
{
  Directional(MitsubaGlobalState *s);

  void commitParameters() override;

  float3 direction() const { return m_direction; }
  float irradiance() const { return m_irradiance; }

 private:
  float3 m_direction{0.f, 0.f, -1.f};
  float m_irradiance{1.f};
};

// ANARI 'point' light -> Mitsuba 'point' emitter.
struct Point : public Light
{
  Point(MitsubaGlobalState *s);

  void commitParameters() override;

  float3 position() const { return m_position; }
  // radiant intensity (W/sr), after power->intensity conversion if needed
  float intensity() const { return m_intensity; }

 private:
  float3 m_position{0.f, 0.f, 0.f};
  float m_intensity{1.f};
};

// ANARI 'spot' light -> Mitsuba 'spot' emitter.
struct Spot : public Light
{
  Spot(MitsubaGlobalState *s);

  void commitParameters() override;

  float3 position() const { return m_position; }
  float3 direction() const { return m_direction; }
  // full cone opening angle and falloff region size, in radians
  float openingAngle() const { return m_openingAngle; }
  float falloffAngle() const { return m_falloffAngle; }
  // radiant intensity along the axis (W/sr)
  float intensity() const { return m_intensity; }

 private:
  float3 m_position{0.f, 0.f, 0.f};
  float3 m_direction{0.f, 0.f, -1.f};
  float m_openingAngle{3.14159265358979323846f};
  float m_falloffAngle{0.1f};
  float m_intensity{1.f};
};

// ANARI 'quad' light -> Mitsuba 'rectangle' shape with an 'area' emitter.
struct QuadLight : public Light
{
  QuadLight(MitsubaGlobalState *s);

  void commitParameters() override;

  float3 position() const { return m_position; }
  float3 edge1() const { return m_edge1; }
  float3 edge2() const { return m_edge2; }
  // emitted radiance (W/sr/m^2), after intensity/power conversion if needed
  float radiance() const { return m_radiance; }

 private:
  float3 m_position{0.f, 0.f, 0.f};
  float3 m_edge1{1.f, 0.f, 0.f};
  float3 m_edge2{0.f, 1.f, 0.f};
  float m_radiance{1.f};
};

// ANARI 'hdri' light -> Mitsuba 'envmap' emitter.
struct HDRI : public Light
{
  HDRI(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  float3 up() const { return m_up; }
  float3 direction() const { return m_direction; }
  float scale() const { return m_scale; }
  bool visible() const { return m_visible; }
  const Array2D *radiance() const { return m_radiance.get(); }

 private:
  float3 m_up{0.f, 0.f, 1.f};
  float3 m_direction{1.f, 0.f, 0.f};
  float m_scale{1.f};
  bool m_visible{true};
  helium::ChangeObserverPtr<Array2D> m_radiance;
  bool m_valid{false};
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Light *, ANARI_LIGHT);
