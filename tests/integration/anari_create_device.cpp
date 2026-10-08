// mitsuba-anari integration test: create/release the "default" device through
// the public C API, verify device properties and unknown-subtype behavior.
// SPDX-License-Identifier: Apache-2.0
//
// Usage: anari_create_device_test [device-library-dir]

#include <anari/anari.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_warnings = 0;
int g_errors = 0;

void statusFunc(const void * /*userData*/,
    ANARIDevice /*device*/,
    ANARIObject /*source*/,
    ANARIDataType /*sourceType*/,
    ANARIStatusSeverity severity,
    ANARIStatusCode /*code*/,
    const char *message)
{
  if (severity == ANARI_SEVERITY_WARNING)
    ++g_warnings;
  if (severity == ANARI_SEVERITY_ERROR || severity == ANARI_SEVERITY_FATAL_ERROR)
    ++g_errors;
  std::fprintf(stderr, "[anari status %d] %s\n", (int)severity, message);
}

} // namespace

int main(int argc, char **argv)
{
  std::string libraryName = "mitsuba";
  if (argc > 1) {
    std::string dir = argv[1];
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
      dir += '/';
    libraryName += "," + dir;
  }

  ANARILibrary lib = anariLoadLibrary(libraryName.c_str(), statusFunc, nullptr);
  if (!lib) {
    std::fprintf(stderr, "FAIL: anariLoadLibrary returned null\n");
    return 1;
  }

  // --- default device -------------------------------------------------------
  ANARIDevice device = anariNewDevice(lib, "default");
  if (!device) {
    std::fprintf(stderr, "FAIL: anariNewDevice(\"default\") returned null\n");
    return 1;
  }

  // Device property: version must be readable.
  int version = -1;
  if (!anariGetProperty(device,
          device,
          "version",
          ANARI_INT32,
          &version,
          sizeof(version),
          ANARI_WAIT)) {
    std::fprintf(stderr, "FAIL: device property 'version' not available\n");
    return 1;
  }
  std::printf("device version property: %d\n", version);

  // Extension string list must be queryable (may be empty at this stage).
  const char **extensions = nullptr;
  if (!anariGetProperty(device,
          device,
          "extension",
          ANARI_STRING_LIST,
          &extensions,
          sizeof(extensions),
          ANARI_WAIT)
      || !extensions) {
    std::fprintf(stderr, "FAIL: device property 'extension' not available\n");
    return 1;
  }
  for (const char **e = extensions; *e; ++e)
    std::printf("device extension: %s\n", *e);

  anariRelease(device, device);

  // --- unknown device subtype ----------------------------------------------
  // Contract: unknown subtypes are honored with a warning, never silently.
  const int warningsBefore = g_warnings;
  ANARIDevice bogus = anariNewDevice(lib, "definitely_not_a_subtype");
  if (!bogus) {
    std::fprintf(stderr, "FAIL: unknown subtype must still create 'default'\n");
    return 1;
  }
  if (g_warnings <= warningsBefore) {
    std::fprintf(stderr,
        "FAIL: unknown device subtype must emit a status warning\n");
    return 1;
  }
  anariRelease(bogus, bogus);

  if (g_errors > 0) {
    std::fprintf(stderr, "FAIL: unexpected error status callbacks\n");
    return 1;
  }

  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
