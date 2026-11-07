# SPDX-License-Identifier: GPL-3.0-or-later
# Screen moves (M23.11 section 6): pinned past its end a move is exactly the cut's frame,
# pinned inside it is not, and a calm move (presentation intensity 0) is over in 0.3 s.
file(MAKE_DIRECTORY "${OUT_DIR}/screen-transition")
set(common standard --theme manifold --width 960 --height 540 --ui-auto)
function(shot name)
  execute_process(COMMAND "${GUI}" ${common} ${ARGN}
    --shot "${OUT_DIR}/screen-transition/${name}.ppm"
    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} capture failed: ${err}")
  endif()
  file(SHA256 "${OUT_DIR}/screen-transition/${name}.ppm" h)
  set(hash_${name} ${h} PARENT_SCOPE)
endfunction()

function(check_move name from)
  shot(${name}-cut ${ARGN})
  shot(${name}-end ${ARGN} --from ${from} --transition-t 5)
  shot(${name}-mid ${ARGN} --from ${from} --transition-t 0.1)
  shot(${name}-calm ${ARGN} --from ${from} --transition-t 0.4 --presentation 0)
  shot(${name}-calm-end ${ARGN} --presentation 0)
  if(NOT hash_${name}-end STREQUAL hash_${name}-cut)
    message(FATAL_ERROR "${name}: a finished move is not the cut's frame")
  endif()
  if(hash_${name}-mid STREQUAL hash_${name}-cut)
    message(FATAL_ERROR "${name}: the move at 0.1 s shows nothing of the move")
  endif()
  if(NOT hash_${name}-calm STREQUAL hash_${name}-calm-end)
    message(FATAL_ERROR "${name}: a calm move is not over after 0.3 s")
  endif()
endfunction()

check_move(menu2game menu)
check_move(game2pause game --screen pause)
check_move(pause2game pause)
check_move(game2menu pause --screen menu)
