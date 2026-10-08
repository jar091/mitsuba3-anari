// Copyright 2021-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#include "device/MitsubaDevice.h"

#include "array/Array1D.h"
#include "array/Array2D.h"
#include "array/Array3D.h"
#include "array/ObjectArray.h"
#include "camera/Camera.h"
#include "frame/Frame.h"
#include "geometry/Geometry.h"
#include "light/Light.h"
#include "material/Material.h"
#include "mitsuba_backend/MitsubaBackend.h"
#include "renderer/Renderer.h"
#include "sampler/Sampler.h"
#include "scene/Group.h"
#include "scene/Instance.h"
#include "scene/Surface.h"
#include "scene/World.h"
#include "spatial_field/SpatialField.h"
#include "volume/Volume.h"

#include "anari_library_mitsuba_queries.h"

// std
#include <algorithm>
#include <cstring>
#include <string>

namespace mitsuba_anari {

// API Objects ////////////////////////////////////////////////////////////////

ANARIArray1D MitsubaDevice::newArray1D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems)
{
  initDevice();

  Array1DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems = numItems;

  if (anari::isObject(type))
    return (ANARIArray1D) new ObjectArray(deviceState(), md);
  else
    return (ANARIArray1D) new Array1D(deviceState(), md);
}

ANARIArray2D MitsubaDevice::newArray2D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2)
{
  initDevice();

  Array2DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;

  return (ANARIArray2D) new Array2D(deviceState(), md);
}

ANARIArray3D MitsubaDevice::newArray3D(const void *appMemory,
    ANARIMemoryDeleter deleter,
    const void *userData,
    ANARIDataType type,
    uint64_t numItems1,
    uint64_t numItems2,
    uint64_t numItems3)
{
  initDevice();

  Array3DMemoryDescriptor md;
  md.appMemory = appMemory;
  md.deleter = deleter;
  md.deleterPtr = userData;
  md.elementType = type;
  md.numItems1 = numItems1;
  md.numItems2 = numItems2;
  md.numItems3 = numItems3;

  return (ANARIArray3D) new Array3D(deviceState(), md);
}

ANARICamera MitsubaDevice::newCamera(const char *subtype)
{
  initDevice();
  return (ANARICamera)Camera::createInstance(subtype, deviceState());
}

ANARIFrame MitsubaDevice::newFrame()
{
  initDevice();
  return (ANARIFrame) new Frame(deviceState());
}

ANARIGeometry MitsubaDevice::newGeometry(const char *subtype)
{
  initDevice();
  return (ANARIGeometry)Geometry::createInstance(subtype, deviceState());
}

ANARIGroup MitsubaDevice::newGroup()
{
  initDevice();
  return (ANARIGroup) new Group(deviceState());
}

ANARIInstance MitsubaDevice::newInstance(const char * /*subtype*/)
{
  initDevice();
  return (ANARIInstance) new Instance(deviceState());
}

ANARILight MitsubaDevice::newLight(const char *subtype)
{
  initDevice();
  return (ANARILight)Light::createInstance(subtype, deviceState());
}

ANARIMaterial MitsubaDevice::newMaterial(const char *subtype)
{
  initDevice();
  return (ANARIMaterial)Material::createInstance(subtype, deviceState());
}

ANARIRenderer MitsubaDevice::newRenderer(const char *subtype)
{
  initDevice();
  return (ANARIRenderer)Renderer::createInstance(subtype, deviceState());
}

ANARISampler MitsubaDevice::newSampler(const char *subtype)
{
  initDevice();
  return (ANARISampler)Sampler::createInstance(subtype, deviceState());
}

ANARISpatialField MitsubaDevice::newSpatialField(const char *subtype)
{
  initDevice();
  return (ANARISpatialField)SpatialField::createInstance(
      subtype, deviceState());
}

ANARISurface MitsubaDevice::newSurface()
{
  initDevice();
  return (ANARISurface) new Surface(deviceState());
}

ANARIVolume MitsubaDevice::newVolume(const char *subtype)
{
  initDevice();
  return (ANARIVolume)Volume::createInstance(subtype, deviceState());
}

ANARIWorld MitsubaDevice::newWorld()
{
  initDevice();
  return (ANARIWorld) new World(deviceState());
}

// Query functions ////////////////////////////////////////////////////////////

const char **MitsubaDevice::getObjectSubtypes(ANARIDataType objectType)
{
  return mitsuba_anari::query_object_types(objectType);
}

const void *MitsubaDevice::getObjectInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *infoName,
    ANARIDataType infoType)
{
  return mitsuba_anari::query_object_info(
      objectType, objectSubtype, infoName, infoType);
}

const void *MitsubaDevice::getParameterInfo(ANARIDataType objectType,
    const char *objectSubtype,
    const char *parameterName,
    ANARIDataType parameterType,
    const char *infoName,
    ANARIDataType infoType)
{
  return mitsuba_anari::query_param_info(objectType,
      objectSubtype,
      parameterName,
      parameterType,
      infoName,
      infoType);
}

// Other MitsubaDevice definitions /////////////////////////////////////////////

MitsubaDevice::MitsubaDevice(ANARIStatusCallback cb, const void *ptr)
    : helium::BaseDevice(cb, ptr)
{
  m_state = std::make_unique<MitsubaGlobalState>(this_device());
  deviceCommitParameters();
}

