// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geometry/Geometry.h"
#include "material/Material.h"

namespace mitsuba_anari {

// Inherit from this, add your functionality, and add it to 'createInstance()'
struct Surface : public Object
{
  Surface(MitsubaGlobalState *s);
  ~Surface() override = default;

  void commitParameters() override;
  void finalize() override;

  uint32_t id() const;
  const Geometry *geometry() const;
  const Material *material() const;

  bool isValid() const override;

 private:
  uint32_t m_id{~0u};
  helium::IntrusivePtr<Geometry> m_geometry;
  helium::IntrusivePtr<Material> m_material;
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Surface *, ANARI_SURFACE);
