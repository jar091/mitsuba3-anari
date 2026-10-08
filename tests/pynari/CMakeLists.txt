# PyNARI tests (MITSUBA_ANARI_BUILD_PYNARI_TESTS=ON): each test file runs in
# its own pytest process to isolate dynamic-library and renderer state.
#
# Requirements (see PYNARI.md): the configured Python must be able to
# `import pynari` (built against the same ANARI-SDK pin) and numpy/pytest.
# The device library and its runtime dependencies are put on PATH /
# LD_LIBRARY_PATH by the test environment below.

set(_pynari_tests
  test_triangle.py
)

foreach(_file IN LISTS _pynari_tests)
  get_filename_component(_name "${_file}" NAME_WE)
  string(REPLACE "test_" "" _short "${_name}")
  add_test(NAME pynari.${_short}
    COMMAND "${Python3_EXECUTABLE}" -m pytest -q
            "${CMAKE_CURRENT_SOURCE_DIR}/${_file}")
  set_tests_properties(pynari.${_short} PROPERTIES LABELS "pynari")
  if(WIN32)
    set_property(TEST pynari.${_short} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:anari::anari>")
    set_property(TEST pynari.${_short} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:anari_library_mitsuba>")
    if(MITSUBA_ANARI_MITSUBA_RUNTIME_DIR)
      set_property(TEST pynari.${_short} APPEND PROPERTY
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${MITSUBA_ANARI_MITSUBA_RUNTIME_DIR}")
    endif()
  elseif(APPLE)
    # macOS: dlopen("libanari_library_mitsuba.dylib") searches
    # DYLD_LIBRARY_PATH; libmitsuba/libanari resolve via the device's rpath.
    # DYLD_* survives here because the test invokes a non-SIP-protected
    # (Homebrew/venv) Python directly.
    set_property(TEST pynari.${_short} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION
      "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:anari_library_mitsuba>")
    set_property(TEST pynari.${_short} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION
      "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:anari::anari>")
  else()
    set_property(TEST pynari.${_short} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION
      "LD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:anari_library_mitsuba>")
  endif()
  if(MITSUBA_ANARI_PYNARI_PYTHONPATH)
    set_property(TEST pynari.${_short} APPEND PROPERTY
      ENVIRONMENT_MODIFICATION
      "PYTHONPATH=path_list_prepend:${MITSUBA_ANARI_PYNARI_PYTHONPATH}")
  endif()
endforeach()
