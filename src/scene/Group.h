// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "array/ObjectArray.h"
#include "light/Light.h"
#include "scene/Surface.h"
#include "volume/Volume.h"

namespace mitsuba_anari {

struct Group : public Object
{
  Group(MitsubaGlobalState *s);
  ~Group() override;

  bool getProperty(const std::string_view &name,
      ANARIDataType type,
      void *ptr,
      uint64_t size,
      uint32_t flags) override;

  void commitParameters() override;
  void finalize() override;

  const std::vector<Surface *> &surfaces() const;
  const std::vector<Volume *> &volumes() const;
  const std::vector<Light *> &lights() const;

 private:
  helium::ChangeObserverPtr<ObjectArray> m_surfaceData;
  std::vector<Surface *> m_surfaces;

  helium::ChangeObserverPtr<ObjectArray> m_volumeData;
  std::vector<Volume *> m_volumes;

  helium::ChangeObserverPtr<ObjectArray> m_lightData;
  std::vector<Light *> m_lights;
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Group *, ANARI_GROUP);
