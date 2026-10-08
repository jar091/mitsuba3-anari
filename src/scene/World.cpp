// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0

#include "World.h"

// std
#include <algorithm>
#include <cstring>
#include <limits>

namespace mitsuba_anari {

World::World(MitsubaGlobalState *s)
    : Object(ANARI_WORLD, s),
      m_zeroSurfaceData(this),
      m_zeroVolumeData(this),
      m_zeroLightData(this),
      m_instanceData(this)
{
  m_zeroGroup = new Group(s);
  m_zeroInstance = new Instance(s);
  m_zeroInstance->setParamDirect("group", m_zeroGroup.ptr);

  // never any public ref to these objects
  m_zeroGroup->refDec(helium::RefType::PUBLIC);
  m_zeroInstance->refDec(helium::RefType::PUBLIC);
}

World::~World() = default;

bool World::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t size,
    uint32_t flags)
{
  if (name == "bounds" && type == ANARI_FLOAT32_BOX3) {
    // World-space bounds over all instanced triangle geometry (PyNARI's
    // viewer and world.getBounds() rely on this property).
    float3 lo(std::numeric_limits<float>::max());
    float3 hi(std::numeric_limits<float>::lowest());
    bool any = false;
    for (const Instance *inst : m_instances) {
      if (!inst || !inst->isValid() || !inst->group())
        continue;
      const bool xfmIsIdent = inst->xfmIsIdentity();
      for (const Surface *surface : inst->group()->surfaces()) {
        if (!surface || !surface->isValid())
          continue;
        const Array1D *posArray = nullptr;
        float radius = 0.f;
        if (const auto *mgeo =
                dynamic_cast<const MeshGeometry *>(surface->geometry())) {
          posArray = mgeo->vertexPositions();
        } else if (const auto *sph = dynamic_cast<const SphereGeometry *>(
                       surface->geometry())) {
          posArray = sph->centers();
          radius = sph->uniformRadius(); // conservative for per-vertex radii
          if (sph->vertexRadii()) {
            const auto *r = (const float *)sph->vertexRadii()->data();
            for (uint64_t i = 0; i < sph->vertexRadii()->size(); ++i)
              radius = std::max(radius, r[i]);
          }
        }
        if (!posArray)
          continue;
        const auto *pos = posArray->beginAs<float3>();
        const uint64_t n = posArray->size();
        for (uint64_t i = 0; i < n; ++i) {
          float3 p = pos[i];
          if (!xfmIsIdent) {
            const float4 r = linalg::mul(inst->xfm(), float4(p, 1.f));
            p = float3(r.x, r.y, r.z);
          }
          lo = linalg::min(lo, p - float3(radius));
          hi = linalg::max(hi, p + float3(radius));
          any = true;
        }
      }
    }
    if (!any) {
      lo = float3(-1.f);
      hi = float3(1.f);
    }
    float out[6] = {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z};
    std::memcpy(ptr, out, std::min(sizeof(out), size_t(size)));
    return true;
  }

  return Object::getProperty(name, type, ptr, size, flags);
}

void World::commitParameters()
{
  m_zeroSurfaceData = getParamObject<ObjectArray>("surface");
  m_zeroVolumeData = getParamObject<ObjectArray>("volume");
  m_zeroLightData = getParamObject<ObjectArray>("light");
  m_instanceData = getParamObject<ObjectArray>("instance");
}

void World::finalize()
{
  const bool addZeroInstance =
      m_zeroSurfaceData || m_zeroVolumeData || m_zeroLightData;
  if (addZeroInstance)
    reportMessage(ANARI_SEVERITY_DEBUG, "mitsuba_anari::World will add zero instance");

  if (m_zeroSurfaceData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "mitsuba_anari::World found %zu surfaces in zero instance",
        m_zeroSurfaceData->size());
    m_zeroGroup->setParamDirect("surface", getParamDirect("surface"));
  } else
    m_zeroGroup->removeParam("surface");

  if (m_zeroVolumeData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "mitsuba_anari::World found %zu volumes in zero instance",
        m_zeroVolumeData->size());
    m_zeroGroup->setParamDirect("volume", getParamDirect("volume"));
  } else
    m_zeroGroup->removeParam("volume");

  if (m_zeroLightData) {
    reportMessage(ANARI_SEVERITY_DEBUG,
        "mitsuba_anari::World found %zu lights in zero instance",
        m_zeroLightData->size());
    m_zeroGroup->setParamDirect("light", getParamDirect("light"));
  } else
    m_zeroGroup->removeParam("light");

  m_zeroInstance->setParam("id", getParam<uint32_t>("id", ~0u));

  m_zeroGroup->commitParameters();
  m_zeroGroup->finalize();
  m_zeroInstance->commitParameters();
  m_zeroInstance->finalize();

  m_instances.clear();

  if (m_instanceData) {
    std::for_each(m_instanceData->handlesBegin(),
        m_instanceData->handlesEnd(),
        [&](auto *o) {
          if (o && o->isValid())
            m_instances.push_back((Instance *)o);
        });
  }

  if (addZeroInstance)
    m_instances.push_back(m_zeroInstance.ptr);
}

const std::vector<Instance *> &World::instances() const
{
  return m_instances;
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::World *);
