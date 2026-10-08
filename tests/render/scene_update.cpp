// mitsuba-anari render test (Milestone 4): ANARI retained-mode updates must
// produce measurably correct image changes without recreating the device —
// camera moves, material replacement/edits, instance transforms, and one
// group shared by multiple instances. Relational image assertions only.
// SPDX-License-Identifier: Apache-2.0
//
// Usage: anari_scene_update_test [device-library-dir]

#include <anari/anari.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

constexpr uint32_t SIZE = 64;

struct Pixel {
  float r, g, b, a;
};

// Renders the frame and samples the pixel at (x, y).
Pixel renderAndSample(ANARIDevice d, ANARIFrame f, uint32_t x, uint32_t y)
{
  anariRenderFrame(d, f);
  anariFrameReady(d, f, ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType t = ANARI_UNKNOWN;
  const auto *p = (const float *)anariMapFrame(d, f, "channel.color", &w, &h, &t);
  Pixel out{0, 0, 0, 0};
  if (p && w == SIZE && h == SIZE) {
    const size_t i = (size_t(y) * w + x) * 4;
    out = {p[i], p[i + 1], p[i + 2], p[i + 3]};
  }
  anariUnmapFrame(d, f, "channel.color");
  return out;
}

bool isReddish(const Pixel &p)
{
  return p.r > 0.02f && p.r > p.g * 2.f && p.r > p.b * 2.f;
}

bool isGreenish(const Pixel &p)
{
  return p.g > 0.02f && p.g > p.r * 2.f && p.g > p.b * 2.f;
}

bool isBackground(const Pixel &p)
{
  // background is set to opaque black below
  return p.r < 1e-3f && p.g < 1e-3f && p.b < 1e-3f;
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
  REQUIRE(lib != nullptr);
  ANARIDevice device = anariNewDevice(lib, "default");
  REQUIRE(device != nullptr);

  // --- shared scene pieces --------------------------------------------------
  ANARICamera camera = anariNewCamera(device, "perspective");
  float position[3] = {0.f, 0.f, 2.f};
  const float direction[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  const float aspect = 1.f;
  anariSetParameter(device, camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(device, camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(device, camera, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(device, camera, "aspect", ANARI_FLOAT32, &aspect);
  anariCommitParameters(device, camera);

  const float vertices[9] = {
      -0.8f, -0.8f, 0.f, //
      0.8f, -0.8f, 0.f, //
      0.f, 0.8f, 0.f, //
  };
  ANARIArray1D posArray = anariNewArray1D(
      device, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
  ANARIGeometry geometry = anariNewGeometry(device, "triangle");
  anariSetParameter(device, geometry, "vertex.position", ANARI_ARRAY1D, &posArray);
  anariCommitParameters(device, geometry);
  anariRelease(device, posArray);

  ANARIMaterial material = anariNewMaterial(device, "matte");
  const float red[3] = {0.8f, 0.1f, 0.1f};
  anariSetParameter(device, material, "color", ANARI_FLOAT32_VEC3, red);
  anariCommitParameters(device, material);

  ANARISurface surface = anariNewSurface(device);
  anariSetParameter(device, surface, "geometry", ANARI_GEOMETRY, &geometry);
  anariSetParameter(device, surface, "material", ANARI_MATERIAL, &material);
  anariCommitParameters(device, surface);

  ANARILight light = anariNewLight(device, "directional");
  const float lightDir[3] = {0.f, 0.f, -1.f};
  const float irradiance = 3.f;
  anariSetParameter(device, light, "direction", ANARI_FLOAT32_VEC3, lightDir);
  anariSetParameter(device, light, "irradiance", ANARI_FLOAT32, &irradiance);
  anariCommitParameters(device, light);

  ANARIRenderer renderer = anariNewRenderer(device, "default");
  const float background[4] = {0.f, 0.f, 0.f, 1.f};
  const int spp = 8;
  anariSetParameter(device, renderer, "background", ANARI_FLOAT32_VEC4, background);
  anariSetParameter(device, renderer, "pixelSamples", ANARI_INT32, &spp);
  anariCommitParameters(device, renderer);

  ANARIFrame frame = anariNewFrame(device);
  const uint32_t size[2] = {SIZE, SIZE};
  ANARIDataType colorType = ANARI_FLOAT32_VEC4;
  anariSetParameter(device, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(device, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(device, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(device, frame, "camera", ANARI_CAMERA, &camera);
  anariCommitParameters(device, frame);

  // ==========================================================================
  // Part 1: zero-instance world; camera + material updates
  // ==========================================================================
  ANARIWorld world = anariNewWorld(device);
  {
    ANARISurface surfaces[] = {surface};
    ANARIArray1D sArr = anariNewArray1D(
        device, surfaces, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(device, world, "surface", ANARI_ARRAY1D, &sArr);
    anariRelease(device, sArr);
    ANARILight lights[] = {light};
    ANARIArray1D lArr = anariNewArray1D(
        device, lights, nullptr, nullptr, ANARI_LIGHT, 1);
    anariSetParameter(device, world, "light", ANARI_ARRAY1D, &lArr);
    anariRelease(device, lArr);
  }
  anariCommitParameters(device, world);
  anariSetParameter(device, frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(device, frame);

  // Baseline: red triangle at center.
  Pixel base = renderAndSample(device, frame, SIZE / 2, SIZE / 2);
  std::printf("baseline center: %f %f %f\n", base.r, base.g, base.b);
  REQUIRE(isReddish(base));

  // --- camera update: look away => center becomes background ----------------
  const float awayDir[3] = {0.f, 0.f, 1.f};
  anariSetParameter(device, camera, "direction", ANARI_FLOAT32_VEC3, awayDir);
  anariCommitParameters(device, camera);
  Pixel away = renderAndSample(device, frame, SIZE / 2, SIZE / 2);
  REQUIRE(isBackground(away));

  // ...and back.
  anariSetParameter(device, camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariCommitParameters(device, camera);
  Pixel backAgain = renderAndSample(device, frame, SIZE / 2, SIZE / 2);
  REQUIRE(isReddish(backAgain));

  // --- material parameter update: red -> green ------------------------------
  const float green[3] = {0.1f, 0.8f, 0.1f};
  anariSetParameter(device, material, "color", ANARI_FLOAT32_VEC3, green);
  anariCommitParameters(device, material);
  Pixel greenPix = renderAndSample(device, frame, SIZE / 2, SIZE / 2);
  REQUIRE(isGreenish(greenPix));

  // --- material replacement on the surface: back to a new red material ------
  ANARIMaterial material2 = anariNewMaterial(device, "matte");
  anariSetParameter(device, material2, "color", ANARI_FLOAT32_VEC3, red);
  anariCommitParameters(device, material2);
  anariSetParameter(device, surface, "material", ANARI_MATERIAL, &material2);
  anariCommitParameters(device, surface);
  anariRelease(device, material2); // surface keeps it alive
  Pixel redAgain = renderAndSample(device, frame, SIZE / 2, SIZE / 2);
  REQUIRE(isReddish(redAgain));

  // ==========================================================================
  // Part 2: instances — one group shared by two instances; transform updates
  // ==========================================================================
  ANARIGroup group = anariNewGroup(device);
  {
    ANARISurface surfaces[] = {surface};
    ANARIArray1D sArr = anariNewArray1D(
        device, surfaces, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(device, group, "surface", ANARI_ARRAY1D, &sArr);
    anariRelease(device, sArr);
    ANARILight lights[] = {light};
    ANARIArray1D lArr = anariNewArray1D(
        device, lights, nullptr, nullptr, ANARI_LIGHT, 1);
    anariSetParameter(device, group, "light", ANARI_ARRAY1D, &lArr);
    anariRelease(device, lArr);
  }
  anariCommitParameters(device, group);

  // Column-major 4x4 transforms: translate left / right by 0.9.
  auto makeTranslate = [](float tx) {
    std::vector<float> m = {1, 0, 0, 0, /**/ 0, 1, 0, 0, /**/ 0, 0, 1, 0,
        /**/ tx, 0, 0, 1};
    return m;
  };

  ANARIInstance inst1 = anariNewInstance(device, "transform");
  ANARIInstance inst2 = anariNewInstance(device, "transform");
  anariSetParameter(device, inst1, "group", ANARI_GROUP, &group);
  anariSetParameter(device, inst2, "group", ANARI_GROUP, &group);
  auto left = makeTranslate(-0.9f);
  auto right = makeTranslate(0.9f);
  anariSetParameter(device, inst1, "transform", ANARI_FLOAT32_MAT4, left.data());
  anariSetParameter(device, inst2, "transform", ANARI_FLOAT32_MAT4, right.data());
  anariCommitParameters(device, inst1);
  anariCommitParameters(device, inst2);
  anariRelease(device, group); // instances keep it alive

  ANARIWorld world2 = anariNewWorld(device);
  {
    ANARIInstance instances[] = {inst1, inst2};
    ANARIArray1D iArr = anariNewArray1D(
        device, instances, nullptr, nullptr, ANARI_INSTANCE, 2);
    anariSetParameter(device, world2, "instance", ANARI_ARRAY1D, &iArr);
    anariRelease(device, iArr);
  }
  anariCommitParameters(device, world2);
  anariSetParameter(device, frame, "world", ANARI_WORLD, &world2);
  anariCommitParameters(device, frame);

  // Two copies of the shared group: left and right thirds show the triangle,
  // the exact center (between them) is background.
  Pixel leftPix = renderAndSample(device, frame, SIZE / 4, SIZE / 2);
  Pixel rightPix = renderAndSample(device, frame, 3 * SIZE / 4, SIZE / 2);
  std::printf("left: %f %f %f  right: %f %f %f\n",
      leftPix.r, leftPix.g, leftPix.b, rightPix.r, rightPix.g, rightPix.b);
  REQUIRE(isReddish(leftPix));
  REQUIRE(isReddish(rightPix));

  // --- transform update: move the right instance far away -------------------
  auto gone = makeTranslate(100.f);
  anariSetParameter(device, inst2, "transform", ANARI_FLOAT32_MAT4, gone.data());
  anariCommitParameters(device, inst2);
  Pixel rightGone = renderAndSample(device, frame, 3 * SIZE / 4, SIZE / 2);
  Pixel leftStill = renderAndSample(device, frame, SIZE / 4, SIZE / 2);
  REQUIRE(isBackground(rightGone));
  REQUIRE(isReddish(leftStill));

  // --- release source objects after commit; clean teardown ------------------
  anariRelease(device, inst1);
  anariRelease(device, inst2);
  anariRelease(device, geometry);
  anariRelease(device, material);
  anariRelease(device, surface);
  anariRelease(device, light);
  anariRelease(device, camera);
  anariRelease(device, renderer);
  anariRelease(device, world);
  anariRelease(device, world2);
  anariRelease(device, frame);

  REQUIRE(g_errors == 0);
  anariRelease(device, device);
  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
