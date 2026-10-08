// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "Geometry.h"

#include "spatial_field/SpatialField.h"
// std
#include <algorithm>
#include <cmath>
#include <cstring>

namespace mitsuba_anari {

// Attribute helpers //////////////////////////////////////////////////////////

static const char *s_attributeNames[] = {
    "color", "attribute0", "attribute1", "attribute2", "attribute3"};

bool attributeIdFromName(std::string_view name, AttributeId &id)
{
  for (size_t i = 0; i < size_t(AttributeId::Count); ++i) {
    if (name == s_attributeNames[i]) {
      id = AttributeId(i);
      return true;
    }
  }
  return false;
}

const char *attributeName(AttributeId id)
{
  return s_attributeNames[size_t(id)];
}

bool isAttributeElementType(ANARIDataType t)
{
  switch (t) {
  case ANARI_FLOAT32:
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4:
  case ANARI_FLOAT64:
  case ANARI_FLOAT64_VEC2:
  case ANARI_FLOAT64_VEC3:
  case ANARI_FLOAT64_VEC4:
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4:
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB:
  case ANARI_UFIXED16:
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED16_VEC4:
    return true;
  default:
    return false;
  }
}

static float srgbToLinear(float c)
{
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float4 readAttribute(const Array1D *array, uint64_t i)
{
  float4 v(0.f, 0.f, 0.f, 1.f);
  if (!array || i >= array->size())
    return v;
  const ANARIDataType t = array->elementType();
  const int n = int(anari::componentsOf(t));
  const auto *base = (const uint8_t *)array->data();
  auto set = [&](int c, float x) {
    if (c < 4)
      v[c] = x;
  };
  switch (t) {
  case ANARI_FLOAT32:
  case ANARI_FLOAT32_VEC2:
  case ANARI_FLOAT32_VEC3:
  case ANARI_FLOAT32_VEC4: {
    const float *p = (const float *)base + i * n;
    for (int c = 0; c < n; ++c)
      set(c, p[c]);
    break;
  }
  case ANARI_FLOAT64:
  case ANARI_FLOAT64_VEC2:
  case ANARI_FLOAT64_VEC3:
  case ANARI_FLOAT64_VEC4: {
    const double *p = (const double *)base + i * n;
    for (int c = 0; c < n; ++c)
      set(c, float(p[c]));
    break;
  }
  case ANARI_UFIXED8:
  case ANARI_UFIXED8_VEC2:
  case ANARI_UFIXED8_VEC3:
  case ANARI_UFIXED8_VEC4: {
    const uint8_t *p = base + i * n;
    for (int c = 0; c < n; ++c)
      set(c, p[c] / 255.f);
    break;
  }
  case ANARI_UFIXED8_R_SRGB:
  case ANARI_UFIXED8_RA_SRGB:
  case ANARI_UFIXED8_RGB_SRGB:
  case ANARI_UFIXED8_RGBA_SRGB: {
    const uint8_t *p = base + i * n;
    for (int c = 0; c < n; ++c) {
      const bool alpha = (n == 2 && c == 1) || c == 3;
      set(c, alpha ? p[c] / 255.f : srgbToLinear(p[c] / 255.f));
    }
    break;
  }
  case ANARI_UFIXED16:
  case ANARI_UFIXED16_VEC2:
  case ANARI_UFIXED16_VEC3:
  case ANARI_UFIXED16_VEC4: {
    const uint16_t *p = (const uint16_t *)base + i * n;
    for (int c = 0; c < n; ++c)
      set(c, p[c] / 65535.f);
    break;
  }
  default:
    break;
  }
  return v;
}

// Geometry ///////////////////////////////////////////////////////////////////

Geometry::Geometry(MitsubaGlobalState *s) : Object(ANARI_GEOMETRY, s)
{
  for (size_t i = 0; i < size_t(AttributeId::Count); ++i) {
    m_vertexAttributes.emplace_back(this);
    m_primitiveAttributes.emplace_back(this);
  }
}

Geometry *Geometry::createInstance(
    std::string_view type, MitsubaGlobalState *s)
{
  if (type == "triangle")
    return new Triangle(s);
  if (type == "quad")
    return new Quad(s);
  if (type == "sphere")
    return new SphereGeometry(s);
  if (type == "curve")
    return new CurveGeometry(s);
  if (type == "cylinder")
    return new ConeCylinderGeometry(s, false);
  if (type == "cone")
    return new ConeCylinderGeometry(s, true);
  if (type == "isosurface")
    return new IsosurfaceGeometry(s);
  return (Geometry *)new UnknownObject(ANARI_GEOMETRY, type, s);
}

void Geometry::commitParameters()
{
  for (size_t i = 0; i < size_t(AttributeId::Count); ++i) {
    const std::string name = attributeName(AttributeId(i));
    m_vertexAttributes[i] = getParamObject<Array1D>("vertex." + name);
    m_primitiveAttributes[i] = getParamObject<Array1D>("primitive." + name);
    m_uniformAttributes[i].reset();
    float4 v(0.f, 0.f, 0.f, 1.f);
    if (getParam(name, ANARI_FLOAT32_VEC4, &v))
      m_uniformAttributes[i] = v;
    else if (getParam(name, ANARI_FLOAT32_VEC3, &v))
      m_uniformAttributes[i] = float4(v.x, v.y, v.z, 1.f);
    else if (getParam(name, ANARI_FLOAT32_VEC2, &v))
      m_uniformAttributes[i] = float4(v.x, v.y, 0.f, 1.f);
    else if (getParam(name, ANARI_FLOAT32, &v))
      m_uniformAttributes[i] = float4(v.x, 0.f, 0.f, 1.f);
  }
}

const Array1D *Geometry::vertexAttribute(AttributeId id) const
{
  return m_vertexAttributes[size_t(id)].get();
}

const Array1D *Geometry::primitiveAttribute(AttributeId id) const
{
  return m_primitiveAttributes[size_t(id)].get();
}

std::optional<float4> Geometry::uniformAttribute(AttributeId id) const
{
  return m_uniformAttributes[size_t(id)];
}

void Geometry::validateAttributes(const char *what)
{
  const uint64_t nv = numVertices();
  const uint64_t np = numPrimitives();
  for (size_t i = 0; i < size_t(AttributeId::Count); ++i) {
    auto check = [&](helium::ChangeObserverPtr<Array1D> &arr,
                     const char *prefix,
                     uint64_t count) {
      if (!arr)
        return;
      if (!isAttributeElementType(arr->elementType()) || arr->size() < count) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "[mitsuba] %s '%s.%s' has an unsupported element type or too few "
            "elements (%llu < %llu); ignoring",
            what,
            prefix,
            attributeName(AttributeId(i)),
            (unsigned long long)arr->size(),
            (unsigned long long)count);
        arr = nullptr;
      }
    };
    check(m_vertexAttributes[i], "vertex", nv);
    check(m_primitiveAttributes[i], "primitive", np);
  }
}

