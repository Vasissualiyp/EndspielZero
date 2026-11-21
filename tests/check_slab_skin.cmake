# SPDX-License-Identifier: GPL-3.0-or-later
# M24.5b: the slab skin. A torus game in the lab at three morph points is three
# pictures; the skin off is a different picture from the skin on; the same capture twice
# is the same picture; a pinned latch flash changes the frame and is gone by 0.3 s.
file(MAKE_DIRECTORY "${OUT_DIR}/slab-skin")
set(common torus --theme manifold --width 640 --height 360 --lab 1 --geometry)
function(shot name)
  execute_process(COMMAND "${GUI}" ${common} ${ARGN}
    --shot "${OUT_DIR}/slab-skin/${name}.ppm"
    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} capture failed: ${err}")
  endif()
  file(SHA256 "${OUT_DIR}/slab-skin/${name}.ppm" h)
  set(hash_${name} ${h} PARENT_SCOPE)
endfunction()

set(points 0 0.5 1)
foreach(f IN LISTS points)
  shot(formed-${f} --formed ${f})
endforeach()
foreach(a IN LISTS points)
  foreach(b IN LISTS points)
    if(NOT a STREQUAL b AND hash_formed-${a} STREQUAL hash_formed-${b})
      message(FATAL_ERROR "skin frames at formed ${a} and ${b} are identical")
    endif()
  endforeach()
endforeach()

# The skin is what changes the formed shape: off is the old solid sheet.
shot(formed-1-off --formed 1 --skin 0)
if(hash_formed-1 STREQUAL hash_formed-1-off)
  message(FATAL_ERROR "--skin 0 and --skin 1 give the same frame")
endif()

# Determinism.
shot(formed-0.5-again --formed 0.5)
if(NOT hash_formed-0.5 STREQUAL hash_formed-0.5-again)
  message(FATAL_ERROR "two skin captures at formed 0.5 differ")
endif()

# The ring's latch: a flash 0.03 s after it closes, nothing left at 0.3 s.
shot(latch-0.03 --formed 1 --latch-t 0.03)
shot(latch-0.3 --formed 1 --latch-t 0.3)
if(hash_latch-0.03 STREQUAL hash_formed-1)
  message(FATAL_ERROR "the latch flash does not change the frame")
endif()
if(NOT hash_latch-0.3 STREQUAL hash_formed-1)
  message(FATAL_ERROR "the latch flash is still showing at 0.3 s")
endif()

# M25.4 only changes physical 2-D skins. A stacked shape is projected light;
# switching skin must not remove the physical slabs parked above it (M24.6).
set(common torus3d --theme manifold --width 640 --height 360 --lab 1 --geometry)
shot(stacked-skin-on --formed 1 --skin 1)
shot(stacked-skin-off --formed 1 --skin 0)
if(NOT hash_stacked-skin-on STREQUAL hash_stacked-skin-off)
  message(FATAL_ERROR "the 2-D skin toggle changes the stacked shape's parked hardware")
endif()
