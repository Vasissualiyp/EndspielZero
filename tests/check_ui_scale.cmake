# SPDX-License-Identifier: GPL-3.0-or-later
# The capture override must actually scale the tutorial/library interface; neither
# setting is persisted, and each screenshot must still validate at both extremes.
foreach(screen welcome newgame watch)
  foreach(scale 0.6 3.0)
    set(out "${OUT_DIR}/gui-${screen}-scale-${scale}.ppm")
    execute_process(COMMAND "${GUI}" standard --screen ${screen} --width 1920
                            --height 1080 --ui-scale ${scale} --shot "${out}"
                    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                    OUTPUT_QUIET ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "${screen} at ${scale}x failed: ${err}")
    endif()
    file(SHA256 "${out}" hash_${screen}_${scale})
  endforeach()
  if("${hash_${screen}_0.6}" STREQUAL "${hash_${screen}_3.0}")
    message(FATAL_ERROR "${screen} ignores --ui-scale")
  endif()
  # Even a too-small window must scroll rather than tripping the picker's
  # formerly inverted clamp bounds.
  execute_process(COMMAND "${GUI}" standard --screen ${screen} --width 960
                          --height 540 --ui-scale 3.0
                          --shot "${OUT_DIR}/gui-${screen}-scale-small.ppm"
                  WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                  OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${screen} small 3x capture failed: ${err}")
  endif()
endforeach()
