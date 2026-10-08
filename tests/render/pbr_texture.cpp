// mitsuba-anari render test (Milestone 5): physicallyBased material mapping
// and image2D texturing. Relational assertions only (no golden bytes):
//   - textured quad shows the texel colors on the right sides;
//   - texture data updates (map/unmap) propagate to the next render;
//   - sRGB texture types decode darker than linear 8-bit types;
//   - roughness and metallic changes alter the image meaningfully.
// SPDX-License-Identifier: Apache-2.0
//
// Usage: anari_pbr_texture_test [device-library-dir]

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

struct Ctx {
  ANARIDevice device{nullptr};
  ANARICamera camera{nullptr};
  ANARISurface surface{nullptr};
  ANARIWorld world{nullptr};
  ANARIRenderer renderer{nullptr};
  ANARIFrame frame{nullptr};
};

// Builds a camera-facing unit quad (two triangles) with UVs, a directional
// light, and a frame; the material is supplied by the caller.
Ctx buildQuadScene(ANARIDevice d, ANARIMaterial material)
{
  Ctx c;
  c.device = d;

  c.camera = anariNewCamera(d, "perspective");
  const float position[3] = {0.f, 0.f, 2.f};
  const float direction[3] = {0.f, 0.f, -1.f};
  const float up[3] = {0.f, 1.f, 0.f};
  const float aspect = 1.f;
  anariSetParameter(d, c.camera, "position", ANARI_FLOAT32_VEC3, position);
  anariSetParameter(d, c.camera, "direction", ANARI_FLOAT32_VEC3, direction);
  anariSetParameter(d, c.camera, "up", ANARI_FLOAT32_VEC3, up);
  anariSetParameter(d, c.camera, "aspect", ANARI_FLOAT32, &aspect);
  anariCommitParameters(d, c.camera);

  const float vertices[12] = {
      -1.f, -1.f, 0.f, //
      1.f, -1.f, 0.f, //
      1.f, 1.f, 0.f, //
      -1.f, 1.f, 0.f, //
  };
  const float uvs[8] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
  const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};

  ANARIArray1D posArr =
      anariNewArray1D(d, vertices, nullptr, nullptr, ANARI_FLOAT32_VEC3, 4);
  ANARIArray1D uvArr =
      anariNewArray1D(d, uvs, nullptr, nullptr, ANARI_FLOAT32_VEC2, 4);
  ANARIArray1D idxArr =
      anariNewArray1D(d, indices, nullptr, nullptr, ANARI_UINT32_VEC3, 2);

  ANARIGeometry geom = anariNewGeometry(d, "triangle");
  anariSetParameter(d, geom, "vertex.position", ANARI_ARRAY1D, &posArr);
  anariSetParameter(d, geom, "vertex.attribute0", ANARI_ARRAY1D, &uvArr);
  anariSetParameter(d, geom, "primitive.index", ANARI_ARRAY1D, &idxArr);
  anariCommitParameters(d, geom);
  anariRelease(d, posArr);
  anariRelease(d, uvArr);
  anariRelease(d, idxArr);

  c.surface = anariNewSurface(d);
  anariSetParameter(d, c.surface, "geometry", ANARI_GEOMETRY, &geom);
  anariSetParameter(d, c.surface, "material", ANARI_MATERIAL, &material);
  anariCommitParameters(d, c.surface);
  anariRelease(d, geom);

  ANARILight light = anariNewLight(d, "directional");
  const float lightDir[3] = {0.f, 0.f, -1.f};
  const float irradiance = 3.f;
  anariSetParameter(d, light, "direction", ANARI_FLOAT32_VEC3, lightDir);
  anariSetParameter(d, light, "irradiance", ANARI_FLOAT32, &irradiance);
  anariCommitParameters(d, light);

  c.world = anariNewWorld(d);
  {
    ANARISurface surfaces[] = {c.surface};
    ANARIArray1D sArr =
        anariNewArray1D(d, surfaces, nullptr, nullptr, ANARI_SURFACE, 1);
    anariSetParameter(d, c.world, "surface", ANARI_ARRAY1D, &sArr);
    anariRelease(d, sArr);
    ANARILight lights[] = {light};
    ANARIArray1D lArr =
        anariNewArray1D(d, lights, nullptr, nullptr, ANARI_LIGHT, 1);
    anariSetParameter(d, c.world, "light", ANARI_ARRAY1D, &lArr);
    anariRelease(d, lArr);
  }
  anariCommitParameters(d, c.world);
  anariRelease(d, light);

  c.renderer = anariNewRenderer(d, "default");
  const float background[4] = {0.f, 0.f, 0.f, 1.f};
  const int spp = 16;
  anariSetParameter(d, c.renderer, "background", ANARI_FLOAT32_VEC4, background);
  anariSetParameter(d, c.renderer, "pixelSamples", ANARI_INT32, &spp);
  anariCommitParameters(d, c.renderer);

  c.frame = anariNewFrame(d);
  const uint32_t size[2] = {SIZE, SIZE};
  ANARIDataType colorType = ANARI_FLOAT32_VEC4;
  anariSetParameter(d, c.frame, "size", ANARI_UINT32_VEC2, size);
  anariSetParameter(d, c.frame, "channel.color", ANARI_DATA_TYPE, &colorType);
  anariSetParameter(d, c.frame, "renderer", ANARI_RENDERER, &c.renderer);
  anariSetParameter(d, c.frame, "camera", ANARI_CAMERA, &c.camera);
  anariSetParameter(d, c.frame, "world", ANARI_WORLD, &c.world);
  anariCommitParameters(d, c.frame);
  return c;
}

