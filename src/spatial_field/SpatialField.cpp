// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#include "SpatialField.h"
// std
#include <algorithm>
#include <cmath>

namespace mitsuba_anari {

SpatialField::SpatialField(MitsubaGlobalState *s)
    : Object(ANARI_SPATIAL_FIELD, s)
{}

SpatialField *SpatialField::createInstance(
    std::string_view subtype, MitsubaGlobalState *s)
{
  if (subtype == "structuredRegular")
    return new StructuredRegular(s);
  if (subtype == "unstructured")
    return new Unstructured(s);
  return (SpatialField *)new UnknownObject(ANARI_SPATIAL_FIELD, subtype, s);
}

// StructuredRegular //////////////////////////////////////////////////////////

StructuredRegular::StructuredRegular(MitsubaGlobalState *s)
    : SpatialField(s), m_data(this)
{}

void StructuredRegular::commitParameters()
{
  m_data = getParamObject<Array3D>("data");
  m_origin = getParam<float3>("origin", float3(0.f, 0.f, 0.f));
  m_spacing = getParam<float3>("spacing", float3(1.f, 1.f, 1.f));
  if (hasParam("filter") && getParamString("filter", "linear") != "linear") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] structuredRegular 'filter' other than linear is not "
        "supported; using linear");
  }
}

void StructuredRegular::finalize()
{
  m_values.clear();
  m_maxValue = 0.f;
  m_dims = uint3(0u, 0u, 0u);
  if (!m_data) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] structuredRegular field is missing required 'data'");
    return;
  }

  m_dims = uint3(uint32_t(m_data->size().x),
      uint32_t(m_data->size().y),
      uint32_t(m_data->size().z));
  const size_t count = size_t(m_dims.x) * m_dims.y * m_dims.z;
  m_values.resize(count);
  const void *data = m_data->data();
  switch (m_data->elementType()) {
  case ANARI_FLOAT32: {
    const auto *src = (const float *)data;
    std::copy(src, src + count, m_values.begin());
    break;
  }
  case ANARI_FLOAT64: {
    const auto *src = (const double *)data;
    for (size_t i = 0; i < count; ++i)
      m_values[i] = float(src[i]);
    break;
  }
  case ANARI_UFIXED8: {
    const auto *src = (const uint8_t *)data;
    for (size_t i = 0; i < count; ++i)
      m_values[i] = src[i] / 255.f;
    break;
  }
  case ANARI_UFIXED16: {
    const auto *src = (const uint16_t *)data;
    for (size_t i = 0; i < count; ++i)
      m_values[i] = src[i] / 65535.f;
    break;
  }
  default:
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] unsupported structuredRegular 'data' element type %s",
        anari::toString(m_data->elementType()));
    m_values.clear();
    m_dims = uint3(0u, 0u, 0u);
    return;
  }
  for (float v : m_values)
    m_maxValue = std::max(m_maxValue, v);
}

bool StructuredRegular::isValid() const
{
  return !m_values.empty();
}

box3 StructuredRegular::bounds() const
{
  const float3 extent = float3(float(std::max(m_dims.x, 2u) - 1),
                            float(std::max(m_dims.y, 2u) - 1),
                            float(std::max(m_dims.z, 2u) - 1))
      * m_spacing;
  return box3(m_origin, m_origin + extent);
}

