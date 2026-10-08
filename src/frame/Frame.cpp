// Copyright 2024-2026 The Khronos Group
// Copyright 2026 IT4Innovations, VSB-Technical University of Ostrava
// SPDX-License-Identifier: Apache-2.0
// Derived from the ANARI-SDK empty_helium_device skeleton; see THIRD_PARTY.md.

#include "Frame.h"

#include "mitsuba_backend/MitsubaBackend.h"
#include "scene/MitsubaSceneBuilder.h"

// std
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

namespace mitsuba_anari {

Frame::Frame(MitsubaGlobalState *s) : helium::BaseFrame(s) {}

Frame::~Frame()
{
  wait();
}

bool Frame::isValid() const
{
  return m_valid;
}

MitsubaGlobalState *Frame::deviceState() const
{
  return (MitsubaGlobalState *)helium::BaseObject::m_state;
}

void Frame::commitParameters()
{
  m_renderer = getParamObject<Renderer>("renderer");
  m_camera = getParamObject<Camera>("camera");
  m_world = getParamObject<World>("world");
  m_colorType = getParam<anari::DataType>("channel.color", ANARI_UNKNOWN);
  m_depthType = getParam<anari::DataType>("channel.depth", ANARI_UNKNOWN);
  m_size = getParam<uint2>("size", uint2(0u, 0u));
}

void Frame::finalize()
{
  if (!m_renderer) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "missing required parameter 'renderer' on frame");
  }

  if (!m_camera) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'camera' on frame");
  }

  if (!m_world) {
    reportMessage(
        ANARI_SEVERITY_WARNING, "missing required parameter 'world' on frame");
  }

  if (m_size.x == 0 || m_size.y == 0) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "frame 'size' is zero (%u x %u)",
        m_size.x,
        m_size.y);
  }

  if (m_colorType != ANARI_UNKNOWN && m_colorType != ANARI_UFIXED8_VEC4
      && m_colorType != ANARI_UFIXED8_RGBA_SRGB
      && m_colorType != ANARI_FLOAT32_VEC4) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "unsupported 'channel.color' type; supported: UFIXED8_VEC4, "
        "UFIXED8_RGBA_SRGB, FLOAT32_VEC4");
    m_colorType = ANARI_UNKNOWN;
  }

  if (m_depthType != ANARI_UNKNOWN && m_depthType != ANARI_FLOAT32) {
    reportMessage(ANARI_SEVERITY_WARNING,
        "[mitsuba] 'channel.depth' only supports ANARI_FLOAT32; "
        "the channel is disabled");
    m_depthType = ANARI_UNKNOWN;
  }

  const auto numPixels = size_t(m_size.x) * size_t(m_size.y);
  m_perPixelBytes = 4 * (m_colorType == ANARI_FLOAT32_VEC4 ? 4 : 1);
  m_pixelBuffer.resize(numPixels * m_perPixelBytes);
  std::fill(m_pixelBuffer.begin(), m_pixelBuffer.end(), uint8_t(0));
  m_depthBuffer.clear();
  if (m_depthType == ANARI_FLOAT32)
    m_depthBuffer.resize(numPixels, 0.f);

  m_valid = m_renderer && m_renderer->isValid() && m_camera
      && m_camera->isValid() && m_world && m_world->isValid()
      && m_colorType != ANARI_UNKNOWN && m_size.x > 0 && m_size.y > 0;
}

bool Frame::getProperty(const std::string_view &name,
    ANARIDataType type,
    void *ptr,
    uint64_t /*size*/,
    uint32_t /*flags*/)
{
  if (type == ANARI_FLOAT32 && name == "duration") {
    helium::writeToVoidP(ptr, m_duration);
    return true;
  }

  return false;
}