void releaseCtx(Ctx &c)
{
  anariRelease(c.device, c.frame);
  anariRelease(c.device, c.renderer);
  anariRelease(c.device, c.world);
  anariRelease(c.device, c.surface);
  anariRelease(c.device, c.camera);
}

std::vector<float> renderImage(const Ctx &c)
{
  anariRenderFrame(c.device, c.frame);
  anariFrameReady(c.device, c.frame, ANARI_WAIT);
  uint32_t w = 0, h = 0;
  ANARIDataType t = ANARI_UNKNOWN;
  const auto *p =
      (const float *)anariMapFrame(c.device, c.frame, "channel.color", &w, &h, &t);
  std::vector<float> out;
  if (p && w == SIZE && h == SIZE)
    out.assign(p, p + size_t(SIZE) * SIZE * 4);
  anariUnmapFrame(c.device, c.frame, "channel.color");
  return out;
}

float meanLuminance(const std::vector<float> &img)
{
  double sum = 0.0;
  for (size_t i = 0; i < img.size(); i += 4)
    sum += 0.2126 * img[i] + 0.7152 * img[i + 1] + 0.0722 * img[i + 2];
  return float(sum / (img.size() / 4));
}

struct Px {
  float r, g, b;
};

Px at(const std::vector<float> &img, uint32_t x, uint32_t y)
{
  const size_t i = (size_t(y) * SIZE + x) * 4;
  return {img[i], img[i + 1], img[i + 2]};
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

  // ==========================================================================
  // Textured quad through matte 'color' sampler; texel lookup + update
  // ==========================================================================
  {
    // 2x1 texture: left texel red, right texel blue.
    float texels[8] = {1.f, 0.f, 0.f, 1.f, /**/ 0.f, 0.f, 1.f, 1.f};
    ANARIArray2D image = anariNewArray2D(
        device, nullptr, nullptr, nullptr, ANARI_FLOAT32_VEC4, 2, 1);
    {
      void *ptr = anariMapArray(device, image);
      std::memcpy(ptr, texels, sizeof(texels));
      anariUnmapArray(device, image);
    }

    ANARISampler sampler = anariNewSampler(device, "image2D");
    anariSetParameter(device, sampler, "image", ANARI_ARRAY2D, &image);
    anariSetParameter(device, sampler, "inAttribute", ANARI_STRING, "attribute0");
    anariSetParameter(device, sampler, "filter", ANARI_STRING, "nearest");
    anariCommitParameters(device, sampler);

    ANARIMaterial mat = anariNewMaterial(device, "matte");
    anariSetParameter(device, mat, "color", ANARI_SAMPLER, &sampler);
    anariCommitParameters(device, mat);

    Ctx c = buildQuadScene(device, mat);
    auto img = renderImage(c);
    REQUIRE(!img.empty());

    Px left = at(img, SIZE / 4, SIZE / 2);
    Px right = at(img, 3 * SIZE / 4, SIZE / 2);
    std::printf("textured: left %f %f %f | right %f %f %f\n",
        left.r, left.g, left.b, right.r, right.g, right.b);
    REQUIRE(left.r > left.b * 2.f); // red texel side
    REQUIRE(right.b > right.r * 2.f); // blue texel side

    // --- texture update: swap the texels through map/unmap ------------------
    {
      void *ptr = anariMapArray(device, image);
      float swapped[8] = {0.f, 0.f, 1.f, 1.f, /**/ 1.f, 0.f, 0.f, 1.f};
      std::memcpy(ptr, swapped, sizeof(swapped));
      anariUnmapArray(device, image);
    }
    auto img2 = renderImage(c);
    Px left2 = at(img2, SIZE / 4, SIZE / 2);
    Px right2 = at(img2, 3 * SIZE / 4, SIZE / 2);
    REQUIRE(left2.b > left2.r * 2.f); // now blue on the left
    REQUIRE(right2.r > right2.b * 2.f); // now red on the right

    releaseCtx(c);
    anariRelease(device, mat);
    anariRelease(device, sampler);
    anariRelease(device, image);
  }

  // ==========================================================================
  // sRGB vs linear 8-bit decoding: mid-gray decodes darker under sRGB
  // ==========================================================================
  {
    auto renderGray = [&](ANARIDataType texelType) {
      uint8_t texel[3] = {128, 128, 128};
      ANARIArray2D image = anariNewArray2D(
          device, nullptr, nullptr, nullptr, texelType, 1, 1);
      void *ptr = anariMapArray(device, image);
      std::memcpy(ptr, texel, sizeof(texel));
      anariUnmapArray(device, image);

      ANARISampler sampler = anariNewSampler(device, "image2D");
      anariSetParameter(device, sampler, "image", ANARI_ARRAY2D, &image);
      anariCommitParameters(device, sampler);
      ANARIMaterial mat = anariNewMaterial(device, "matte");
      anariSetParameter(device, mat, "color", ANARI_SAMPLER, &sampler);
      anariCommitParameters(device, mat);

      Ctx c = buildQuadScene(device, mat);
      auto img = renderImage(c);
      const float lum = meanLuminance(img);
      releaseCtx(c);
      anariRelease(device, mat);
      anariRelease(device, sampler);
      anariRelease(device, image);
      return lum;
    };

    const float linear = renderGray(ANARI_UFIXED8_VEC3);
    const float srgb = renderGray(ANARI_UFIXED8_RGB_SRGB);
    std::printf("gray 128 luminance: linear %f, srgb %f\n", linear, srgb);
    REQUIRE(srgb < linear * 0.75f); // 0.216 vs 0.502 before lighting
  }

  // ==========================================================================
  // physicallyBased: roughness and metallic must change the image
  // ==========================================================================
  {
    auto renderPBR = [&](float metallic, float roughness) {
      ANARIMaterial mat = anariNewMaterial(device, "physicallyBased");
      const float base[3] = {0.9f, 0.9f, 0.9f};
      anariSetParameter(device, mat, "baseColor", ANARI_FLOAT32_VEC3, base);
      anariSetParameter(device, mat, "metallic", ANARI_FLOAT32, &metallic);
      anariSetParameter(device, mat, "roughness", ANARI_FLOAT32, &roughness);
      anariCommitParameters(device, mat);
      Ctx c = buildQuadScene(device, mat);
      auto img = renderImage(c);
      releaseCtx(c);
      anariRelease(device, mat);
      return img;
    };

    auto smooth = renderPBR(1.f, 0.05f);
    auto rough = renderPBR(1.f, 0.8f);
    auto dielectric = renderPBR(0.f, 0.8f);
    REQUIRE(!smooth.empty() && !rough.empty() && !dielectric.empty());

    const float lumSmooth = meanLuminance(smooth);
    const float lumRough = meanLuminance(rough);
    const float lumDielectric = meanLuminance(dielectric);
    std::printf("pbr luminance: smooth-metal %f, rough-metal %f, "
                "rough-dielectric %f\n",
        lumSmooth, lumRough, lumDielectric);

    // Relational assertions (master prompt §19): parameter changes must alter
    // the image meaningfully.
    auto differs = [](float a, float b) {
      return std::fabs(a - b) > 0.05f * std::max(a, b);
    };
    REQUIRE(differs(lumSmooth, lumRough));
    REQUIRE(differs(lumRough, lumDielectric));
  }

  REQUIRE(g_errors == 0);
  anariRelease(device, device);
  anariUnloadLibrary(lib);
  std::printf("PASS\n");
  return 0;
}
