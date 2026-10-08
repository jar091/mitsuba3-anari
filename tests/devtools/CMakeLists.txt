# Developer diagnostics: not registered as CTest tests. Built on demand for
# debugging the Mitsuba embedding in isolation from the ANARI layers.

# The ISA/ABI baseline flags come through the mitsuba::mitsuba INTERFACE.
add_executable(mitsuba_direct_repro EXCLUDE_FROM_ALL mitsuba_direct_repro.cpp)
target_link_libraries(mitsuba_direct_repro PRIVATE mitsuba::mitsuba)
target_include_directories(mitsuba_direct_repro PRIVATE ${CMAKE_SOURCE_DIR}/src)
