// mitsuba-anari integration smoke test: load the "mitsuba" ANARI library
// exactly like an external application would — through the public C API only.
// SPDX-License-Identifier: Apache-2.0
//
// Usage:
//   anari_load_library_test [device-library-dir]
//   anari_load_library_test --install-tree
//
// When a directory is given, the ANARI-SDK "name,path/" syntax pins the loader
// to that directory (used for both build-tree and install-tree runs, so the
// test never silently picks up a stale library from the OS search path).
//
// --install-tree reads the MITSUBA_ANARI_INSTALL_PREFIX environment variable
// and loads the library from the installed layout: bin/ on Windows,
// CMAKE_INSTALL_LIBDIR elsewhere. That directory is *not* always "lib" —
// GNUInstallDirs resolves it to "lib64" on RHEL-family 64-bit distributions —
// so the build passes it in as MITSUBA_ANARI_INSTALL_LIBDIR rather than
// hardcoding it here. Exits with code 77 (CTest SKIP_RETURN_CODE) when the
// variable is not set — CI always sets it after `cmake --install`.

#include <anari/anari.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int g_statusCalls = 0;

void statusFunc(const void * /*userData*/,
    ANARIDevice /*device*/,
    ANARIObject /*source*/,
    ANARIDataType /*sourceType*/,
    ANARIStatusSeverity severity,
    ANARIStatusCode /*code*/,
    const char *message)
{
  ++g_statusCalls;
  std::fprintf(stderr, "[anari status] severity=%d: %s\n", (int)severity, message);
}

} // namespace

int main(int argc, char **argv)
{
  std::string dir;
  if (argc > 1 && std::strcmp(argv[1], "--install-tree") == 0) {
    const char *prefix = std::getenv("MITSUBA_ANARI_INSTALL_PREFIX");
    if (!prefix || !*prefix) {
      std::printf("SKIP: MITSUBA_ANARI_INSTALL_PREFIX not set\n");
      return 77;
    }
    dir = prefix;
#ifdef _WIN32
    dir += "\\bin";
#else
    dir += "/";
    dir += MITSUBA_ANARI_INSTALL_LIBDIR;
#endif
  } else if (argc > 1) {
    dir = argv[1];
  }

  std::string libraryName = "mitsuba";
  if (!dir.empty()) {
    if (dir.back() != '/' && dir.back() != '\\')
      dir += '/';
    libraryName += "," + dir;
  }

  std::printf("loading ANARI library '%s'\n", libraryName.c_str());
  ANARILibrary lib = anariLoadLibrary(libraryName.c_str(), statusFunc, nullptr);
  if (!lib) {
    std::fprintf(stderr, "FAIL: anariLoadLibrary returned null\n");
    return 1;
  }

  const char **subtypes = anariGetDeviceSubtypes(lib);
  if (!subtypes) {
    std::fprintf(stderr, "FAIL: anariGetDeviceSubtypes returned null\n");
    return 1;
  }
  bool foundDefault = false;
  for (const char **s = subtypes; *s; ++s) {
    std::printf("device subtype: %s\n", *s);
    if (std::strcmp(*s, "default") == 0)
      foundDefault = true;
  }
  if (!foundDefault) {
    std::fprintf(stderr, "FAIL: device subtype 'default' not reported\n");
    return 1;
  }

  const char **extensions = anariGetDeviceExtensions(lib, "default");
  if (!extensions) {
    std::fprintf(stderr, "FAIL: anariGetDeviceExtensions returned null\n");
    return 1;
  }
  // Honesty contract: every advertised extension must be on the list of
  // extensions this device actually implements (kept in sync with
  // json/MitsubaDefinitions.json and SUPPORTED_FEATURES.md).
  static const char *implemented[] = {
      "ANARI_KHR_DEVICE_SYNCHRONIZATION", // helium per-object locking
      "ANARI_KHR_CAMERA_PERSPECTIVE",
      "ANARI_KHR_GEOMETRY_TRIANGLE",
      "ANARI_KHR_MATERIAL_MATTE",
      "ANARI_KHR_GEOMETRY_QUAD",
      "ANARI_KHR_GEOMETRY_SPHERE",
      "ANARI_KHR_GEOMETRY_CURVE",
      "ANARI_KHR_LIGHT_DIRECTIONAL",
      "ANARI_KHR_LIGHT_POINT",
      "ANARI_KHR_LIGHT_QUAD",
      "ANARI_KHR_LIGHT_HDRI",
      "ANARI_KHR_CAMERA_ORTHOGRAPHIC",
      "ANARI_KHR_MATERIAL_PHYSICALLY_BASED",
      "ANARI_KHR_SAMPLER_IMAGE2D",
      "ANARI_KHR_SAMPLER_IMAGE1D",
      "ANARI_KHR_FRAME_ACCUMULATION",
      "ANARI_KHR_LIGHT_SPOT",
      "ANARI_KHR_GEOMETRY_CONE",
      "ANARI_KHR_GEOMETRY_CYLINDER",
      "ANARI_KHR_GEOMETRY_ISOSURFACE",
      "ANARI_KHR_CAMERA_DEPTH_OF_FIELD",
      "ANARI_KHR_RENDERER_AMBIENT_LIGHT",
      "ANARI_KHR_RENDERER_BACKGROUND_COLOR",
      "ANARI_KHR_RENDERER_BACKGROUND_IMAGE",
      "ANARI_KHR_SPATIAL_FIELD_STRUCTURED_REGULAR",
      "ANARI_KHR_SPATIAL_FIELD_UNSTRUCTURED",
      "ANARI_KHR_VOLUME_TRANSFER_FUNCTION1D",
      "ANARI_MITSUBA_DEVICE_VARIANT", // vendor: 'mitsuba.variant' selection
  };
  for (const char **e = extensions; *e; ++e) {
    bool known = false;
    for (const char *i : implemented)
      known = known || std::strcmp(*e, i) == 0;
    if (!known) {
      std::fprintf(stderr,
          "FAIL: library advertises extension '%s' which is not on the "
          "implemented list\n",
          *e);
      return 1;
    }
    std::printf("extension: %s\n", *e);
  }

  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
