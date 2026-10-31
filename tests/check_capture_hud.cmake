# SPDX-License-Identifier: GPL-3.0-or-later
# The first frame of a replay state must have the same HUD as its next identical frame.
set(dir "${OUT_DIR}/capture-hud")
file(MAKE_DIRECTORY "${dir}")
foreach(theme console manifold)
  foreach(scale 1 3)
    if(scale EQUAL 3)
      set(width 1920)
      set(height 1080)
    else()
      set(width 640)
      set(height 400)
    endif()
    set(common --play "${WORKDIR}/tests/data/games/standard.cbgame" --theme ${theme}
               --width ${width} --height ${height} --ui-scale ${scale}
               --visual-time 0.25 --fps 2)
    set(still "${dir}/${theme}-${scale}.ppm")
    set(clip "${dir}/${theme}-${scale}-clip")
    execute_process(COMMAND "${GUI}" ${common} --play-at 0.5 --shot "${still}"
                    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                    OUTPUT_QUIET ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "HUD still failed: ${err}")
    endif()
    execute_process(COMMAND "${GUI}" ${common} --clip "${clip}" --frames 2
                    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                    OUTPUT_QUIET ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "HUD clip failed: ${err}")
    endif()
    file(SHA256 "${still}" a)
    file(SHA256 "${clip}/frame_0001.ppm" b)
    if(NOT a STREQUAL b)
      message(FATAL_ERROR "first HUD differs from settled HUD: ${still} / ${clip}/frame_0001.ppm")
    endif()
  endforeach()
endforeach()
