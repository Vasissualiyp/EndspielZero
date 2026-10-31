# SPDX-License-Identifier: GPL-3.0-or-later
# Explicit presentation seconds must survive capture warmup and clip interpolation.
execute_process(COMMAND "${GUI}" --help OUTPUT_VARIABLE help)
if(NOT help MATCHES "--visual-time")
  message(FATAL_ERROR "capture has no explicit visual clock")
endif()
function(shot name)
  execute_process(COMMAND "${GUI}" standard --theme manifold --width 640 --height 400
                  --shot "${OUT_DIR}/visual-${name}.ppm" ${ARGN}
                  WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                  OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} failed: ${err}")
  endif()
endfunction()
shot(upper --visual-time 0.75)
shot(lower --visual-time 2.75)
shot(again --visual-time 0.75 --ui-click "1,1")
file(SHA256 "${OUT_DIR}/visual-upper.ppm" upper)
file(SHA256 "${OUT_DIR}/visual-lower.ppm" lower)
file(SHA256 "${OUT_DIR}/visual-again.ppm" again)
if(upper STREQUAL lower OR NOT upper STREQUAL again)
  message(FATAL_ERROR "visual time is missing or changes during pointer warmup")
endif()
execute_process(COMMAND "${GUI}" standard --theme manifold --width 640 --height 400
                --visual-time 0.25 --clip "${OUT_DIR}/visual-clip" --frames 2
                --fps 2 --t0 0 --t1 0
                WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "explicit-time clip failed")
endif()
file(SHA256 "${OUT_DIR}/visual-clip/frame_0001.ppm" clip)
if(NOT clip STREQUAL upper)
  message(FATAL_ERROR "clip frame seconds do not match a still at the same visual time")
endif()
foreach(theme console manifold)
  shot(${theme}-matte-a --theme ${theme} --matte --visual-time 0.75)
  shot(${theme}-matte-b --theme ${theme} --matte --visual-time 2.75)
  file(SHA256 "${OUT_DIR}/visual-${theme}-matte-a.ppm" a)
  file(SHA256 "${OUT_DIR}/visual-${theme}-matte-b.ppm" b)
  if(NOT a STREQUAL b)
    message(FATAL_ERROR "matte ${theme} changes with material time")
  endif()
endforeach()
shot(overture-flat --screen newgame --t 0 --visual-time 0.75)
shot(overture-formed --screen newgame --t 1 --visual-time 0.75)
file(SHA256 "${OUT_DIR}/visual-overture-flat.ppm" flat)
file(SHA256 "${OUT_DIR}/visual-overture-formed.ppm" formed)
if(flat STREQUAL formed)
  message(FATAL_ERROR "--visual-time broke normalized overture --t")
endif()
# Saved playback uses its global cursor, independent of output/warmup redraws.
set(game "${WORKDIR}/tests/data/games/standard.cbgame")
shot(play-still --cinema --play "${game}" --play-at 0.5 --visual-time 0.25)
execute_process(COMMAND "${GUI}" --play "${game}" --cinema --theme manifold --width 640 --height 400
                --visual-time 0.25 --clip "${OUT_DIR}/visual-play" --frames 2 --fps 2
                WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "saved playback clip failed")
endif()
file(SHA256 "${OUT_DIR}/visual-play/frame_0001.ppm" clip)
file(SHA256 "${OUT_DIR}/visual-play-still.ppm" still)
if(NOT clip STREQUAL still)
  message(FATAL_ERROR "saved playback visual time differs between still and clip")
endif()
foreach(value -1 nan inf 1junk)
  execute_process(COMMAND "${GUI}" --visual-time ${value}
                  --shot "${OUT_DIR}/visual-invalid.ppm"
                  WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                  OUTPUT_QUIET ERROR_QUIET TIMEOUT 10)
  if(rc EQUAL 0)
    message(FATAL_ERROR "invalid visual seconds accepted: ${value}")
  endif()
endforeach()
