// mitsuba-anari render test: the Milestone 3 scene — one triangle with a
// matte material, perspective camera, directional light, default renderer.
// Behavioral assertions only (finite values, coverage, color relations);
// never exact golden bytes (master prompt §19).
// SPDX-License-Identifier: Apache-2.0
//
// Usage: anari_triangle_matte_test [device-library-dir]

#include <anari/anari.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int g_errors = 0;

void statusFunc(const void *,
    ANARIDevice,
    ANARIObject,
    ANARIDataType,
    ANARIStatusSeverity severity,
    ANARIStatusCode,
    const char *message)
{
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

constexpr uint32_t WIDTH = 64;
constexpr uint32_t HEIGHT = 64;

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

  // Optional variant selection (vendor extension ANARI_MITSUBA_DEVICE_VARIANT)
  // driven by the CTest registrations: the llvm/cuda runs request a JIT
  // variant and SKIP when the runtime falls back (backend not available on
  // this machine) — GPU/LLVM runs are opt-in, never mandatory.
  const char *requestedVariant = std::getenv("MITSUBA_ANARI_TEST_VARIANT");
  if (requestedVariant && *requestedVariant) {
    anariSetParameter(
        device, device, "mitsuba.variant", ANARI_STRING, requestedVariant);
    anariCommitParameters(device, device);
  }

  // --- camera ---------------------------------------------------------------
  ANARICamera camera = anariNewCamera(device, "perspective");
  const float position[3] = {0.f, 0.f, 2.f};
  const float direction[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  const float aspect = float(WIDTH) / float(HEIGHT);
  anariSetParameter(device, camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(device, camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(device, camera, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(device, camera, "aspect", ANARI_FLOAT32, &aspect);
  anariCommitParameters(device, camera);

  // --- triangle geometry (faces the camera, spans the view center) ----------
  const float vertices[9] = {
      -1.f, -1.f, 0.f, //
      1.f, -1.f, 0.f, //
      0.f, 1.f, 0.f, //
  };
  const uint32_t indices[3] = {0, 1, 2};

  ANARIArray1D posArray = anariNewArray1D(
      device, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIArray1D idxArray = anariNewArray1D(
      device, indices, nullptr, nullptr, ANARI_UINT32_VEC3, 1);

  ANARIGeometry geometry = anariNewGeometry(device, "triangle");
  anariSetParameter(device, geometry, "vertex.position", ANARI_ARRAY1D, &posArray);
  anariSetParameter(device, geometry, "primitive.index", ANARI_ARRAY1D, &idxArray);
  anariCommitParameters(device, geometry);
  anariRelease(device, posArray);
  anariRelease(device, idxArray);

  // --- red matte material ---------------------------------------------------
  ANARIMaterial material = anariNewMaterial(device, "matte");
  const float red[3] = {0.8f, 0.1f, 0.1f};
  anariSetParameter(device, material, "color", ANARI_FLOAT32_VEC3, red);
  anariCommitParameters(device, material);

  ANARISurface surface = anariNewSurface(device);
  anariSetParameter(device, surface, "geometry", ANARI_GEOMETRY, &geometry);
  anariSetParameter(device, surface, "material", ANARI_MATERIAL, &material);
  anariCommitParameters(device, surface);
  anariRelease(device, geometry);
  anariRelease(device, material);

  // --- directional light pointing at the triangle ---------------------------
  ANARILight light = anariNewLight(device, "directional");
  const float lightDir[3] = {0.f, 0.f, -1.f};
  const float irradiance = 3.f;
  anariSetParameter(device, light, "direction", ANARI_FLOAT32_VEC3, lightDir);
  anariSetParameter(device, light, "irradiance", ANARI_FLOAT32, &irradiance);
  anariCommitParameters(device, light);

  // --- world ----------------------------------------------------------------
  ANARIWorld world = anariNewWorld(device);
  {
    ANARISurface surfaces[] = {surface};
    ANARIArray1D surfArray = anariNewArray1D(
        device, surfaces, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(device, world, "surface", ANARI_ARRAY1D, &surfArray);
    anariRelease(device, surfArray);
    ANARILight lights[] = {light};
    ANARIArray1D lightArray = anariNewArray1D(
        device, lights, nullptr, nullptr, ANARI_LIGHT, 1);
    anariSetParameter(device, world, "light", ANARI_ARRAY1D, &lightArray);
    anariRelease(device, lightArray);
  }
  anariCommitParameters(device, world);
  anariRelease(device, surface);
  anariRelease(device, light);

  // World bounds property must cover the triangle.
  float bounds[6] = {0};
  REQUIRE(anariGetProperty(device,
      world,
      "bounds",
      ANARI_FLOAT32_BOX3,
      bounds,
      sizeof(bounds),
      ANARI_WAIT));
  REQUIRE(bounds[0] <= -1.f && bounds[3] >= 1.f);

  // --- renderer -------------------------------------------------------------
  ANARIRenderer renderer = anariNewRenderer(device, "default");
  const float background[4] = {0.f, 0.f, 0.2f, 1.f};
  const int spp = 16;
  anariSetParameter(device, renderer, "background", ANARI_FLOAT32_VEC4, background);
  anariSetParameter(device, renderer, "pixelSamples", ANARI_INT32, &spp);
  anariCommitParameters(device, renderer);

  // --- frame (FLOAT32 color) ------------------------------------------------
  ANARIFrame frame = anariNewFrame(device);
  const uint32_t size[2] = {WIDTH, HEIGHT};
  ANARIDataType colorType = ANARI_FLOAT32_VEC4;
  anariSetParameter(device, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(device, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(device, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(device, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(device, frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(device, frame);

  anariRenderFrame(device, frame);
  REQUIRE(anariFrameReady(device, frame, ANARI_WAIT) == 1);

  if (requestedVariant && *requestedVariant) {
    char active[64] = {0};
    REQUIRE(anariGetProperty(device,
        device,
        "mitsuba.variant",
        ANARI_STRING,
        active,
        sizeof(active),
        ANARI_WAIT));
    std::printf("active variant: %s\n", active);
    if (std::strcmp(active, requestedVariant) != 0) {
      std::printf("SKIP: variant '%s' unavailable on this machine (active: "
                  "'%s')\n",
          requestedVariant,
          active);
      return 77;
    }
  }

  uint32_t w = 0, h = 0;
  ANARIDataType mappedType = ANARI_UNKNOWN;
  const auto *pixels = (const float *)anariMapFrame(
      device, frame, "channel.color", &w, &h, &mappedType);
  REQUIRE(pixels != nullptr);
  REQUIRE(w == WIDTH && h == HEIGHT);
  REQUIRE(mappedType == ANARI_FLOAT32_VEC4);

  // Behavioral image checks.
  size_t nonBackground = 0;
  double luminanceSum = 0.0;
  for (size_t i = 0; i < size_t(WIDTH) * HEIGHT; ++i) {
    const float r = pixels[i * 4 + 0];
    const float g = pixels[i * 4 + 1];
    const float b = pixels[i * 4 + 2];
    const float a = pixels[i * 4 + 3];
    REQUIRE(std::isfinite(r) && std::isfinite(g) && std::isfinite(b)
        && std::isfinite(a));
    REQUIRE(a >= 0.f && a <= 1.0001f);
    luminanceSum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const bool isBg = std::fabs(r - background[0]) < 1e-3f
        && std::fabs(g - background[1]) < 1e-3f
        && std::fabs(b - background[2]) < 1e-3f;
    if (!isBg)
      ++nonBackground;
  }
  // The triangle spans a large part of the view: expect meaningful coverage.
  REQUIRE(nonBackground > size_t(WIDTH) * HEIGHT / 10);
  REQUIRE(luminanceSum > 0.0);

  // Center pixel: lit red triangle => red dominates green/blue.
  {
    const size_t c = (size_t(HEIGHT / 2) * WIDTH + WIDTH / 2) * 4;
    const float r = pixels[c + 0], g = pixels[c + 1], b = pixels[c + 2];
    std::printf("center pixel: %f %f %f\n", r, g, b);
    REQUIRE(r > 0.02f);
    REQUIRE(r > g * 2.f && r > b * 2.f);
  }

  // Corner pixel: outside the triangle => background color.
  {
    const float r = pixels[0], g = pixels[1], b = pixels[2];
    REQUIRE(std::fabs(r - background[0]) < 1e-3f);
    REQUIRE(std::fabs(g - background[1]) < 1e-3f);
    REQUIRE(std::fabs(b - background[2]) < 1e-3f);
  }
  anariUnmapFrame(device, frame, "channel.color");

  // --- re-render as UFIXED8_RGBA_SRGB --------------------------------------
  colorType = ANARI_UFIXED8_RGBA_SRGB;
  anariSetParameter(device, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariCommitParameters(device, frame);
  anariRenderFrame(device, frame);
  REQUIRE(anariFrameReady(device, frame, ANARI_WAIT) == 1);

  const auto *pixels8 = (const uint8_t *)anariMapFrame(
      device, frame, "channel.color", &w, &h, &mappedType);
  REQUIRE(pixels8 != nullptr);
  REQUIRE(mappedType == ANARI_UFIXED8_RGBA_SRGB);
  {
    const size_t c = (size_t(HEIGHT / 2) * WIDTH + WIDTH / 2) * 4;
    REQUIRE(pixels8[c + 0] > 20); // lit red triangle
    REQUIRE(pixels8[c + 0] > pixels8[c + 1]);
  }
  anariUnmapFrame(device, frame, "channel.color");

  // Frame duration property is reported.
  float duration = -1.f;
  REQUIRE(anariGetProperty(device,
      frame,
      "duration",
      ANARI_FLOAT32,
      &duration,
      sizeof(duration),
      ANARI_WAIT));
  REQUIRE(duration >= 0.f);
  std::printf("frame duration: %f s\n", duration);

  anariRelease(device, frame);
  anariRelease(device, renderer);
  anariRelease(device, camera);
  anariRelease(device, world);

  REQUIRE(g_errors == 0);

  anariRelease(device, device);
  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