// MeshGeometry ///////////////////////////////////////////////////////////////

MeshGeometry::MeshGeometry(MitsubaGlobalState *s, uint32_t vertsPerPrimitive)
    : Geometry(s),
      m_vertsPerPrim(vertsPerPrimitive),
      m_index(this),
      m_vertexPosition(this),
      m_vertexNormal(this)
{}

void MeshGeometry::commitParameters()
{
  Geometry::commitParameters();
  m_index = getParamObject<Array1D>("primitive.index");
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexNormal = getParamObject<Array1D>("vertex.normal");
}

uint64_t MeshGeometry::numVertices() const
{
  return m_vertexPosition ? m_vertexPosition->size() : 0;
}

uint64_t MeshGeometry::numPrimitives() const
{
  return m_index ? m_index->size() : numVertices() / m_vertsPerPrim;
}

void MeshGeometry::finalize()
{
  m_valid = false;
  const char *what = m_vertsPerPrim == 3 ? "triangle" : "quad";
  const ANARIDataType indexType =
      m_vertsPerPrim == 3 ? ANARI_UINT32_VEC3 : ANARI_UINT32_VEC4;

  if (!m_vertexPosition) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] %s geometry is missing required 'vertex.position'", what);
    return;
  }
  if (m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] %s 'vertex.position' must be ANARI_FLOAT32_VEC3", what);
    return;
  }
  const uint64_t numVerts = m_vertexPosition->size();
  if (numVerts == 0) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "[mitsuba] %s geometry has no vertices", what);
    return;
  }

  if (m_index) {
    if (m_index->elementType() != indexType) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] %s 'primitive.index' has the wrong element type", what);
      return;
    }
    // Untrusted input: every index must be in range (master prompt §31).
    // Raw access: the element type was validated above; beginAs<> would
    // reject the scalar view of the vector type.
    const auto *idx = (const uint32_t *)m_index->data();
    const uint64_t numIndices = m_index->size() * m_vertsPerPrim;
    for (uint64_t i = 0; i < numIndices; ++i) {
      if (idx[i] >= numVerts) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "[mitsuba] %s 'primitive.index' references out-of-range vertex",
            what);
        return;
      }
    }
  } else if (numVerts % m_vertsPerPrim != 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] %s soup 'vertex.position' count (%llu) is not a multiple "
        "of %u",
        what,
        (unsigned long long)numVerts,
        m_vertsPerPrim);
    return;
  }

  if (m_vertexNormal
      && (m_vertexNormal->elementType() != ANARI_FLOAT32_VEC3
          || m_vertexNormal->size() < numVerts)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] %s 'vertex.normal' has an unsupported element type or too "
        "few elements; ignoring",
        what);
    m_vertexNormal = nullptr;
  }
  validateAttributes(what);

  m_valid = true;
}

