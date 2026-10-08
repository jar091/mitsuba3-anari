// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/BaseGlobalDeviceState.h"
// std
#include <memory>
#include <string>

namespace mitsuba_anari {

struct MitsubaGlobalState : public helium::BaseGlobalDeviceState
{
  // Cache of translated Mitsuba objects, owned by the scene builder
  // (type-erased here so that this header stays free of Mitsuba types;
  // see MitsubaSceneBuilder.cpp). Milestone 11.
  std::shared_ptr<void> translationCache;

  // Vendor extension ANARI_MITSUBA_DEVICE_VARIANT: variant requested via the
  // 'mitsuba.variant' device parameter; the active variant is fixed at first
  // render (one variant per process, ADR 0004).
  std::string requestedVariant{"scalar_rgb"};
  std::string activeVariant;
  bool backendAcquired{false};
  // True when the process-global Mitsuba runtime initialized successfully
  // (frames refuse to render otherwise).
  bool mitsubaReady{false};

  MitsubaGlobalState(ANARIDevice d);
};

// Helper functions/macros ////////////////////////////////////////////////////

inline MitsubaGlobalState *asMitsubaState(
    helium::BaseGlobalDeviceState *s)
{
  return (MitsubaGlobalState *)s;
}

#define MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(type, anari_type)                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_SPECIALIZATION(type, anari_type);                              \
  }

#define MITSUBA_ANARI_TYPEFOR_DEFINITION(type)                                  \
  namespace anari {                                                            \
  ANARI_TYPEFOR_DEFINITION(type);                                              \
  }

} // namespace mitsuba_anari
