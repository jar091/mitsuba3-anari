# anari_library_mitsuba: the loadable ANARI device library.
# The target name matters — the ANARI-SDK loader resolves
# anariLoadLibrary("mitsuba") to:
#   Windows: anari_library_mitsuba.dll
#   Linux:   libanari_library_mitsuba.so
#   macOS:   libanari_library_mitsuba.dylib
# which CMake produces from this target name via platform prefixes/suffixes.
#
# The retained object model is derived from the ANARI-SDK's
# empty_helium_device example skeleton (Apache-2.0, Khronos Group), adapted
# for this project — see THIRD_PARTY.md.

add_library(anari_library_mitsuba SHARED
  library/Library.cpp
  device/MitsubaDevice.cpp
  device/MitsubaGlobalState.cpp
  core/Object.cpp
  camera/Camera.cpp
  frame/Frame.cpp
  geometry/Geometry.cpp
  geometry/Tessellation.cpp
  light/Light.cpp
  material/Material.cpp
  mitsuba_backend/MitsubaBackend.cpp
  renderer/Renderer.cpp
  sampler/Sampler.cpp
  spatial_field/SpatialField.cpp
  scene/MitsubaSceneBuilder.cpp
  scene/Surface.cpp
  scene/Group.cpp
  scene/Instance.cpp
  scene/World.cpp
  volume/Volume.cpp
)

include(GenerateExportHeader)
generate_export_header(anari_library_mitsuba
  EXPORT_MACRO_NAME "MITSUBA_ANARI_EXPORT"
  EXPORT_FILE_NAME "mitsuba_anari_export.h")

target_include_directories(anari_library_mitsuba
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}   # generated export header
)

target_compile_definitions(anari_library_mitsuba PRIVATE
  "MITSUBA_ANARI_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
  "MITSUBA_ANARI_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
  "MITSUBA_ANARI_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
  "MITSUBA_ANARI_VERSION_STRING=\"${PROJECT_VERSION}\"")

# helium is the (static) device foundation; anari is the shared frontend.
# helium does not link the frontend transitively — link it explicitly.
target_link_libraries(anari_library_mitsuba
  PRIVATE
    anari::anari
    anari::helium
    mitsuba::mitsuba
)

# Introspection is generated from the definitions JSON — the single source of
# truth for advertised subtypes/parameters/extensions (SUPPORTED_FEATURES.md
# must stay in sync with it).
anari_generate_queries(
  DEVICE_TARGET anari_library_mitsuba
  CPP_NAMESPACE mitsuba_anari
  JSON_DEFINITIONS_FILE ${CMAKE_SOURCE_DIR}/json/MitsubaDefinitions.json
  JSON_ROOT_LOCATION ${CMAKE_SOURCE_DIR}/json
  JSON_EXTENSION_FILES ${CMAKE_SOURCE_DIR}/json/anari_mitsuba_device_variant.json
)

# helium is statically linked into every device module; hidden visibility
# prevents cross-module symbol binding between multiple helium-based devices.
set_target_properties(anari_library_mitsuba PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON)

mitsuba_anari_set_warnings(anari_library_mitsuba)

# Self-contained install tree on Unix: libmitsuba/libanari carry @rpath (macOS)
# / soname (Linux) install names and are co-installed into the same lib/
# directory as the device, so the device resolves them relative to itself.
if(APPLE)
  set_target_properties(anari_library_mitsuba PROPERTIES
    INSTALL_RPATH "@loader_path")
elseif(UNIX)
  set_target_properties(anari_library_mitsuba PROPERTIES
    INSTALL_RPATH "$ORIGIN")
endif()

install(TARGETS anari_library_mitsuba
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
