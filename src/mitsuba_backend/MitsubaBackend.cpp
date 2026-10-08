// mitsuba-anari: process-global Mitsuba runtime bring-up/teardown.
// The initialization order mirrors Mitsuba's own executable entry point
// (src/mitsuba/mitsuba.cpp).
// SPDX-License-Identifier: Apache-2.0

#include "mitsuba_backend/MitsubaBackend.h"
#include "mitsuba_backend/MitsubaTypes.h"

#include <mitsuba/core/appender.h>
#include <mitsuba/core/fresolver.h>
#include <mitsuba/core/profiler.h>

#include <anari/anari.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace mitsuba_anari {

namespace {

std::mutex g_mutex;
int g_refCount = 0;
MitsubaBackend::LogSink g_sink;
std::string g_activeVariant;
bool g_jitCuda = false;
bool g_jitLlvm = false;
bool g_jitMetal = false;

void emit(int severity, const std::string &msg)
{
  if (g_sink)
    g_sink(severity, msg.c_str());
}

// Routes Mitsuba's logger into ANARI status callbacks.
class StatusCallbackAppender final : public mitsuba::Appender {
 public:
  void append(mitsuba::LogLevel level, std::string_view text) override
  {
    MitsubaBackend::LogSink sink;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      sink = g_sink;
    }
    if (!sink)
      return;
    int severity = ANARI_SEVERITY_INFO;
    switch (level) {
    case mitsuba::LogLevel::Error:
      severity = ANARI_SEVERITY_ERROR;
      break;
    case mitsuba::LogLevel::Warn:
      severity = ANARI_SEVERITY_WARNING;
      break;
    case mitsuba::LogLevel::Info:
      severity = ANARI_SEVERITY_INFO;
      break;
    default:
      severity = ANARI_SEVERITY_DEBUG;
      break;
    }
    sink(severity, std::string(text).c_str());
  }

  void log_progress(float /*progress*/,
      std::string_view /*name*/,
      std::string_view /*formatted*/,
      std::string_view /*eta*/,
      const void * /*ptr*/) override
  {
    // Render progress is intentionally not forwarded as a status message.
  }
};

mitsuba::LogLevel logLevelFromEnv()
{
  // ANARI_MITSUBA_LOG_LEVEL: off|error|warning|info|debug|trace
  // (master prompt §12); default keeps output quiet.
  const char *env = std::getenv("ANARI_MITSUBA_LOG_LEVEL");
  std::string v = env ? env : "";
  if (v == "trace")
    return mitsuba::LogLevel::Trace;
  if (v == "debug")
    return mitsuba::LogLevel::Debug;
  if (v == "info")
    return mitsuba::LogLevel::Info;
  if (v == "error" || v == "off")
    return mitsuba::LogLevel::Error;
  return mitsuba::LogLevel::Warn;
}

bool startsWith(const std::string &s, const char *prefix)
{
  return s.rfind(prefix, 0) == 0;
}

// Probes and initializes the JIT backend needed by `variant`.
// Returns true when the variant's backend is usable in this process.
bool initVariantBackend(const std::string &variant)
{
  if (startsWith(variant, "scalar_"))
    return true;
#if defined(MI_ENABLE_CUDA)
  if (startsWith(variant, "cuda_")) {
    jit_init(1u << (uint32_t)JitBackend::CUDA);
    g_jitCuda = jit_has_backend(JitBackend::CUDA);
    return g_jitCuda;
  }
#endif
#if defined(MI_ENABLE_LLVM)
  if (startsWith(variant, "llvm_")) {
    jit_init(1u << (uint32_t)JitBackend::LLVM);
    g_jitLlvm = jit_has_backend(JitBackend::LLVM);
#if defined(_WIN32)
    // Dr.Jit unloads LLVM-C.dll in jit_shutdown(), but LLVM leaves
    // thread/fiber-local destructors behind that run at thread or process
    // exit and crash in the unloaded image. Pin the module for the lifetime
    // of the process.
    if (g_jitLlvm) {
      HMODULE module = nullptr;
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"LLVM-C.dll", &module);
    }
#endif
    return g_jitLlvm;
  }
#endif
#if defined(MI_ENABLE_METAL)
  if (startsWith(variant, "metal_")) {
    jit_init(1u << (uint32_t)JitBackend::Metal);
    g_jitMetal = jit_has_backend(JitBackend::Metal);
    return g_jitMetal;
  }
#endif
  return false;
}

void variantAccelInitialization(const std::string &variant)
{
  if (startsWith(variant, "scalar_")) {
    mitsuba::Scene<mi::ScalarFloat, mi::ScalarSpectrum>::
        static_accel_initialization();
    return;
  }
#if defined(MI_ENABLE_CUDA)
  if (startsWith(variant, "cuda_")) {
    using Float = dr::DiffArray<JitBackend::CUDA, float>;
    mitsuba::Scene<Float, mitsuba::Color<Float, 3>>::
        static_accel_initialization();
    return;
  }
#endif
#if defined(MI_ENABLE_LLVM)
  if (startsWith(variant, "llvm_")) {
    using Float = dr::DiffArray<JitBackend::LLVM, float>;
    mitsuba::Scene<Float, mitsuba::Color<Float, 3>>::
        static_accel_initialization();
    return;
  }
#endif
#if defined(MI_ENABLE_METAL)
  if (startsWith(variant, "metal_")) {
    using Float = dr::DiffArray<JitBackend::Metal, float>;
    mitsuba::Scene<Float, mitsuba::Color<Float, 3>>::
        static_accel_initialization();
    return;
  }
#endif
}

