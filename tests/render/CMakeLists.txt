# Render regression tests: behavioral image assertions through the public
# ANARI C API (no golden-byte comparisons; see TESTING.md).

add_executable(anari_triangle_matte_test triangle_matte.cpp)
target_link_libraries(anari_triangle_matte_test PRIVATE anari::anari)
mitsuba_anari_set_warnings(anari_triangle_matte_test)
if(MSVC)
  target_compile_definitions(anari_triangle_matte_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(anari_scene_update_test scene_update.cpp)
target_link_libraries(anari_scene_update_test PRIVATE anari::anari)
mitsuba_anari_set_warnings(anari_scene_update_test)

add_test(NAME anari.scene_update
  COMMAND anari_scene_update_test "$<TARGET_FILE_DIR:anari_library_mitsuba>")
set_tests_properties(anari.scene_update PROPERTIES
  LABELS "integration;render")

add_executable(anari_pbr_texture_test pbr_texture.cpp)
target_link_libraries(anari_pbr_texture_test PRIVATE anari::anari)
mitsuba_anari_set_warnings(anari_pbr_texture_test)

add_test(NAME anari.pbr_texture
  COMMAND anari_pbr_texture_test "$<TARGET_FILE_DIR:anari_library_mitsuba>")
set_tests_properties(anari.pbr_texture PROPERTIES
  LABELS "integration;render")

add_executable(anari_lights_depth_test lights_depth.cpp)
target_link_libraries(anari_lights_depth_test PRIVATE anari::anari)
mitsuba_anari_set_warnings(anari_lights_depth_test)

add_test(NAME anari.lights_depth
  COMMAND anari_lights_depth_test "$<TARGET_FILE_DIR:anari_library_mitsuba>")
set_tests_properties(anari.lights_depth PROPERTIES
  LABELS "integration;render")

add_executable(anari_geometry_test geometry.cpp)
target_link_libraries(anari_geometry_test PRIVATE anari::anari)
mitsuba_anari_set_warnings(anari_geometry_test)

add_test(NAME anari.geometry
  COMMAND anari_geometry_test "$<TARGET_FILE_DIR:anari_library_mitsuba>")
set_tests_properties(anari.geometry PROPERTIES
  LABELS "integration;render")

add_test(NAME anari.triangle_matte
  COMMAND anari_triangle_matte_test "$<TARGET_FILE_DIR:anari_library_mitsuba>")
set_tests_properties(anari.triangle_matte PROPERTIES
  LABELS "integration;render")

# Optional accelerated-variant runs (vendor extension
# ANARI_MITSUBA_DEVICE_VARIANT). These SKIP (code 77) on machines where the
# requested backend is unavailable at runtime — the mandatory suite never
# requires LLVM or a GPU (master prompt §21). Each runs in its own process,
# which is also required because one process can hold only one Mitsuba
# variant (ADR 0004).
add_test(NAME llvm.triangle_matte
  COMMAND anari_triangle_matte_test "$<TARGET_FILE_DIR:anari_library_mitsuba>")
set_tests_properties(llvm.triangle_matte PROPERTIES
  LABELS "integration;render;llvm"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MITSUBA_ANARI_TEST_VARIANT=llvm_ad_rgb")

add_test(NAME gpu.triangle_matte
  COMMAND anari_triangle_matte_test "$<TARGET_FILE_DIR:anari_library_mitsuba>")
set_tests_properties(gpu.triangle_matte PROPERTIES
  LABELS "integration;render;gpu"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MITSUBA_ANARI_TEST_VARIANT=cuda_ad_rgb")

add_test(NAME metal.triangle_matte
  COMMAND anari_triangle_matte_test "$<TARGET_FILE_DIR:anari_library_mitsuba>")
set_tests_properties(metal.triangle_matte PROPERTIES
  LABELS "integration;render;gpu;metal"
  SKIP_RETURN_CODE 77
  ENVIRONMENT "MITSUBA_ANARI_TEST_VARIANT=metal_ad_rgb")

if(WIN32)
  set(_render_tests anari.triangle_matte anari.scene_update anari.pbr_texture
      anari.lights_depth anari.geometry llvm.triangle_matte gpu.triangle_matte
      metal.triangle_matte)
  set_property(TEST ${_render_tests}
    APPEND PROPERTY ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:anari::anari>")
  if(MITSUBA_ANARI_MITSUBA_RUNTIME_DIR)
    set_property(TEST ${_render_tests}
      APPEND PROPERTY ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${MITSUBA_ANARI_MITSUBA_RUNTIME_DIR}")
  endif()
endif()
