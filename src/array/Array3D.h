// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#pragma once

// helium
#include "helium/array/Array3D.h"

namespace mitsuba_anari {

// You may want to add on additional array functionality by subclassing helium's
// arrays. However, some devices may be able to live with using helium arrays
// directly, so we will just alias them here.

using Array3DMemoryDescriptor = helium::Array3DMemoryDescriptor;
using Array3D = helium::Array3D;

} // namespace mitsuba_anari
