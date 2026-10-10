// mitsuba-anari: translation of committed ANARI scene state into a renderable
// Mitsuba scene, templated over the Mitsuba variant.
// SPDX-License-Identifier: Apache-2.0

#include "scene/MitsubaSceneBuilder.h"

#include "camera/Camera.h"
#include "device/MitsubaGlobalState.h"
#include "geometry/Tessellation.h"
#include "light/Light.h"
#include "material/Material.h"
#include "renderer/Renderer.h"
#include "sampler/Sampler.h"
#include "scene/Group.h"
#include "scene/Instance.h"
#include "scene/Surface.h"
#include "scene/World.h"
#include "volume/Volume.h"

// std
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
// mitsuba
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100 4127 4244 4251 4324 4458 4459 4702)
#endif
#include <mitsuba/render/interaction.h>
#include <mitsuba/render/medium.h>
#include <mitsuba/render/texture.h>
#include <mitsuba/render/volume.h>
#include <mitsuba/render/volumegrid.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace mitsuba {

// Texture returning one RGB color per primitive (si.prim_index), used for
// per-primitive colors on bulk shapes (the 'ellipsoids' sphere sets) which
// have no mesh attributes. Constructed directly (not a registered plugin);
// JitObject registration makes it usable in vectorized calls.
template <typename Float, typename Spectrum>
class AnariPrimitiveColor final : public Texture<Float, Spectrum>
{
 public:
  MI_IMPORT_TYPES(Texture)
  using FloatStorage = DynamicBuffer<Float>;

  AnariPrimitiveColor(const std::vector<float> &rgb, float scale)
      : Texture(Properties("anari_primitive_color")),
        m_count(uint32_t(rgb.size() / 3))
  {
    std::vector<float> scaled(rgb.size());
    double sum = 0.0;
    for (size_t i = 0; i < rgb.size(); ++i) {
      scaled[i] = rgb[i] * scale;
      sum += scaled[i];
      m_max = std::max(m_max, scaled[i]);
    }
    m_mean = rgb.empty() ? 0.f : float(sum / double(rgb.size()));
    m_colors = dr::load<FloatStorage>(scaled.data(), scaled.size());
  }

  UnpolarizedSpectrum eval(
      const SurfaceInteraction3f &si, Mask active = true) const override
  {
    return eval_3(si, active);
  }

  Color3f eval_3(
      const SurfaceInteraction3f &si, Mask active = true) const override
  {
    const UInt32 idx = dr::minimum(si.prim_index, m_count - 1);
    return dr::gather<Color3f>(m_colors, idx, active);
  }

  Float eval_1(
      const SurfaceInteraction3f &si, Mask active = true) const override
  {
    return luminance(eval_3(si, active));
  }

  Float mean() const override { return m_mean; }
  ScalarFloat max() const override { return m_max; }
  bool is_spatially_varying() const override { return true; }

  MI_DECLARE_CLASS(AnariPrimitiveColor)

 private:
  FloatStorage m_colors;
  uint32_t m_count{0};
  float m_mean{0.f};
  float m_max{0.f};
};

} // namespace mitsuba

namespace mitsuba_anari {

namespace {

using MiProps = mitsuba::Properties;
using MiXform = mitsuba::AffineTransform<mitsuba::Point<float, 4>>;

// Sphere sets larger than this (or with per-primitive colors) become one
// Mitsuba 'ellipsoids' shape instead of one 'sphere' shape per primitive.
constexpr uint64_t kBulkSphereThreshold = 1024;
// Tessellation density of cones/cylinders and of colored curves.
constexpr uint32_t kConeSegments = 64;
constexpr uint32_t kCurveSegments = 16;
constexpr uint32_t kJointRings = 8;

mitsuba::Color<float, 3> toColor(const float3 &c)
{
  return mitsuba::Color<float, 3>(c.x, c.y, c.z);
}

// Properties stores nested objects as ref<mitsuba::Object>; typed refs do not
// convert implicitly.
void setObject(MiProps &p, std::string_view name, mitsuba::Object *obj)
{
  p.set(name, mitsuba::ref<mitsuba::Object>(obj));
}

// Cache of translated Mitsuba objects (Milestone 11), owned per device via
// MitsubaGlobalState::translationCache. Keys are the source ANARI objects;
// entries are invalidated by comparing composite helium timestamps. One
// Mitsuba variant per process (ADR 0004), so type-erased refs are safe.
struct TranslationCache {
  struct Entry {
    mitsuba::ref<mitsuba::Object> obj;
    uint64_t stamp{UINT64_MAX};
  };
  struct ShapesEntry {
    std::vector<mitsuba::ref<mitsuba::Object>> shapes;
    uint64_t stamp{UINT64_MAX};
  };
  // BSDFs shared by all surfaces with identical material parameters and
  // base color source (see bsdfKey()): one BSDF instance per distinct
  // appearance keeps the vectorized BSDF/texture calls small.
  std::map<std::string, mitsuba::ref<mitsuba::Object>> bsdfs;
  uint64_t uniqueKeyCounter{0};
  std::map<const void *, ShapesEntry> surfaces; // Surface* (object space)
  // Same without area emitters, for surfaces in shapegroups (Mitsuba does
  // not support emitters in instanced geometry).
  std::map<const void *, ShapesEntry> surfacesNoEmitter;
  std::map<const void *, Entry> emitters; // Light*
  std::map<const void *, Entry> groups; // Group* -> shapegroup
  std::map<const void *, Entry> instances; // Instance* -> instance shape
  // (Volume*, Instance*) -> medium cube in world space
  std::map<std::pair<const void *, const void *>, Entry> volumes;
  mitsuba::ref<mitsuba::Object> scene;
  uint64_t sceneStamp{UINT64_MAX};
  std::string variant;
};

TranslationCache *getCache(MitsubaGlobalState *state, const std::string &variant)
{
  if (!state->translationCache)
    state->translationCache = std::make_shared<TranslationCache>();
  auto *cache = (TranslationCache *)state->translationCache.get();
  if (cache->variant != variant) {
    *cache = TranslationCache();
    cache->variant = variant;
  }
  return cache;
}

// Composite change stamp of a helium object: any commit/finalize/update
// activity advances it.
uint64_t stampOf(const helium::BaseObject *o)
{
  return std::max<uint64_t>(o->lastUpdated(), o->lastFinalized());
}

float maxComponent(const float3 &c)
{
  return std::max({c.x, c.y, c.z});
}

// Where the base color of a surface comes from: the material constant, or a
// geometry attribute (per vertex or per primitive), optionally mapped
// through an image sampler.
struct ColorSource
{
  const ImageSampler *sampler{nullptr};
  // Sampler driven by the surface position (image3D) rather than by a
  // geometry attribute; exclusive with 'sampler' and the attribute arrays.
  const ImageSampler *positionSampler{nullptr};
  const Array1D *vertexAttr{nullptr};
  const Array1D *primAttr{nullptr};
  float3 constant{0.8f, 0.8f, 0.8f};

  bool varying() const { return vertexAttr || primAttr; }
  float3 colorOf(const float4 &attribute) const
  {
    if (sampler) {
      const float4 c = sampler->evaluate(attribute);
      return float3(c.x, c.y, c.z);
    }
    return float3(attribute.x, attribute.y, attribute.z);
  }
  float3 vertexColor(uint64_t v) const
  {
    return colorOf(readAttribute(vertexAttr, v));
  }
  float3 primitiveColor(uint64_t p) const
  {
    return colorOf(readAttribute(primAttr, p));
  }
};

ColorSource resolveColorSource(const Material *material, const Geometry *geo)
{
  ColorSource cs;
  const ColorParameter *param = materialBaseColor(material);
  if (!param)
    return cs;
  cs.constant = param->constant();
  cs.sampler = param->sampler();
  if (cs.sampler && cs.sampler->positionInput()) {
    cs.positionSampler = cs.sampler;
    cs.sampler = nullptr;
    return cs;
  }
  AttributeId id;
  if (param->sourceAttribute(id)) {
    // Lookup order as in the other ANARI devices: per vertex, per
    // primitive, geometry-wide constant.
    cs.vertexAttr = geo->vertexAttribute(id);
    if (!cs.vertexAttr)
      cs.primAttr = geo->primitiveAttribute(id);
    if (!cs.varying()) {
      if (auto u = geo->uniformAttribute(id))
        cs.constant = cs.colorOf(*u);
      // else: reported by Surface::finalize, the constant color is used
    }
  }
  if (!cs.varying())
    cs.sampler = nullptr;
  return cs;
}

// All Mitsuba-variant-typed translation code lives in this template; the
// string dispatch at the bottom of this file instantiates it once per variant
// compiled into the linked Mitsuba build.
template <typename Float, typename Spectrum>
struct VariantRender {
  using Scene = mitsuba::Scene<Float, Spectrum>;
  using Mesh = mitsuba::Mesh<Float, Spectrum>;
  using Shape = mitsuba::Shape<Float, Spectrum>;
  using BSDF = mitsuba::BSDF<Float, Spectrum>;
  using Emitter = mitsuba::Emitter<Float, Spectrum>;
  using Sensor = mitsuba::Sensor<Float, Spectrum>;
  using Film = mitsuba::Film<Float, Spectrum>;
  using Sampler = mitsuba::Sampler<Float, Spectrum>;
  using Integrator = mitsuba::Integrator<Float, Spectrum>;
  using Texture = mitsuba::Texture<Float, Spectrum>;
  using Medium = mitsuba::Medium<Float, Spectrum>;
  using MiVolume = mitsuba::Volume<Float, Spectrum>;
  using VolumeGrid = mitsuba::VolumeGrid<Float, Spectrum>;
  using FloatStorage = mitsuba::DynamicBuffer<Float>;
  using TensorXf = dr::Tensor<FloatStorage>;
  using PrimitiveColorTexture = mitsuba::AnariPrimitiveColor<Float, Spectrum>;

