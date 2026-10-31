# SPDX-License-Identifier: GPL-3.0-or-later
# A menu has no board material animation: changed pixels prove real UI clock wiring.
foreach(theme console manifold)
  foreach(time 0.3 2.3)
    set(path "${OUT_DIR}/live-${theme}-${time}.ppm")
    execute_process(COMMAND "${GUI}" --screen menu --theme ${theme}
                    --width 1280 --height 720 --visual-time ${time} --shot "${path}"
                    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                    OUTPUT_QUIET ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${theme} live control capture failed: ${err}")
    endif()
    file(SHA256 "${path}" hash_${time})
  endforeach()
  if(theme STREQUAL "manifold")
    if(hash_0.3 STREQUAL hash_2.3)
      message(FATAL_ERROR "manifold controls ignore explicit time")
    endif()
  elseif(NOT hash_0.3 STREQUAL hash_2.3)
    message(FATAL_ERROR "console UI changes with visual time")
  endif()
endforeach()
execute_process(COMMAND "${GUI}" --screen menu --theme manifold --width 1280 --height 720
                --visual-time 0.3 --ui-click "1,1" --shot "${OUT_DIR}/live-again.ppm"
                WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET)
file(SHA256 "${OUT_DIR}/live-again.ppm" again)
if(NOT rc EQUAL 0 OR NOT again STREQUAL hash_0.3)
  message(FATAL_ERROR "pointer warmup shifted control visual time")
endif()
