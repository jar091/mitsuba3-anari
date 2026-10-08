// mitsuba-anari example: load the "mitsuba" ANARI library and print what it
// reports about itself (subtypes, extensions). A standard ANARI client — no
// internal headers.
// SPDX-License-Identifier: Apache-2.0
//
// Usage:
//   mitsubaAnariInfo [device-library-dir]

#include <anari/anari.h>

#include <cstdio>
#include <string>

static void statusFunc(const void * /*userData*/,
    ANARIDevice /*device*/,
    ANARIObject /*source*/,
    ANARIDataType /*sourceType*/,
    ANARIStatusSeverity severity,
    ANARIStatusCode /*code*/,
    const char *message)
{
  std::fprintf(stderr, "[status %d] %s\n", (int)severity, message);
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
    std::fprintf(stderr, "failed to load ANARI library 'mitsuba'\n");
    return 1;
  }

  std::printf("library: mitsuba\n");

  const char **subtypes = anariGetDeviceSubtypes(lib);
  for (const char **s = subtypes; s && *s; ++s) {
    std::printf("device subtype: %s\n", *s);
    const char **extensions = anariGetDeviceExtensions(lib, *s);
    int count = 0;
    for (const char **e = extensions; e && *e; ++e, ++count)
      std::printf("  extension: %s\n", *e);
    if (count == 0)
      std::printf("  (no extensions advertised)\n");
  }

  anariUnloadLibrary(lib);
  return 0;
}
