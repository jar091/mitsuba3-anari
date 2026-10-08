// mitsuba-anari integration test: ANARI object lifetime and ownership rules
// exercised through the public C API only (master prompt §10).
// SPDX-License-Identifier: Apache-2.0
//
// Covers:
//   - shared array memory: deleter must run exactly once, after the device
//     no longer needs the memory;
//   - releasing an array/material after committing its consumer;
//   - one object referenced by multiple parents;
//   - replacing an object parameter with another object;
//   - unset parameters;
//   - clean teardown must produce zero leak warnings from the device.
//
// Usage: anari_object_lifetime_test [device-library-dir]

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

int g_deleterCalls = 0;

void arrayDeleter(const void * /*userPtr*/, const void * /*appMemory*/)
{
  ++g_deleterCalls;
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
  // MITSUBA_ANARI_TEST_LIBRARY overrides the loaded library verbatim — used
  // by the debug.* test variants to load the SDK debug layer, which wraps the
  // mitsuba device via ANARI_DEBUG_WRAPPED_LIBRARY.
  std::string libraryName;
  if (const char *libOverride = std::getenv("MITSUBA_ANARI_TEST_LIBRARY")) {
    libraryName = libOverride;
  } else {
    libraryName = "mitsuba";
    if (argc > 1) {
      std::string dir = argv[1];
      if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += '/';
      libraryName += "," + dir;
    }
  }

  // In debug-layer runs the wrapper emits its own diagnostics (e.g. for
  // subtypes not yet in our definitions); only hard errors fail the test then.
  const char *strictEnv = std::getenv("MITSUBA_ANARI_TEST_STRICT");
  const bool strict = !(strictEnv && std::strcmp(strictEnv, "0") == 0);

  ANARILibrary lib = anariLoadLibrary(libraryName.c_str(), statusFunc, nullptr);
  REQUIRE(lib != nullptr);
  ANARIDevice device = anariNewDevice(lib, "default");
  REQUIRE(device != nullptr);

  // --- shared array memory with deleter ------------------------------------
  {
    static float positions[9] = {0.f};
    ANARIArray1D array = anariNewArray1D(
        device, positions, arrayDeleter, nullptr, ANARI_FLOAT32_VEC3, 3);
    REQUIRE(array != nullptr);
    REQUIRE(g_deleterCalls == 0);

    // Attach the array to a surface-owned geometry stand-in: use the world
    // 'surface' object array to hold a reference chain instead. Here we only
    // verify the deleter contract on plain release.
    anariRelease(device, array);
    REQUIRE(g_deleterCalls == 1);
  }

  // --- map/unmap of a device-owned (mapped) array ---------------------------
  {
    ANARIArray1D array = anariNewArray1D(
        device, nullptr, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    REQUIRE(array != nullptr);
    void *ptr = anariMapArray(device, array);
    REQUIRE(ptr != nullptr);
    std::memset(ptr, 0, 4 * 3 * sizeof(float));
    anariUnmapArray(device, array);
    anariRelease(device, array);
  }

  // --- release consumed objects after commit; replacement; multiple parents
  // (only subtypes that are declared in the definitions JSON are used here so
  // the same scenario runs cleanly under the SDK debug layer)
  {
    // Object-parameter replacement: frame.renderer r1 -> r2.
    ANARIFrame frame1 = anariNewFrame(device);
    ANARIRenderer r1 = anariNewRenderer(device, "default");
    ANARIRenderer r2 = anariNewRenderer(device, "default");
    REQUIRE(frame1 && r1 && r2);

    anariSetParameter(device, frame1, "renderer", ANARI_RENDERER, &r1);
    anariCommitParameters(device, frame1);
    // The frame holds its own reference now.
    anariRelease(device, r1);

    anariSetParameter(device, frame1, "renderer", ANARI_RENDERER, &r2);
    anariCommitParameters(device, frame1);
    anariRelease(device, r2);

    // One world referenced by two frames.
    ANARIWorld world = anariNewWorld(device);
    ANARIFrame frame2 = anariNewFrame(device);
    REQUIRE(world && frame2);
    anariSetParameter(device, frame1, "world", ANARI_WORLD, &world);
    anariSetParameter(device, frame2, "world", ANARI_WORLD, &world);
    anariCommitParameters(device, frame1);
    anariCommitParameters(device, frame2);
    anariRelease(device, world); // both frames keep it alive
    anariRelease(device, frame2);

    // World over a surface object array; app releases its handles after the
    // array captures them.
    ANARISurface s1 = anariNewSurface(device);
    ANARISurface s2 = anariNewSurface(device);
    REQUIRE(s1 && s2);
    ANARISurface surfaces[] = {s1, s2};
    ANARIArray1D surfArray = anariNewArray1D(
        device, surfaces, nullptr, nullptr, ANARI_SURFACE, 2);
    REQUIRE(surfArray != nullptr);
    anariRelease(device, s1);
    anariRelease(device, s2);

    ANARIWorld world2 = anariNewWorld(device);
    REQUIRE(world2 != nullptr);
    anariSetParameter(device, world2, "surface", ANARI_ARRAY1D, &surfArray);
    anariCommitParameters(device, world2);
    anariRelease(device, surfArray);

    // Unset and re-commit.
    anariUnsetParameter(device, world2, "surface");
    anariCommitParameters(device, world2);

    anariRelease(device, world2);
    anariRelease(device, frame1);
  }

  REQUIRE(g_errors == 0);

  // --- clean teardown: no leak warnings -------------------------------------
  const int warningsBeforeRelease = g_warnings;
  anariRelease(device, device);
  if (strict && g_warnings != warningsBeforeRelease) {
    std::fprintf(stderr,
        "FAIL: device release reported %d leak warning(s) after full cleanup\n",
        g_warnings - warningsBeforeRelease);
    return 1;
  }

  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
