# Sanitizer configuration (Linux/macOS Clang/GCC; not supported for MSVC here).
# Controlled by MITSUBA_ANARI_ENABLE_ASAN / MITSUBA_ANARI_ENABLE_UBSAN /
# MITSUBA_ANARI_ENABLE_COVERAGE. Applied globally so that all project targets
# and fetched dependencies are instrumented consistently.

if(MITSUBA_ANARI_ENABLE_ASAN OR MITSUBA_ANARI_ENABLE_UBSAN OR MITSUBA_ANARI_ENABLE_COVERAGE)
  if(MSVC)
    message(FATAL_ERROR
      "MITSUBA_ANARI_ENABLE_ASAN/UBSAN/COVERAGE are not supported with MSVC in "
      "this project yet. Use a Linux/macOS preset for sanitizer runs.")
  endif()
endif()

set(_mitsuba_anari_san_flags "")
if(MITSUBA_ANARI_ENABLE_ASAN)
  list(APPEND _mitsuba_anari_san_flags -fsanitize=address -fno-omit-frame-pointer)
endif()
if(MITSUBA_ANARI_ENABLE_UBSAN)
  list(APPEND _mitsuba_anari_san_flags -fsanitize=undefined)
endif()
if(MITSUBA_ANARI_ENABLE_COVERAGE)
  list(APPEND _mitsuba_anari_san_flags --coverage)
endif()

if(_mitsuba_anari_san_flags)
  add_compile_options(${_mitsuba_anari_san_flags})
  add_link_options(${_mitsuba_anari_san_flags})
endif()