MitsubaDevice::MitsubaDevice(ANARILibrary l) : helium::BaseDevice(l)
{
  m_state = std::make_unique<MitsubaGlobalState>(this_device());
  deviceCommitParameters();
}

MitsubaDevice::~MitsubaDevice()
{
  auto &state = *deviceState();
  state.commitBuffer.clear();
  reportMessage(ANARI_SEVERITY_DEBUG, "destroying Mitsuba ANARI device (%p)", this);
  // The translation cache holds Mitsuba objects (JIT-variant buffers among
  // them) and must be destroyed before the runtime shuts down.
  state.translationCache.reset();
  if (state.backendAcquired)
    MitsubaBackend::release();
}

void MitsubaDevice::initDevice()
{
  if (m_initialized)
    return;
  reportMessage(ANARI_SEVERITY_DEBUG, "initializing Mitsuba ANARI device (%p)", this);
  // The Mitsuba runtime itself is brought up lazily at first render so the
  // 'mitsuba.variant' device parameter can be honored (see Frame.cpp).
  m_initialized = true;
}

void MitsubaDevice::deviceCommitParameters()
{
  auto *state = deviceState();

  // Vendor extension ANARI_MITSUBA_DEVICE_VARIANT: select the Mitsuba
  // rendering variant. Validated against the compiled-in variant list; the
  // runtime availability probe (and scalar fallback) happens at first render.
  const std::string requested =
      getParamString("mitsuba.variant", state->requestedVariant);
  bool known = false;
  for (const auto &v : MitsubaBackend::compiledVariants())
    known = known || v == requested;
  if (!known) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] unknown/uncompiled variant '%s' for 'mitsuba.variant'; "
        "keeping '%s'",
        requested.c_str(),
        state->requestedVariant.c_str());
  } else {
    if (state->backendAcquired && requested != state->activeVariant) {
      reportMessage(ANARI_SEVERITY_WARNING,
          "[mitsuba] 'mitsuba.variant' changed to '%s' after the runtime "
          "already initialized variant '%s'; one variant per process "
          "(ADR 0004) — keeping '%s'",
          requested.c_str(),
          state->activeVariant.c_str(),
          state->activeVariant.c_str());
    }
    state->requestedVariant = requested;
  }

  helium::BaseDevice::deviceCommitParameters();
}

int MitsubaDevice::deviceGetProperty(
    const char *name, ANARIDataType type, void *mem, uint64_t size, uint32_t mask)
{
  static const std::string deviceVersion = MITSUBA_ANARI_VERSION_STRING;
  std::string_view prop = name;
  if (prop == "extension" && type == ANARI_STRING_LIST) {
    helium::writeToVoidP(mem, query_extensions());
    return 1;
  } else if (prop == "version" && type == ANARI_INT32) {
    int version = MITSUBA_ANARI_VERSION_MAJOR * 10000
        + MITSUBA_ANARI_VERSION_MINOR * 100 + MITSUBA_ANARI_VERSION_PATCH;
    helium::writeToVoidP(mem, version);
    return 1;
  } else if (prop == "version.major" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, int(MITSUBA_ANARI_VERSION_MAJOR));
    return 1;
  } else if (prop == "version.minor" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, int(MITSUBA_ANARI_VERSION_MINOR));
    return 1;
  } else if (prop == "version.patch" && type == ANARI_INT32) {
    helium::writeToVoidP(mem, int(MITSUBA_ANARI_VERSION_PATCH));
    return 1;
  } else if (prop == "version.name.size" && type == ANARI_UINT64) {
    helium::writeToVoidP(mem, uint64_t(deviceVersion.size() + 1));
    return 1;
  } else if (prop == "version.name" && type == ANARI_STRING) {
    std::memset(mem, 0, size);
    std::memcpy(mem,
        deviceVersion.c_str(),
        std::min(size, uint64_t(deviceVersion.size() + 1)));
    return 1;
  } else if (prop == "mitsuba" && type == ANARI_BOOL) {
    // Device-identification marker for applications probing which ANARI
    // implementation they are talking to.
    helium::writeToVoidP(mem, true);
    return 1;
  } else if (prop == "mitsuba.variant" && type == ANARI_STRING) {
    // Active variant once the runtime is up; the requested one before that
    // (vendor extension ANARI_MITSUBA_DEVICE_VARIANT).
    auto *state = deviceState();
    const std::string &v = state->backendAcquired ? state->activeVariant
                                                  : state->requestedVariant;
    std::memset(mem, 0, size);
    std::memcpy(mem, v.c_str(), std::min(size, uint64_t(v.size() + 1)));
    return 1;
  } else if (prop == "mitsuba.variant.size" && type == ANARI_UINT64) {
    auto *state = deviceState();
    const std::string &v = state->backendAcquired ? state->activeVariant
                                                  : state->requestedVariant;
    helium::writeToVoidP(mem, uint64_t(v.size() + 1));
    return 1;
  }
  return helium::BaseDevice::deviceGetProperty(name, type, mem, size, mask);
}

MitsubaGlobalState *MitsubaDevice::deviceState() const
{
  return (MitsubaGlobalState *)helium::BaseDevice::m_state.get();
}

} // namespace mitsuba_anari
