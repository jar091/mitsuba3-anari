// mitsuba-anari render test (Milestone 6): behavioral light matrix and the
// depth channel.
//   For each light type (directional, point, quad, hdri):
//     - the light alone (no ambient) produces nonzero expected luminance;
//     - changing its color changes the corresponding channel;
//     - raising its strength raises luminance.
//   Orthographic camera: renders the scene with expected coverage.
//   channel.depth: center depth equals the camera-plane distance; the
//   background is +infinity.
// SPDX-License-Identifier: Apache-2.0
//
// Usage: anari_lights_depth_test [device-library-dir]

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

constexpr uint32_t SIZE = 48;

struct SceneRefs {
  ANARIDevice d{nullptr};
  ANARICamera camera{nullptr};
  ANARISurface surface{nullptr};
  ANARIWorld world{nullptr};
  ANARIRenderer renderer{nullptr};
  ANARIFrame frame{nullptr};
};

// White quad facing +Z at z=0; camera on +Z looking down -Z; the light under
// test is installed by the caller into world's light array.
SceneRefs buildScene(ANARIDevice d, ANARILight light, const char *cameraType)
{
  SceneRefs c;
  c.d = d;

  c.camera = anariNewCamera(d, cameraType);
  const float position[3] = {0.f, 0.f, 2.f};
  const float direction[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  const float aspect = 1.f;
  anariSetParameter(d, c.camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, c.camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(d, c.camera, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(d, c.camera, "aspect", ANARI_FLOAT32, &aspect);
  if (std::strcmp(cameraType, "orthographic") == 0) {
    const float height = 2.f;
    anariSetParameter(d, c.camera, "height", ANARI_FLOAT32, &height);
  }
  anariCommitParameters(d, c.camera);

  const float vertices[12] = {
      -1.f, -1.f, 0.f, //
      1.f, -1.f, 0.f, //
      1.f, 1.f, 0.f, //
      -1.f, 1.f, 0.f, //
  };
  const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
  ANARIArray1D posArr =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIArray1D idxArr =
      anariNewArray1D(d, indices, nullptr, nullptr, ANARI_UINT32_VEC3, 2);
  ANARIGeometry geom = anariNewGeometry(d, "triangle");
  anariSetParameter(d, geom, "vertex.position", ANARI_ARRAY1D, &posArr);
  anariSetParameter(d, geom, "primitive.index", ANARI_ARRAY1D, &idxArr);
  anariCommitParameters(d, geom);
  anariRelease(d, posArr);
  anariRelease(d, idxArr);

  ANARIMaterial mat = anariNewMaterial(d, "matte");
  const float white[3] = {0.9f, 0.9f, 0.9f};
  anariSetParameter(d, mat, "color", ANARI_FLOAT32_VEC3, white);
  anariCommitParameters(d, mat);

  c.surface = anariNewSurface(d);
  anariSetParameter(d, c.surface, "geometry", ANARI_GEOMETRY, &geom);
  anariSetParameter(d, c.surface, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(d, c.surface);
  anariRelease(d, geom);
  anariRelease(d, mat);

  c.world = anariNewWorld(d);
  {
    ANARISurface surfaces[] = {c.surface};
    ANARIArray1D sArr =
        anariNewArray1D(d, surfaces, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(d, c.world, "surface", ANARI_ARRAY1D, &sArr);
    anariRelease(d, sArr);
    if (light) {
      ANARILight lights[] = {light};
      ANARIArray1D lArr =
          anariNewArray1D(d, lights, nullptr, nullptr, ANARI_LIGHT, 1);
      anariSetParameter(d, c.world, "light", ANARI_ARRAY1D, &lArr);
      anariRelease(d, lArr);
    }
  }
  anariCommitParameters(d, c.world);

  c.renderer = anariNewRenderer(d, "default");
  const float background[4] = {0.f, 0.f, 0.f, 1.f};
  const int spp = 16;
  // No ambient: the light under test must be the only illumination.
  anariSetParameter(d, c.renderer, "background", ANARI_FLOAT32_VEC4, background);
  anariSetParameter(d, c.renderer, "pixelSamples", ANARI_INT32, &spp);
  anariCommitParameters(d, c.renderer);

  c.frame = anariNewFrame(d);
  const uint32_t size[2] = {SIZE, SIZE};
  ANARIDataType colorType = ANARI_FLOAT32_VEC4;
  ANARIDataType depthType = ANARI_FLOAT32;
  anariSetParameter(d, c.frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, c.frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, c.frame, "channel.depth", ANARI_DATA_TYPE, &depthType);
  anariSetParameter(d, c.frame, "renderer", ANARI_RENDERER, &c.renderer);
  anariSetParameter(d, c.frame, "camera", ANARI_CAMERA, &c.camera);
  anariSetParameter(d, c.frame, "world", ANARI_WORLD, &c.world);
  anariCommitParameters(d, c.frame);
  return c;
}

void releaseScene(SceneRefs &c)
{
  anariRelease(c.d, c.frame);
  anariRelease(c.d, c.renderer);
  anariRelease(c.d, c.world);
  anariRelease(c.d, c.surface);
  anariRelease(c.d, c.camera);
}

struct RGB {
  float r, g, b;
};

// Renders and returns the mean RGB over the image.
RGB renderMean(const SceneRefs &c)
{
  anariRenderFrame(c.d, c.frame);
  anariFrameReady(c.d, c.frame, ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType t = ANARI_UNKNOWN;
  const auto *p =
      (const float *)anariMapFrame(c.d, c.frame, "channel.color", &w, &h, &t);
  RGB mean{0, 0, 0};
  if (p) {
    double r = 0, g = 0, b = 0;
    const size_t n = size_t(w) * h;
    for (size_t i = 0; i < n; ++i) {
      r += p[i * 4 + 0];
      g += p[i * 4 + 1];
      b += p[i * 4 + 2];
    }
    mean = {float(r / n), float(g / n), float(b / n)};
  }
  anariUnmapFrame(c.d, c.frame, "channel.color");
  return mean;
}

float luminance(const RGB &c)
{
  return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
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

  // --- light matrix ---------------------------------------------------------
  struct LightCase {
    const char *subtype;
    const char *strengthParam;
  };
  const LightCase lightCases[] = {
      {"directional", "irradiance"},
      {"point", "intensity"},
      {"quad", "radiance"},
      {"hdri", "scale"},
  };

  // 4x2 uniform white environment. Shared arrays borrow the app memory, so
  // the buffer must outlive every use of the array.
  std::vector<float> hdriData(4 * 2 * 3, 1.f);
  ANARIArray2D hdriRadiance = anariNewArray2D(
      device, hdriData.data(), nullptr, nullptr, ANARI_FLOAT32_VEC3, 4, 2);

  for (const LightCase &lc : lightCases) {
    auto makeLight = [&](const float *color, float strength) {
      ANARILight light = anariNewLight(device, lc.subtype);
      anariSetParameter(device, light, "color", ANARI_FLOAT32_VEC3, color);
      anariSetParameter(device, light, lc.strengthParam, ANARI_FLOAT32, &strength);
      if (std::strcmp(lc.subtype, "directional") == 0) {
        const float dir[3] = {0.f, 0.f, -1.f};
        anariSetParameter(device, light, "direction", ANARI_FLOAT32_VEC3, dir);
      } else if (std::strcmp(lc.subtype, "point") == 0) {
        const float pos[3] = {0.f, 0.f, 1.5f};
        anariSetParameter(device, light, "position", ANARI_FLOAT32_VEC3, pos);
      } else if (std::strcmp(lc.subtype, "quad") == 0) {
        const float pos[3] = {-0.5f, -0.5f, 1.5f};
        const float e1[3] = {1.f, 0.f, 0.f};
        const float e2[3] = {0.f, 1.f, 0.f};
        anariSetParameter(device, light, "position", ANARI_FLOAT32_VEC3, pos);
        anariSetParameter(device, light, "edge1", ANARI_FLOAT32_VEC3, e1);
        anariSetParameter(device, light, "edge2", ANARI_FLOAT32_VEC3, e2);
      } else if (std::strcmp(lc.subtype, "hdri") == 0) {
        anariSetParameter(
            device, light, "radiance", ANARI_ARRAY2D, &hdriRadiance);
      }
      anariCommitParameters(device, light);
      return light;
    };

    const float white[3] = {1.f, 1.f, 1.f};
    const float red[3] = {1.f, 0.05f, 0.05f};

    // Light alone -> nonzero luminance.
    ANARILight light = makeLight(white, 2.f);
    SceneRefs scene = buildScene(device, light, "perspective");
    const RGB base = renderMean(scene);
    std::printf("%s: base luminance %f\n", lc.subtype, luminance(base));
    REQUIRE(luminance(base) > 0.005f);

    // Color change -> relative channel change.
    anariSetParameter(device, light, "color", ANARI_FLOAT32_VEC3, red);
    anariCommitParameters(device, light);
    const RGB tinted = renderMean(scene);
    REQUIRE(tinted.r > tinted.g * 2.f);

    // Strength change -> higher luminance.
    anariSetParameter(device, light, "color", ANARI_FLOAT32_VEC3, white);
    const float strong = 4.f;
    anariSetParameter(device, light, lc.strengthParam, ANARI_FLOAT32, &strong);
    anariCommitParameters(device, light);
    const RGB brighter = renderMean(scene);
    REQUIRE(luminance(brighter) > luminance(base) * 1.5f);

    releaseScene(scene);
    anariRelease(device, light);
  }
  anariRelease(device, hdriRadiance);

  // --- orthographic camera --------------------------------------------------
  {
    ANARILight light = anariNewLight(device, "directional");
    const float dir[3] = {0.f, 0.f, -1.f};
    const float irr = 3.f;
    anariSetParameter(device, light, "direction", ANARI_FLOAT32_VEC3, dir);
    anariSetParameter(device, light, "irradiance", ANARI_FLOAT32, &irr);
    anariCommitParameters(device, light);

    SceneRefs scene = buildScene(device, light, "orthographic");
    const RGB mean = renderMean(scene);
    std::printf("orthographic: mean luminance %f\n", luminance(mean));
    // height==2 frames the 2x2 quad exactly: nearly full coverage.
    REQUIRE(luminance(mean) > 0.1f);
    releaseScene(scene);
    anariRelease(device, light);
  }

  // --- depth channel --------------------------------------------------------
  {
    ANARILight light = anariNewLight(device, "directional");
    const float dir[3] = {0.f, 0.f, -1.f};
    const float irr = 3.f;
    anariSetParameter(device, light, "direction", ANARI_FLOAT32_VEC3, dir);
    anariSetParameter(device, light, "irradiance", ANARI_FLOAT32, &irr);
    anariCommitParameters(device, light);

    SceneRefs scene = buildScene(device, light, "perspective");
    anariRenderFrame(device, scene.frame);
    anariFrameReady(device, scene.frame, ANARI_WAIT);

    uint32_t w = 0, h = 0;
    ANARIDataType t = ANARI_UNKNOWN;
    const auto *depth = (const float *)anariMapFrame(
        device, scene.frame, "channel.depth", &w, &h, &t);
    REQUIRE(depth != nullptr);
    REQUIRE(t == ANARI_FLOAT32);
    REQUIRE(w == SIZE && h == SIZE);

    // Quad at z=0, camera at z=2 looking straight at it: center depth == 2.
    const float center = depth[(SIZE / 2) * SIZE + SIZE / 2];
    std::printf("depth: center %f corner %f\n", center, depth[0]);
    REQUIRE(std::isfinite(center));
    REQUIRE(center > 1.9f && center < 2.1f);
    // The corner ray misses all geometry -> background = +infinity.
    REQUIRE(std::isinf(depth[0]));

    anariUnmapFrame(device, scene.frame, "channel.depth");
    releaseScene(scene);
    anariRelease(device, light);
  }

  REQUIRE(g_errors == 0);
  anariRelease(device, device);
  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
