# C++ examples: standard ANARI client applications (public C API only).

add_executable(mitsubaAnariInfo mitsubaAnariInfo.cpp)
target_link_libraries(mitsubaAnariInfo PRIVATE anari::anari)
mitsuba_anari_set_warnings(mitsubaAnariInfo)
