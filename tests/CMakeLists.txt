add_subdirectory(integration)
add_subdirectory(render)
add_subdirectory(devtools)

if(MITSUBA_ANARI_BUILD_PYNARI_TESTS)
  add_subdirectory(pynari)
endif()

# Future layers (added milestone by milestone, see TESTING.md):
#   tests/unit/    — internal-class unit tests where separable
#   tests/cts/     — Milestone 8 (MITSUBA_ANARI_BUILD_CTS)
