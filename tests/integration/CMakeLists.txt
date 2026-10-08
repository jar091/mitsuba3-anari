# Native ANARI integration tests: true out-of-process clients that link only
# the public frontend and load the device via anariLoadLibrary(), never
# against internal implementation classes.

set(_device_dir "$<TARGET_FILE_DIR:anari_library_mitsuba>")

function(mitsuba_anari_add_client_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE anari::anari)
  mitsuba_anari_set_warnings(${name})
  # The installed library directory is GNUInstallDirs' choice ("lib64" on
  # RHEL-family 64-bit distributions), so the install-tree test must be told
  # rather than assume "lib".
  target_compile_definitions(${name} PRIVATE
    "MITSUBA_ANARI_INSTALL_LIBDIR=\"${CMAKE_INSTALL_LIBDIR}\"")
  if(MSVC)
    # std::getenv is the portable API here; MSVC's deprecation warning is noise.
    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endfunction()

mitsuba_anari_add_client_test(anari_load_library_test anari_load_library.cpp)
mitsuba_anari_add_client_test(anari_create_device_test anari_create_device.cpp)
mitsuba_anari_add_client_test(anari_introspection_test anari_introspection.cpp)
mitsuba_anari_add_client_test(anari_object_lifetime_test anari_object_lifetime.cpp)
mitsuba_anari_add_client_test(anari_invalid_subtype_test anari_invalid_subtype.cpp)

# Build-tree runs: pin the loader to the freshly built device library so the
# tests can never pick up a stale installed copy.
add_test(NAME anari.load_library COMMAND anari_load_library_test "${_device_dir}")
add_test(NAME anari.create_device COMMAND anari_create_device_test "${_device_dir}")
add_test(NAME anari.introspection COMMAND anari_introspection_test "${_device_dir}")
add_test(NAME anari.object_lifetime COMMAND anari_object_lifetime_test "${_device_dir}")
add_test(NAME anari.invalid_subtype COMMAND anari_invalid_subtype_test "${_device_dir}")
set_tests_properties(anari.load_library anari.create_device anari.introspection
  anari.object_lifetime anari.invalid_subtype
  PROPERTIES LABELS "integration")

# Debug-layer variant: the SDK debug device wraps mitsuba and validates API
# usage; debug-layer errors are test failures (warnings are informational here
# because the wrapper flags subtypes that are not yet in our definitions).
add_test(NAME debug.object_lifetime COMMAND anari_object_lifetime_test)
set_tests_properties(debug.object_lifetime PROPERTIES
  LABELS "integration;debug"
  ENVIRONMENT
  "MITSUBA_ANARI_TEST_LIBRARY=debug,$<TARGET_FILE_DIR:anari::anari>/;ANARI_DEBUG_WRAPPED_LIBRARY=mitsuba,${_device_dir}/;MITSUBA_ANARI_TEST_STRICT=0")

# Install-tree run: loads the device from MITSUBA_ANARI_INSTALL_PREFIX
# (bin/ on Windows, CMAKE_INSTALL_LIBDIR elsewhere). Skips when the prefix is
# not set; set it after `cmake --install` and rerun with -L install (see
# TESTING.md).
add_test(NAME install.load_library
  COMMAND anari_load_library_test --install-tree)
set_tests_properties(install.load_library PROPERTIES
  LABELS "integration;install"
  SKIP_RETURN_CODE 77)

# SDK tool: anariInfo against the built device (validates the entry point and
# generated introspection end-to-end from an unrelated client binary).
find_program(MITSUBA_ANARI_ANARIINFO_EXECUTABLE anariInfo
  HINTS "${anari_DIR}/../../../bin")
if(MITSUBA_ANARI_ANARIINFO_EXECUTABLE)
  add_test(NAME sdk.anari_info
    COMMAND "${MITSUBA_ANARI_ANARIINFO_EXECUTABLE}" -l "mitsuba,${_device_dir}/")
  set_tests_properties(sdk.anari_info PROPERTIES LABELS "integration;sdk")
else()
  message(WARNING "anariInfo not found in the ANARI-SDK install; sdk.anari_info test disabled")
endif()

set(_win_path_tests anari.load_library anari.create_device anari.introspection
    anari.object_lifetime anari.invalid_subtype debug.object_lifetime
    install.load_library)
if(TEST sdk.anari_info)
  list(APPEND _win_path_tests sdk.anari_info)
endif()
if(WIN32)
  # Test executables need anari.dll (from the provisioned/installed SDK) on
  # PATH, and the device DLL needs mitsuba.dll; the device DLL itself is
  # located via the loader path argument.
  set_property(TEST ${_win_path_tests} APPEND PROPERTY
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:anari::anari>")
  if(MITSUBA_ANARI_MITSUBA_RUNTIME_DIR)
    set_property(TEST ${_win_path_tests} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:${MITSUBA_ANARI_MITSUBA_RUNTIME_DIR}")
  endif()
endif()
