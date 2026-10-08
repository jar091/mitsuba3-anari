// mitsuba-anari: CPU tessellation of ANARI geometry Mitsuba has no matching
// shape for (cones, colored cylinders/curves, isosurfaces).
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "core/Math.h"
// std
#include <cstdint>
#include <vector>

namespace mitsuba_anari {

struct StructuredRegular;

// An indexed triangle mesh with smooth normals and optional per-vertex or
// per-face colors.
struct TriMesh
{
  std::vector<float3> positions;
  std::vector<float3> normals;
  std::vector<uint3> triangles;
  // Either empty or one entry per vertex / per triangle.
  std::vector<float3> vertexColors;
  std::vector<float3> faceColors;

  size_t vertexCount() const { return positions.size(); }
  size_t triangleCount() const { return triangles.size(); }
};

// Optional colors of a tessellated primitive: per end vertex (interpolated
// along the primitive) and/or per primitive (constant per face).
struct PrimitiveColors
{
  const float3 *vertex0{nullptr};
  const float3 *vertex1{nullptr};
  const float3 *face{nullptr};
};

// Appends a cone frustum from (p0, r0) to (p1, r1) with 'segments' sides,
// optionally capped by disks at either end.
void appendFrustum(TriMesh &mesh,
    const float3 &p0,
    float r0,
    const float3 &p1,
    float r1,
    bool cap0,
    bool cap1,
    uint32_t segments,
    const PrimitiveColors &colors);

// Appends a UV sphere with 'rings' latitude bands and 'segments' longitude
// bands.
void appendSphere(TriMesh &mesh,
    const float3 &center,
    float radius,
    uint32_t rings,
    uint32_t segments,
    const PrimitiveColors &colors);

// Extracts the isosurfaces of a structuredRegular field (marching
// tetrahedra on the Kuhn decomposition of each cell, vertices shared along
// grid edges, normals from the field gradient) and appends them.
void appendIsosurface(
    TriMesh &mesh, const StructuredRegular &field, float isovalue);

} // namespace mitsuba_anari