bool MeshGeometry::isValid() const
{
  return m_valid;
}

// CurveGeometry //////////////////////////////////////////////////////////////

CurveGeometry::CurveGeometry(MitsubaGlobalState *s)
    : Geometry(s), m_vertexPosition(this), m_vertexRadius(this), m_index(this)
{}

void CurveGeometry::commitParameters()
{
  Geometry::commitParameters();
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_index = getParamObject<Array1D>("primitive.index");
  m_radius = getParam<float>("radius", 0.01f);
}

uint64_t CurveGeometry::numVertices() const
{
  return m_vertexPosition ? m_vertexPosition->size() : 0;
}

uint64_t CurveGeometry::numPrimitives() const
{
  if (m_index)
    return m_index->size();
  return numVertices() > 0 ? numVertices() - 1 : 0;
}

void CurveGeometry::finalize()
{
  m_valid = false;
  if (!m_vertexPosition
      || m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3
      || m_vertexPosition->size() < 2) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] curve geometry requires 'vertex.position' "
        "(ANARI_FLOAT32_VEC3, >= 2 vertices)");
    return;
  }
  const uint64_t numVerts = m_vertexPosition->size();

  if (m_vertexRadius
      && (m_vertexRadius->elementType() != ANARI_FLOAT32
          || m_vertexRadius->size() < numVerts)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] curve 'vertex.radius' must be ANARI_FLOAT32 with one "
        "element per vertex; ignoring");
    m_vertexRadius = nullptr;
  }
  if (!(m_radius > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] curve 'radius' must be positive; using 0.01");
    m_radius = 0.01f;
  }

  if (m_index) {
    if (m_index->elementType() != ANARI_UINT32) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] curve 'primitive.index' must be ANARI_UINT32");
      return;
    }
    // Untrusted input (§31): each index i starts the segment (i, i+1).
    const auto *idx = (const uint32_t *)m_index->data();
    for (uint64_t i = 0; i < m_index->size(); ++i) {
      if (uint64_t(idx[i]) + 1 >= numVerts) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "[mitsuba] curve 'primitive.index' references out-of-range "
            "segment start");
        return;
      }
    }
  }
  validateAttributes("curve");

  m_valid = true;
}

bool CurveGeometry::isValid() const
{
  return m_valid;
}

// SphereGeometry /////////////////////////////////////////////////////////////

SphereGeometry::SphereGeometry(MitsubaGlobalState *s)
    : Geometry(s), m_vertexPosition(this), m_vertexRadius(this), m_index(this)
{}

void SphereGeometry::commitParameters()
{
  Geometry::commitParameters();
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_index = getParamObject<Array1D>("primitive.index");
  m_radius = getParam<float>("radius", 0.01f);
}

uint64_t SphereGeometry::numVertices() const
{
  return m_vertexPosition ? m_vertexPosition->size() : 0;
}

uint64_t SphereGeometry::numPrimitives() const
{
  return m_index ? m_index->size() : numVertices();
}