  // The base color slot of a BSDF: a constant, or a texture factory taking
  // a scale factor (Mitsuba textures can't be scaled after construction).
  struct BaseColor {
    float3 constant{0.8f, 0.8f, 0.8f};
    std::function<mitsuba::ref<Texture>(float)> texture;
    // Identity of the texture for BSDF sharing (empty: unique).
    std::string textureKey;
  };

  // Key of a BSDF built from 'material' and 'color'; BSDFs with equal keys
  // are interchangeable.
  static std::string bsdfKey(TranslationCache *cache,
      const Material &material,
      const BaseColor &color)
  {
    char buf[512];
    std::string key;
    if (dynamic_cast<const Matte *>(&material)) {
      key = "matte";
    } else if (const auto *pbr =
                   dynamic_cast<const PhysicallyBased *>(&material)) {
      const float3 sc = pbr->specularColor();
      std::snprintf(buf,
          sizeof(buf),
          "pbr %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g",
          pbr->metallic(),
          pbr->roughness(),
          pbr->ior(),
          pbr->opacity(),
          pbr->transmission(),
          pbr->specular(),
          sc.x,
          sc.y,
          sc.z);
      key = buf;
    } else {
      return "unique " + std::to_string(cache->uniqueKeyCounter++);
    }
    if (color.texture) {
      if (color.textureKey.empty())
        return "unique " + std::to_string(cache->uniqueKeyCounter++);
      key += " tex " + color.textureKey;
    } else {
      std::snprintf(buf,
          sizeof(buf),
          " rgb %.9g %.9g %.9g",
          color.constant.x,
          color.constant.y,
          color.constant.z);
      key += buf;
    }
    return key;
  }

  static mitsuba::ref<BSDF> sharedBSDF(TranslationCache *cache,
      const Material &material,
      const BaseColor &color)
  {
    auto &entry = cache->bsdfs[bsdfKey(cache, material, color)];
    if (!entry)
      entry = buildBSDF(material, color).get();
    return mitsuba::ref<BSDF>((BSDF *)entry.get());
  }

  static MiXform toXform(const mat4 &m)
  {
    return frameTransform(float3(m[0].x, m[0].y, m[0].z),
        float3(m[1].x, m[1].y, m[1].z),
        float3(m[2].x, m[2].y, m[2].z),
        float3(m[3].x, m[3].y, m[3].z));
  }

  static mitsuba::ref<VolumeGrid> makeGrid(
      const uint3 &res, uint32_t channels, const std::vector<float> &data)
  {
    mitsuba::ref<VolumeGrid> grid = new VolumeGrid(
        mitsuba::Vector<uint32_t, 3>(res.x, res.y, res.z), channels);
    std::memcpy(grid->data(), data.data(), data.size() * sizeof(float));
    std::vector<float> maxPerChannel(channels, 0.f);
    for (size_t i = 0; i < data.size(); ++i) {
      float &m = maxPerChannel[i % channels];
      m = std::max(m, data[i]);
    }
    grid->set_max(*std::max_element(maxPerChannel.begin(), maxPerChannel.end()));
    grid->set_max_per_channel(maxPerChannel.data());
    return grid;
  }

  // ANARI transferFunction1D volume -> a cube with a null BSDF enclosing a
  // heterogeneous medium whose extinction and albedo grids are classified
  // from the field (see TransferFunction1D::classifyGrid). Built in world
  // space: Mitsuba can't instance media.
  static mitsuba::ref<Shape> buildVolumeShape(
      const TransferFunction1D &volume, const mat4 &instanceXfm)
  {
    ClassifiedVolume cv;
    if (!volume.classifyGrid(cv))
      return {};
    auto *pmgr = mitsuba::PluginManager::instance();

    const float3 size = cv.upper - cv.lower;
    const mat4 unitToBox = mat4(float4(size.x, 0.f, 0.f, 0.f),
        float4(0.f, size.y, 0.f, 0.f),
        float4(0.f, 0.f, size.z, 0.f),
        float4(cv.lower.x, cv.lower.y, cv.lower.z, 1.f));
    const mat4 box = linalg::mul(instanceXfm, unitToBox);

    MiProps ps("gridvolume");
    setObject(ps, "grid", makeGrid(cv.resolution, 1, cv.sigmaT).get());
    ps.set("to_world", toXform(box));
    auto sigma = pmgr->template create_object<MiVolume>(ps);

    MiProps pa("gridvolume");
    setObject(pa, "grid", makeGrid(cv.resolution, 3, cv.albedo).get());
    pa.set("to_world", toXform(box));
    pa.set("raw", true);
    auto albedo = pmgr->template create_object<MiVolume>(pa);

    MiProps pm("heterogeneous");
    setObject(pm, "sigma_t", sigma.get());
    setObject(pm, "albedo", albedo.get());
    pm.set("scale", 1.f);
    auto medium = pmgr->template create_object<Medium>(pm);

    // Mitsuba's cube spans [-1, 1]^3.
    const mat4 cubeToUnit = mat4(float4(0.5f, 0.f, 0.f, 0.f),
        float4(0.f, 0.5f, 0.f, 0.f),
        float4(0.f, 0.f, 0.5f, 0.f),
        float4(0.5f, 0.5f, 0.5f, 1.f));
    MiProps pc("cube");
    pc.set("to_world", toXform(linalg::mul(box, cubeToUnit)));
    setObject(pc, "interior", medium.get());
    setObject(pc,
        "bsdf",
        pmgr->template create_object<BSDF>(MiProps("null")).get());
    return pmgr->template create_object<Shape>(pc);
  }

  static mitsuba::ref<Texture> expandTexture(mitsuba::ref<mitsuba::Object> tex)
  {
    // Some plugins (bitmap among them) are facades whose concrete
    // implementation is produced by expand() — the XML/dict parser does this
    // automatically, programmatic construction must do it explicitly.
    auto expanded = tex->expand();
    if (!expanded.empty())
      tex = expanded[0];
    return mitsuba::ref<Texture>((Texture *)tex.get());
  }

  // ANARI image1D/image2D sampler -> Mitsuba 'bitmap' texture over linear
  // float RGB texels with the sampler's output transform and the given scale
  // baked in. Texture coordinates come from the mesh (the sampler's input
  // transform is applied to them on translation); image1D is a 1-row image.
  static mitsuba::ref<Texture> buildBitmapTexture(
      const ImageSampler &sampler, float scale)
  {
    const uint3 res = sampler.resolution();
    const uint32_t w = res.x;
    const uint32_t h = std::max(res.y, 1u);
    const auto &texels = sampler.texels();

    // The Bitmap owns its pixel storage — plugins may hold on to the Bitmap
    // beyond construction, so it must never wrap a transient buffer. ANARI
    // element (0,0) is the uv=(0,0) texel; Bitmap row 0 is sampled at v=0,
    // so rows map through in array order.
    mitsuba::ref<mitsuba::Bitmap> bmp =
        new mitsuba::Bitmap(mitsuba::Bitmap::PixelFormat::RGB,
            mitsuba::sj::Type::Float32,
            mitsuba::Vector<uint32_t, 2>(w, h));
    auto *dst = (float *)bmp->data();
    for (size_t i = 0; i < size_t(w) * h; ++i) {
      const float4 c = sampler.transformOutput(texels[i]);
      dst[i * 3 + 0] = c.x * scale;
      dst[i * 3 + 1] = c.y * scale;
      dst[i * 3 + 2] = c.z * scale;
    }

    auto wrapMode = [](const std::string &m) -> const char * {
      if (m == "repeat")
        return "repeat";
      if (m == "mirrorRepeat")
        return "mirror";
      return "clamp"; // clampToEdge / default
    };

    MiProps p("bitmap");
    setObject(p, "bitmap", bmp.get());
    p.set("raw", true); // texels are linear already
    p.set("filter_type", sampler.nearestFilter() ? "nearest" : "bilinear");
    // Mitsuba's bitmap texture has a single wrap mode for both axes; a
    // wrapMode1 != wrapMode2 mismatch is reported by Image2D::finalize().
    p.set("wrap_mode", wrapMode(sampler.wrapMode(0)));
    auto *pmgr = mitsuba::PluginManager::instance();
    return expandTexture(pmgr->create_object(p,
        mitsuba::detail::variant<Float, Spectrum>::name,
        mitsuba::ObjectType::Texture));
  }

  // ANARI image3D sampler driven by the surface position -> Mitsuba 'volume'
  // texture over an RGB 'gridvolume', with the sampler's output transform
  // and the given scale baked into the texels. The grid spans the unit cube
  // of the texture coordinate (inTransform * position + inOffset), so its
  // to_world is the inverse of that map. 'objectPosition' is looked up in
  // world space too, which is the same thing for surfaces whose instance
  // transform is the identity.
  static mitsuba::ref<Texture> buildVolumeTexture(
      const ImageSampler &sampler, float scale)
  {
    const auto &texels = sampler.texels();
    std::vector<float> data(texels.size() * 3);
    for (size_t i = 0; i < texels.size(); ++i) {
      const float4 c = sampler.transformOutput(texels[i]);
      data[i * 3 + 0] = c.x * scale;
      data[i * 3 + 1] = c.y * scale;
      data[i * 3 + 2] = c.z * scale;
    }

    auto wrapMode = [](const std::string &m) -> const char * {
      if (m == "repeat")
        return "repeat";
      if (m == "mirrorRepeat")
        return "mirror";
      return "clamp"; // clampToEdge / default
    };

    auto *pmgr = mitsuba::PluginManager::instance();
    MiProps pg("gridvolume");
    setObject(pg, "grid", makeGrid(sampler.resolution(), 3, data).get());
    pg.set("to_world", toXform(linalg::inverse(sampler.inputMatrix())));
    pg.set("raw", true); // texels are linear already
    pg.set("filter_type", sampler.nearestFilter() ? "nearest" : "trilinear");
    // A grid volume has one wrap mode for all axes; a mismatch is reported
    // by Image3D::finalize().
    pg.set("wrap_mode", wrapMode(sampler.wrapMode(0)));
    auto grid = pmgr->template create_object<MiVolume>(pg);

    MiProps pt("volume");
    setObject(pt, "volume", grid.get());
    return pmgr->template create_object<Texture>(pt);
  }

