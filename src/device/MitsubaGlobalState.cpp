// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "device/MitsubaGlobalState.h"

// std
#include <cstdlib>

namespace mitsuba_anari {

MitsubaGlobalState::MitsubaGlobalState(ANARIDevice d)
    : helium::BaseGlobalDeviceState(d)
{
  // ANARI_MITSUBA_VARIANT selects the default rendering variant for clients
  // that cannot set string device parameters (e.g. PyNARI). The
  // 'mitsuba.variant' device parameter (ANARI_MITSUBA_DEVICE_VARIANT)
  // overrides it; validation and the scalar fallback happen at first render.
  if (const char *env = std::getenv("ANARI_MITSUBA_VARIANT"); env && *env)
    requestedVariant = env;
}

} // namespace mitsuba_anari
