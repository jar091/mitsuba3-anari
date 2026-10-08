cmake_minimum_required(VERSION 3.24)

project(mitsuba-anari
  VERSION 0.1.0
  DESCRIPTION "Khronos ANARI device backed by the Mitsuba 3 renderer"
  LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported; use a build directory (see CMakePresets.json).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(MITSUBA_ANARI_BUILD_TESTS "Build tests and register them with CTest" ON)
option(MITSUBA_ANARI_BUILD_EXAMPLES "Build C++ examples" ON)
option(MITSUBA_ANARI_BUILD_PYNARI_TESTS "Enable PyNARI-based tests (requires Python + PyNARI)" OFF)
option(MITSUBA_ANARI_BUILD_CTS "Enable ANARI CTS integration" OFF)
option(MITSUBA_ANARI_FETCH_DEPENDENCIES "Provision pinned dependencies automatically" ON)
option(MITSUBA_ANARI_ENABLE_ASAN "Enable AddressSanitizer (GCC/Clang)" OFF)
option(MITSUBA_ANARI_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
option(MITSUBA_ANARI_ENABLE_COVERAGE "Enable coverage instrumentation (GCC/Clang)" OFF)
option(MITSUBA_ANARI_WARNINGS_AS_ERRORS "Treat project warnings as errors (never third-party)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(Sanitizers)
include(CompilerWarnings)
include(Dependencies)

# ---------------------------------------------------------------------------
# Project targets
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(MITSUBA_ANARI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MITSUBA_ANARI_BUILD_EXAMPLES)
  add_subdirectory(examples/cpp)
endif()

# ---------------------------------------------------------------------------
# Install: docs/licensing + co-installed runtimes (Milestone 12)
#
# The install tree is self-contained: the Mitsuba runtime (mitsuba + drjit
# DLLs, plugins/, data/) and the ANARI frontend runtime are installed next to
# the device library, so applications only need this prefix on their loader
# path.
# ---------------------------------------------------------------------------
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
  "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
  "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/mitsuba-anari")

if(MITSUBA_ANARI_MITSUBA_RUNTIME_DIR)
  if(WIN32)
    set(_mi_runtime_dest "${CMAKE_INSTALL_BINDIR}")
  else()
    set(_mi_runtime_dest "${CMAKE_INSTALL_LIBDIR}")
  endif()
  install(DIRECTORY "${MITSUBA_ANARI_MITSUBA_RUNTIME_DIR}/"
    DESTINATION "${_mi_runtime_dest}"
    PATTERN "*.exe" EXCLUDE
    PATTERN "setpath.*" EXCLUDE)
  if(WIN32)
    install(FILES "$<TARGET_FILE:anari::anari>"
      DESTINATION "${_mi_runtime_dest}")
  else()
    # The device links against the frontend's soname (@rpath/libanari.N on
    # macOS, libanari.so.N on Linux); install both the real file and the
    # soname-named file so the loader resolves it inside the install tree.
    install(FILES "$<TARGET_FILE:anari::anari>" "$<TARGET_SONAME_FILE:anari::anari>"
      DESTINATION "${_mi_runtime_dest}")
  endif()
endif()

# The llvm_ad_rgb variant needs the LLVM C API library at runtime (Dr.Jit
# loads LLVM-C.dll / libLLVM via the loader path or DRJIT_LIBLLVM_PATH).
# Optionally stage one next to the device (e.g. bin/LLVM-C.dll of an
# official LLVM release).
set(MITSUBA_ANARI_LLVM_RUNTIME "" CACHE FILEPATH
  "LLVM C API shared library to install next to the device (optional)")
if(MITSUBA_ANARI_LLVM_RUNTIME)
  if(WIN32)
    install(FILES "${MITSUBA_ANARI_LLVM_RUNTIME}"
      DESTINATION "${CMAKE_INSTALL_BINDIR}")
  else()
    install(FILES "${MITSUBA_ANARI_LLVM_RUNTIME}"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  endif()
endif()