  static mitsuba::ref<Texture> meshAttributeTexture(
      const char *name, float scale)
  {
    auto *pmgr = mitsuba::PluginManager::instance();
    MiProps p("mesh_attribute");
    p.set("name", name);
    p.set("scale", scale);
    return pmgr->template create_object<Texture>(p);
  }

  // Constant color texture which may exceed 1 (plain colors are reflectance
  // spectra, which Mitsuba restricts to [0, 1]).
  static mitsuba::ref<Texture> unboundedColor(const float3 &c)
  {
    auto *pmgr = mitsuba::PluginManager::instance();
    MiProps p("srgb");
    p.set("color", toColor(c));
    p.set("unbounded", true);
    return pmgr->template create_object<Texture>(p);
  }

  // Sets a color slot ('reflectance', 'base_color', ...) from a base color
  // scaled by 'scale'.
  static void setColorSlot(MiProps &p,
      std::string_view slot,
      const BaseColor &color,
      float scale = 1.f)
  {
    if (color.texture)
      setObject(p, slot, color.texture(scale).get());
    else if (scale == 1.f && maxComponent(color.constant) <= 1.f)
      p.set(slot, toColor(color.constant));
    else
      setObject(p, slot, unboundedColor(color.constant * scale).get());
  }

  static mitsuba::ref<BSDF> twoSided(mitsuba::ref<BSDF> bsdf)
  {
    auto *pmgr = mitsuba::PluginManager::instance();
    MiProps p("twosided");
    setObject(p, "bsdf", bsdf.get());
    return pmgr->template create_object<BSDF>(p);
  }

  // (1 - weight) * a + weight * b
  static mitsuba::ref<BSDF> blend(
      mitsuba::ref<BSDF> a, mitsuba::ref<BSDF> b, float weight)
  {
    if (weight >= 1.f)
      return b;
    auto *pmgr = mitsuba::PluginManager::instance();
    MiProps p("blendbsdf");
    p.set("weight", weight);
    setObject(p, "bsdf_0", a.get());
    setObject(p, "bsdf_1", b.get());
    return pmgr->template create_object<BSDF>(p);
  }

  // Area emitter for an emissive surface, one per shape.
  static mitsuba::ref<Emitter> buildSurfaceEmitter(const Material &material)
  {
    const auto *pbr = dynamic_cast<const PhysicallyBased *>(&material);
    if (!pbr || !pbr->isEmissive())
      return {};
    auto *pmgr = mitsuba::PluginManager::instance();
    MiProps p("area");
    p.set("radiance", toColor(pbr->emissive()));
    return pmgr->template create_object<Emitter>(p);
  }

  static mitsuba::ref<BSDF> buildBSDF(
      const Material &material, const BaseColor &baseColor)
  {
    auto *pmgr = mitsuba::PluginManager::instance();

    if (dynamic_cast<const Matte *>(&material)) {
      MiProps p("diffuse");
      setColorSlot(p, "reflectance", baseColor);
      // ANARI surfaces are two-sided, Mitsuba BSDFs are one-sided.
      return twoSided(pmgr->template create_object<BSDF>(p));
    }

    if (const auto *pbr = dynamic_cast<const PhysicallyBased *>(&material)) {
      // ANARI PBR, modeled like glTF / Cycles principled materials: a GGX
      // specular layer with dielectric Fresnel over a Lambertian base that
      // is attenuated by the energy the specular layer reflects, blended
      // with a metal and a glass lobe.
      //
      // Mitsuba's Disney 'principled' BSDF differs from that noticeably: its
      // diffuse lobe is not attenuated by the specular layer and has a
      // retro-reflective Fresnel boost, making dielectrics 4-10% brighter
      // (more at grazing angles). So the dielectric part is built from the
      // exact pieces instead:
      //   specular: 'roughplastic' with a black diffuse substrate
      //   diffuse:  'diffuse' scaled by 1 - F0 (the specular albedo of a
      //             rough dielectric coating is close to its normal
      //             incidence Fresnel reflectance)
      // summed through a 50% 'blendbsdf' of both lobes at twice their
      // weight. Without a specular layer (specular = 0) only the diffuse
      // lobe remains: a (rough)plastic with zero reflectances has a 0/0
      // lobe sampling weight and produces NaNs.
      const float roughness = pbr->roughness();
      const float alpha = roughness * roughness;
      const bool smooth = alpha < 1e-3f;
      const float ior = pbr->ior();
      const float f0 = ((ior - 1.f) / (ior + 1.f)) * ((ior - 1.f) / (ior + 1.f));
      const float3 specularTint = pbr->specularColor() * pbr->specular();
      const bool hasSpecular = maxComponent(specularTint) > 0.f;
      const float diffuseScale =
          std::max(0.f, 1.f - f0 * std::min(pbr->specular(), 1.f));

      mitsuba::ref<BSDF> bsdf;
      if (hasSpecular) {
        MiProps ps(smooth ? "plastic" : "roughplastic");
        if (!smooth) {
          ps.set("distribution", "ggx");
          ps.set("alpha", alpha);
        }
        ps.set("int_ior", ior);
        ps.set("nonlinear", false);
        ps.set("diffuse_reflectance", toColor(float3(0.f)));
        setObject(ps,
            "specular_reflectance",
            unboundedColor(specularTint * 2.f).get());

        MiProps pd("diffuse");
        setColorSlot(pd, "reflectance", baseColor, 2.f * diffuseScale);

        bsdf = blend(pmgr->template create_object<BSDF>(ps),
            pmgr->template create_object<BSDF>(pd),
            0.5f);
      } else {
        MiProps pd("diffuse");
        setColorSlot(pd, "reflectance", baseColor, diffuseScale);
        bsdf = pmgr->template create_object<BSDF>(pd);
      }

      if (pbr->transmission() > 0.f) {
        MiProps pt(smooth ? "dielectric" : "roughdielectric");
        if (!smooth) {
          pt.set("distribution", "ggx");
          pt.set("alpha", alpha);
        }
        pt.set("int_ior", ior);
        setColorSlot(pt, "specular_transmittance", baseColor);
        // ANARI surfaces are two-sided, Mitsuba BSDFs are not; a
        // transmissive BSDF handles both sides itself.
        auto glass = pmgr->template create_object<BSDF>(pt);
        bsdf = pbr->transmission() >= 1.f
            ? glass
            : blend(twoSided(bsdf), glass, pbr->transmission());
      } else {
        bsdf = twoSided(bsdf);
      }

      if (pbr->metallic() > 0.f) {
        MiProps pm("principled");
        setColorSlot(pm, "base_color", baseColor);
        pm.set("metallic", 1.f);
        pm.set("roughness", roughness);
        pm.set("eta", ior);
        bsdf = blend(bsdf,
            twoSided(pmgr->template create_object<BSDF>(pm)),
            pbr->metallic());
      }

      if (pbr->opacity() < 1.f) {
        // Opacity maps to a 'mask' blend with a null BSDF.
        MiProps pm("mask");
        pm.set("opacity", pbr->opacity());
        setObject(pm, "bsdf", bsdf.get());
        bsdf = pmgr->template create_object<BSDF>(pm);
      }
      return bsdf;
    }

    throw std::runtime_error("unsupported ANARI material subtype");
  }

  // Creates a Mitsuba mesh (without BSDF, not yet initialized) from flat
  // buffers. Optional buffers may be empty.
  static mitsuba::ref<Mesh> createMesh(const std::vector<float> &P,
      const std::vector<float> &N,
      const std::vector<float> &UV,
      const std::vector<uint32_t> &I,
      const std::vector<float> &vertexColors,
      const std::vector<float> &faceColors,
      Emitter *emitter)
  {
    const uint32_t vcount = uint32_t(P.size() / 3);
    const uint32_t fcount = uint32_t(I.size() / 3);
    MiProps props;
    if (emitter)
      setObject(props, "emitter", emitter);
    if (N.empty()) {
      // ANARI triangles without vertex normals shade with facet normals.
      props.set("face_normals", true);
    }
    mitsuba::ref<Mesh> mesh = new Mesh("anari_triangle",
        vcount,
        fcount,
        props,
        !N.empty(),
        !UV.empty());
    using PosBuffer = std::decay_t<decltype(mesh->vertex_positions_buffer())>;
    using FaceBuffer = std::decay_t<decltype(mesh->faces_buffer())>;
    mesh->vertex_positions_buffer() = dr::load<PosBuffer>(P.data(), P.size());
    if (!N.empty())
      mesh->vertex_normals_buffer() = dr::load<PosBuffer>(N.data(), N.size());
    if (!UV.empty())
      mesh->vertex_texcoords_buffer() =
          dr::load<PosBuffer>(UV.data(), UV.size());
    mesh->faces_buffer() = dr::load<FaceBuffer>(I.data(), I.size());
    // Colors are exposed as mesh attributes, read by 'mesh_attribute'
    // textures in the BSDF.
    if (!vertexColors.empty())
      mesh->add_attribute("vertex_color", 3, vertexColors);
    if (!faceColors.empty())
      mesh->add_attribute("face_color", 3, faceColors);
    return mesh;
  }

