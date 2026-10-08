// mitsuba-anari: CPU tessellation helpers.
// SPDX-License-Identifier: Apache-2.0

#include "geometry/Tessellation.h"

#include "spatial_field/SpatialField.h"
// std
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace mitsuba_anari {

namespace {

constexpr float kPi = 3.14159265358979323846f;

void orthonormalBasis(const float3 &d, float3 &u, float3 &v)
{
  // Duff et al., "Building an Orthonormal Basis, Revisited"
  const float sign = std::copysign(1.f, d.z);
  const float a = -1.f / (sign + d.z);
  const float b = d.x * d.y * a;
  u = float3(1.f + sign * d.x * d.x * a, sign * b, -sign * d.x);
  v = float3(b, sign + d.y * d.y * a, -d.y);
}

uint32_t pushVertex(TriMesh &m, const float3 &p, const float3 &n,
    const float3 *color)
{
  m.positions.push_back(p);
  m.normals.push_back(n);
  if (color)
    m.vertexColors.push_back(*color);
  return uint32_t(m.positions.size() - 1);
}

void pushTriangle(TriMesh &m, uint32_t a, uint32_t b, uint32_t c,
    const float3 *faceColor)
{
  m.triangles.push_back(uint3(a, b, c));
  if (faceColor)
    m.faceColors.push_back(*faceColor);
}

} // namespace

void appendFrustum(TriMesh &mesh,
    const float3 &p0,
    float r0,
    const float3 &p1,
    float r1,
    bool cap0,
    bool cap1,
    uint32_t segments,
    const PrimitiveColors &colors)
{
  const float3 axis = p1 - p0;
  const float len = linalg::length(axis);
  if (!(len > 0.f) || !(r0 >= 0.f) || !(r1 >= 0.f) || (r0 == 0.f && r1 == 0.f))
    return;
  const float3 d = axis / len;
  float3 u, v;
  orthonormalBasis(d, u, v);
  const float3 *c0 = colors.vertex0;
  const float3 *c1 = colors.vertex1 ? colors.vertex1 : colors.vertex0;

  // Side: rings of 'segments' vertices at both ends, smooth normals.
  const uint32_t base0 = uint32_t(mesh.positions.size());
  for (uint32_t i = 0; i < segments; ++i) {
    const float phi = 2.f * kPi * float(i) / float(segments);
    const float3 radial = std::cos(phi) * u + std::sin(phi) * v;
    const float3 n = linalg::normalize(radial * len + d * (r0 - r1));
    pushVertex(mesh, p0 + r0 * radial, n, c0);
  }
  const uint32_t base1 = uint32_t(mesh.positions.size());
  for (uint32_t i = 0; i < segments; ++i) {
    const float phi = 2.f * kPi * float(i) / float(segments);
    const float3 radial = std::cos(phi) * u + std::sin(phi) * v;
    const float3 n = linalg::normalize(radial * len + d * (r0 - r1));
    pushVertex(mesh, p1 + r1 * radial, n, c1);
  }
  for (uint32_t i = 0; i < segments; ++i) {
    const uint32_t j = (i + 1) % segments;
    if (r0 > 0.f)
      pushTriangle(mesh, base0 + i, base0 + j, base1 + j, colors.face);
    if (r1 > 0.f)
      pushTriangle(mesh, base0 + i, base1 + j, base1 + i, colors.face);
  }

  // Caps: flat disks with their own vertices (hard edge).
  auto cap = [&](const float3 &center, float r, const float3 &n,
                 const float3 *color, bool flip) {
    if (!(r > 0.f))
      return;
    const uint32_t c = pushVertex(mesh, center, n, color);
    const uint32_t ring = uint32_t(mesh.positions.size());
    for (uint32_t i = 0; i < segments; ++i) {
      const float phi = 2.f * kPi * float(i) / float(segments);
      const float3 radial = std::cos(phi) * u + std::sin(phi) * v;
      pushVertex(mesh, center + r * radial, n, color);
    }
    for (uint32_t i = 0; i < segments; ++i) {
      const uint32_t j = (i + 1) % segments;
      if (flip)
        pushTriangle(mesh, c, ring + j, ring + i, colors.face);
      else
        pushTriangle(mesh, c, ring + i, ring + j, colors.face);
    }
  };
  if (cap0)
    cap(p0, r0, -d, c0, true);
  if (cap1)
    cap(p1, r1, d, c1, false);
}

void appendSphere(TriMesh &mesh,
    const float3 &center,
    float radius,
    uint32_t rings,
    uint32_t segments,
    const PrimitiveColors &colors)
{
  if (!(radius > 0.f) || rings < 2 || segments < 3)
    return;
  const float3 *c = colors.vertex0;
  const uint32_t north =
      pushVertex(mesh, center + float3(0.f, 0.f, radius), float3(0, 0, 1), c);
  const uint32_t first = uint32_t(mesh.positions.size());
  for (uint32_t k = 1; k < rings; ++k) {
    const float theta = kPi * float(k) / float(rings);
    for (uint32_t j = 0; j < segments; ++j) {
      const float phi = 2.f * kPi * float(j) / float(segments);
      const float3 n(std::sin(theta) * std::cos(phi),
          std::sin(theta) * std::sin(phi),
          std::cos(theta));
      pushVertex(mesh, center + radius * n, n, c);
    }
  }
  const uint32_t south =
      pushVertex(mesh, center - float3(0.f, 0.f, radius), float3(0, 0, -1), c);
  auto ringVertex = [&](uint32_t k, uint32_t j) {
    return first + (k - 1) * segments + (j % segments);
  };
  for (uint32_t j = 0; j < segments; ++j)
    pushTriangle(mesh, north, ringVertex(1, j), ringVertex(1, j + 1), colors.face);
  for (uint32_t k = 1; k + 1 < rings; ++k) {
    for (uint32_t j = 0; j < segments; ++j) {
      const uint32_t a = ringVertex(k, j), b = ringVertex(k + 1, j),
                     cc = ringVertex(k + 1, j + 1), dd = ringVertex(k, j + 1);
      pushTriangle(mesh, a, b, cc, colors.face);
      pushTriangle(mesh, a, cc, dd, colors.face);
    }
  }
  for (uint32_t j = 0; j < segments; ++j) {
    pushTriangle(mesh,
        ringVertex(rings - 1, j),
        south,
        ringVertex(rings - 1, j + 1),
        colors.face);
  }
}

void appendIsosurface(
    TriMesh &mesh, const StructuredRegular &field, float isovalue)
{
  const uint3 fieldDims = field.dims();
  if (fieldDims.x < 2 || fieldDims.y < 2 || fieldDims.z < 2)
    return;
  // Coarse fields are resampled (trilinearly) to at least ~48 cells along
  // the longest axis: the extracted surface then follows the trilinear
  // interpolant, and the orientation bias of the tetrahedral decomposition
  // on coarse grids becomes negligible.
  const box3 bounds = field.bounds();
  const float3 extent = bounds.upper - bounds.lower;
  const float maxExtent = std::max({extent.x, extent.y, extent.z});
  uint3 dims = fieldDims;
  for (int a = 0; a < 3; ++a) {
    const float wanted = std::ceil(48.f * extent[a] / maxExtent) + 1.f;
    dims[a] = std::max(dims[a], uint32_t(std::min(wanted, 1024.f)));
  }
  std::vector<float> resampled;
  const bool refine = dims != fieldDims;
  float3 spacing = field.spacing();
  if (refine) {
    spacing = extent / float3(float(dims.x - 1), float(dims.y - 1), float(dims.z - 1));
    resampled.resize(size_t(dims.x) * dims.y * dims.z);
    size_t i = 0;
    for (uint32_t z = 0; z < dims.z; ++z)
      for (uint32_t y = 0; y < dims.y; ++y)
        for (uint32_t x = 0; x < dims.x; ++x, ++i)
          resampled[i] = field.sample(bounds.lower
              + float3(float(x), float(y), float(z)) * spacing);
  }
  const std::vector<float> &values = refine ? resampled : field.values();
  const float3 origin = bounds.lower;
  const size_t sx = 1, sy = dims.x, sz = size_t(dims.x) * dims.y;

  auto value = [&](size_t i) { return values[i]; };
  auto gridPos = [&](size_t i) {
    const size_t x = i % dims.x, y = (i / dims.x) % dims.y, z = i / sz;
    return origin + float3(float(x), float(y), float(z)) * spacing;
  };
  // Outward normal = -gradient (from high to low values), central
  // differences (one-sided at the boundary).
  auto gradient = [&](size_t i) {
    const size_t x = i % dims.x, y = (i / dims.x) % dims.y, z = i / sz;
    auto diff = [&](size_t c, size_t n, size_t stride, float h) {
      const size_t lo = c > 0 ? i - stride : i;
      const size_t hi = c + 1 < n ? i + stride : i;
      const float dist = float((c + 1 < n ? 1 : 0) + (c > 0 ? 1 : 0)) * h;
      return dist > 0.f ? (value(hi) - value(lo)) / dist : 0.f;
    };
    return float3(diff(x, dims.x, sx, spacing.x),
        diff(y, dims.y, sy, spacing.y),
        diff(z, dims.z, sz, spacing.z));
  };

  const size_t firstVertex = mesh.positions.size();
  const size_t firstTriangle = mesh.triangles.size();
  std::unordered_map<uint64_t, uint32_t> edgeVertices;
  auto edgeVertex = [&](size_t a, size_t b) -> uint32_t {
    if (a > b)
      std::swap(a, b);
    const uint64_t key = (uint64_t(a) << 32) | uint64_t(b);
    auto it = edgeVertices.find(key);
    if (it != edgeVertices.end())
      return it->second;
    const float fa = value(a), fb = value(b);
    const float t = fb != fa ? std::clamp((isovalue - fa) / (fb - fa), 0.f, 1.f)
                             : 0.5f;
    const float3 p = gridPos(a) + t * (gridPos(b) - gridPos(a));
    const float3 g = -(gradient(a) * (1.f - t) + gradient(b) * t);
    const float gl = linalg::length(g);
    const uint32_t idx = pushVertex(
        mesh, p, gl > 0.f ? g / gl : float3(0.f), nullptr);
    edgeVertices.emplace(key, idx);
    return idx;
  };

  // Kuhn decomposition: 6 tetrahedra {0, A, A|B, 7} per cell (corner bits:
  // 1 = +x, 2 = +y, 4 = +z), conforming across neighboring cells.
  static const int kTets[6][4] = {{0, 1, 3, 7},
      {0, 1, 5, 7},
      {0, 2, 3, 7},
      {0, 2, 6, 7},
      {0, 4, 5, 7},
      {0, 4, 6, 7}};
  for (uint32_t z = 0; z + 1 < dims.z; ++z) {
    for (uint32_t y = 0; y + 1 < dims.y; ++y) {
      for (uint32_t x = 0; x + 1 < dims.x; ++x) {
        const size_t c0 = x + y * sy + z * sz;
        size_t corner[8];
        float cmin = 0.f, cmax = 0.f;
        for (int b = 0; b < 8; ++b) {
          corner[b] = c0 + ((b & 1) ? sx : 0) + ((b & 2) ? sy : 0)
              + ((b & 4) ? sz : 0);
          const float f = value(corner[b]);
          cmin = b == 0 ? f : std::min(cmin, f);
          cmax = b == 0 ? f : std::max(cmax, f);
        }
        if (cmax < isovalue || cmin >= isovalue)
          continue;
        for (const auto &tet : kTets) {
          size_t above[4], below[4];
          int na = 0, nb = 0;
          for (int k = 0; k < 4; ++k) {
            const size_t g = corner[tet[k]];
            if (value(g) >= isovalue)
              above[na++] = g;
            else
              below[nb++] = g;
          }
          if (na == 0 || nb == 0)
            continue;
          if (na == 1 || nb == 1) {
            const size_t apex = na == 1 ? above[0] : below[0];
            const size_t *others = na == 1 ? below : above;
            pushTriangle(mesh,
                edgeVertex(apex, others[0]),
                edgeVertex(apex, others[1]),
                edgeVertex(apex, others[2]),
                nullptr);
          } else {
            const uint32_t q0 = edgeVertex(above[0], below[0]);
            const uint32_t q1 = edgeVertex(above[0], below[1]);
            const uint32_t q2 = edgeVertex(above[1], below[1]);
            const uint32_t q3 = edgeVertex(above[1], below[0]);
            pushTriangle(mesh, q0, q1, q2, nullptr);
            pushTriangle(mesh, q0, q2, q3, nullptr);
          }
        }
      }
    }
  }

  // Orient every triangle along its (gradient) vertex normals; vertices
  // without a usable gradient take the area-weighted face normals.
  std::vector<float3> faceAccum(mesh.positions.size() - firstVertex, float3(0.f));
  for (size_t t = firstTriangle; t < mesh.triangles.size(); ++t) {
    uint3 &tri = mesh.triangles[t];
    const float3 &a = mesh.positions[tri.x];
    const float3 &b = mesh.positions[tri.y];
    const float3 &c = mesh.positions[tri.z];
    float3 fn = linalg::cross(b - a, c - a);
    const float3 vn =
        mesh.normals[tri.x] + mesh.normals[tri.y] + mesh.normals[tri.z];
    if (linalg::dot(fn, vn) < 0.f) {
      std::swap(tri.y, tri.z);
      fn = -fn;
    }
    faceAccum[tri.x - firstVertex] += fn;
    faceAccum[tri.y - firstVertex] += fn;
    faceAccum[tri.z - firstVertex] += fn;
  }
  for (size_t v = firstVertex; v < mesh.positions.size(); ++v) {
    if (linalg::length2(mesh.normals[v]) == 0.f) {
      const float3 &acc = faceAccum[v - firstVertex];
      const float l = linalg::length(acc);
      mesh.normals[v] = l > 0.f ? acc / l : float3(0.f, 0.f, 1.f);
    }
  }
}

} // namespace mitsuba_anari