void variantAccelShutdown(const std::string &variant)
{
  if (startsWith(variant, "scalar_")) {
    mitsuba::Scene<mi::ScalarFloat, mi::ScalarSpectrum>::
        static_accel_shutdown();
    return;
  }
#if defined(MI_ENABLE_CUDA)
  if (startsWith(variant, "cuda_")) {
    using Float = dr::DiffArray<JitBackend::CUDA, float>;
    mitsuba::Scene<Float, mitsuba::Color<Float, 3>>::static_accel_shutdown();
    return;
  }
#endif
#if defined(MI_ENABLE_LLVM)
  if (startsWith(variant, "llvm_")) {
    using Float = dr::DiffArray<JitBackend::LLVM, float>;
    mitsuba::Scene<Float, mitsuba::Color<Float, 3>>::static_accel_shutdown();
    return;
  }
#endif
#if defined(MI_ENABLE_METAL)
  if (startsWith(variant, "metal_")) {
    using Float = dr::DiffArray<JitBackend::Metal, float>;
    mitsuba::Scene<Float, mitsuba::Color<Float, 3>>::static_accel_shutdown();
    return;
  }
#endif
}

} // namespace

std::string MitsubaBackend::acquire(
    const LogSink &sink, const std::string &requestedVariant)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  g_sink = sink;

  if (g_refCount > 0) {
    if (!requestedVariant.empty() && requestedVariant != g_activeVariant) {
      emit(ANARI_SEVERITY_WARNING,
          "Mitsuba variant '" + requestedVariant
              + "' requested, but this process already initialized variant '"
              + g_activeVariant
              + "' (one variant per process; see ADR 0004). Keeping '"
              + g_activeVariant + "'.");
    }
    ++g_refCount;
    return g_activeVariant;
  }

  std::string variant =
      requestedVariant.empty() ? std::string(mi::scalarVariantName)
                               : requestedVariant;

  try {
    mitsuba::Thread::static_initialization();
    mitsuba::Logger::static_initialization();
    mitsuba::Bitmap::static_initialization();

    // Route all Mitsuba log output into ANARI status callbacks; never print
    // to stdout/stderr from library internals (master prompt §12).
    auto *logger = mitsuba::logger();
    logger->clear_appenders();
    logger->add_appender(new StatusCallbackAppender());
    logger->set_log_level(logLevelFromEnv());

    // The default FileResolver only searches the CWD; register the directory
    // of the Mitsuba runtime library so plugins/ and data/ resolve, exactly
    // as Mitsuba's executable does.
    mitsuba::file_resolver()->append(
        mitsuba::util::library_path().parent_path());

    if (!initVariantBackend(variant)) {
      emit(ANARI_SEVERITY_WARNING,
          "Mitsuba variant '" + variant
              + "' is unavailable in this process (backend not compiled in or "
                "runtime not present); falling back to scalar_rgb");
      variant = mi::scalarVariantName;
    }

    mitsuba::Profiler::static_initialization();
    mitsuba::color_management_static_initialization(
        g_jitCuda, g_jitLlvm, g_jitMetal);
    // The scalar accel state is always initialized (baseline + fallbacks);
    // the active JIT variant's accel state on top when applicable.
    variantAccelInitialization(mi::scalarVariantName);
    if (!startsWith(variant, "scalar_"))
      variantAccelInitialization(variant);
  } catch (const std::exception &e) {
    emit(ANARI_SEVERITY_FATAL_ERROR,
        std::string("Mitsuba runtime initialization failed: ") + e.what());
    return {};
  }

  g_activeVariant = variant;
  ++g_refCount;
  return g_activeVariant;
}

void MitsubaBackend::release()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_refCount <= 0)
    return;
  if (--g_refCount > 0)
    return;

  try {
    if (!startsWith(g_activeVariant, "scalar_"))
      variantAccelShutdown(g_activeVariant);
    variantAccelShutdown(mi::scalarVariantName);
    mitsuba::color_management_static_shutdown();
    mitsuba::Profiler::static_shutdown();
    mitsuba::Bitmap::static_shutdown();
    mitsuba::Logger::static_shutdown();
    mitsuba::Thread::static_shutdown();
#if defined(MI_ENABLE_JIT)
    if (g_jitCuda || g_jitLlvm || g_jitMetal)
      jit_shutdown();
#endif
  } catch (const std::exception &e) {
    emit(ANARI_SEVERITY_ERROR,
        std::string("Mitsuba runtime shutdown failed: ") + e.what());
  }
  g_activeVariant.clear();
  g_jitCuda = g_jitLlvm = g_jitMetal = false;
  g_sink = nullptr;
}

void MitsubaBackend::setLogSink(const LogSink &sink)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  g_sink = sink;
}

std::vector<std::string> MitsubaBackend::compiledVariants()
{
  std::vector<std::string> v;
  v.push_back(mi::scalarVariantName);
#if defined(MI_ENABLE_LLVM)
  v.push_back(mi::llvmVariantName);
#endif
#if defined(MI_ENABLE_CUDA)
  v.push_back(mi::cudaVariantName);
#endif
#if defined(MI_ENABLE_METAL)
  v.push_back(mi::metalVariantName);
#endif
  return v;
}

} // namespace mitsuba_anari
