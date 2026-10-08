// mitsuba-anari render test (Milestone 7): quad geometry, sphere geometry,
// and interpolated vertex colors. Relational assertions only.
// SPDX-License-Identifier: Apache-2.0
//
// Usage: anari_geometry_test [device-library-dir]

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

struct Px {
  float r, g, b, a;
};

// Renders the given geometry+material in a fixed single-light scene and
// returns the full image.
std::vector<float> renderGeometry(
    ANARIDevice d, ANARIGeometry geom, ANARIMaterial mat)
{
  ANARICamera camera = anariNewCamera(d, "perspective");
  const float position[3] = {0.f, 0.f, 2.5f};
  const float direction[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  const float aspect = 1.f;
  anariSetParameter(d, camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(d, camera, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(d, camera, "aspect", ANARI_FLOAT32, &aspect);
  anariCommitParameters(d, camera);

  ANARISurface surface = anariNewSurface(d);
  anariSetParameter(d, surface, "geometry", ANARI_GEOMETRY, &geom);
  anariSetParameter(d, surface, "material", ANARI_MATERIAL, &mat);
  anariCommitParameters(d, surface);

  ANARILight light = anariNewLight(d, "directional");
  const float lightDir[3] = {0.f, 0.f, -1.f};
  const float irradiance = 3.f;
  anariSetParameter(d, light, "direction", ANARI_FLOAT32_VEC3, lightDir);
  anariSetParameter(d, light, "irradiance", ANARI_FLOAT32, &irradiance);
  anariCommitParameters(d, light);

  ANARIWorld world = anariNewWorld(d);
  {
    ANARISurface surfaces[] = {surface};
    ANARIArray1D sArr =
        anariNewArray1D(d, surfaces, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(d, world, "surface", ANARI_ARRAY1D, &sArr);
    anariRelease(d, sArr);
    ANARILight lights[] = {light};
    ANARIArray1D lArr =
        anariNewArray1D(d, lights, nullptr, nullptr, ANARI_LIGHT, 1);
    anariSetParameter(d, world, "light", ANARI_ARRAY1D, &lArr);
    anariRelease(d, lArr);
  }
  anariCommitParameters(d, world);

  ANARIRenderer renderer = anariNewRenderer(d, "default");
  const float background[4] = {0.f, 0.f, 0.f, 1.f};
  const int spp = 16;
  anariSetParameter(d, renderer, "background", ANARI_FLOAT32_VEC4, background);
  anariSetParameter(d, renderer, "pixelSamples", ANARI_INT32, &spp);
  anariCommitParameters(d, renderer);

  ANARIFrame frame = anariNewFrame(d);
  const uint32_t size[2] = {SIZE, SIZE};
  ANARIDataType colorType = ANARI_FLOAT32_VEC4;
  anariSetParameter(d, frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, frame, "renderer", ANARI_RENDERER, &renderer);
  anariSetParameter(d, frame, "camera", ANARI_CAMERA, &camera);
  anariSetParameter(d, frame, "world", ANARI_WORLD, &world);
  anariCommitParameters(d, frame);

  anariRenderFrame(d, frame);
  anariFrameReady(d, frame, ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType t = ANARI_UNKNOWN;
  const auto *p =
      (const float *)anariMapFrame(d, frame, "channel.color", &w, &h, &t);
  std::vector<float> out;
  if (p && w == SIZE && h == SIZE)
    out.assign(p, p + size_t(SIZE) * SIZE * 4);
  anariUnmapFrame(d, frame, "channel.color");

  anariRelease(d, frame);
  anariRelease(d, renderer);
  anariRelease(d, world);
  anariRelease(d, light);
  anariRelease(d, surface);
  anariRelease(d, camera);
  return out;
}

Px at(const std::vector<float> &img, uint32_t x, uint32_t y)
{
  const size_t i = (size_t(y) * SIZE + x) * 4;
  return {img[i], img[i + 1], img[i + 2], img[i + 3]};
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

  ANARIMaterial red = anariNewMaterial(device, "matte");
  {
    const float c[3] = {0.8f, 0.1f, 0.1f};
    anariSetParameter(device, red, "color", ANARI_FLOAT32_VEC3, c);
    anariCommitParameters(device, red);
  }

  // --- quad geometry (soup of 4 vertices = one camera-facing quad) ----------
  {
    const float vertices[12] = {
        -1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 1.f, 1.f, 0.f, -1.f, 1.f, 0.f};
    ANARIArray1D posArr =
        anariNewArray1D(device, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
    ANARIGeometry quad = anariNewGeometry(device, "quad");
    anariSetParameter(device, quad, "vertex.position", ANARI_ARRAY1D, &posArr);
    anariCommitParameters(device, quad);
    anariRelease(device, posArr);

    auto img = renderGeometry(device, quad, red);
    REQUIRE(!img.empty());
    // Both triangle halves of the quad must be present.
    Px lower = at(img, SIZE / 4, SIZE / 4);
    Px upper = at(img, 3 * SIZE / 4, 3 * SIZE / 4);
    std::printf("quad: lower %f upper %f\n", lower.r, upper.r);
    REQUIRE(lower.r > 0.02f && upper.r > 0.02f);
    anariRelease(device, quad);
  }

  // --- sphere geometry ------------------------------------------------------
  {
    const float centers[6] = {-0.7f, 0.f, 0.f, 0.7f, 0.f, 0.f};
    const float radii[2] = {0.5f, 0.25f};
    ANARIArray1D cArr =
        anariNewArray1D(device, centers, nullptr, nullptr, ANARI_FLOAT32_VEC3, 2);
    ANARIArray1D rArr =
        anariNewArray1D(device, radii, nullptr, nullptr, ANARI_FLOAT32, 2);
    ANARIGeometry spheres = anariNewGeometry(device, "sphere");
    anariSetParameter(device, spheres, "vertex.position", ANARI_ARRAY1D, &cArr);
    anariSetParameter(device, spheres, "vertex.radius", ANARI_ARRAY1D, &rArr);
    anariCommitParameters(device, spheres);
    anariRelease(device, cArr);
    anariRelease(device, rArr);

    auto img = renderGeometry(device, spheres, red);
    REQUIRE(!img.empty());
    // Sphere centers visible; midpoint between them is background. With the
    // camera at z=2.5 and fovy 60 deg, world x maps to pixel
    // 0.5 + x / (2 * tan(30 deg) * 2.5) => centers +-0.7 land near 0.26/0.74.
    Px left = at(img, (uint32_t)(SIZE * 0.26f), SIZE / 2);
    Px mid = at(img, SIZE / 2, SIZE / 2);
    Px right = at(img, (uint32_t)(SIZE * 0.74f), SIZE / 2);
    std::printf("spheres: left %f mid %f right %f\n", left.r, mid.r, right.r);
    REQUIRE(left.r > 0.02f);
    REQUIRE(right.r > 0.02f);
    REQUIRE(mid.r < 0.01f);
    anariRelease(device, spheres);
  }

  // --- vertex colors (triangle with R/G/B corners) --------------------------
  {
    const float vertices[9] = {
        -1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f};
    const float colors[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    ANARIArray1D posArr =
        anariNewArray1D(device, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
    ANARIArray1D colArr =
        anariNewArray1D(device, colors, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
    ANARIGeometry tri = anariNewGeometry(device, "triangle");
    anariSetParameter(device, tri, "vertex.position", ANARI_ARRAY1D, &posArr);
    anariSetParameter(device, tri, "vertex.color", ANARI_ARRAY1D, &colArr);
    anariCommitParameters(device, tri);
    anariRelease(device, posArr);
    anariRelease(device, colArr);

    ANARIMaterial vc = anariNewMaterial(device, "matte");
    anariSetParameter(device, vc, "color", ANARI_STRING, "color");
    anariCommitParameters(device, vc);

    auto img = renderGeometry(device, tri, vc);
    REQUIRE(!img.empty());
    // Near the bottom-left corner red dominates; near the top green/blue mix
    // dominates red.
    Px nearRed = at(img, (uint32_t)(SIZE * 0.30f), (uint32_t)(SIZE * 0.30f));
    Px nearTop = at(img, SIZE / 2, (uint32_t)(SIZE * 0.68f));
    std::printf("vertex colors: nearRed(%f %f %f) nearTop(%f %f %f)\n",
        nearRed.r, nearRed.g, nearRed.b, nearTop.r, nearTop.g, nearTop.b);
    REQUIRE(nearRed.r > nearRed.g && nearRed.r > nearRed.b);
    REQUIRE(nearTop.b > nearTop.r);
    anariRelease(device, vc);
    anariRelease(device, tri);
  }

  // --- curve geometry (horizontal polyline through the view center) --------
  {
    const float vertices[9] = {
        -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f};
    ANARIArray1D posArr =
        anariNewArray1D(device, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 3);
    ANARIGeometry curve = anariNewGeometry(device, "curve");
    anariSetParameter(device, curve, "vertex.position", ANARI_ARRAY1D, &posArr);
    const float radius = 0.15f;
    anariSetParameter(device, curve, "radius", ANARI_FLOAT32, &radius);
    anariCommitParameters(device, curve);
    anariRelease(device, posArr);

    auto img = renderGeometry(device, curve, red);
    REQUIRE(!img.empty());
    // The tube crosses the center horizontally; above it is background.
    Px center = at(img, SIZE / 2, SIZE / 2);
    Px above = at(img, SIZE / 2, (uint32_t)(SIZE * 0.85f));
    std::printf("curve: center %f above %f\n", center.r, above.r);
    REQUIRE(center.r > 0.02f);
    REQUIRE(above.r < 0.01f);
    anariRelease(device, curve);
  }

  anariRelease(device, red);

  REQUIRE(g_errors == 0);
  anariRelease(device, device);
  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
