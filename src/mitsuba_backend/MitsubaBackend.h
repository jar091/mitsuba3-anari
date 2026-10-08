// mitsuba-anari: process-global Mitsuba runtime bring-up/teardown.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace mitsuba_anari {

// Refcounted process-global initialization of the Mitsuba runtime (static
// initializers, file resolver, log routing, JIT backend bring-up,
// acceleration-structure globals). Mirrors the canonical sequence in
// Mitsuba's own src/mitsuba/mitsuba.cpp.
//
// One rendering variant is active per process (ADR 0004): it is fixed by the
// first successful acquire(). Later requests for a different variant keep the
// active one and report through the sink — never silent (master prompt §7.3).
struct MitsubaBackend {
  using LogSink = std::function<void(int /*anariSeverity*/, const char *)>;

  // Acquire a reference; the first acquisition initializes the runtime with
  // the requested variant (probing JIT availability; falls back to
  // scalar_rgb with a message when the requested backend is unavailable).
  // Returns the ACTIVE variant name, or an empty string on failure.
  static std::string acquire(const LogSink &sink, const std::string &requestedVariant);
  // Drop a reference; the last release shuts the runtime down.
  static void release();

  // Route Mitsuba log output to the given sink (last writer wins).
  static void setLogSink(const LogSink &sink);

  // Variants compiled into the linked Mitsuba build.
  static std::vector<std::string> compiledVariants();
};

} // namespace mitsuba_anari
