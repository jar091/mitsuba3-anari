// mitsuba-anari integration test: verify that runtime introspection is
// truthful — only the device object is advertised at this stage, and every
// advertised item answers info queries with non-null data.
// SPDX-License-Identifier: Apache-2.0
//
// Usage: anari_introspection_test [device-library-dir]

#include <anari/anari.h>

#include <cstdio>
#include <cstring>
#include <string>

static void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
  std::fprintf(stderr, "[anari status %d] %s\n", (int)severity, message);
}

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
  ANARIDevice device = anariNewDevice(lib, "default");
  if (!device) {
    std::fprintf(stderr, "FAIL: anariNewDevice returned null\n");
    return 1;
  }

  int failures = 0;

  // Advertised subtypes must exactly match what is implemented and tested
  // (master prompt §13/§30). Keep in sync with json/MitsubaDefinitions.json
  // and SUPPORTED_FEATURES.md.
  struct ExpectedSubtypes {
    ANARIDataType type;
    const char *name;
    // nullptr-terminated exact expected set (order-insensitive)
    const char *expected[4];
  };
  struct ExpectedSubtypes5 {
    ANARIDataType type;
    const char *name;
    const char *expected[10];
  };
  const ExpectedSubtypes5 cases[] = {
      {ANARI_RENDERER, "renderer", {"default", nullptr}},
      {ANARI_CAMERA, "camera",
          {"perspective", "orthographic", "default", nullptr}},
      {ANARI_GEOMETRY, "geometry",
          {"triangle",
              "quad",
              "sphere",
              "curve",
              "cylinder",
              "cone",
              "isosurface",
              nullptr}},
      {ANARI_MATERIAL, "material", {"matte", "physicallyBased", nullptr}},
      {ANARI_LIGHT, "light",
          {"directional", "point", "quad", "spot", "hdri", nullptr}},
      {ANARI_SAMPLER, "sampler", {"image1D", "image2D", nullptr}},
      {ANARI_VOLUME, "volume", {"transferFunction1D", nullptr}},
      {ANARI_SPATIAL_FIELD,
          "spatial field",
          {"structuredRegular", "unstructured", nullptr}},
  };

  for (const auto &c : cases) {
    const char **subtypes = anariGetObjectSubtypes(device, c.type);
    // Every advertised subtype must be expected.
    for (const char **s = subtypes; s && *s; ++s) {
      bool found = false;
      for (const char *const *e = c.expected; *e; ++e)
        found = found || std::strcmp(*s, *e) == 0;
      if (!found) {
        std::fprintf(stderr,
            "FAIL: %s advertises unexpected subtype '%s'\n",
            c.name,
            *s);
        ++failures;
      }
    }
    // Every expected subtype must be advertised.
    for (const char *const *e = c.expected; *e; ++e) {
      bool found = false;
      for (const char **s = subtypes; s && *s; ++s)
        found = found || std::strcmp(*s, *e) == 0;
      if (!found) {
        std::fprintf(stderr,
            "FAIL: %s does not advertise expected subtype '%s'\n",
            c.name,
            *e);
        ++failures;
      }
    }
  }

  anariRelease(device, device);
  anariUnloadLibrary(lib);

  if (failures) {
    std::fprintf(stderr, "FAIL (%d failures)\n", failures);
    return 1;
  }
  std::printf("PASS\n");
  return 0;
}
