# SPDX-License-Identifier: GPL-3.0-or-later
# The final position, wave, army collapse and standby are distinct deterministic frames.
file(MAKE_DIRECTORY "${OUT_DIR}/end-shutdown")
set(common standard --theme manifold --width 960 --height 540 --ui-auto)
set(mate "click f2\nclick f3\nclick e7\nclick e5\nclick g2\nclick g4\nclick d8\nclick h4")
function(shot name)
  execute_process(COMMAND "${GUI}" ${common} --script "${mate}" ${ARGN}
    --shot "${OUT_DIR}/end-shutdown/${name}.ppm"
    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} capture failed: ${err}")
  endif()
  file(SHA256 "${OUT_DIR}/end-shutdown/${name}.ppm" h)
  set(hash_${name} ${h} PARENT_SCOPE)
endfunction()

set(previous "")
foreach(age IN ITEMS 3.0 6.0 9.0 11.0 20.0)
  shot(age-${age} --end-t ${age})
  if(previous AND hash_age-${age} STREQUAL previous)
    message(FATAL_ERROR "end at ${age} is identical to the preceding age")
  endif()
  set(previous "${hash_age-${age}}")
endforeach()

# Explicit end age includes the final move landing; a frozen travelling copy must not
# survive the shutdown just because --move-t was omitted.
shot(settled --end-t 20.0 --move-t 1)
if(NOT hash_settled STREQUAL hash_age-20.0)
  message(FATAL_ERROR "end at 20 s still contains an unsettled final move")
endif()
shot(landed --end-t 3.0 --move-t 1)
if(NOT hash_landed STREQUAL hash_age-3.0)
  message(FATAL_ERROR "the final move has not landed by the end of the 3 s hold")
endif()
