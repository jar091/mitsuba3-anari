// Developer diagnostic (not a registered test): reproduces the Milestone 3
// mesh-construction sequence by linking Mitsuba directly, isolating it from
// the ANARI layers. Prints one marker per step.
// SPDX-License-Identifier: Apache-2.0

#include "mitsuba_backend/MitsubaTypes.h"

#include <mitsuba/core/fresolver.h>
#include <mitsuba/core/profiler.h>

#include <cstdio>
#include <numeric>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace mitsuba_anari;

// Local scalar-variant aliases (MitsubaTypes.h only exports the base types
// since the variant-dispatch refactor).
namespace miv {
using Float = mi::ScalarFloat;
using Spectrum = mi::ScalarSpectrum;
using Scene = mitsuba::Scene<Float, Spectrum>;
using Mesh = mitsuba::Mesh<Float, Spectrum>;
using BSDF = mitsuba::BSDF<Float, Spectrum>;
using Emitter = mitsuba::Emitter<Float, Spectrum>;
using Sensor = mitsuba::Sensor<Float, Spectrum>;
using Film = mitsuba::Film<Float, Spectrum>;
using Sampler = mitsuba::Sampler<Float, Spectrum>;
using Integrator = mitsuba::Integrator<Float, Spectrum>;
} // namespace miv

static void checkHeap(const char *where)
{
  const BOOL ok = HeapValidate(GetProcessHeap(), 0, nullptr);
  std::printf("HEAP %s: %s\n", ok ? "ok" : "CORRUPT", where);
  std::fflush(stdout);
}

#define STEP(msg)                                                              \
  do {                                                                         \
    std::printf("STEP: %s\n", msg);                                            \
    std::fflush(stdout);                                                       \
    checkHeap(msg);                                                            \
  } while (0)

int main()
{
  STEP("static init");
  mitsuba::Thread::static_initialization();
  mitsuba::Logger::static_initialization();
  mitsuba::Bitmap::static_initialization();
  mitsuba::logger()->set_log_level(mitsuba::LogLevel::Info);
  mitsuba::file_resolver()->append(
      mitsuba::util::library_path().parent_path());
  mitsuba::Profiler::static_initialization();
  mitsuba::color_management_static_initialization(false, false, false);
  miv::Scene::static_accel_initialization();

  STEP("bsdf");
  auto *pmgr = mitsuba::PluginManager::instance();
  mitsuba::Properties pb("diffuse");
  pb.set("reflectance", mitsuba::Color<float, 3>(0.8f, 0.1f, 0.1f));
  auto bsdf = pmgr->create_object<miv::BSDF>(pb);
  std::printf("bsdf ptr: %p\n", (void *)bsdf.get());

  STEP("mesh ctor");
  mitsuba::Properties props;
  props.set("face_normals", true);
  mitsuba::ref<miv::Mesh> mesh =
      new miv::Mesh("repro_triangle", 3, 1, props, false, false);

  STEP("fill positions");
  std::vector<float> P = {-1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f};
  using PosBuffer = std::decay_t<decltype(mesh->vertex_positions_buffer())>;
  auto loaded = dr::load<PosBuffer>(P.data(), P.size());
  STEP("assign positions");
  mesh->vertex_positions_buffer() = std::move(loaded);

  STEP("fill faces");
  std::vector<uint32_t> I(3);
  std::iota(I.begin(), I.end(), 0u);
  using FaceBuffer = std::decay_t<decltype(mesh->faces_buffer())>;
  mesh->faces_buffer() = dr::load<FaceBuffer>(I.data(), I.size());

  STEP("set_bsdf");
  mesh->set_bsdf(bsdf.get());
  STEP("initialize");
  mesh->initialize();

  STEP("envmap scene");
  {
    using MiProps = mitsuba::Properties;
    auto setObj = [](MiProps &p, std::string_view name, mitsuba::Object *o) {
      p.set(name, mitsuba::ref<mitsuba::Object>(o));
    };

    mitsuba::ref<mitsuba::Bitmap> env =
        new mitsuba::Bitmap(mitsuba::Bitmap::PixelFormat::RGB,
            mitsuba::sj::Type::Float32,
            mitsuba::Vector<uint32_t, 2>(4, 2));
    float *px = (float *)env->data();
    for (size_t i = 0; i < 4 * 2 * 3; ++i)
      px[i] = 2.f;

    MiProps pe("envmap");
    setObj(pe, "bitmap", env.get());
    {
      // Reproduce the builder's frameTransform for up=(0,0,1), dir=(1,0,0):
      // columns X=(0,1,0), Y=(0,0,1), Z=(1,0,0).
      using MiXform = mitsuba::AffineTransform<mitsuba::Point<float, 4>>;
      using V1 = dr::Array<float, 1>;
      using M = typename MiXform::Matrix;
      M m = dr::transpose(M(
          dr::concat(mitsuba::Vector<float, 3>(0, 1, 0), V1(0)),
          dr::concat(mitsuba::Vector<float, 3>(0, 0, 1), V1(0)),
          dr::concat(mitsuba::Vector<float, 3>(1, 0, 0), V1(0)),
          dr::concat(mitsuba::Vector<float, 3>(0, 0, 0), V1(1))));
      pe.set("to_world", MiXform(m));
    }
    auto emitter = pmgr->create_object<miv::Emitter>(pe);
    std::printf("emitter class: %s\n",
        emitter->class_name());
    std::fflush(stdout);

    MiProps pf("hdrfilm");
    pf.set("width", (int64_t)8);
    pf.set("height", (int64_t)8);
    pf.set("pixel_format", "rgba");
    pf.set("component_format", "float32");
    MiProps psam("independent");
    psam.set("sample_count", (int64_t)8);
    MiProps psen("perspective");
    psen.set("fov", 45.0);
    setObj(psen, "film", pmgr->create_object<miv::Film>(pf).get());
    setObj(psen, "sampler", pmgr->create_object<miv::Sampler>(psam).get());
    auto sensor = pmgr->create_object<miv::Sensor>(psen);

    MiProps pi("path");
    auto integrator = pmgr->create_object<miv::Integrator>(pi);

    MiProps psc("scene");
    setObj(psc, "integrator", integrator.get());
    setObj(psc, "sensor", sensor.get());
    setObj(psc, "emitter_0", emitter.get());
    auto sceneObj =
        pmgr->create_object(psc, "scalar_rgb", mitsuba::ObjectType::Scene);
    auto *scene = (miv::Scene *)sceneObj.get();
    std::printf("scene environment: %p\n", (void *)scene->environment());
    std::fflush(stdout);

    integrator->render(scene, sensor.get(), (uint32_t)0, 0, false, true);
    auto bmp = sensor->film()->bitmap(false)->convert(
        mitsuba::Bitmap::PixelFormat::RGBA, mitsuba::sj::Type::Float32, false);
    const float *d = (const float *)bmp->data();
    std::printf("envmap render center: %f %f %f %f\n", d[0], d[1], d[2], d[3]);
    std::fflush(stdout);
  }

  STEP("shutdown");
  mesh = nullptr;
  bsdf = nullptr;
  miv::Scene::static_accel_shutdown();
  mitsuba::color_management_static_shutdown();
  mitsuba::Profiler::static_shutdown();
  mitsuba::Bitmap::static_shutdown();
  mitsuba::Logger::static_shutdown();
  mitsuba::Thread::static_shutdown();

  STEP("done");
  return 0;
}
