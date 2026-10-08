# Pinned dependency resolution for mitsuba-anari.
#
# All versions are pinned — the ANARI-SDK and PyNARI here, Mitsuba by the
# ext/mitsuba3 submodule commit — and mirrored in DEPENDENCIES.md. Never build
# against floating branches.
#
# ANARI-SDK cannot be consumed via FetchContent/add_subdirectory: its package
# config hard-errors on build-tree consumption ("Consuming ANARI-SDK from a
# build directory is not supported"). We therefore build + install the pinned
# tag into the build directory at configure time (reproducible developer mode),
# or use a preinstalled SDK (system mode, MITSUBA_ANARI_FETCH_DEPENDENCIES=OFF).

# ---------------------------------------------------------------------------
# Pins (update DEPENDENCIES.md when changing these)
# ---------------------------------------------------------------------------
set(MITSUBA_ANARI_ANARI_SDK_VERSION "0.16.0")
set(MITSUBA_ANARI_ANARI_SDK_TAG "v0.16.0")
set(MITSUBA_ANARI_ANARI_SDK_COMMIT "7534bd263d6ff97764eda93d0e1bd6bd2f108c32")
set(MITSUBA_ANARI_ANARI_SDK_URL "https://github.com/KhronosGroup/ANARI-SDK")

# Mitsuba is a git submodule (ext/mitsuba3: the jar091/mitsuba3 fork, branch
# `master`); the commit recorded in this repository is the pin. Point this at
# another checkout to build the backend from a different Mitsuba tree.
set(MITSUBA_ANARI_MITSUBA_SOURCE_DIR "${PROJECT_SOURCE_DIR}/ext/mitsuba3"
    CACHE PATH "Mitsuba source tree the rendering backend is built from")

# PyNARI is a test/example client, never a build dependency of the device.
set(MITSUBA_ANARI_PYNARI_COMMIT "a860d0b68d08096f552d158265a90d040bd4498d")
set(MITSUBA_ANARI_PYNARI_URL "https://github.com/jar091/pynari")

# Which configurations of dependencies to build for multi-config generators.
# The presets pin this to the single config they build (Debug preset -> Debug
# deps, etc.) so device and dependencies always share one CRT/runtime config.
set(MITSUBA_ANARI_DEP_CONFIGS "Release;Debug"
    CACHE STRING "Configs to build for locally-provisioned dependencies (multi-config generators)")

