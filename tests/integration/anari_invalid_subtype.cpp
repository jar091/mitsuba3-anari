// mitsuba-anari integration test: unknown object subtypes must produce a
// status warning, yield an invalid (but lifetime-correct) object, and never
// crash or silently succeed (master prompt §12/§30).
// SPDX-License-Identifier: Apache-2.0
//
// Usage: anari_invalid_subtype_test [device-library-dir]

#include <anari/anari.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_warnings = 0;
int g_errors = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  if (severity == ANARI_SEVERITY_WARNING)
    ++g_warnings;
  if (severity == ANARI_SEVERITY_ERROR || severity == ANARI_SEVERITY_FATAL_ERROR)
    ++g_errors;
  std::fprintf(stderr, "[anari status %d] %s\n", (int)severity, message);
}

#define REQUIRE(cond)                                                          \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAIL(%d): %s\n", __LINE__, #cond);                 \
      return 1;                                                                \
    }                                                                          \
  } while (0)

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
  REQUIRE(lib != nullptr);
  ANARIDevice device = anariNewDevice(lib, "default");
  REQUIRE(device != nullptr);

  struct Case {
    const char *what;
    ANARIObject handle;
  };

  int before = g_warnings;
  ANARIGeometry geom = anariNewGeometry(device, "no_such_geometry");
  REQUIRE(geom != nullptr);
  REQUIRE(g_warnings > before); // must warn, not silently succeed

  before = g_warnings;
  ANARIMaterial mat = anariNewMaterial(device, "no_such_material");
  REQUIRE(mat != nullptr);
  REQUIRE(g_warnings > before);

  before = g_warnings;
  ANARICamera cam = anariNewCamera(device, "no_such_camera");
  REQUIRE(cam != nullptr);
  REQUIRE(g_warnings > before);

  // Invalid objects must still be parameterizable, committable, queryable,
  // and releasable without errors or crashes.
  float dummy[3] = {0.f, 0.f, 0.f};
  anariSetParameter(device, cam, "position", ANARI_FLOAT32_VEC3, dummy);
  anariCommitParameters(device, cam);

  unsigned char valid = 0xff; // ANARI_BOOL is a single byte
  REQUIRE(anariGetProperty(device,
      geom,
      "valid",
      ANARI_BOOL,
      &valid,
      sizeof(valid),
      ANARI_WAIT));
  REQUIRE(valid == 0); // unknown subtype => invalid object

  anariRelease(device, geom);
  anariRelease(device, mat);
  anariRelease(device, cam);

  REQUIRE(g_errors == 0);

  const int warningsBeforeRelease = g_warnings;
  anariRelease(device, device);
  REQUIRE(g_warnings == warningsBeforeRelease); // no leaks

  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