  static void finishMesh(Mesh *mesh, BSDF *bsdf)
  {
    mesh->set_bsdf(bsdf);
    mesh->initialize();
  }

  // ANARI triangle/quad geometry -> Mitsuba mesh in object space; instance
  // transforms are applied through Mitsuba-native shapegroup/instance
  // shapes (Milestone 11), so one mesh is shared by all instances. The base
  // color is filled in for the BSDF: vertex colors, face colors or texture
  // coordinates for an image sampler.
  static mitsuba::ref<Mesh> buildMesh(const MeshGeometry &geo,
      const ColorSource &cs,
      Emitter *emitter,
      BaseColor &baseColor)
  {
    const Array1D *posArray = geo.vertexPositions();
    const uint64_t vcount = posArray->size();
    const auto *pos = (const float3 *)posArray->data();

    const uint32_t vpp = geo.vertsPerPrimitive(); // 3 = triangle, 4 = quad
    const Array1D *idxArray = geo.indices();
    const uint64_t primCount = geo.numPrimitives();
    // Quads are triangulated (0,1,2)+(0,2,3).
    const uint64_t fcount = vpp == 3 ? primCount : primCount * 2;

    std::vector<float> P(vcount * 3);
    std::memcpy(P.data(), pos, vcount * 3 * sizeof(float));

    std::vector<float> N;
    if (const Array1D *nrmArray = geo.vertexNormals()) {
      N.resize(vcount * 3);
      std::memcpy(N.data(), nrmArray->data(), vcount * 3 * sizeof(float));
    }

    std::vector<uint32_t> I(fcount * 3);
    if (vpp == 3) {
      if (idxArray) {
        const auto *idx = (const uint32_t *)idxArray->data();
        std::copy(idx, idx + fcount * 3, I.begin());
      } else {
        std::iota(I.begin(), I.end(), 0u);
      }
    } else {
      for (uint64_t i = 0; i < primCount; ++i) {
        uint32_t q[4];
        if (idxArray) {
          const auto *idx = (const uint32_t *)idxArray->data();
          for (uint32_t k = 0; k < 4; ++k)
            q[k] = idx[i * 4 + k];
        } else {
          for (uint32_t k = 0; k < 4; ++k)
            q[k] = (uint32_t)(i * 4 + k);
        }
        I[i * 6 + 0] = q[0];
        I[i * 6 + 1] = q[1];
        I[i * 6 + 2] = q[2];
        I[i * 6 + 3] = q[0];
        I[i * 6 + 4] = q[2];
        I[i * 6 + 5] = q[3];
      }
    }

    std::vector<float> UV, vertexColors, faceColors;
    baseColor.constant = cs.constant;
    if (cs.vertexAttr && cs.sampler) {
      // Image sampler on a per-vertex attribute: the transformed attribute
      // becomes the texture coordinate (exact, the input transform is
      // affine); image1D uses a 1-row image.
      const bool is1D = cs.sampler->resolution().y <= 1;
      UV.resize(vcount * 2);
      for (uint64_t i = 0; i < vcount; ++i) {
        const float4 tc =
            cs.sampler->transformInput(readAttribute(cs.vertexAttr, i));
        UV[i * 2 + 0] = tc.x;
        UV[i * 2 + 1] = is1D ? 0.5f : tc.y;
      }
      const ImageSampler *sampler = cs.sampler;
      baseColor.texture = [sampler](float scale) {
        return buildBitmapTexture(*sampler, scale);
      };
      baseColor.textureKey = "sampler " + std::to_string(uintptr_t(sampler))
          + " " + std::to_string(stampOf(sampler));
    } else if (cs.vertexAttr) {
      vertexColors.resize(vcount * 3);
      for (uint64_t i = 0; i < vcount; ++i) {
        const float3 c = cs.vertexColor(i);
        vertexColors[i * 3 + 0] = c.x;
        vertexColors[i * 3 + 1] = c.y;
        vertexColors[i * 3 + 2] = c.z;
      }
      baseColor.texture = [](float scale) {
        return meshAttributeTexture("vertex_color", scale);
      };
      baseColor.textureKey = "vertex_color";
    } else if (cs.primAttr) {
      faceColors.resize(fcount * 3);
      const uint32_t trisPerPrim = vpp == 3 ? 1 : 2;
      for (uint64_t i = 0; i < primCount; ++i) {
        const float3 c = cs.primitiveColor(i);
        for (uint32_t t = 0; t < trisPerPrim; ++t) {
          float *f = &faceColors[(i * trisPerPrim + t) * 3];
          f[0] = c.x;
          f[1] = c.y;
          f[2] = c.z;
        }
      }
      baseColor.texture = [](float scale) {
        return meshAttributeTexture("face_color", scale);
      };
      baseColor.textureKey = "face_color";
    }

    return createMesh(P, N, UV, I, vertexColors, faceColors, emitter);
  }

  // Mitsuba mesh from a CPU-tessellated triangle mesh.
  static mitsuba::ref<Mesh> buildTriMesh(
      const TriMesh &tm, Emitter *emitter, BaseColor &baseColor)
  {
    std::vector<float> P(tm.vertexCount() * 3), N(tm.vertexCount() * 3);
    std::memcpy(P.data(), tm.positions.data(), P.size() * sizeof(float));
    std::memcpy(N.data(), tm.normals.data(), N.size() * sizeof(float));
    std::vector<uint32_t> I(tm.triangleCount() * 3);
    std::memcpy(I.data(), tm.triangles.data(), I.size() * sizeof(uint32_t));
    std::vector<float> vertexColors, faceColors;
    if (!tm.vertexColors.empty()) {
      vertexColors.resize(tm.vertexColors.size() * 3);
      std::memcpy(vertexColors.data(),
          tm.vertexColors.data(),
          vertexColors.size() * sizeof(float));
      baseColor.texture = [](float scale) {
        return meshAttributeTexture("vertex_color", scale);
      };
      baseColor.textureKey = "vertex_color";
    } else if (!tm.faceColors.empty()) {
      faceColors.resize(tm.faceColors.size() * 3);
      std::memcpy(faceColors.data(),
          tm.faceColors.data(),
          faceColors.size() * sizeof(float));
      baseColor.texture = [](float scale) {
        return meshAttributeTexture("face_color", scale);
      };
      baseColor.textureKey = "face_color";
    }
    return createMesh(P, N, {}, I, vertexColors, faceColors, emitter);
  }

  // Builds a rotation-and-translation transform from explicit basis columns.
  static MiXform frameTransform(
      const float3 &x, const float3 &y, const float3 &z, const float3 &origin)
  {
    using V1 = dr::Array<float, 1>;
    using M = typename MiXform::Matrix;
    // dr::Matrix rows -> transpose yields the column-based transform.
    M m = dr::transpose(M(
        dr::concat(mitsuba::Vector<float, 3>(x.x, x.y, x.z), V1(0)),
        dr::concat(mitsuba::Vector<float, 3>(y.x, y.y, y.z), V1(0)),
        dr::concat(mitsuba::Vector<float, 3>(z.x, z.y, z.z), V1(0)),
        dr::concat(
            mitsuba::Vector<float, 3>(origin.x, origin.y, origin.z), V1(1))));
    return MiXform(m);
  }

  // Non-shape lights (directional, point, hdri). Quad lights are shapes and
  // handled separately in run().
  static mitsuba::ref<Emitter> buildEmitter(const Light &light)
  {
    auto *pmgr = mitsuba::PluginManager::instance();

    if (const auto *dir = dynamic_cast<const Directional *>(&light)) {
      MiProps p("directional");
      const float3 d = dir->direction();
      p.set("direction", mitsuba::Vector<float, 3>(d.x, d.y, d.z));
      p.set("irradiance", toColor(dir->color() * dir->irradiance()));
      return pmgr->template create_object<Emitter>(p);
    }

    if (const auto *pt = dynamic_cast<const Point *>(&light)) {
      MiProps p("point");
      const float3 pos = pt->position();
      p.set("position", mitsuba::Point<float, 3>(pos.x, pos.y, pos.z));
      p.set("intensity", toColor(pt->color() * pt->intensity()));
      return pmgr->template create_object<Emitter>(p);
    }

    if (const auto *spot = dynamic_cast<const Spot *>(&light)) {
      MiProps p("spot");
      const float3 pos = spot->position();
      const float3 dir = spot->direction();
      // The spot emitter shines along local +Z.
      const float3 up = std::fabs(dir.z) < 0.99f ? float3(0.f, 0.f, 1.f)
                                                 : float3(0.f, 1.f, 0.f);
      p.set("to_world",
          MiXform::look_at(mitsuba::Point<float, 3>(pos.x, pos.y, pos.z),
              mitsuba::Point<float, 3>(
                  pos.x + dir.x, pos.y + dir.y, pos.z + dir.z),
              mitsuba::Vector<float, 3>(up.x, up.y, up.z)));
      const float toDegrees = 180.f / 3.14159265358979323846f;
      const float cutoff = 0.5f * spot->openingAngle();
      const float beam = std::max(cutoff - spot->falloffAngle(), 1e-4f);
      p.set("cutoff_angle", cutoff * toDegrees);
      p.set("beam_width", beam * toDegrees);
      p.set("intensity", toColor(spot->color() * spot->intensity()));
      return pmgr->template create_object<Emitter>(p);
    }

    if (const auto *hdri = dynamic_cast<const HDRI *>(&light)) {
      const Array2D *rad = hdri->radiance();
      const uint32_t w = (uint32_t)rad->size(0);
      const uint32_t h = (uint32_t)rad->size(1);
      // Tint and scale are baked into the (copied) radiance data; the envmap
      // plugin itself only exposes a scalar 'scale'.
      const float3 tint = hdri->color() * hdri->scale();
      const auto *src = (const float *)rad->data();
      // ANARI's first row is the bottom of the environment (the direction
      // opposite to 'up', as in the other ANARI devices), Mitsuba's bitmap
      // starts at the top.
      std::vector<float> rgb(size_t(w) * h * 3);
      for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
          const size_t i = (h - 1 - y) * w + x;
          const size_t o = y * w + x;
          rgb[o * 3 + 0] = src[i * 3 + 0] * tint.x;
          rgb[o * 3 + 1] = src[i * 3 + 1] * tint.y;
          rgb[o * 3 + 2] = src[i * 3 + 2] * tint.z;
        }
      }
      // The Bitmap owns its storage — the envmap plugin keeps referencing the
      // bitmap after construction, so wrapping a transient buffer would leave
      // a dangling pointer (verified the hard way).
      mitsuba::ref<mitsuba::Bitmap> bmp =
          new mitsuba::Bitmap(mitsuba::Bitmap::PixelFormat::RGB,
              mitsuba::sj::Type::Float32,
              mitsuba::Vector<uint32_t, 2>(w, h));
      std::memcpy(bmp->data(), rgb.data(), rgb.size() * sizeof(float));

