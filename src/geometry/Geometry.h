// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#pragma once

#include "array/Array1D.h"
#include "core/Object.h"
#include "spatial_field/SpatialField.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"
// std
#include <optional>
#include <string_view>
#include <vector>

namespace mitsuba_anari {

// The ANARI geometry attributes materials and samplers can bind by name.
enum class AttributeId
{
  Color = 0,
  Attribute0,
  Attribute1,
  Attribute2,
  Attribute3,
  Count
};

// "color", "attribute0".."attribute3" -> id; false for anything else.
bool attributeIdFromName(std::string_view name, AttributeId &id);
const char *attributeName(AttributeId id);

// Whether an array element type can be read as an attribute value.
bool isAttributeElementType(ANARIDataType t);

// Element i of an attribute array as float4, missing components filled with
// the ANARI defaults (0, 0, 0, 1). Normalized fixed-point types map to
// [0, 1], sRGB types are linearized.
float4 readAttribute(const Array1D *array, uint64_t i);

struct Geometry : public Object
{
  Geometry(MitsubaGlobalState *s);
  ~Geometry() override = default;
  static Geometry *createInstance(
      std::string_view type, MitsubaGlobalState *state);

  void commitParameters() override;

  // Per-vertex / per-primitive attribute arrays ('vertex.color',
  // 'primitive.attribute0', ...), nullptr when absent or unusable.
  const Array1D *vertexAttribute(AttributeId id) const;
  const Array1D *primitiveAttribute(AttributeId id) const;
  // Geometry-wide constant attribute value ('color', 'attribute0', ...).
  std::optional<float4> uniformAttribute(AttributeId id) const;

  // Number of primitives / vertices the attribute arrays are indexed by
  // (valid after finalize()).
  virtual uint64_t numPrimitives() const { return 0; }
  virtual uint64_t numVertices() const { return 0; }

 protected:
  // Drops attribute arrays with unsupported element types or too few
  // elements (with a warning).
  void validateAttributes(const char *what);

 private:
  std::vector<helium::ChangeObserverPtr<Array1D>> m_vertexAttributes;
  std::vector<helium::ChangeObserverPtr<Array1D>> m_primitiveAttributes;
  std::optional<float4> m_uniformAttributes[size_t(AttributeId::Count)];
};

// Shared storage/validation for the mesh-based subtypes (triangle, quad).
struct MeshGeometry : public Geometry
{
  MeshGeometry(MitsubaGlobalState *s, uint32_t vertsPerPrimitive);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  uint64_t numPrimitives() const override;
  uint64_t numVertices() const override;

  uint32_t vertsPerPrimitive() const { return m_vertsPerPrim; }
  const Array1D *vertexPositions() const { return m_vertexPosition.get(); }
  const Array1D *vertexNormals() const { return m_vertexNormal.get(); }
  const Array1D *vertexColors() const
  {
    return vertexAttribute(AttributeId::Color);
  }
  const Array1D *vertexAttribute0() const
  {
    return vertexAttribute(AttributeId::Attribute0);
  }
  const Array1D *indices() const { return m_index.get(); }

 private:
  uint32_t m_vertsPerPrim{3};
  helium::ChangeObserverPtr<Array1D> m_index;
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexNormal;
  bool m_valid{false};
};

// ANARI 'triangle' geometry: indexed or soup triangle mesh.
struct Triangle : public MeshGeometry
{
  Triangle(MitsubaGlobalState *s) : MeshGeometry(s, 3) {}
};

// ANARI 'quad' geometry: indexed or soup quad mesh (triangulated on
// translation).
struct Quad : public MeshGeometry
{
  Quad(MitsubaGlobalState *s) : MeshGeometry(s, 4) {}
};

// ANARI 'curve' geometry: round linear segments. Approximated by cylinder
// segments with sphere joints (Mitsuba's native curve plugins are
// file-based); the per-segment radius uses the mean of the endpoint radii
// (documented approximation). With bound color attributes the curves are
// tessellated instead (cone frustums with sphere joints).
struct CurveGeometry : public Geometry
{
  CurveGeometry(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  uint64_t numPrimitives() const override;
  uint64_t numVertices() const override;

  const Array1D *positions() const { return m_vertexPosition.get(); }
  const Array1D *vertexRadii() const { return m_vertexRadius.get(); }
  const Array1D *indices() const { return m_index.get(); }
  float uniformRadius() const { return m_radius; }

 private:
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  helium::ChangeObserverPtr<Array1D> m_index;
  float m_radius{0.01f};
  bool m_valid{false};
};

// ANARI 'sphere' geometry: per-vertex centers with per-vertex or uniform
// radius.
struct SphereGeometry : public Geometry
{
  SphereGeometry(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  uint64_t numPrimitives() const override;
  uint64_t numVertices() const override;

  const Array1D *centers() const { return m_vertexPosition.get(); }
  const Array1D *vertexRadii() const { return m_vertexRadius.get(); }
  const Array1D *indices() const { return m_index.get(); }
  float uniformRadius() const { return m_radius; }

 private:
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  helium::ChangeObserverPtr<Array1D> m_index;
  float m_radius{0.01f};
  bool m_valid{false};
};

// ANARI 'cylinder' and 'cone' geometry: segments between two vertices
// (primitive.index, ANARI_UINT32_VEC2, or consecutive vertex pairs), open
// unless capped ('caps' / 'vertex.cap'). Cylinders have a per-primitive or
// uniform radius, cones a per-vertex radius.
struct ConeCylinderGeometry : public Geometry
{
  ConeCylinderGeometry(MitsubaGlobalState *s, bool isCone);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  uint64_t numPrimitives() const override;
  uint64_t numVertices() const override;

  bool isCone() const { return m_isCone; }
  const Array1D *positions() const { return m_vertexPosition.get(); }
  // Segment i -> its two vertex indices.
  uint2 segment(uint64_t i) const;
  // Radius at the given end (0/1) of segment i.
  float radius(uint64_t i, int end) const;
  // Whether the given end (0/1) of segment i is capped.
  bool capped(uint64_t i, int end) const;

 private:
  bool m_isCone{false};
  helium::ChangeObserverPtr<Array1D> m_vertexPosition;
  helium::ChangeObserverPtr<Array1D> m_vertexRadius;
  helium::ChangeObserverPtr<Array1D> m_primitiveRadius;
  helium::ChangeObserverPtr<Array1D> m_vertexCap;
  helium::ChangeObserverPtr<Array1D> m_index;
  float m_radius{1.f};
  std::string m_caps{"none"};
  bool m_valid{false};
};

// ANARI 'isosurface' geometry on a structuredRegular field, extracted into a
// triangle mesh (marching tetrahedra) at translation time.
struct IsosurfaceGeometry : public Geometry
{
  IsosurfaceGeometry(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;
  bool isValid() const override;

  const StructuredRegular *field() const;
  const std::vector<float> &isovalues() const { return m_isovalues; }

 private:
  helium::ChangeObserverPtr<SpatialField> m_field;
  helium::ChangeObserverPtr<Array1D> m_isovalueArray;
  std::vector<float> m_isovalues;
  bool m_valid{false};
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Geometry *, ANARI_GEOMETRY);
