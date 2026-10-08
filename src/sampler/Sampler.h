// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#pragma once

#include "array/Array1D.h"
#include "array/Array2D.h"
#include "core/Object.h"
// helium
#include "helium/utility/ChangeObserverPtr.h"
// std
#include <string>
#include <vector>

namespace mitsuba_anari {

struct Sampler : public Object
{
  Sampler(MitsubaGlobalState *s);
  ~Sampler() override = default;
  static Sampler *createInstance(
      std::string_view subtype, MitsubaGlobalState *state);
};

// Common state of the image samplers: the input attribute, its transform
// (coord = inTransform * attribute + inOffset), the output transform
// (color = outTransform * texel + outOffset), filtering and wrapping.
//
// Images are converted once at finalize() into linear float RGBA texels
// (sRGB formats linearized) which serve both the CPU evaluation (per-
// primitive colors) and the Mitsuba bitmap textures.
struct ImageSampler : public Sampler
{
  ImageSampler(MitsubaGlobalState *s);

  void commitParameters() override;
  bool isValid() const override;

  const std::string &inAttribute() const { return m_inAttribute; }
  bool nearestFilter() const { return m_filter == "nearest"; }
  const std::string &wrapMode(int axis) const { return m_wrapMode[axis]; }

  // ANARI input transform applied to an attribute value.
  float4 transformInput(const float4 &attribute) const;
  // Output transform applied to a sampled texel.
  float4 transformOutput(const float4 &texel) const;
  bool hasOutputTransform() const;

  // Texels (linear RGBA, x fastest) and their resolution (height/depth 1
  // for lower-dimensional images).
  const std::vector<float4> &texels() const { return m_texels; }
  uint3 resolution() const { return m_resolution; }

  // CPU evaluation: input transform, filtered lookup, output transform.
  float4 evaluate(const float4 &attribute) const;

 protected:
  // Converts 'count' elements of an image array into m_texels; false (with
  // a warning) for unsupported element types.
  bool loadTexels(const void *data, ANARIDataType type, size_t count);
  float4 fetch(int x, int y) const;
  int wrapIndex(int i, int n, int axis) const;

  uint3 m_resolution{0u, 0u, 0u};
  std::vector<float4> m_texels;
  bool m_valid{false};

 private:
  std::string m_inAttribute{"attribute0"};
  std::string m_filter{"linear"};
  std::string m_wrapMode[2]{"clampToEdge", "clampToEdge"};
  mat4 m_inTransform{linalg::identity};
  float4 m_inOffset{0.f, 0.f, 0.f, 0.f};
  mat4 m_outTransform{linalg::identity};
  float4 m_outOffset{0.f, 0.f, 0.f, 0.f};
};

// ANARI 'image1D' sampler (colormaps): a 1-row Mitsuba 'bitmap' texture
// looked up through texture coordinates on meshes, evaluated on the CPU for
// per-primitive colors elsewhere.
struct Image1D : public ImageSampler
{
  Image1D(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;

 private:
  helium::ChangeObserverPtr<Array1D> m_image;
};

// ANARI 'image2D' sampler -> Mitsuba 'bitmap' texture.
struct Image2D : public ImageSampler
{
  Image2D(MitsubaGlobalState *s);

  void commitParameters() override;
  void finalize() override;

 private:
  helium::ChangeObserverPtr<Array2D> m_image;
};

// Known ANARI sampler subtypes the device does not implement (image3D,
// primitive, transform): invalid, materials using them fall back to their
// constant value (reported once on commit).
struct UnsupportedSampler : public Sampler
{
  UnsupportedSampler(MitsubaGlobalState *s, std::string_view subtype);

  bool isValid() const override;
  const std::string &subtype() const { return m_subtype; }

 private:
  std::string m_subtype;
};

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_SPECIALIZATION(mitsuba_anari::Sampler *, ANARI_SAMPLER);
