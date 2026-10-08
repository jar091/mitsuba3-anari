// mitsuba-anari: ANARI library entry object for the "mitsuba" device library.
// SPDX-License-Identifier: Apache-2.0

#include "Library.h"

#include "device/MitsubaDevice.h"
#include "mitsuba_anari_export.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

namespace mitsuba_anari {

// Generated introspection (anari_generate_queries)
const char **query_extensions();

static void reportStatus(anari::LibraryImpl &lib,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *format,
    ...)
{
  auto cb = lib.defaultStatusCB();
  if (!cb)
    return;

  va_list args;
  va_start(args, format);
  char buffer[512];
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // A library has no device yet; the source object is the library itself.
  cb(lib.defaultStatusCBUserPtr(),
      nullptr,
      reinterpret_cast<ANARIObject>(lib.this_library()),
      ANARI_LIBRARY,
      severity,
      code,
      buffer);
}

Library::Library(
    void *lib, ANARIStatusCallback defaultStatusCB, const void *statusCBPtr)
    : anari::LibraryImpl(lib, defaultStatusCB, statusCBPtr)
{}

ANARIDevice Library::newDevice(const char *subtype)
{
  // Only the "default" subtype exists. Unknown names are honored with a
  // warning (matching reference implementations), never silently.
  if (subtype && std::strcmp(subtype, "default") != 0) {
    reportStatus(*this,
        ANARI_SEVERITY_WARNING,
        ANARI_STATUS_INVALID_ARGUMENT,
        "[mitsuba] unknown device subtype '%s'; creating 'default' instead",
        subtype);
  }
  return (ANARIDevice) new MitsubaDevice(this_library());
}

const char **Library::getDeviceExtensions(const char * /*deviceType*/)
{
  return query_extensions();
}

const char **Library::getDeviceSubtypes()
{
  static const char *subtypes[] = {"default", nullptr};
  return subtypes;
}

} // namespace mitsuba_anari

// ---------------------------------------------------------------------------
// Exported ANARI entry point: resolved by the ANARI-SDK loader as
// anari_library_mitsuba_new_library().
// ---------------------------------------------------------------------------
namespace {

// The Mitsuba runtime (Dr.Jit, nanothread) installs thread-specific keys and
// exit handlers that do not survive unloading the image mid-process: after
// the ANARI loader's dlclose(), they point into unmapped memory and the host
// process crashes at exit (observed on macOS arm64). Pin this image (and
// thereby its Mitsuba dependencies) with RTLD_NODELETE on first library
// creation — anariUnloadLibrary then unloads the handle but keeps the image
// mapped. One-per-process runtime residency is already the documented model
// (ADR 0004); CUDA-based ANARI devices use the same self-pinning pattern.
void pinLibraryInProcess()
{
#if !defined(_WIN32)
  static bool pinned = false;
  if (pinned)
    return;
  Dl_info info{};
  if (dladdr(reinterpret_cast<void *>(&pinLibraryInProcess), &info)
      && info.dli_fname) {
    // The returned handle is intentionally never dlclose()d.
    if (dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE))
      pinned = true;
  }
#endif
}

} // namespace

extern "C" MITSUBA_ANARI_EXPORT ANARI_DEFINE_LIBRARY_ENTRYPOINT(
    mitsuba, handle, scb, scbPtr)
{
  pinLibraryInProcess();
  return (ANARILibrary) new mitsuba_anari::Library(handle, scb, scbPtr);
}