float StructuredRegular::sample(const float3 &p) const
{
  if (m_values.empty())
    return 0.f;
  const float3 g = (p - m_origin) / m_spacing;
  const int d[3] = {int(m_dims.x), int(m_dims.y), int(m_dims.z)};
  int i0[3];
  float a[3];
  for (int k = 0; k < 3; ++k) {
    const float c = std::clamp(g[k], 0.f, float(d[k] - 1));
    i0[k] = std::clamp(int(c), 0, std::max(d[k] - 2, 0));
    a[k] = d[k] > 1 ? c - float(i0[k]) : 0.f;
  }
  auto at = [&](int x, int y, int z) {
    x = std::min(x, d[0] - 1);
    y = std::min(y, d[1] - 1);
    z = std::min(z, d[2] - 1);
    return m_values[(size_t(z) * d[1] + y) * d[0] + x];
  };
  const int x = i0[0], y = i0[1], z = i0[2];
  const float c00 = at(x, y, z) * (1.f - a[0]) + at(x + 1, y, z) * a[0];
  const float c10 =
      at(x, y + 1, z) * (1.f - a[0]) + at(x + 1, y + 1, z) * a[0];
  const float c01 =
      at(x, y, z + 1) * (1.f - a[0]) + at(x + 1, y, z + 1) * a[0];
  const float c11 =
      at(x, y + 1, z + 1) * (1.f - a[0]) + at(x + 1, y + 1, z + 1) * a[0];
  const float c0 = c00 * (1.f - a[1]) + c10 * a[1];
  const float c1 = c01 * (1.f - a[1]) + c11 * a[1];
  return c0 * (1.f - a[2]) + c1 * a[2];
}

void StructuredRegular::resample(const box3 &b,
    uint3 res,
    std::vector<float> &values,
    std::vector<uint8_t> &inside) const
{
  const size_t count = size_t(res.x) * res.y * res.z;
  values.assign(count, 0.f);
  inside.assign(count, 1);
  const float3 cell = (b.upper - b.lower) / float3(float(res.x), float(res.y), float(res.z));
  size_t i = 0;
  for (uint32_t z = 0; z < res.z; ++z)
    for (uint32_t y = 0; y < res.y; ++y)
      for (uint32_t x = 0; x < res.x; ++x, ++i)
        values[i] = sample(
            b.lower + (float3(float(x), float(y), float(z)) + 0.5f) * cell);
}

// Unstructured ///////////////////////////////////////////////////////////////

Unstructured::Unstructured(MitsubaGlobalState *s)
    : SpatialField(s),
      m_vertexPosition(this),
      m_vertexData(this),
      m_index(this),
      m_cellIndex(this),
      m_cellType(this),
      m_cellData(this)
{}

void Unstructured::commitParameters()
{
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexData = getParamObject<Array1D>("vertex.data");
  m_index = getParamObject<Array1D>("index");
  m_cellIndex = getParamObject<Array1D>("cell.index");
  m_cellType = getParamObject<Array1D>("cell.type");
  m_cellData = getParamObject<Array1D>("cell.data");
}

// Scalar array elements as float (fixed-point types normalized).
static bool readScalars(const Array1D *a, std::vector<float> &out)
{
  out.resize(a->size());
  const void *d = a->data();
  switch (a->elementType()) {
  case ANARI_FLOAT32:
    std::copy((const float *)d, (const float *)d + a->size(), out.begin());
    return true;
  case ANARI_FLOAT64:
    for (size_t i = 0; i < out.size(); ++i)
      out[i] = float(((const double *)d)[i]);
    return true;
  case ANARI_UFIXED8:
    for (size_t i = 0; i < out.size(); ++i)
      out[i] = ((const uint8_t *)d)[i] / 255.f;
    return true;
  case ANARI_UFIXED16:
    for (size_t i = 0; i < out.size(); ++i)
      out[i] = ((const uint16_t *)d)[i] / 65535.f;
    return true;
  case ANARI_FIXED16:
    for (size_t i = 0; i < out.size(); ++i)
      out[i] = std::max(-1.f, ((const int16_t *)d)[i] / 32767.f);
    return true;
  default:
    out.clear();
    return false;
  }
}

static bool readIndices(const Array1D *a, std::vector<uint64_t> &out)
{
  out.resize(a->size());
  if (a->elementType() == ANARI_UINT32) {
    const auto *d = (const uint32_t *)a->data();
    std::copy(d, d + a->size(), out.begin());
    return true;
  }
  if (a->elementType() == ANARI_UINT64) {
    const auto *d = (const uint64_t *)a->data();
    std::copy(d, d + a->size(), out.begin());
    return true;
  }
  out.clear();
  return false;
}