      MiProps p("envmap");
      setObject(p, "bitmap", bmp.get());
      // Orientation: ANARI 'up' maps to the environment's up axis (Mitsuba
      // +Y) and 'direction' to the horizontal image center. Documented as an
      // orientation approximation in SUPPORTED_FEATURES.md.
      const float3 up = linalg::normalize(hdri->up());
      const float3 dir = hdri->direction();
      const float3 x = linalg::normalize(linalg::cross(up, dir));
      const float3 z = linalg::cross(x, up);
      p.set("to_world", frameTransform(x, up, z, float3(0.f)));
      return pmgr->template create_object<Emitter>(p);
    }

    throw std::runtime_error("unsupported ANARI light subtype");
  }

  // Quad lights are area emitters attached to a rectangle shape.
  static mitsuba::ref<Shape> buildQuadLightShape(const QuadLight &quad)
  {
    auto *pmgr = mitsuba::PluginManager::instance();

    MiProps pe("area");
    pe.set("radiance", toColor(quad.color() * quad.radiance()));
    auto emitter = pmgr->template create_object<Emitter>(pe);

    // Mitsuba's rectangle spans [-1,1]^2 in its local XY plane; map it onto
    // position + edge1/edge2.
    const float3 e1h = quad.edge1() * 0.5f;
    const float3 e2h = quad.edge2() * 0.5f;
    const float3 n = linalg::normalize(
        linalg::cross(quad.edge1(), quad.edge2()));
    const float3 center = quad.position() + e1h + e2h;

    MiProps ps("rectangle");
    ps.set("to_world", frameTransform(e1h, e2h, n, center));
    setObject(ps, "emitter", emitter.get());
    // ANARI lights are not geometry: the rectangle must not occlude or
    // reflect (the default BSDF would be a gray diffuse surface).
    setObject(ps,
        "bsdf",
        pmgr->template create_object<BSDF>(MiProps("null")).get());
    return pmgr->template create_object<Shape>(ps);
  }

  static mitsuba::ref<Sensor> buildSensor(
      const std::string &variant, const Camera &camera, uint2 frameSize,
      int pixelSamples)
  {
    auto *pmgr = mitsuba::PluginManager::instance();

    // 'imageRegion': the frame shows the part (x0, y0, x1, y1) of the image
    // plane which 'fovy' (or 'height') spans. That part is a view volume
    // around the same direction whose axis is off its center: 'center' is
    // the center of the region relative to the center of the image plane, in
    // units of the region's size (x to the right, y up).
    const float4 region = camera.imageRegion();
    const bool fullRegion = camera.hasFullImageRegion();
    const float2 regionSize(region.z - region.x, region.w - region.y);
    const float2 center((region.x + region.z - 1.f) / (2.f * regionSize.x),
        (region.y + region.w - 1.f) / (2.f * regionSize.y));

    const auto *persp = dynamic_cast<const Perspective *>(&camera);
    const bool dof = persp && persp->apertureRadius() > 0.f;

    // Mitsuba's thin lens camera has no principal point offset. For it the
    // region is the crop window of a larger film which is centered on the
    // optical axis. Film size and crop offset are integral: the window can be
    // off by up to a quarter of a pixel.
    const bool cropRegion = dof && !fullRegion;
    uint2 filmSize = frameSize;
    uint2 cropOffset(0u, 0u);
    if (cropRegion) {
      const float2 halfExtent(
          std::max(std::fabs(region.x - 0.5f), std::fabs(region.z - 0.5f))
              / regionSize.x,
          std::max(std::fabs(region.y - 0.5f), std::fabs(region.w - 0.5f))
              / regionSize.y);
      // 'c': center of the window from the film's center, in window sizes,
      // along the film axis (x to the right, y down)
      const double c[2] = {center.x, -center.y};
      for (int i = 0; i < 2; ++i) {
        const double size = frameSize[i];
        const double first = (c[i] - 0.5) * size; // from the film's center
        const uint32_t minSize =
            (uint32_t)std::ceil(2.0 * halfExtent[i] * size) + 2u;
        double bestError = 1.0;
        for (uint32_t s = minSize; s < minSize + 2u; ++s) {
          const double offset = 0.5 * s + first;
          const double rounded = std::floor(offset + 0.5);
          if (std::fabs(offset - rounded) < bestError) {
            bestError = std::fabs(offset - rounded);
            filmSize[i] = s;
            cropOffset[i] = (uint32_t)std::min(
                std::max(rounded, 0.0), double(s) - size);
          }
        }
      }
    }

    MiProps pf("hdrfilm");
    pf.set("width", (int64_t)filmSize.x);
    pf.set("height", (int64_t)filmSize.y);
    if (cropRegion) {
      pf.set("crop_offset_x", (int64_t)cropOffset.x);
      pf.set("crop_offset_y", (int64_t)cropOffset.y);
      pf.set("crop_width", (int64_t)frameSize.x);
      pf.set("crop_height", (int64_t)frameSize.y);
    }
    pf.set("pixel_format", "rgba");
    pf.set("component_format", "float32");
    {
      // Box filter keeps render tests deterministic and ringing-free.
      MiProps prf("box");
      setObject(pf,
          "rfilter",
          pmgr->create_object(
                  prf, variant, mitsuba::ObjectType::ReconstructionFilter)
              .get());
    }

    MiProps psampler("independent");
    psampler.set("sample_count", (int64_t)pixelSamples);
    psampler.set("seed", (int64_t)0); // deterministic baseline

    const float3 org = camera.position();
    const float3 tgt = org + camera.direction();
    const float3 up = camera.up();
    const MiXform lookAt =
        MiXform::look_at(mitsuba::Point<float, 3>(org.x, org.y, org.z),
            mitsuba::Point<float, 3>(tgt.x, tgt.y, tgt.z),
            mitsuba::Vector<float, 3>(up.x, up.y, up.z));

    MiProps ps;
    if (persp) {
      // Depth of field (apertureRadius > 0) uses Mitsuba's thin lens camera.
      ps = MiProps(dof ? "thinlens" : "perspective");
      // Mitsuba's fov is the one of the whole film (the frame, or the larger
      // film around the crop window); its pixels are square, so the
      // horizontal extent follows from the film ('aspect' is not used).
      double fovy = persp->fovy();
      if (!fullRegion) {
        fovy = 2.0
            * std::atan(std::tan(0.5 * fovy) * regionSize.y
                * (double(filmSize.y) / double(frameSize.y)));
      }
      ps.set("fov", fovy * (180.0 / 3.14159265358979323846));
      ps.set("fov_axis", "y");
      ps.set("to_world", lookAt);
      if (dof) {
        ps.set("aperture_radius", persp->apertureRadius());
        ps.set("focus_distance", persp->focusDistance());
      } else if (!fullRegion) {
        // in units of the film size, y pointing down
        ps.set("principal_point_offset_x", center.x);
        ps.set("principal_point_offset_y", -center.y);
      }
    } else if (const auto *ortho =
                   dynamic_cast<const Orthographic *>(&camera)) {
      // Mitsuba's orthographic sensor spans [-1,1] along its local x axis and
      // [-1/a,1/a] along y, 'a' being the aspect ratio of the film. Scale it
      // to the part of the ANARI view (height, aspect) which the region
      // selects, and move it to the center of that part.
      ps = MiProps("orthographic");
      const float halfH = 0.5f * ortho->height();
      const float halfW = halfH * ortho->aspect();
      const float filmAspect = float(frameSize.x) / float(frameSize.y);
      MiXform toWorld = lookAt;
      if (!fullRegion) {
        // The local x axis of look_at points to the left of the image.
        toWorld = toWorld
            * MiXform::translate(mitsuba::Vector<float, 3>(
                -2.f * center.x * regionSize.x * halfW,
                2.f * center.y * regionSize.y * halfH,
                0.f));
      }
      ps.set("to_world",
          toWorld
              * MiXform::scale(mitsuba::Vector<float, 3>(halfW * regionSize.x,
                  halfH * regionSize.y * filmAspect,
                  1.f)));
    } else {
      throw std::runtime_error("unsupported ANARI camera subtype");
    }

    // Clip planes of the camera rays: Mitsuba starts these rays on the near
    // plane and ends them on the far plane, both perpendicular to the camera
    // direction (Frame adds Camera::nearClipDistance() to the depth channel).
    if (camera.hasNearClip())
      ps.set("near_clip", camera.nearClip());
    if (camera.hasFarClip())
      ps.set("far_clip", camera.farClip());

    setObject(ps, "film", pmgr->template create_object<Film>(pf).get());
    setObject(
        ps, "sampler", pmgr->template create_object<Sampler>(psampler).get());
    return pmgr->template create_object<Sensor>(ps);
  }

