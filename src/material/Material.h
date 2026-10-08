// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#pragma once

#include "core/Object.h"
#include "geometry/Geometry.h"
#include "sampler/Sampler.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"

namespace mitsuba_anari {

// A material color parameter: a constant, a geometry attribute binding
// ("color", "attribute0".."attribute3") or an image sampler.
struct ColorParameter
{
  ColorParameter(Object *owner, float3 defaultValue);

  void commit(Object &owner, const char *name);
  // Validates the binding, reporting unsupported samplers/attributes once.
  void finalize(Object &owner, const char *name);

  float3 constant() const { return m_constant; }
  // Valid image sampler (image1D/image2D) bound to the parameter, if any.
  const ImageSampler *sampler() const { return m_validSampler; }
  // Attribute bound directly (string parameter), if any.
  bool hasAttribute() const { return m_hasAttribute; }
  AttributeId attribute() const { return m_attribute; }
  // Attribute the color varies with: the direct binding or the sampler's
  // 'inAttribute'. False for constant colors.
  bool sourceAttribute(AttributeId &id) const;
  bool isConstant() const { return !m_hasAttribute && !m_validSampler; }

 private:
  float3 m_default;
  float3 m_constant;
  helium::ChangeObserverPtr<Sampler> m_sampler;
  const ImageSampler *m_validSampler{nullptr};
  std::string m_attributeName;
  bool m_hasAttribute{false};
  AttributeId m_attribute{AttributeId::Color};
};

struct Material : public Object
{
  Material(MitsubaGlobalState *s);
  ~Material() override = default;
  static Material *createInstance(
      std::string_view subtype, MitsubaGlobalState *state);
};

// The base color parameter (matte 'color', PBR 'baseColor') of a material,
// nullptr for unknown subtypes.
const ColorParameter *materialBaseColor(const Material *material);

// ANARI 'matte' material -> Mitsuba 'diffuse' BSDF.
struct Matte : public Material
{
  Matte(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  const ColorParameter &baseColor() const { return m_color; }

 private:
  ColorParameter m_color;
};

// ANARI 'physicallyBased' material -> layered Mitsuba BSDFs (see the scene
// builder and docs/PBR_MAPPING.md).
struct PhysicallyBased : public Material
{
  PhysicallyBased(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;

  const ColorParameter &baseColor() const { return m_baseColor; }
  float metallic() const { return m_metallic; }
  float roughness() const { return m_roughness; }
  float ior() const { return m_ior; }
  // Opacity as rendered: 'opacity' only applies with alphaMode "blend"
  // (fractional) or "mask" (cut at alphaCutoff); "opaque" (the default)
  // ignores it.
  float opacity() const;
  float transmission() const { return m_transmission; }
  float specular() const { return m_specular; }
  float3 specularColor() const { return m_specularColor; }
  float3 emissive() const { return m_emissive; }
  bool isEmissive() const
  {
    return m_emissive.x > 0.f || m_emissive.y > 0.f || m_emissive.z > 0.f;
  }

 private:
  ColorParameter m_baseColor;
  float m_metallic{1.f};
  float m_roughness{1.f};
  float m_ior{1.5f};
  float m_opacity{1.f};
  std::string m_alphaMode{"opaque"};
  float m_alphaCutoff{0.5f};
  float m_transmission{0.f};
  float m_specular{1.f};
  float3 m_specularColor{1.f, 1.f, 1.f};
  float3 m_emissive{0.f, 0.f, 0.f};
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Material *, ANARI_MATERIAL);