void Frame::renderFrame()
{
  // The deferred commit buffer is flushed by the frame, not by helium — this
  // is the single point where staged ANARI state becomes renderable state.
  deviceState()->commitBuffer.flush();

  const auto start = std::chrono::steady_clock::now();
  m_duration = 0.f;

  auto *state = deviceState();

  // The process-global Mitsuba runtime is brought up on first render so that
  // the device-level 'mitsuba.variant' parameter (vendor extension
  // ANARI_MITSUBA_DEVICE_VARIANT) can be honored; the active variant is fixed
  // per process from then on (ADR 0004).
  if (!state->backendAcquired) {
    state->activeVariant = MitsubaBackend::acquire(
        [this](int severity, const char *msg) {
          reportMessage((ANARIStatusSeverity)severity, "[mitsuba] %s", msg);
        },
        state->requestedVariant);
    state->backendAcquired = !state->activeVariant.empty();
    state->mitsubaReady = state->backendAcquired;
    if (state->mitsubaReady) {
      reportMessage(ANARI_SEVERITY_INFO,
          "[mitsuba] Mitsuba runtime ready (variant: %s)",
          state->activeVariant.c_str());
    }
  }

  if (!state->mitsubaReady) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[mitsuba] cannot render: the Mitsuba runtime failed to initialize");
    return;
  }

  if (!isValid()) {
    reportMessage(ANARI_SEVERITY_ERROR,
        "[mitsuba] cannot render: frame is incomplete or invalid "
        "(renderer/camera/world/size/channel.color)");
    return;
  }

  try {
    // Documented early-milestone shortcut (§9): full scene translation per
    // render, isolated behind the scene builder; incremental updates come
    // with the object cache in Milestone 11. Timings are the §26 counters.
    // Restart accumulation when anything in the scene changed since the last
    // render (any object finalization) or the frame was reconfigured.
    const auto sceneStamp = state->commitBuffer.lastObjectFinalization();
    const size_t numPixels = size_t(m_size.x) * size_t(m_size.y);
    if (sceneStamp > m_accumStamp || m_accumBuffer.size() != numPixels) {
      m_accumBuffer.assign(numPixels, float4(0.f, 0.f, 0.f, 0.f));
      m_accumCount = 0;
      m_accumStamp = sceneStamp;
    }
    m_accumCount++;
    const float accumWeight = 1.f / float(m_accumCount);

    RenderStats stats;
    mitsuba::ref<mitsuba::Bitmap> depthBitmap;
    auto bitmap = renderMitsubaScene(state->activeVariant,
        state,
        *m_world,
        *m_camera,
        *m_renderer,
        m_size,
        !m_depthBuffer.empty(),
        depthBitmap,
        stats,
        m_accumCount - 1);


    const float4 bgColor = m_renderer->background();
    const bool bgImage = m_renderer->hasBackgroundImage();
    const auto *src = (const float *)bitmap->data();
    const float *depthSrc =
        depthBitmap ? (const float *)depthBitmap->data() : nullptr;
    const uint32_t w = m_size.x;
    const uint32_t h = m_size.y;
    for (uint32_t y = 0; y < h; ++y) {
      // Mitsuba's row 0 is the image top; ANARI's origin is bottom-left.
      const float *row = src + size_t(h - 1 - y) * w * 4;
      const float *depthRow =
          depthSrc ? depthSrc + size_t(h - 1 - y) * w : nullptr;
      for (uint32_t x = 0; x < w; ++x) {
        const float4 c(row[x * 4 + 0], row[x * 4 + 1], row[x * 4 + 2],
            row[x * 4 + 3]);
        // Composite the (premultiplied) rendered image over the ANARI
        // background color or image (stretched over the frame).
        const float4 bg = bgImage
            ? m_renderer->backgroundAt((x + 0.5f) / float(w), (y + 0.5f) / float(h))
            : bgColor;
        const float ia = 1.f - c.w;
        const float4 outc(c.x + ia * bg.x,
            c.y + ia * bg.y,
            c.z + ia * bg.z,
            c.w + ia * bg.w);
        float4 &accum = m_accumBuffer[size_t(y) * w + x];
        accum += outc;
        PixelSample s(accum * accumWeight);
        if (depthRow) {
          // Primary-hit distance; misses develop to 0 -> report the ANARI
          // convention of +inf for the background (ADR 0005).
          const float d = depthRow[x];
          s.depth = d > 0.f ? d : std::numeric_limits<float>::infinity();
        }
        writeSample((int)x, (int)y, s);
      }
    }

    const auto end = std::chrono::steady_clock::now();
    m_duration = std::chrono::duration<float>(end - start).count();
    reportMessage(ANARI_SEVERITY_DEBUG,
        "[mitsuba] frame rendered (%s): translate %.2f ms (%zu shapes, %zu "
        "emitters, cache %zu hit/%zu miss%s), render+extract %.2f ms",
        state->activeVariant.c_str(),
        stats.translateMs,
        stats.shapeCount,
        stats.emitterCount,
        stats.cacheHits,
        stats.cacheMisses,
        stats.sceneReused ? ", scene reused" : "",
        stats.renderMs);
  } catch (const std::exception &e) {
    reportMessage(
        ANARI_SEVERITY_ERROR, "[mitsuba] render failed: %s", e.what());
  }
}

void *Frame::map(std::string_view channel,
    uint32_t *width,
    uint32_t *height,
    ANARIDataType *pixelType)
{
  wait();

  *width = m_size.x;
  *height = m_size.y;

  if (channel == "channel.color" && m_colorType != ANARI_UNKNOWN) {
    *pixelType = m_colorType;
    return m_pixelBuffer.data();
  } else if (channel == "channel.depth" && !m_depthBuffer.empty()) {
    *pixelType = ANARI_FLOAT32;
    return m_depthBuffer.data();
  } else {
    *width = 0;
    *height = 0;
    *pixelType = ANARI_UNKNOWN;
    return nullptr;
  }
}

void Frame::unmap(std::string_view /*channel*/)
{
  // no-op
}

int Frame::frameReady(ANARIWaitMask m)
{
  if (m == ANARI_NO_WAIT)
    return ready();
  else {
    wait();
    return 1;
  }
}

void Frame::discard()
{
  // Rendering is synchronous for now (documented conservative threading
  // model); there is nothing in flight to discard.
}

bool Frame::ready() const
{
  // Rendering is synchronous inside renderFrame() for now.
  return true;
}

void Frame::wait() const
{
  // Rendering is synchronous inside renderFrame() for now.
}

void Frame::writeSample(int x, int y, const PixelSample &s)
{
  const auto idx = size_t(y) * m_size.x + size_t(x);
  auto *color = m_pixelBuffer.data() + (idx * m_perPixelBytes);
  switch (m_colorType) {
  case ANARI_UFIXED8_VEC4: {
    auto c = helium::cvt_color_to_uint32(s.color);
    std::memcpy(color, &c, sizeof(c));
    break;
  }
  case ANARI_UFIXED8_RGBA_SRGB: {
    auto c = helium::cvt_color_to_uint32_srgb(s.color);
    std::memcpy(color, &c, sizeof(c));
    break;
  }
  case ANARI_FLOAT32_VEC4: {
    std::memcpy(color, &s.color, sizeof(s.color));
    break;
  }
  default:
    break;
  }
  if (!m_depthBuffer.empty())
    m_depthBuffer[idx] = s.depth;
}

} // namespace mitsuba_anari

MITSUBA_ANARI_TYPEFOR_DEFINITION(mitsuba_anari::Frame *);
