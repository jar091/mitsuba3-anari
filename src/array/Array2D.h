// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/Array2D.h"

namespace mitsuba_anari {

// You may want to add on additional array functionality by subclassing helium's
// arrays. However, some devices may be able to live with using helium arrays
// directly, so we will just alias them here.

using Array2DMemoryDescriptor = helium::Array2DMemoryDescriptor;
using Array2D = helium::Array2D;

} // namespace mitsuba_anari