  static mitsuba::ref<Shape> buildSphere(
      const float3 &c, float r, BSDF *bsdf, Emitter *emitter)
  {
    auto *pmgr = mitsuba::PluginManager::instance();
    MiProps p("sphere");
    p.set("center", mitsuba::Point<float, 3>(c.x, c.y, c.z));
    p.set("radius", r);
    setObject(p, "bsdf", bsdf);
    if (emitter)
      setObject(p, "emitter", emitter);
    return pmgr->template create_object<Shape>(p);
  }

  static mitsuba::ref<Shape> buildCylinder(const float3 &a,
      const float3 &b,
      float r,
      BSDF *bsdf,
      Emitter *emitter)
  {
    auto *pmgr = mitsuba::PluginManager::instance();
    MiProps p("cylinder");
    p.set("p0", mitsuba::Point<float, 3>(a.x, a.y, a.z));
    p.set("p1", mitsuba::Point<float, 3>(b.x, b.y, b.z));
    p.set("radius", r);
    setObject(p, "bsdf", bsdf);
    if (emitter)
      setObject(p, "emitter", emitter);
    return pmgr->template create_object<Shape>(p);
  }

  // Disk of radius r at 'center' facing 'normal'.
  static mitsuba::ref<Shape> buildDisk(const float3 &center,
      const float3 &normal,
      float r,
      BSDF *bsdf,
      Emitter *emitter)
  {
    auto *pmgr = mitsuba::PluginManager::instance();
    const float3 n = linalg::normalize(normal);
    const float3 t = std::fabs(n.x) < 0.9f ? float3(1.f, 0.f, 0.f)
                                          : float3(0.f, 1.f, 0.f);
    const float3 u = linalg::normalize(linalg::cross(t, n));
    const float3 v = linalg::cross(n, u);
    MiProps p("disk");
    p.set("to_world", frameTransform(u * r, v * r, n, center));
    setObject(p, "bsdf", bsdf);
    if (emitter)
      setObject(p, "emitter", emitter);
    return pmgr->template create_object<Shape>(p);
  }

  // One Mitsuba 'ellipsoids' shape for a set of spheres (centers + radii,
  // identity rotations).
  static mitsuba::ref<Shape> buildEllipsoids(
      const std::vector<float> &data, BSDF *bsdf)
  {
    auto *pmgr = mitsuba::PluginManager::instance();
    const size_t n = data.size() / 10;
    size_t shape[2] = {n, 10};
    TensorXf tensor(dr::load<FloatStorage>(data.data(), data.size()), 2, shape);
    MiProps p("ellipsoids");
    p.set_any("data", std::move(tensor));
    p.set("extent", 1.f);
    setObject(p, "bsdf", bsdf);
    return pmgr->template create_object<Shape>(p);
  }

  // Builds (or reuses) the object-space shape list for one surface.
  static const std::vector<mitsuba::ref<mitsuba::Object>> &surfaceShapes(
      TranslationCache *cache,
      const Surface *surface,
      RenderStats &stats,
      bool withEmitters = true)
  {
    const uint64_t stamp = std::max({stampOf(surface),
        stampOf(surface->geometry()),
        stampOf(surface->material())});
    auto &entry = withEmitters ? cache->surfaces[surface]
                               : cache->surfacesNoEmitter[surface];
    if (entry.stamp == stamp) {
      ++stats.cacheHits;
      return entry.shapes;
    }
    ++stats.cacheMisses;
    entry.shapes.clear();
    entry.stamp = stamp;

    const Material &material = *surface->material();
    const Geometry *geometry = surface->geometry();
    const ColorSource cs = resolveColorSource(&material, geometry);
    const auto *pbr = dynamic_cast<const PhysicallyBased *>(&material);
    const bool transmissive = pbr && pbr->transmission() > 0.f;

    // Emissive materials get an area emitter on every shape (one each, an
    // emitter belongs to a single shape).
    const bool emissive =
        withEmitters && buildSurfaceEmitter(material).get() != nullptr;
    auto newEmitter = [&]() -> mitsuba::ref<Emitter> {
      return emissive ? buildSurfaceEmitter(material)
                      : mitsuba::ref<Emitter>();
    };
    // A position-driven sampler (image3D) textures every kind of geometry
    // the same way: it replaces what would be a constant color.
    auto applyPositionSampler = [&](BaseColor &bc) {
      const ImageSampler *sampler = cs.positionSampler;
      if (!sampler || bc.texture)
        return;
      bc.texture = [sampler](float scale) {
        return buildVolumeTexture(*sampler, scale);
      };
      bc.textureKey = "sampler3d " + std::to_string(uintptr_t(sampler)) + " "
          + std::to_string(stampOf(sampler));
    };
    auto constantBSDF = [&](const float3 &c) {
      BaseColor bc;
      bc.constant = c;
      applyPositionSampler(bc);
      return sharedBSDF(cache, material, bc);
    };
    auto addShape = [&](mitsuba::Object *s) {
      if (s)
        entry.shapes.emplace_back(s);
    };
    auto addTriMesh = [&](const TriMesh &tm) {
      if (tm.triangleCount() == 0)
        return;
      BaseColor bc;
      bc.constant = cs.constant;
      auto emitter = newEmitter();
      auto mesh = buildTriMesh(tm, emitter.get(), bc);
      applyPositionSampler(bc);
      finishMesh(mesh.get(), sharedBSDF(cache, material, bc).get());
      addShape(mesh.get());
    };
    // Per-vertex colors of the geometry's vertices (CPU-evaluated), used by
    // the tessellated paths.
    auto vertexColorTable = [&](uint64_t numVertices) {
      std::vector<float3> colors;
      if (cs.vertexAttr) {
        colors.resize(numVertices);
        for (uint64_t i = 0; i < numVertices; ++i)
          colors[i] = cs.vertexColor(i);
      }
      return colors;
    };
    auto primitiveColorTable = [&](uint64_t numPrimitives) {
      std::vector<float3> colors;
      if (cs.primAttr) {
        colors.resize(numPrimitives);
        for (uint64_t i = 0; i < numPrimitives; ++i)
          colors[i] = cs.primitiveColor(i);
      }
      return colors;
    };

    if (const auto *mgeo = dynamic_cast<const MeshGeometry *>(geometry)) {
      BaseColor bc;
      auto emitter = newEmitter();
      auto mesh = buildMesh(*mgeo, cs, emitter.get(), bc);
      applyPositionSampler(bc);
      finishMesh(mesh.get(), sharedBSDF(cache, material, bc).get());
      addShape(mesh.get());
    } else if (const auto *sph = dynamic_cast<const SphereGeometry *>(geometry)) {
      const auto *centers = (const float3 *)sph->centers()->data();
      const float *radii = sph->vertexRadii()
          ? (const float *)sph->vertexRadii()->data()
          : nullptr;
      const uint32_t *index = sph->indices()
          ? (const uint32_t *)sph->indices()->data()
          : nullptr;
      const uint64_t n = sph->numPrimitives();
      auto vertexOf = [&](uint64_t i) -> uint64_t {
        return index ? index[i] : i;
      };
      auto colorOf = [&](uint64_t i) {
        return cs.vertexAttr ? cs.vertexColor(vertexOf(i))
                             : cs.primitiveColor(i);
      };
      // Large sphere sets and per-sphere colors use one 'ellipsoids' shape;
      // it culls back faces (no hits from inside), so transmissive spheres
      // and emitters (which need sampling routines) stay individual shapes.
      const bool bulk = !emissive && !transmissive
          && (cs.varying() || n > kBulkSphereThreshold);
      if (bulk) {
        std::vector<float> data;
        std::vector<float> rgb;
        data.reserve(n * 10);
        for (uint64_t i = 0; i < n; ++i) {
          const uint64_t v = vertexOf(i);
          const float r = radii ? radii[v] : sph->uniformRadius();
          if (!(r > 0.f))
            continue; // reported by SphereGeometry::finalize (§31)
          const float3 &c = centers[v];
          const float e[10] = {c.x, c.y, c.z, r, r, r, 0.f, 0.f, 0.f, 1.f};
          data.insert(data.end(), e, e + 10);
          if (cs.varying()) {
            const float3 col = colorOf(i);
            rgb.push_back(col.x);
            rgb.push_back(col.y);
            rgb.push_back(col.z);
          }
        }
        if (!data.empty()) {
          BaseColor bc;
          bc.constant = cs.constant;
          if (cs.varying()) {
            bc.texture = [rgb](float scale) {
              return mitsuba::ref<Texture>(new PrimitiveColorTexture(rgb, scale));
            };
          }
          auto bsdf = sharedBSDF(cache, material, bc);
          addShape(buildEllipsoids(data, bsdf.get()).get());
        }
      } else {
        // One Mitsuba 'sphere' shape per ANARI sphere primitive, in object
        // space; instance transforms apply through shapegroup/instance
        // (which handles non-uniform scaling correctly). Per-sphere colors
        // get one BSDF per distinct color.
        std::map<std::array<float, 3>, mitsuba::ref<BSDF>> bsdfs;
        mitsuba::ref<BSDF> uniform =
            cs.varying() ? mitsuba::ref<BSDF>() : constantBSDF(cs.constant);
        for (uint64_t i = 0; i < n; ++i) {
          const uint64_t v = vertexOf(i);
          const float r = radii ? radii[v] : sph->uniformRadius();
          if (!(r > 0.f))
            continue; // reported by SphereGeometry::finalize (§31)
          mitsuba::ref<BSDF> bsdf = uniform;
          if (!bsdf) {
            const float3 col = colorOf(i);
            auto &b = bsdfs[{col.x, col.y, col.z}];
            if (!b)
              b = constantBSDF(col);
            bsdf = b;
          }
          addShape(buildSphere(centers[v], r, bsdf.get(), newEmitter().get()).get());
        }
      }
    } else if (const auto *cc =
                   dynamic_cast<const ConeCylinderGeometry *>(geometry)) {
      const auto *pos = (const float3 *)cc->positions()->data();
      const uint64_t n = cc->numPrimitives();
      if (cc->isCone() || cs.varying()) {
        // Cones, and cylinders with varying colors, are tessellated.
        const auto vColors = vertexColorTable(cc->numVertices());
        const auto pColors = primitiveColorTable(n);
        TriMesh tm;
        for (uint64_t i = 0; i < n; ++i) {
          const uint2 s = cc->segment(i);
          PrimitiveColors colors;
          if (!vColors.empty()) {
            colors.vertex0 = &vColors[s.x];
            colors.vertex1 = &vColors[s.y];
          }
          if (!pColors.empty())
            colors.face = &pColors[i];
          appendFrustum(tm,
              pos[s.x],
              cc->radius(i, 0),
              pos[s.y],
              cc->radius(i, 1),
              cc->capped(i, 0),
              cc->capped(i, 1),
              kConeSegments,
              colors);
        }
        addTriMesh(tm);
      } else {
        // Cylinders: Mitsuba 'cylinder' shapes (open) with 'disk' caps.
        auto bsdf = constantBSDF(cs.constant);
        for (uint64_t i = 0; i < n; ++i) {
          const uint2 s = cc->segment(i);
          const float r = cc->radius(i, 0);
          const float3 a = pos[s.x], b = pos[s.y];
          if (!(r > 0.f) || !(linalg::length(b - a) > 0.f))
            continue;
          addShape(buildCylinder(a, b, r, bsdf.get(), newEmitter().get()).get());
          if (cc->capped(i, 0))
            addShape(buildDisk(a, a - b, r, bsdf.get(), newEmitter().get()).get());
          if (cc->capped(i, 1))
            addShape(buildDisk(b, b - a, r, bsdf.get(), newEmitter().get()).get());
        }
      }
    } else if (const auto *curve =
                   dynamic_cast<const CurveGeometry *>(geometry)) {
      // Round linear curve segments: cylinders (or, with varying colors,
      // tessellated cone frustums) with sphere joints. Joints at shared
      // positions are emitted once.
      const auto *pos = (const float3 *)curve->positions()->data();
      const float *radii = curve->vertexRadii()
          ? (const float *)curve->vertexRadii()->data()
          : nullptr;
      const uint32_t *idx = curve->indices()
          ? (const uint32_t *)curve->indices()->data()
          : nullptr;
      const uint64_t numSegs = curve->numPrimitives();

      auto radiusAt = [&](uint64_t v) {
        return radii ? radii[v] : curve->uniformRadius();
      };
      std::map<std::array<float, 4>, bool> joints;
      auto newJoint = [&](uint64_t v) {
        const float r = radiusAt(v);
        if (!(r > 0.f))
          return false;
        return joints.emplace(std::array<float, 4>{pos[v].x, pos[v].y, pos[v].z, r}, true)
            .second;
      };

      if (cs.varying()) {
        const auto vColors = vertexColorTable(curve->numVertices());
        const auto pColors = primitiveColorTable(numSegs);
        TriMesh tm;
        for (uint64_t s = 0; s < numSegs; ++s) {
          const uint64_t a = idx ? idx[s] : s;
          const uint64_t b = a + 1;
          PrimitiveColors colors;
          if (!vColors.empty()) {
            colors.vertex0 = &vColors[a];
            colors.vertex1 = &vColors[b];
          }
          if (!pColors.empty())
            colors.face = &pColors[s];
          appendFrustum(tm,
              pos[a],
              radiusAt(a),
              pos[b],
              radiusAt(b),
              false,
              false,
              kCurveSegments,
              colors);
          for (uint64_t v : {a, b}) {
            if (!newJoint(v))
              continue;
            PrimitiveColors jc;
            jc.vertex0 = colors.vertex0 ? &vColors[v] : nullptr;
            jc.face = colors.face;
            appendSphere(tm, pos[v], radiusAt(v), kJointRings, kCurveSegments, jc);
          }
        }
        addTriMesh(tm);
      } else {
        // The per-segment radius is the mean of the endpoint radii —
        // documented approximation.
        auto bsdf = constantBSDF(cs.constant);
        for (uint64_t s = 0; s < numSegs; ++s) {
          const uint64_t a = idx ? idx[s] : s;
          const uint64_t b = a + 1;
          const float r = 0.5f * (radiusAt(a) + radiusAt(b));
          const float len = linalg::length(pos[b] - pos[a]);
          if (r > 0.f && len > 0.f) {
            addShape(buildCylinder(
                pos[a], pos[b], r, bsdf.get(), newEmitter().get()).get());
          }
          for (uint64_t v : {a, b}) {
            if (newJoint(v)) {
              addShape(buildSphere(
                  pos[v], radiusAt(v), bsdf.get(), newEmitter().get()).get());
            }
          }
        }
      }
    } else if (const auto *iso =
                   dynamic_cast<const IsosurfaceGeometry *>(geometry)) {
      TriMesh tm;
      for (float v : iso->isovalues())
        appendIsosurface(tm, *iso->field(), v);
      addTriMesh(tm);
    } else {
      throw std::runtime_error("unsupported ANARI geometry subtype");
    }
    return entry.shapes;
  }

