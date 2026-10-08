// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#include "Surface.h"

namespace mitsuba_anari {

Surface::Surface(MitsubaGlobalState *s) : Object(ANARI_SURFACE, s) {}

void Surface::commitParameters()
{
  m_id = getParam<uint32_t>("id", ~0u);
  m_geometry = getParamObject<Geometry>("geometry");
  m_material = getParamObject<Material>("material");

  if (!m_material) {
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'material' on ANARISurface");
    return;
  }

  if (!m_geometry) {
    reportMessage(ANARI_SEVERITY_WARNING, "missing 'geometry' on ANARISurface");
    return;
  }
}

void Surface::finalize()
{
  // A material binding an attribute the geometry does not provide falls
  // back to the constant color at translation time — never silently.
  if (!m_material || !m_geometry || !m_material->isValid()
      || !m_geometry->isValid())
    return;
  const ColorParameter *color = materialBaseColor(m_material.ptr);
  AttributeId id;
  if (color && color->sourceAttribute(id)
      && !m_geometry->vertexAttribute(id)
      && !m_geometry->primitiveAttribute(id)
      && !m_geometry->uniformAttribute(id)) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] surface material uses the '%s' attribute but the geometry "
        "provides no 'vertex.%s'/'primitive.%s'; using the constant color",
        attributeName(id),
        attributeName(id),
        attributeName(id));
  }
}

const Geometry *Surface::geometry() const
{
  return m_geometry.ptr;
}

const Material *Surface::material() const
{
  return m_material.ptr;
}

bool Surface::isValid() const
{
  return m_geometry && m_material && m_geometry->isValid()
      && m_material->isValid();
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Surface *);