void SphereGeometry::finalize()
{
  m_valid = false;
  if (!m_vertexPosition
      || m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3
      || m_vertexPosition->size() == 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] sphere geometry requires 'vertex.position' "
        "(ANARI_FLOAT32_VEC3, non-empty)");
    return;
  }
  if (m_vertexRadius
      && (m_vertexRadius->elementType() != ANARI_FLOAT32
          || m_vertexRadius->size() < m_vertexPosition->size())) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] sphere 'vertex.radius' must be ANARI_FLOAT32 with one "
        "element per vertex; ignoring");
    m_vertexRadius = nullptr;
  }
  if (m_vertexRadius) {
    // Untrusted input (§31): non-positive radii would corrupt the ray
    // tracing acceleration structures; those primitives are skipped at
    // translation time.
    const auto *r = (const float *)m_vertexRadius->data();
    uint64_t bad = 0;
    for (uint64_t i = 0; i < m_vertexPosition->size(); ++i)
      bad += !(r[i] > 0.f);
    if (bad > 0) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] sphere geometry has %llu non-positive 'vertex.radius' "
          "value(s); those primitives are skipped",
          (unsigned long long)bad);
    }
  }
  if (!(m_radius > 0.f)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] sphere 'radius' must be positive; using 0.01");
    m_radius = 0.01f;
  }
  if (m_index) {
    if (m_index->elementType() != ANARI_UINT32) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] sphere 'primitive.index' must be ANARI_UINT32");
      return;
    }
    const auto *idx = (const uint32_t *)m_index->data();
    for (uint64_t i = 0; i < m_index->size(); ++i) {
      if (idx[i] >= m_vertexPosition->size()) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "[mitsuba] sphere 'primitive.index' references out-of-range "
            "vertex");
        return;
      }
    }
  }
  validateAttributes("sphere");
  m_valid = true;
}

bool SphereGeometry::isValid() const
{
  return m_valid;
}

// ConeCylinderGeometry ///////////////////////////////////////////////////////

ConeCylinderGeometry::ConeCylinderGeometry(MitsubaGlobalState *s, bool isCone)
    : Geometry(s),
      m_isCone(isCone),
      m_vertexPosition(this),
      m_vertexRadius(this),
      m_primitiveRadius(this),
      m_vertexCap(this),
      m_index(this)
{}

void ConeCylinderGeometry::commitParameters()
{
  Geometry::commitParameters();
  m_vertexPosition = getParamObject<Array1D>("vertex.position");
  m_vertexRadius = getParamObject<Array1D>("vertex.radius");
  m_primitiveRadius = getParamObject<Array1D>("primitive.radius");
  m_vertexCap = getParamObject<Array1D>("vertex.cap");
  m_index = getParamObject<Array1D>("primitive.index");
  m_radius = getParam<float>("radius", 1.f);
  m_caps = getParamString("caps", "none");
}

uint64_t ConeCylinderGeometry::numVertices() const
{
  return m_vertexPosition ? m_vertexPosition->size() : 0;
}

uint64_t ConeCylinderGeometry::numPrimitives() const
{
  return m_index ? m_index->size() : numVertices() / 2;
}

