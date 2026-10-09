// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "Material.h"

// std
#include <algorithm>

namespace mitsuba_anari {

// ColorParameter /////////////////////////////////////////////////////////////

ColorParameter::ColorParameter(Object *owner, float3 defaultValue)
    : m_default(defaultValue), m_constant(defaultValue), m_sampler(owner)
{}

void ColorParameter::commit(Object &owner, const char *name)
{
  m_sampler = owner.getParamObject<Sampler>(name);
  m_attributeName = owner.getParamString(name, "");
  m_constant = owner.getParam<float3>(name, m_default);
  float4 c4;
  if (owner.getParam(name, ANARI_FLOAT32_VEC4, &c4))
    m_constant = float3(c4.x, c4.y, c4.z);
}

void ColorParameter::finalize(Object &owner, const char *name)
{
  m_validSampler = nullptr;
  m_hasAttribute = false;

  if (!m_attributeName.empty()) {
    if (attributeIdFromName(m_attributeName, m_attribute)) {
      m_hasAttribute = true;
    } else {
      owner.reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] '%s' attribute binding '%s' is not supported (supported: "
          "color, attribute0..attribute3); using the constant color",
          name,
          m_attributeName.c_str());
    }
  }

  if (m_sampler) {
    const auto *image = dynamic_cast<const ImageSampler *>(m_sampler.get());
    AttributeId id;
    if (!image) {
      // UnsupportedSampler / unknown subtypes were reported on creation.
      owner.reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] '%s' is bound to an unsupported sampler; using the "
          "constant color",
          name);
    } else if (!image->isValid()) {
      owner.reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] '%s' is bound to an invalid sampler; using the constant "
          "color",
          name);
    } else if (dynamic_cast<const Image3D *>(image)) {
      // Mitsuba looks a volume texture up at the surface position.
      if (image->positionInput()) {
        m_validSampler = image;
      } else {
        owner.reportMessage(ANARI_SEVERITY_WARNING,
            "[mitsuba] image3D sampler 'inAttribute' '%s' is not supported "
            "(supported: worldPosition, objectPosition); '%s' uses the "
            "constant color",
            image->inAttribute().c_str(),
            name);
      }
    } else if (!attributeIdFromName(image->inAttribute(), id)) {
      owner.reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] sampler 'inAttribute' '%s' is not supported (supported: "
          "color, attribute0..attribute3); '%s' uses the constant color",
          image->inAttribute().c_str(),
          name);
    } else {
      m_validSampler = image;
    }
  }
}

bool ColorParameter::sourceAttribute(AttributeId &id) const
{
  if (m_validSampler) {
    // A position-driven sampler (image3D) needs no geometry attribute.
    return !m_validSampler->positionInput()
        && attributeIdFromName(m_validSampler->inAttribute(), id);
  }
  if (m_hasAttribute) {
    id = m_attribute;
    return true;
  }
  return false;
}

// Material ///////////////////////////////////////////////////////////////////

Material::Material(MitsubaGlobalState *s) : Object(ANARI_MATERIAL, s) {}

Material *Material::createInstance(
    std::string_view subtype, MitsubaGlobalState *s)
{
  if (subtype == "matte")
    return new Matte(s);
  if (subtype == "physicallyBased")
    return new PhysicallyBased(s);
  return (Material *)new UnknownObject(ANARI_MATERIAL, subtype, s);
}

const ColorParameter *materialBaseColor(const Material *material)
{
  if (const auto *m = dynamic_cast<const Matte *>(material))
    return &m->baseColor();
  if (const auto *m = dynamic_cast<const PhysicallyBased *>(material))
    return &m->baseColor();
  return nullptr;
}

// Matte //////////////////////////////////////////////////////////////////////

Matte::Matte(MitsubaGlobalState *s)
    : Material(s), m_color(this, float3(0.8f, 0.8f, 0.8f))
{}

void Matte::commitParameters()
{
  m_color.commit(*this, "color");
}

void Matte::finalize()
{
  m_color.finalize(*this, "color");
}

// PhysicallyBased ////////////////////////////////////////////////////////////

PhysicallyBased::PhysicallyBased(MitsubaGlobalState *s)
    : Material(s), m_baseColor(this, float3(0.8f, 0.8f, 0.8f))
{}

void PhysicallyBased::commitParameters()
{
  m_baseColor.commit(*this, "baseColor");
  m_metallic = std::clamp(getParam<float>("metallic", 1.f), 0.f, 1.f);
  m_roughness = std::clamp(getParam<float>("roughness", 1.f), 0.f, 1.f);
  m_ior = std::max(1.0001f, getParam<float>("ior", 1.5f));
  m_opacity = std::clamp(getParam<float>("opacity", 1.f), 0.f, 1.f);
  m_alphaMode = getParamString("alphaMode", "opaque");
  m_alphaCutoff = getParam<float>("alphaCutoff", 0.5f);
  if (m_alphaMode != "opaque" && m_alphaMode != "blend"
      && m_alphaMode != "mask") {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] physicallyBased 'alphaMode' must be opaque/blend/mask; "
        "using opaque");
    m_alphaMode = "opaque";
  }
  m_transmission = std::clamp(getParam<float>("transmission", 0.f), 0.f, 1.f);
  m_specular = std::max(0.f, getParam<float>("specular", 1.f));
  m_specularColor = getParam<float3>("specularColor", float3(1.f, 1.f, 1.f));
  m_emissive = getParam<float3>("emissive", float3(0.f));

  // Parameters accepted by ANARI but not mapped yet are called out loudly so
  // nothing silently renders differently than the app expects.
  if (getParamObject<Object>("normal")) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] physicallyBased 'normal' mapping is not supported yet");
  }
  if (getParamObject<Object>("emissive")) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] physicallyBased 'emissive' samplers are not supported; "
        "using the constant emission");
  }
  for (const char *p : {"clearcoat", "sheenColor", "iridescence"}) {
    if (getParamObject<Object>(p) || !getParamString(p, "").empty()
        || getParam<float>(p, 0.f) > 0.f
        || linalg::length(getParam<float3>(p, float3(0.f))) > 0.f) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] physicallyBased '%s' is not supported; ignored",
          p);
    }
  }
  for (const char *p : {"metallic", "roughness", "opacity", "transmission"}) {
    if (getParamObject<Object>(p) || !getParamString(p, "").empty()) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] physicallyBased '%s' samplers/attribute bindings are not "
          "supported; using the constant value",
          p);
    }
  }
}

float PhysicallyBased::opacity() const
{
  if (m_alphaMode == "blend")
    return m_opacity;
  if (m_alphaMode == "mask")
    return m_opacity >= m_alphaCutoff ? 1.f : 0.f;
  return 1.f;
}

void PhysicallyBased::finalize()
{
  m_baseColor.finalize(*this, "baseColor");
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Material *);