void Unstructured::finalize()
{
  m_valid = false;
  m_tets.clear();
  m_tetCell.clear();
  m_positions.clear();
  m_vertexValues.clear();
  m_cellValues.clear();
  m_bounds = box3();
  m_numCells = 0;

  if (!m_vertexPosition
      || m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3 || !m_index
      || !m_cellIndex || !m_cellType) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] unstructured field requires 'vertex.position' "
        "(FLOAT32_VEC3), 'index', 'cell.index' and 'cell.type'");
    return;
  }
  if (m_cellType->elementType() != ANARI_UINT8) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] unstructured field 'cell.type' must be ANARI_UINT8");
    return;
  }
  std::vector<uint64_t> index, cellIndex;
  if (!readIndices(m_index.get(), index)
      || !readIndices(m_cellIndex.get(), cellIndex)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] unstructured field 'index'/'cell.index' must be UINT32 or "
        "UINT64");
    return;
  }
  const auto *pos = (const float3 *)m_vertexPosition->data();
  m_positions.assign(pos, pos + m_vertexPosition->size());
  const size_t numVerts = m_positions.size();
  m_numCells = std::min<size_t>(cellIndex.size(), m_cellType->size());

  if (m_vertexData) {
    if (!readScalars(m_vertexData.get(), m_vertexValues)
        || m_vertexValues.size() < numVerts) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] unstructured field 'vertex.data' has an unsupported "
          "type or too few elements");
      return;
    }
  } else if (m_cellData) {
    if (!readScalars(m_cellData.get(), m_cellValues)
        || m_cellValues.size() < m_numCells) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] unstructured field 'cell.data' has an unsupported type "
          "or too few elements");
      return;
    }
  } else {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] unstructured field requires 'vertex.data' or 'cell.data'");
    return;
  }

  // Cells -> tetrahedra (VTK vertex ordering).
  static const int kTet[1][4] = {{0, 1, 2, 3}};
  static const int kPyramid[2][4] = {{0, 1, 2, 4}, {0, 2, 3, 4}};
  static const int kWedge[3][4] = {{0, 1, 2, 3}, {1, 2, 3, 4}, {2, 3, 4, 5}};
  static const int kHex[6][4] = {{0, 1, 2, 6},
      {0, 2, 3, 6},
      {0, 3, 7, 6},
      {0, 7, 4, 6},
      {0, 4, 5, 6},
      {0, 5, 1, 6}};
  const auto *types = (const uint8_t *)m_cellType->data();
  size_t skipped = 0;
  for (size_t c = 0; c < m_numCells; ++c) {
    const int(*tets)[4] = nullptr;
    int numTets = 0, numCellVerts = 0;
    switch (types[c]) {
    case 10: // VTK_TETRA
      tets = kTet, numTets = 1, numCellVerts = 4;
      break;
    case 12: // VTK_HEXAHEDRON
      tets = kHex, numTets = 6, numCellVerts = 8;
      break;
    case 13: // VTK_WEDGE
      tets = kWedge, numTets = 3, numCellVerts = 6;
      break;
    case 14: // VTK_PYRAMID
      tets = kPyramid, numTets = 2, numCellVerts = 5;
      break;
    default:
      ++skipped;
      continue;
    }
    const uint64_t first = cellIndex[c];
    if (first + numCellVerts > index.size()) {
      ++skipped;
      continue;
    }
    bool ok = true;
    for (int k = 0; k < numCellVerts; ++k)
      ok = ok && index[first + k] < numVerts;
    if (!ok) {
      ++skipped;
      continue;
    }
    for (int t = 0; t < numTets; ++t) {
      for (int k = 0; k < 4; ++k)
        m_tets.push_back(uint32_t(index[first + tets[t][k]]));
      m_tetCell.push_back(uint32_t(c));
    }
  }
  if (skipped > 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] unstructured field: %zu cell(s) with unsupported types or "
        "out-of-range indices are skipped",
        skipped);
  }
  for (uint32_t v : m_tets)
    m_bounds.extend(m_positions[v]);
  m_valid = !m_tets.empty();
}

