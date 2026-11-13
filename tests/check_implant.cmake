# SPDX-License-Identifier: GPL-3.0-or-later
include("${WORKDIR}/tools/ppm_close.cmake")
file(MAKE_DIRECTORY "${OUT_DIR}/implant")
set(common standard --theme manifold --lab 1 --width 480 --height 270 --ui-auto
           --heartbeat 0 --living 0 --screen newgame --overture-t 0.5)
function(shot name)
  execute_process(COMMAND "${GUI}" ${common} ${ARGN}
    --shot "${OUT_DIR}/implant/${name}.ppm"
    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} capture failed: ${err}")
  endif()
  file(SHA256 "${OUT_DIR}/implant/${name}.ppm" h)
  set(hash_${name} ${h} PARENT_SCOPE)
endfunction()
shot(forming --from menu --transition-t 0.7 --panel-t 0.1)
shot(formed --from menu --transition-t 0.7 --panel-t 0.5)
shot(settled)
if(hash_forming STREQUAL hash_formed)
  message(FATAL_ERROR "pinned forming implant equals the fully formed pane")
endif()
# The authored menu -> library camera takes about 0.963 s. Pin the panel independently,
# but wait until the camera, projector and saccadic dip have settled for this comparison.
shot(settled_flight --from menu --transition-t 1.2 --panel-t 0.5)
ppm_close("${OUT_DIR}/implant/settled_flight.ppm" "${OUT_DIR}/implant/settled.ppm")
shot(direct --implant 0)
ppm_close("${OUT_DIR}/implant/settled.ppm" "${OUT_DIR}/implant/direct.ppm")