  static MiXform instanceTransform(const Instance &instance)
  {
    const mat4 &m = instance.xfm();
    return frameTransform(float3(m[0].x, m[0].y, m[0].z),
        float3(m[1].x, m[1].y, m[1].z),
        float3(m[2].x, m[2].y, m[2].z),
        float3(m[3].x, m[3].y, m[3].z));
  }

  static mitsuba::ref<mitsuba::Bitmap> run(const std::string &variant,
      MitsubaGlobalState *state,
      const World &world,
      const Camera &camera,
      const Renderer &renderer,
      uint2 frameSize,
      bool wantDepth,
      mitsuba::ref<mitsuba::Bitmap> &depthOut,
      RenderStats &stats,
      uint32_t seed)
  {
    const auto t0 = std::chrono::steady_clock::now();
    auto *pmgr = mitsuba::PluginManager::instance();
    auto *cache = getCache(state, variant);

    auto sensor =
        buildSensor(variant, camera, frameSize, renderer.pixelSamples());

    // An environment which isn't visible to the camera (ANARI 'visible' on
    // the hdri light) can only be hidden together with all emitters.
    bool hiddenEnvironment = false;
    bool hasHdri = false;
    for (const Instance *instance : world.instances()) {
      if (!instance || !instance->isValid() || !instance->group())
        continue;
      for (const Light *light : instance->group()->lights()) {
        const auto *hdri = dynamic_cast<const HDRI *>(light);
        if (!hdri || !hdri->isValid())
          continue;
        hasHdri = true;
        hiddenEnvironment = hiddenEnvironment || !hdri->visible();
      }
    }

    // Mitsuba scenes hold a single environment emitter. An hdri light
    // replaces the renderer's ambient light, as in barney ("the environment
    // is either an hdri map or the uniform ambient radiance") and Cycles.
    const bool ambient = renderer.ambientRadiance() > 0.f && !hasHdri;

    bool hasVolumes = false;
    for (const Instance *instance : world.instances()) {
      if (!instance || !instance->isValid() || !instance->group())
        continue;
      for (const Volume *volume : instance->group()->volumes())
        hasVolumes = hasVolumes || (volume && volume->isValid());
    }

    MiProps pi(hasVolumes ? "volpath" : "path");
    // Mitsuba's depth counts the camera segment: N bounces = depth N + 1.
    pi.set("max_depth", (int64_t)renderer.maxRayDepth() + 1);
    if (ambient || hiddenEnvironment) {
      // The ambient light is implemented as a constant environment emitter
      // that illuminates but stays invisible to primary rays: the visible
      // background is the app-controlled 'background' color, composited at
      // frame extraction (ANARI semantics).
      pi.set("hide_emitters", true);
    }
    auto integrator = pmgr->template create_object<Integrator>(pi);

    if (wantDepth) {
      // channel.depth rides on Mitsuba's 'aov' integrator wrapping the path
      // integrator (ADR 0005): 'depth' is the primary-hit ray distance.
      MiProps pa("aov");
      pa.set("aovs", "dd:depth");
      setObject(pa, "integrator", integrator.get());
      integrator = pmgr->template create_object<Integrator>(pa);
    }

    // ------------------------------------------------------------------
    // Assemble the scene content from cached translations. The composite
    // stamp decides whether the previous Mitsuba Scene can be reused
    // outright (the ambient emitter depends on the renderer, hence its
    // stamp participates).
    // ------------------------------------------------------------------
    std::vector<mitsuba::ref<mitsuba::Object>> sceneShapes;
    std::vector<mitsuba::ref<mitsuba::Object>> sceneEmitters;
    // Shapegroups referenced by instances must also be registered with the
    // Scene itself (its internal ray-tracing IR maps shapegroup -> BLAS).
    std::vector<mitsuba::ref<mitsuba::Object>> sceneGroups;
    uint64_t sceneStamp =
        std::max(stampOf(&world), stampOf(&renderer));

    for (const Instance *instance : world.instances()) {
      if (!instance || !instance->isValid())
        continue;
      const Group *group = instance->group();
      if (!group)
        continue;

      uint64_t groupContentStamp = stampOf(group);
      for (const Surface *surface : group->surfaces()) {
        if (!surface || !surface->isValid())
          continue;
        // (re)build into the cache; shapegroups can't hold emitters
        surfaceShapes(cache, surface, stats, instance->xfmIsIdentity());
        groupContentStamp = std::max({groupContentStamp,
            stampOf(surface),
            stampOf(surface->geometry()),
            stampOf(surface->material())});
      }

      const bool identity = instance->xfmIsIdentity();
      if (identity) {
        for (const Surface *surface : group->surfaces()) {
          if (!surface || !surface->isValid())
            continue;
          for (auto &s : cache->surfaces[surface].shapes)
            sceneShapes.push_back(s);
        }
      } else {
        // Mitsuba-native instancing: shapegroup per Group, instance per
        // ANARI Instance.
        auto &sgEntry = cache->groups[group];
        if (!sgEntry.obj || sgEntry.stamp != groupContentStamp) {
          MiProps psg("shapegroup");
          size_t i = 0;
          for (const Surface *surface : group->surfaces()) {
            if (!surface || !surface->isValid())
              continue;
            for (auto &s : cache->surfacesNoEmitter[surface].shapes)
              setObject(psg, "shape_" + std::to_string(i++), s.get());
          }
          sgEntry.obj = pmgr->template create_object<Shape>(psg).get();
          sgEntry.stamp = groupContentStamp;
        }

        const uint64_t instStamp =
            std::max(stampOf(instance), groupContentStamp);
        auto &instEntry = cache->instances[instance];
        if (!instEntry.obj || instEntry.stamp != instStamp) {
          MiProps pin("instance");
          setObject(pin, "shapegroup", sgEntry.obj.get());
          pin.set("to_world", instanceTransform(*instance));
          instEntry.obj = pmgr->template create_object<Shape>(pin).get();
          instEntry.stamp = instStamp;
        }
        sceneShapes.push_back(instEntry.obj);
        bool known = false;
        for (auto &g : sceneGroups)
          known = known || g.get() == sgEntry.obj.get();
        if (!known)
          sceneGroups.push_back(sgEntry.obj);
      }
      sceneStamp = std::max(
          {sceneStamp, stampOf(instance), groupContentStamp});

      for (const Volume *volume : group->volumes()) {
        const auto *tf = dynamic_cast<const TransferFunction1D *>(volume);
        if (!tf || !tf->isValid())
          continue;
        const uint64_t vStamp = std::max({stampOf(volume),
            stampOf(tf->spatialField()),
            stampOf(instance)});
        auto &vEntry = cache->volumes[{volume, instance}];
        if (vEntry.stamp != vStamp) {
          ++stats.cacheMisses;
          vEntry.obj = buildVolumeShape(*tf, instance->xfm()).get();
          vEntry.stamp = vStamp;
        } else {
          ++stats.cacheHits;
        }
        if (vEntry.obj)
          sceneShapes.push_back(vEntry.obj);
        sceneStamp = std::max(sceneStamp, vStamp);
      }

      for (const Light *light : group->lights()) {
        if (!light || !light->isValid())
          continue;
        const uint64_t lStamp = stampOf(light);
        auto &lEntry = cache->emitters[light];
        if (!lEntry.obj || lEntry.stamp != lStamp) {
          ++stats.cacheMisses;
          if (const auto *quad = dynamic_cast<const QuadLight *>(light))
            lEntry.obj = buildQuadLightShape(*quad).get();
          else
            lEntry.obj = buildEmitter(*light).get();
          lEntry.stamp = lStamp;
        } else {
          ++stats.cacheHits;
        }
        // Quad lights are shapes; the rest are emitters. Lights are not
        // transformed by instances (documented limitation).
        if (dynamic_cast<const QuadLight *>(light))
          sceneShapes.push_back(lEntry.obj);
        else
          sceneEmitters.push_back(lEntry.obj);
        sceneStamp = std::max(sceneStamp, lStamp);
      }
    }

    stats.shapeCount = sceneShapes.size();
    stats.emitterCount = sceneEmitters.size() + (ambient ? 1 : 0);

    mitsuba::ref<Scene> scene;
    if (cache->scene && cache->sceneStamp == sceneStamp) {
      scene = mitsuba::ref<Scene>((Scene *)cache->scene.get());
      stats.sceneReused = true;
    } else {
      MiProps pscene("scene");
      setObject(pscene, "integrator", integrator.get());
      setObject(pscene, "sensor", sensor.get());
      size_t idx = 0;
      for (auto &s : sceneShapes)
        setObject(pscene, "shape_" + std::to_string(idx++), s.get());
      idx = 0;
      for (auto &g : sceneGroups) {
        // On OptiX a Scene keeps the acceleration structure of each of its
        // shapegroups in its own state, but only builds it for groups flagged
        // dirty — and the previous Scene cleared that flag. A cached
        // shapegroup entering a new Scene must be re-flagged, otherwise
        // Mitsuba indexes an empty array there (access violation on cuda
        // variants). Harmless elsewhere: the constructor clears the flag.
        ((Shape *)g.get())->mark_dirty();
        setObject(pscene, "shapegroup_" + std::to_string(idx++), g.get());
      }
      idx = 0;
      for (auto &e : sceneEmitters)
        setObject(pscene, "emitter_" + std::to_string(idx++), e.get());
      if (ambient) {
        MiProps pa("constant");
        pa.set("radiance",
            toColor(renderer.ambientColor() * renderer.ambientRadiance()));
        setObject(pscene,
            "emitter_ambient",
            pmgr->template create_object<Emitter>(pa).get());
      }
      auto sceneObj =
          pmgr->create_object(pscene, variant, mitsuba::ObjectType::Scene);
      scene = mitsuba::ref<Scene>((Scene *)sceneObj.get());
      cache->scene = sceneObj;
      cache->sceneStamp = sceneStamp;
    }

    const auto t1 = std::chrono::steady_clock::now();

    integrator->render(scene.get(),
        sensor.get(),
        seed /*per accumulated frame, 0 for the first one*/,
        0 /*spp: use sampler count*/,
        false /*develop*/,
        true /*evaluate*/);

    mitsuba::ref<mitsuba::Bitmap> raw = sensor->film()->bitmap(false);

    mitsuba::ref<mitsuba::Bitmap> bitmap;
    if (raw->pixel_format() == mitsuba::Bitmap::PixelFormat::MultiChannel) {
      // With AOVs the film develops to a multi-channel image; split it into
      // the named layers (the base image is the unnamed "<root>" layer).
      for (auto &[name, layer] : raw->split()) {
        if (name == "dd")
          depthOut = layer;
        else if (name.empty() || name == "<root>")
          bitmap = layer;
      }
      if (!bitmap)
        throw std::runtime_error("film develop returned no base image layer");
    } else {
      bitmap = raw;
    }
    bitmap = bitmap->convert(
        mitsuba::Bitmap::PixelFormat::RGBA, mitsuba::sj::Type::Float32, false);
    if (depthOut) {
      // The split AOV layer is a single float32 channel already; generic
      // pixel-format conversion does not apply to MultiChannel layers.
      if (depthOut->channel_count() != 1
          || depthOut->component_format() != mitsuba::sj::Type::Float32) {
        throw std::runtime_error("unexpected depth AOV layer format");
      }
    }
    (void)wantDepth;

    const auto t2 = std::chrono::steady_clock::now();
    stats.translateMs =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.renderMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    return bitmap;
  }
};

} // namespace