bool Unstructured::isValid() const
{
  return m_valid;
}

uint3 Unstructured::sampleResolution() const
{
  // About two voxels per cell along each axis, 128..256 along the longest.
  const float3 e = m_bounds.upper - m_bounds.lower;
  const float maxE = std::max({e.x, e.y, e.z, 1e-30f});
  const float n = std::clamp(
      2.f * std::cbrt(float(std::max<size_t>(m_numCells, 1))), 128.f, 256.f);
  return uint3(uint32_t(std::max(2.f, std::ceil(n * e.x / maxE))),
      uint32_t(std::max(2.f, std::ceil(n * e.y / maxE))),
      uint32_t(std::max(2.f, std::ceil(n * e.z / maxE))));
}

void Unstructured::resample(const box3 &b,
    uint3 res,
    std::vector<float> &values,
    std::vector<uint8_t> &inside) const
{
  const size_t count = size_t(res.x) * res.y * res.z;
  values.assign(count, 0.f);
  inside.assign(count, 0);
  const float3 cell = (b.upper - b.lower) / float3(float(res.x), float(res.y), float(res.z));
  const int r[3] = {int(res.x), int(res.y), int(res.z)};

  // Rasterize every tetrahedron into the voxels whose centers it contains.
  for (size_t t = 0; t < m_tetCell.size(); ++t) {
    const uint32_t *vi = &m_tets[t * 4];
    const float3 p0 = m_positions[vi[0]], p1 = m_positions[vi[1]],
                 p2 = m_positions[vi[2]], p3 = m_positions[vi[3]];
    // Barycentric coordinates via the inverse of [p1-p0, p2-p0, p3-p0].
    const float3 e1 = p1 - p0, e2 = p2 - p0, e3 = p3 - p0;
    const float det = linalg::dot(e1, linalg::cross(e2, e3));
    if (std::abs(det) < 1e-30f)
      continue;
    const float3 c23 = linalg::cross(e2, e3) / det;
    const float3 c31 = linalg::cross(e3, e1) / det;
    const float3 c12 = linalg::cross(e1, e2) / det;
    box3 tb;
    tb.extend(p0).extend(p1).extend(p2).extend(p3);
    int lo[3], hi[3];
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::max(0,
          int(std::ceil((tb.lower[k] - b.lower[k]) / cell[k] - 0.5f)));
      hi[k] = std::min(r[k] - 1,
          int(std::floor((tb.upper[k] - b.lower[k]) / cell[k] - 0.5f)));
    }
    const float cv = m_cellValues.empty() ? 0.f : m_cellValues[m_tetCell[t]];
    for (int z = lo[2]; z <= hi[2]; ++z) {
      for (int y = lo[1]; y <= hi[1]; ++y) {
        for (int x = lo[0]; x <= hi[0]; ++x) {
          const float3 p =
              b.lower + (float3(float(x), float(y), float(z)) + 0.5f) * cell;
          const float3 d = p - p0;
          const float w1 = linalg::dot(d, c23);
          const float w2 = linalg::dot(d, c31);
          const float w3 = linalg::dot(d, c12);
          const float w0 = 1.f - w1 - w2 - w3;
          const float eps = -1e-5f;
          if (w0 < eps || w1 < eps || w2 < eps || w3 < eps)
            continue;
          const size_t i = (size_t(z) * res.y + y) * res.x + x;
          values[i] = m_vertexValues.empty()
              ? cv
              : w0 * m_vertexValues[vi[0]] + w1 * m_vertexValues[vi[1]]
                  + w2 * m_vertexValues[vi[2]] + w3 * m_vertexValues[vi[3]];
          inside[i] = 1;
        }
      }
    }
  }
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::SpatialField *);
