// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/Array1D.h"
#include "array/Array3D.h"
#include "core/Object.h"
// std
#include <vector>

namespace mitsuba_anari {

struct SpatialField : public Object
{
  SpatialField(MitsubaGlobalState *d);
  virtual ~SpatialField() = default;
  static SpatialField *createInstance(
      std::string_view subtype, MitsubaGlobalState *d);

  // Whether the field can be resampled for volume rendering.
  virtual bool isSampleable() const { return false; }
  // World-space (object-space) bounds of the field's domain.
  virtual box3 bounds() const { return box3(); }
  // Grid resolution that represents the field without loss of detail.
  virtual uint3 sampleResolution() const { return uint3(0u); }
  // Resamples the field at the voxel centers of a regular grid over
  // 'bounds' (x fastest). 'inside' marks voxels covered by the field.
  virtual void resample(const box3 & /*bounds*/,
      uint3 /*res*/,
      std::vector<float> & /*values*/,
      std::vector<uint8_t> & /*inside*/) const
  {}
};

// ANARI 'structuredRegular' field: data(i, j, k) at origin + (i, j, k) *
// spacing (vertex centered), converted to float.
struct StructuredRegular : public SpatialField
{
  StructuredRegular(MitsubaGlobalState *d);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  bool isSampleable() const override { return true; }
  box3 bounds() const override;
  uint3 sampleResolution() const override { return m_dims; }
  void resample(const box3 &bounds,
      uint3 res,
      std::vector<float> &values,
      std::vector<uint8_t> &inside) const override;

  uint3 dims() const { return m_dims; }
  float3 origin() const { return m_origin; }
  float3 spacing() const { return m_spacing; }
  // Voxel values converted to float, x fastest.
  const std::vector<float> &values() const { return m_values; }
  float maxValue() const { return m_maxValue; }
  // Trilinear interpolation at a world-space position (clamped to the grid).
  float sample(const float3 &p) const;

 private:
  helium::ChangeObserverPtr<Array3D> m_data;
  float3 m_origin{0.f, 0.f, 0.f};
  float3 m_spacing{1.f, 1.f, 1.f};
  uint3 m_dims{0u, 0u, 0u};
  std::vector<float> m_values;
  float m_maxValue{0.f};
};

// ANARI 'unstructured' field (tetrahedra, hexahedra, wedges, pyramids with
// vertex- or cell-centered scalars). Cells are split into tetrahedra and
// rasterized into a regular grid for volume rendering (linear interpolation
// inside each tetrahedron for vertex data).
struct Unstructured : public SpatialField
{
  Unstructured(MitsubaGlobalState *d);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  bool isSampleable() const override { return true; }
  box3 bounds() const override { return m_bounds; }
  uint3 sampleResolution() const override;
  void resample(const box3 &bounds,
      uint3 res,
      std::vector<float> &values,
      std::vector<uint8_t> &inside) const override;

 private:
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexData;
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_cellIndex;
  helium::ChangeObserverPtr<Array1D> m_cellType;
  helium::ChangeObserverPtr<Array1D> m_cellData;

  // Tetrahedra: 4 vertex indices and the owning cell each.
  std::vector<uint32_t> m_tets;
  std::vector<uint32_t> m_tetCell;
  std::vector<float3> m_positions;
  std::vector<float> m_vertexValues; // empty for cell-centered data
  std::vector<float> m_cellValues; // empty for vertex-centered data
  box3 m_bounds;
  size_t m_numCells{0};
  bool m_valid{false};
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(
    mitsuba_anari::SpatialField *, ANARI_SPATIAL_FIELD);