mitsuba::ref<mitsuba::Bitmap> renderMitsubaScene(const std::string &variant,
    MitsubaGlobalState *state,
    const World &world,
    const Camera &camera,
    const Renderer &renderer,
    uint2 frameSize,
    bool wantDepth,
    mitsuba::ref<mitsuba::Bitmap> &depthOut,
    RenderStats &stats,
    uint32_t seed)
{
  if (variant == mi::scalarVariantName) {
    return VariantRender<mi::ScalarFloat, mi::ScalarSpectrum>::run(variant,
        state, world, camera, renderer, frameSize, wantDepth, depthOut, stats, seed);
  }
#if defined(MI_ENABLE_LLVM)
  if (variant == mi::llvmVariantName) {
    using Float = dr::DiffArray<JitBackend::LLVM, float>;
    return VariantRender<Float, mitsuba::Color<Float, 3>>::run(variant,
        state, world, camera, renderer, frameSize, wantDepth, depthOut, stats, seed);
  }
#endif
#if defined(MI_ENABLE_CUDA)
  if (variant == mi::cudaVariantName) {
    using Float = dr::DiffArray<JitBackend::CUDA, float>;
    return VariantRender<Float, mitsuba::Color<Float, 3>>::run(variant,
        state, world, camera, renderer, frameSize, wantDepth, depthOut, stats, seed);
  }
#endif
#if defined(MI_ENABLE_METAL)
  if (variant == mi::metalVariantName) {
    using Float = dr::DiffArray<JitBackend::Metal, float>;
    return VariantRender<Float, mitsuba::Color<Float, 3>>::run(variant,
        state, world, camera, renderer, frameSize, wantDepth, depthOut, stats, seed);
  }
#endif
  throw std::runtime_error(
      "Mitsuba variant '" + variant + "' is not compiled into this device");
}

} // namespace mitsuba_anari
