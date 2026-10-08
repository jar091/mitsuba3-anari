# Defines mitsuba_anari_set_warnings(<target>) applying the project warning set.
# Warnings are applied only to project targets, never to third-party code.

function(mitsuba_anari_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4
      /permissive-
      /Zc:__cplusplus
      # C4251: dll-interface warnings on STL members; noisy and not actionable
      # for an internal-ABI device library.
      /wd4251
    )
    if(MITSUBA_ANARI_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
    )
    if(MITSUBA_ANARI_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
