# SPDX-License-Identifier: GPL-3.0-or-later
# Idle plates are quiet; hover must sample the explicit clock without shifting input.
foreach(menuMode public dev)
  set(menuFlags)
  # The seven-row public menu centres its first row at y243 (bounds 222..264).
  # The original nine-row developer menu centres it at y193 (bounds 172..214).
  set(rowY 243)
  if(menuMode STREQUAL "dev")
    set(menuFlags --dev)
    set(rowY 193)
  endif()
  foreach(theme console manifold)
    unset(first_idle)
    unset(first_hover)
    set(changed FALSE)
    foreach(time 0.3 0.425 0.55 0.675)
      foreach(state idle hover)
        set(path "${OUT_DIR}/live-${menuMode}-${theme}-${state}-${time}.ppm")
        set(input)
        if(state STREQUAL "hover")
          # Start in empty pane space, release in the measured first row (x686..1006).
          set(input --ui-click "drag:650,${rowY},760,${rowY},0")
        endif()
        execute_process(COMMAND "${GUI}" --screen menu --theme ${theme}
                        --width 1280 --height 720 --visual-time ${time}
                        ${menuFlags} ${input} --shot "${path}"
                        WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                        OUTPUT_QUIET ERROR_VARIABLE err)
        if(NOT rc EQUAL 0)
          message(FATAL_ERROR "${theme} ${state} control capture failed: ${err}")
        endif()
        file(SHA256 "${path}" hash)
        if(NOT DEFINED first_${state})
          set(first_${state} "${hash}")
        elseif(state STREQUAL "idle" OR theme STREQUAL "console")
          if(NOT hash STREQUAL first_${state})
            message(FATAL_ERROR "${theme} ${state} controls move while they should be quiet")
          endif()
        elseif(NOT hash STREQUAL first_hover)
          set(changed TRUE)
        endif()
      endforeach()
    endforeach()
    if(theme STREQUAL "manifold" AND NOT changed)
      message(FATAL_ERROR "manifold hover glitches ignore explicit time")
    endif()
    execute_process(COMMAND "${GUI}" --screen menu --theme ${theme} --width 1280 --height 720
                    --visual-time 0.3 ${menuFlags}
                    --ui-click "drag:650,${rowY},760,${rowY},0;drag:650,${rowY},760,${rowY},0"
                    --shot "${OUT_DIR}/live-${menuMode}-${theme}-again.ppm"
                    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET)
    file(SHA256 "${OUT_DIR}/live-${menuMode}-${theme}-again.ppm" again)
    if(NOT rc EQUAL 0 OR NOT again STREQUAL first_hover)
      message(FATAL_ERROR "pointer warmup shifted ${theme} hover phase or activated a row")
    endif()
  endforeach()
endforeach()
