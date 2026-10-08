// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/Array1D.h"
#include "core/Object.h"
#include "spatial_field/SpatialField.h"
// std
#include <vector>

namespace mitsuba_anari {

struct Volume : public Object
{
  Volume(MitsubaGlobalState *d);
  virtual ~Volume() = default;
  static Volume *createInstance(
      std::string_view subtype, MitsubaGlobalState *d);

  void commitParameters() override;

  uint32_t id() const;

 private:
  uint32_t m_id{~0u};
};

// A transfer-function classified volume on a regular, cell-centered grid
// covering 'lower'..'upper' (x fastest): extinction per world unit and RGB
// single-scattering albedo, ready for Mitsuba 'gridvolume's.
struct ClassifiedVolume
{
  uint3 resolution{0u, 0u, 0u};
  float3 lower{0.f};
  float3 upper{0.f};
  std::vector<float> sigmaT;
  std::vector<float> albedo; // 3 floats per voxel
  float maxSigmaT{0.f};
};

// ANARI 'transferFunction1D' volume -> Mitsuba 'heterogeneous' medium.
//
// The field is resampled (upsampled for coarse fields, so the transfer
// function is applied to interpolated values as in the other ANARI devices)
// and classified per voxel: extinction = opacity / unitDistance, albedo =
// color. Colors and opacities are piecewise linear over 'valueRange'; without
// an 'opacity' array the opacity comes from the alpha of a FLOAT32_VEC4
// 'color' array (or the constant 'opacity').
struct TransferFunction1D : public Volume
{
  TransferFunction1D(MitsubaGlobalState *d);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  const SpatialField *spatialField() const { return m_field.get(); }
  const StructuredRegular *field() const;
  float2 valueRange() const { return m_valueRange; }

  // Color (rgb) and opacity (w) for a field value.
  float4 classify(float value) const;
  // Resamples and classifies the field; false if there is nothing to render.
  bool classifyGrid(ClassifiedVolume &out) const;

 private:
  helium::ChangeObserverPtr<SpatialField> m_field;
  helium::ChangeObserverPtr<Array1D> m_colorArray;
  helium::ChangeObserverPtr<Array1D> m_opacityArray;
  float2 m_valueRange{0.f, 1.f};
  float3 m_color{1.f, 1.f, 1.f};
  float m_opacity{1.f};
  float m_unitDistance{1.f};
  std::vector<float3> m_colors;
  std::vector<float> m_opacities;
};

inline uint32_t Volume::id() const
{
  return m_id;
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Volume *, ANARI_VOLUME);