void ConeCylinderGeometry::finalize()
{
  m_valid = false;
  const char *what = m_isCone ? "cone" : "cylinder";
  if (!m_vertexPosition
      || m_vertexPosition->elementType() != ANARI_FLOAT32_VEC3
      || m_vertexPosition->size() < 2) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] %s geometry requires 'vertex.position' "
        "(ANARI_FLOAT32_VEC3, >= 2 vertices)",
        what);
    return;
  }
  const uint64_t nv = m_vertexPosition->size();
  if (m_index) {
    if (m_index->elementType() != ANARI_UINT32_VEC2) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] %s 'primitive.index' must be ANARI_UINT32_VEC2", what);
      return;
    }
    const auto *idx = (const uint32_t *)m_index->data();
    for (uint64_t i = 0; i < m_index->size() * 2; ++i) {
      if (idx[i] >= nv) {
        reportMessage(ANARI_SEVERITY_WARNING,
            "[mitsuba] %s 'primitive.index' references out-of-range vertex",
            what);
        return;
      }
    }
  }
  if (m_isCone) {
    if (!m_vertexRadius || m_vertexRadius->elementType() != ANARI_FLOAT32
        || m_vertexRadius->size() < nv) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] cone geometry requires 'vertex.radius' (ANARI_FLOAT32, "
          "one per vertex)");
      return;
    }
    m_primitiveRadius = nullptr;
  } else {
    if (m_primitiveRadius
        && (m_primitiveRadius->elementType() != ANARI_FLOAT32
            || m_primitiveRadius->size() < numPrimitives())) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] cylinder 'primitive.radius' must be ANARI_FLOAT32 with "
          "one element per primitive; using 'radius'");
      m_primitiveRadius = nullptr;
    }
    m_vertexRadius = nullptr;
    if (!(m_radius > 0.f)) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] cylinder 'radius' must be positive; using 1");
      m_radius = 1.f;
    }
  }
  if (m_vertexCap
      && (m_vertexCap->elementType() != ANARI_UINT8
          || m_vertexCap->size() < nv)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] %s 'vertex.cap' must be ANARI_UINT8 with one element per "
        "vertex; ignoring",
        what);
    m_vertexCap = nullptr;
  }
  if (m_caps != "none" && m_caps != "first" && m_caps != "second"
      && m_caps != "both") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] %s 'caps' must be none/first/second/both; using none",
        what);
    m_caps = "none";
  }
  validateAttributes(what);
  m_valid = numPrimitives() > 0;
}

bool ConeCylinderGeometry::isValid() const
{
  return m_valid;
}

uint2 ConeCylinderGeometry::segment(uint64_t i) const
{
  if (m_index) {
    const auto *idx = (const uint32_t *)m_index->data();
    return uint2(idx[2 * i], idx[2 * i + 1]);
  }
  return uint2(uint32_t(2 * i), uint32_t(2 * i + 1));
}

float ConeCylinderGeometry::radius(uint64_t i, int end) const
{
  if (m_vertexRadius) {
    const uint2 s = segment(i);
    return ((const float *)m_vertexRadius->data())[end == 0 ? s.x : s.y];
  }
  if (m_primitiveRadius)
    return ((const float *)m_primitiveRadius->data())[i];
  return m_radius;
}

bool ConeCylinderGeometry::capped(uint64_t i, int end) const
{
  if (m_vertexCap) {
    const uint2 s = segment(i);
    return ((const uint8_t *)m_vertexCap->data())[end == 0 ? s.x : s.y] != 0;
  }
  return m_caps == "both" || (end == 0 && m_caps == "first")
      || (end == 1 && m_caps == "second");
}

// IsosurfaceGeometry /////////////////////////////////////////////////////////

IsosurfaceGeometry::IsosurfaceGeometry(MitsubaGlobalState *s)
    : Geometry(s), m_field(this), m_isovalueArray(this)
{}

void IsosurfaceGeometry::commitParameters()
{
  Geometry::commitParameters();
  m_field = getParamObject<SpatialField>("field");
  m_isovalueArray = getParamObject<Array1D>("isovalue");
  m_isovalues.clear();
  float iso = 0.f;
  if (!m_isovalueArray && getParam("isovalue", ANARI_FLOAT32, &iso))
    m_isovalues.push_back(iso);
}

void IsosurfaceGeometry::finalize()
{
  m_valid = false;
  if (m_isovalueArray) {
    m_isovalues.clear();
    if (m_isovalueArray->elementType() == ANARI_FLOAT32) {
      const auto *v = (const float *)m_isovalueArray->data();
      m_isovalues.assign(v, v + m_isovalueArray->size());
    } else {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] isosurface 'isovalue' array must be ANARI_FLOAT32");
    }
  }
  if (!m_field) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] isosurface geometry is missing required 'field'");
    return;
  }
  if (!field()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] isosurface geometry only supports 'structuredRegular' "
        "fields; the surface is not rendered");
    return;
  }
  if (m_isovalues.empty()) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] isosurface geometry has no 'isovalue'");
    return;
  }
  m_valid = true;
}

bool IsosurfaceGeometry::isValid() const
{
  const auto *f = field();
  return m_valid && f && f->isValid();
}

const StructuredRegular *IsosurfaceGeometry::field() const
{
  return dynamic_cast<const StructuredRegular *>(m_field.get());
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Geometry *);
