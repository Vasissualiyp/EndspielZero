# SPDX-License-Identifier: GPL-3.0-or-later
# Capture ages follow the production clocks. CMake arithmetic is integral, so read
# literal second constants as milliseconds and convert only at the CLI boundary.
function(rig_milliseconds header constant output)
  file(READ "${WORKDIR}/src/view/${header}.hpp" source)
  string(REGEX MATCH "${constant} = ([0-9]+)\\.([0-9]+)f" match "${source}")
  if(NOT match)
    message(FATAL_ERROR "Missing literal timing constant ${header}::${constant}")
  endif()
  set(whole "${CMAKE_MATCH_1}")
  set(fraction "${CMAKE_MATCH_2}000")
  string(SUBSTRING "${fraction}" 0 3 fraction)
  math(EXPR value "${whole} * 1000 + 1${fraction} - 1000")
  set(${output} ${value} PARENT_SCOPE)
endfunction()

function(rig_seconds milliseconds output)
  math(EXPR whole "${milliseconds} / 1000")
  math(EXPR fraction "${milliseconds} % 1000 + 1000")
  string(SUBSTRING "${fraction}" 1 3 fraction)
  set(${output} "${whole}.${fraction}" PARENT_SCOPE)
endfunction()