# ---------------------------------------------------------------------------
# Install-layout helper
#
# Resolves the library directory of an install prefix produced by
# GNUInstallDirs. Debian/macOS/Windows use "lib"; RHEL-family 64-bit
# distributions (Karolina's compute nodes among them) use "lib64", so the
# directory must never be hardcoded — each dependency is installed by its own
# CMake run, whose CMAKE_INSTALL_LIBDIR we do not control.
# ---------------------------------------------------------------------------
function(_mitsuba_anari_libdir prefix out_var)
  foreach(candidate lib64 lib)
    if(IS_DIRECTORY "${prefix}/${candidate}")
      set(${out_var} "${prefix}/${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "${prefix}/lib" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# ANARI-SDK provisioning
# ---------------------------------------------------------------------------
function(_mitsuba_anari_provision_anari_sdk)
  set(sdk_src "${CMAKE_BINARY_DIR}/_deps/anari-sdk-src")
  set(sdk_build "${CMAKE_BINARY_DIR}/_deps/anari-sdk-build")
  set(sdk_prefix "${CMAKE_BINARY_DIR}/_deps/anari-sdk-install")

  get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(is_multi_config)
    set(dep_configs "${MITSUBA_ANARI_DEP_CONFIGS}")
  else()
    if(CMAKE_BUILD_TYPE)
      set(dep_configs "${CMAKE_BUILD_TYPE}")
    else()
      set(dep_configs "Release")
    endif()
  endif()

  # SDK plus the tooling our test layers use: anariInfo (examples), the debug
  # device (validation layer), sink, anariRenderTests (BUILD_TESTING), and
  # the CTS (Milestone 8). helide stays OFF — it builds Embree from source;
  # enable it if A/B device comparisons become necessary.
  set(sdk_feature_args
    -DBUILD_SHARED_LIBS=ON
    -DBUILD_TESTING=ON
    -DBUILD_EXAMPLES=ON
    -DBUILD_VIEWER=OFF
    -DBUILD_CTS=ON
    -DINSTALL_CTS=ON
    -DBUILD_HELIDE_DEVICE=OFF
    -DBUILD_DEBUG_DEVICE=ON
    -DBUILD_SINK_DEVICE=ON
    -DBUILD_REMOTE_DEVICE=OFF
    -DINSTALL_CODE_GEN_SCRIPTS=ON
    -DINSTALL_VIEWER_LIBRARY=OFF)

  set(stamp_content "${MITSUBA_ANARI_ANARI_SDK_COMMIT}|${dep_configs}|${sdk_feature_args}")
  set(stamp_file "${sdk_prefix}/.mitsuba-anari-provisioned")
  if(EXISTS "${stamp_file}")
    file(READ "${stamp_file}" existing_stamp)
    if(existing_stamp STREQUAL stamp_content)
      message(STATUS "mitsuba-anari: reusing provisioned ANARI-SDK at ${sdk_prefix}")
      list(PREPEND CMAKE_PREFIX_PATH "${sdk_prefix}")
      set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
      return()
    endif()
    message(STATUS "mitsuba-anari: ANARI-SDK pin/config changed; re-provisioning")
  endif()

  find_package(Git REQUIRED)

  if(NOT EXISTS "${sdk_src}/CMakeLists.txt")
    message(STATUS "mitsuba-anari: cloning ANARI-SDK ${MITSUBA_ANARI_ANARI_SDK_TAG}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" clone --depth 1
              --branch "${MITSUBA_ANARI_ANARI_SDK_TAG}"
              "${MITSUBA_ANARI_ANARI_SDK_URL}" "${sdk_src}"
      RESULT_VARIABLE git_result)
    if(NOT git_result EQUAL 0)
      message(FATAL_ERROR "mitsuba-anari: failed to clone ANARI-SDK")
    endif()
  endif()

  # Verify the tag still resolves to the pinned commit (tags can be moved).
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${sdk_src}" rev-parse HEAD
    OUTPUT_VARIABLE sdk_head OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE git_result)
  if(NOT git_result EQUAL 0 OR NOT sdk_head STREQUAL MITSUBA_ANARI_ANARI_SDK_COMMIT)
    message(FATAL_ERROR
      "mitsuba-anari: ANARI-SDK checkout is at '${sdk_head}' but the pin is "
      "'${MITSUBA_ANARI_ANARI_SDK_COMMIT}'. Refusing to build an unverified "
      "dependency. Delete ${sdk_src} and reconfigure, or update the pin in "
      "cmake/Dependencies.cmake + DEPENDENCIES.md deliberately.")
  endif()

  set(sdk_cmake_args
    -G "${CMAKE_GENERATOR}"
    -DCMAKE_INSTALL_PREFIX=${sdk_prefix}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    ${sdk_feature_args})
  if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND sdk_cmake_args -A "${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND sdk_cmake_args -T "${CMAKE_GENERATOR_TOOLSET}")
  endif()
  if(NOT is_multi_config)
    list(APPEND sdk_cmake_args -DCMAKE_BUILD_TYPE=${dep_configs})
    if(CMAKE_CXX_COMPILER)
      list(APPEND sdk_cmake_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
    endif()
    if(CMAKE_C_COMPILER)
      list(APPEND sdk_cmake_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
    endif()
  endif()
  if(Python3_EXECUTABLE)
    list(APPEND sdk_cmake_args -DPython3_EXECUTABLE=${Python3_EXECUTABLE})
  endif()

  message(STATUS "mitsuba-anari: configuring ANARI-SDK (${dep_configs})")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${sdk_src}" -B "${sdk_build}" ${sdk_cmake_args}
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "mitsuba-anari: ANARI-SDK configure failed")
  endif()

  foreach(cfg IN LISTS dep_configs)
    message(STATUS "mitsuba-anari: building ANARI-SDK [${cfg}]")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" --build "${sdk_build}" --config "${cfg}" --parallel
      RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "mitsuba-anari: ANARI-SDK build failed [${cfg}]")
    endif()
    execute_process(
      COMMAND "${CMAKE_COMMAND}" --install "${sdk_build}" --config "${cfg}"
      RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "mitsuba-anari: ANARI-SDK install failed [${cfg}]")
    endif()
  endforeach()

  file(WRITE "${stamp_file}" "${stamp_content}")
  list(PREPEND CMAKE_PREFIX_PATH "${sdk_prefix}")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
  set(MITSUBA_ANARI_SDK_PREFIX "${sdk_prefix}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Mitsuba provisioning (rendering backend from Milestone 3 on)
#
# Mitsuba has no find_package support (no exported targets), so it is built
# out-of-tree and consumed through a hand-written IMPORTED target.
# Two upstream quirks are handled here, both recorded in DEPENDENCIES.md:
#   - vendored ext/ projects declare cmake_minimum_required < 3.5, which
#     CMake >= 4 rejects without CMAKE_POLICY_VERSION_MINIMUM=3.5;
#   - with MI_ENABLE_PYTHON=OFF, NANOBIND_INCLUDE_DIRS stays empty although
#     core headers include nanobind's intrusive-ref headers; the include path
#     is injected via CMAKE_CXX_FLAGS.
# ---------------------------------------------------------------------------
# Which Mitsuba variants to build and link. scalar_rgb is the mandatory
# baseline; llvm_ad_rgb is the production CPU path (a JIT variant is required
# anyway: scalar-only builds trip an upstream CMake bug, and non-ad JIT
# variants fail to link against drjit-extra symbols — see DEPENDENCIES.md);
# cuda_ad_rgb enables opt-in GPU rendering (NVIDIA driver + OptiX are probed
# at runtime — no build-time CUDA dependency, and the mandatory test suite
# never requires a GPU).
set(MITSUBA_ANARI_MITSUBA_VARIANTS "scalar_rgb,llvm_ad_rgb,cuda_ad_rgb"
    CACHE STRING "Mitsuba variants to build into the rendering backend")
if(APPLE)
  # CUDA does not exist on macOS; the accelerated GPU path there is Dr.Jit's
  # Metal backend (Apple Silicon, generates MSL). Swap the variant so the ABI
  # macro derivation stays consistent with what actually gets built.
  string(REPLACE ",cuda_ad_rgb" ",metal_ad_rgb" MITSUBA_ANARI_MITSUBA_VARIANTS
         "${MITSUBA_ANARI_MITSUBA_VARIANTS}")
endif()

function(_mitsuba_anari_provision_mitsuba)
  set(mi_src "${MITSUBA_ANARI_MITSUBA_SOURCE_DIR}")
  set(mi_build "${CMAKE_BINARY_DIR}/_deps/mitsuba-build")
  set(mi_prefix "${CMAKE_BINARY_DIR}/_deps/mitsuba-install")

  set(mi_variants "${MITSUBA_ANARI_MITSUBA_VARIANTS}")

  # Mitsuba vendors its own dependencies as nested submodules; drjit stands in
  # for all of them here.
  if(NOT EXISTS "${mi_src}/CMakeLists.txt"
      OR NOT EXISTS "${mi_src}/ext/drjit/CMakeLists.txt")
    message(FATAL_ERROR
      "mitsuba-anari: Mitsuba sources are missing or incomplete in "
      "${mi_src}. Fetch the submodule and its nested submodules with:\n"
      "  git submodule update --init --recursive")
  endif()

  # The stamp covers the checked-out commit and any local edits, so both a
  # submodule update and work in progress inside the Mitsuba tree rebuild it.
  set(mi_revision "unknown")
  find_package(Git QUIET)
  if(Git_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${mi_src}" rev-parse HEAD
      OUTPUT_VARIABLE mi_head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${mi_src}" status --porcelain
      OUTPUT_VARIABLE mi_status ERROR_QUIET)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" -C "${mi_src}" diff HEAD
      OUTPUT_VARIABLE mi_diff ERROR_QUIET)
    string(SHA1 mi_local_edits "${mi_status}${mi_diff}")
    set(mi_revision "${mi_head}+${mi_local_edits}")
  endif()

  set(stamp_content "${mi_revision}|${mi_variants}|Release")
  set(stamp_file "${mi_prefix}/.mitsuba-anari-provisioned")
  if(EXISTS "${stamp_file}")
    file(READ "${stamp_file}" existing_stamp)
    if(existing_stamp STREQUAL stamp_content)
      message(STATUS "mitsuba-anari: reusing provisioned Mitsuba at ${mi_prefix}")
      set(MITSUBA_ANARI_MITSUBA_PREFIX "${mi_prefix}" PARENT_SCOPE)
      return()
    endif()
    message(STATUS "mitsuba-anari: Mitsuba sources/config changed; re-provisioning")
    # MI_DEFAULT_VARIANTS only seeds mitsuba.conf when that file does not
    # exist yet — a cached conf would silently keep the old variant set, so
    # remove it whenever the variants change. (Source-only changes keep it,
    # which keeps the rebuild incremental.)
    string(FIND "${existing_stamp}" "|${mi_variants}|" same_variants)
    if(same_variants EQUAL -1)
      file(REMOVE "${mi_build}/mitsuba.conf")
    endif()
  endif()

  file(TO_CMAKE_PATH "${mi_src}/ext/nanobind/include" mi_nanobind_inc)
  set(mi_cxx_flags "/IF")
  if(MSVC)
    set(mi_cxx_flags "/DWIN32 /D_WINDOWS /W3 /GR /EHsc /I${mi_nanobind_inc}")
  else()
    set(mi_cxx_flags "-I${mi_nanobind_inc}")
  endif()
  # Escape hatch for toolchains whose *default* -march breaks assumptions in
  # the Mitsuba superbuild. Known case: distros with an x86-64-v3 compiler
  # baseline (e.g. Rocky/RHEL 10) implicitly enable AVX in Embree's
  # lowest-ISA translation units, so BVHN<4> is never instantiated and
  # libembree fails to link; -march=x86-64-v2 here restores the upstream
  # premise (per-target -mavx/-mavx2 flags still raise it where intended).
  if(MITSUBA_ANARI_MI_EXTRA_CXX_FLAGS)
    string(APPEND mi_cxx_flags " ${MITSUBA_ANARI_MI_EXTRA_CXX_FLAGS}")
  endif()

  set(mi_cmake_args
    -G "${CMAKE_GENERATOR}"
    -DCMAKE_INSTALL_PREFIX=${mi_prefix}
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DMI_ENABLE_PYTHON=OFF
    "-DMI_DEFAULT_VARIANTS=${mi_variants}"
    "-DMI_NATIVE_FLAGS=${MITSUBA_ANARI_MI_ISA_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${mi_cxx_flags}")
  if(CMAKE_GENERATOR_PLATFORM)
    list(APPEND mi_cmake_args -A "${CMAKE_GENERATOR_PLATFORM}")
  endif()
  if(CMAKE_GENERATOR_TOOLSET)
    list(APPEND mi_cmake_args -T "${CMAKE_GENERATOR_TOOLSET}")
  endif()
  get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(NOT is_multi_config)
    # The Mitsuba backend is always built Release: it is the renderer runtime,
    # and mixing Debug device code with the Release Mitsuba C++ ABI is only
    # supported on single-config platforms where the device is also Release.
    list(APPEND mi_cmake_args -DCMAKE_BUILD_TYPE=Release)
    if(CMAKE_CXX_COMPILER)
      list(APPEND mi_cmake_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
    endif()
    if(CMAKE_C_COMPILER)
      list(APPEND mi_cmake_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
    endif()
  endif()
  if(Python3_EXECUTABLE)
    list(APPEND mi_cmake_args -DPython3_EXECUTABLE=${Python3_EXECUTABLE})
  endif()

  message(STATUS "mitsuba-anari: configuring Mitsuba (variants: ${mi_variants})")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${mi_src}" -B "${mi_build}" ${mi_cmake_args}
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "mitsuba-anari: Mitsuba configure failed")
  endif()

  message(STATUS "mitsuba-anari: building Mitsuba [Release] (this takes a while on first configure)")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${mi_build}" --config Release --parallel
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "mitsuba-anari: Mitsuba build failed")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${mi_build}" --config Release
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "mitsuba-anari: Mitsuba install failed")
  endif()

  if(WIN32)
    # Upstream's install rules ship the DLLs but not the MSVC import
    # libraries; copy them from the build tree so the device can link
    # (drjit-core/extra and nanothread are needed for JIT-variant template
    # instantiation).
    file(COPY "${mi_build}/src/Release/mitsuba.lib"
         DESTINATION "${mi_prefix}/lib")
    file(COPY
         "${mi_build}/ext/drjit/ext/drjit-core/Release/drjit-core.lib"
         "${mi_build}/ext/drjit/src/extra/Release/drjit-extra.lib"
         "${mi_build}/ext/drjit/ext/drjit-core/ext/nanothread/Release/nanothread.lib"
         DESTINATION "${mi_prefix}/lib")
  else()
    # Force a fresh colocation of plugins/data next to the runtime library on
    # every (re-)provision: plugin binaries are variant-dependent, so stale
    # copies from a previous variant set must not survive (the import-time
    # colocation below only fills in missing directories).
    _mitsuba_anari_libdir("${mi_prefix}" _mi_provisioned_libdir)
    file(REMOVE_RECURSE
      "${_mi_provisioned_libdir}/plugins" "${_mi_provisioned_libdir}/data")
  endif()

  file(WRITE "${stamp_file}" "${stamp_content}")
  set(MITSUBA_ANARI_MITSUBA_PREFIX "${mi_prefix}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# ISA baseline shared by the Mitsuba build and every consumer.
#
# Dr.Jit types embedded in Mitsuba's C++ ABI change layout with the enabled
# SIMD instruction set. Mitsuba's build defaults to AVX2 on MSVC and
# -march=native elsewhere; consumers compiled with a different ISA silently
# corrupt the heap (verified in Milestone 3 bring-up). We therefore pin a
# deterministic baseline on both sides: Haswell-class on x86-64, ARMv8/Apple-M1
# on ARM (matching upstream's own portable defaults).
# ---------------------------------------------------------------------------
if(MSVC)
  set(MITSUBA_ANARI_MI_ISA_FLAGS "/arch:AVX2")
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
  set(MITSUBA_ANARI_MI_ISA_FLAGS "-mcpu=apple-m1")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  set(MITSUBA_ANARI_MI_ISA_FLAGS "-march=armv8-a")
else()
  set(MITSUBA_ANARI_MI_ISA_FLAGS "-march=haswell")
endif()

# Defines the mitsuba::mitsuba IMPORTED target from an install prefix.
function(_mitsuba_anari_import_mitsuba prefix)
  if(TARGET mitsuba::mitsuba)
    return()
  endif()
  _mitsuba_anari_libdir("${prefix}" mi_libdir)
  if(WIN32)
    set(mi_lib "${mi_libdir}/mitsuba.lib")
    set(mi_dll "${prefix}/bin/mitsuba.dll")
  elseif(APPLE)
    set(mi_lib "${mi_libdir}/libmitsuba.dylib")
    set(mi_dll "${mi_lib}")
  else()
    set(mi_lib "${mi_libdir}/libmitsuba.so")
    set(mi_dll "${mi_lib}")
  endif()
  if(NOT EXISTS "${mi_lib}")
    # Fallback: some layouts put import libs beside the runtime.
    if(WIN32 AND EXISTS "${prefix}/bin/mitsuba.lib")
      set(mi_lib "${prefix}/bin/mitsuba.lib")
    else()
      message(FATAL_ERROR
        "mitsuba-anari: Mitsuba library not found under ${prefix} "
        "(looked for ${mi_lib})")
    endif()
  endif()

  add_library(mitsuba::mitsuba SHARED IMPORTED)
  set_target_properties(mitsuba::mitsuba PROPERTIES
    IMPORTED_LOCATION "${mi_dll}"
    IMPORTED_IMPLIB "${mi_lib}")
  if(WIN32)
    # JIT-variant template instantiation references drjit-core/extra and
    # nanothread symbols directly.
    foreach(dep drjit-core drjit-extra nanothread)
      if(EXISTS "${mi_libdir}/${dep}.lib")
        target_link_libraries(mitsuba::mitsuba INTERFACE
          "${mi_libdir}/${dep}.lib")
      endif()
    endforeach()
  else()
    foreach(dep drjit-core drjit-extra nanothread)
      if(EXISTS "${mi_libdir}/lib${dep}.so")
        target_link_libraries(mitsuba::mitsuba INTERFACE
          "${mi_libdir}/lib${dep}.so")
      elseif(EXISTS "${mi_libdir}/lib${dep}.dylib")
        target_link_libraries(mitsuba::mitsuba INTERFACE
          "${mi_libdir}/lib${dep}.dylib")
      endif()
    endforeach()
  endif()
  set(mi_ext "${MITSUBA_ANARI_MITSUBA_SOURCE_DIR}/ext")
  target_include_directories(mitsuba::mitsuba INTERFACE
    "${prefix}/include"
    "${mi_ext}/nanobind/include"
    "${mi_ext}/nanobind/ext/robin_map/include"
    "${mi_ext}/drjit/include"
    "${mi_ext}/drjit/ext/drjit-core/include"
    "${mi_ext}/drjit/ext/drjit-core/ext/nanothread/include"
    "${mi_ext}/tinyformat"
    "${mi_ext}/struct-jit/include")
  # ABI requirement, not an optimization: see MITSUBA_ANARI_MI_ISA_FLAGS above.
  target_compile_options(mitsuba::mitsuba INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:${MITSUBA_ANARI_MI_ISA_FLAGS}>)

  # Mitsuba's backend-enable macros are compile definitions, not part of the
  # installed config.h — and they influence header-declared layouts, so
  # consumers must define exactly the set the library was built with. Derived
  # from the same variant list that drives provisioning.
  set(mi_defs MI_ENABLE_EMBREE)
  if(MITSUBA_ANARI_MITSUBA_VARIANTS MATCHES "llvm_|cuda_|metal_")
    list(APPEND mi_defs MI_ENABLE_JIT)
  endif()
  if(MITSUBA_ANARI_MITSUBA_VARIANTS MATCHES "_ad_")
    list(APPEND mi_defs MI_ENABLE_AUTODIFF)
  endif()
  if(MITSUBA_ANARI_MITSUBA_VARIANTS MATCHES "llvm_")
    list(APPEND mi_defs MI_ENABLE_LLVM)
  endif()
  if(MITSUBA_ANARI_MITSUBA_VARIANTS MATCHES "cuda_")
    list(APPEND mi_defs MI_ENABLE_CUDA)
  endif()
  if(MITSUBA_ANARI_MITSUBA_VARIANTS MATCHES "metal_")
    list(APPEND mi_defs MI_ENABLE_METAL)
  endif()
  target_compile_definitions(mitsuba::mitsuba INTERFACE ${mi_defs})

  if(WIN32)
    set(MITSUBA_ANARI_MITSUBA_RUNTIME_DIR "${prefix}/bin" PARENT_SCOPE)
  else()
    # Mitsuba resolves "plugins/..." and "data/..." through the FileResolver
    # relative to the directory containing the runtime library (see
    # src/mitsuba_backend/MitsubaBackend.cpp). Upstream's Unix install layout
    # puts the library in lib/ (lib64/ on RHEL-family distributions) but
    # plugins under bin/plugins and data under share/ — colocate copies next
    # to the library so resolution works exactly as in the Windows bin/
    # layout. Idempotent, pure install-tree fixup: no Mitsuba sources or
    # build rules are modified.
    if(EXISTS "${prefix}/bin/plugins" AND NOT EXISTS "${mi_libdir}/plugins")
      file(COPY "${prefix}/bin/plugins" DESTINATION "${mi_libdir}")
    endif()
    if(NOT EXISTS "${mi_libdir}/data")
      file(MAKE_DIRECTORY "${mi_libdir}/data")
      foreach(_mi_data srgb.coeff ior sunsky)
        if(EXISTS "${prefix}/share/${_mi_data}")
          file(COPY "${prefix}/share/${_mi_data}"
               DESTINATION "${mi_libdir}/data")
        endif()
      endforeach()
    endif()
    set(MITSUBA_ANARI_MITSUBA_RUNTIME_DIR "${mi_libdir}" PARENT_SCOPE)
  endif()
endfunction()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Fetch only Mitsuba and use a preinstalled (possibly newer) ANARI-SDK found
# through CMAKE_PREFIX_PATH / anari_DIR.
option(MITSUBA_ANARI_USE_SYSTEM_ANARI_SDK
  "Use a preinstalled ANARI-SDK even when fetching dependencies" OFF)

if(MITSUBA_ANARI_FETCH_DEPENDENCIES)
  if(NOT MITSUBA_ANARI_USE_SYSTEM_ANARI_SDK)
    _mitsuba_anari_provision_anari_sdk()
  endif()
  _mitsuba_anari_provision_mitsuba()
  _mitsuba_anari_import_mitsuba("${CMAKE_BINARY_DIR}/_deps/mitsuba-install")
else()
  if(NOT MITSUBA_ANARI_MITSUBA_ROOT)
    message(FATAL_ERROR
      "mitsuba-anari: MITSUBA_ANARI_FETCH_DEPENDENCIES=OFF requires "
      "-DMITSUBA_ANARI_MITSUBA_ROOT=<mitsuba-install-prefix>")
  endif()
  _mitsuba_anari_import_mitsuba("${MITSUBA_ANARI_MITSUBA_ROOT}")
endif()

# code_gen component is mandatory: it carries anari_generate_queries() used for
# introspection generation from Milestone 1 on.
find_package(anari ${MITSUBA_ANARI_ANARI_SDK_VERSION} REQUIRED COMPONENTS code_gen)
message(STATUS "mitsuba-anari: using ANARI-SDK ${anari_VERSION} (${anari_DIR})")
