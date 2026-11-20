# SPDX-License-Identifier: GPL-3.0-or-later
# M24.6: every layout deploys with travel, relayouts move the hardware, light bridges and
# 5D arrivals. Each multi-slab variant in the lab renders, deterministically, and the
# lab-off frame is the one captures have always made.
file(MAKE_DIRECTORY "${OUT_DIR}/lab-multislab")
set(common --theme manifold --width 480 --height 270)
function(shot name)
  execute_process(COMMAND "${GUI}" ${common} ${ARGN}
    --shot "${OUT_DIR}/lab-multislab/${name}.ppm"
    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} capture failed: ${err}")
  endif()
  file(SHA256 "${OUT_DIR}/lab-multislab/${name}.ppm" h)
  set(hash_${name} ${h} PARENT_SCOPE)
endfunction()

set(move5d "click (0,1,0,2)\nclick (0,3,0,2)")
foreach(v hyper4 t6 torus3d 5d)
  set(extra)
  if(v STREQUAL "5d")
    set(extra --script "${move5d}")
  endif()
  # Mid-deploy and settled are different pictures, and each is reproducible.
  shot(${v}-deploy ${v} --lab 1 --deploy-t 1.0 ${extra})
  shot(${v}-deploy-again ${v} --lab 1 --deploy-t 1.0 ${extra})
  shot(${v}-lab ${v} --lab 1 ${extra})
  if(NOT hash_${v}-deploy STREQUAL hash_${v}-deploy-again)
    message(FATAL_ERROR "${v}: two deploy captures at 1.0 s differ")
  endif()
  if(hash_${v}-deploy STREQUAL hash_${v}-lab)
    message(FATAL_ERROR "${v}: the deploy at 1.0 s shows the settled game")
  endif()
  # Lab off: byte-identical to the default capture.
  shot(${v}-off ${v} --lab 0 ${extra})
  shot(${v}-default ${v} ${extra})
  if(NOT hash_${v}-off STREQUAL hash_${v}-default)
    message(FATAL_ERROR "${v}: --lab 0 differs from the default capture")
  endif()
  if(hash_${v}-off STREQUAL hash_${v}-lab)
    message(FATAL_ERROR "${v}: the lab game shows no lab")
  endif()
endforeach()

# A layout change moves the hardware: mid-move differs from the end, the end is the
# snapped layout, and it reproduces.
set(axes "axes file,aeon vertical")
shot(relayout-0.5 hyper4 --lab 1 --script "${axes}" --relayout-t 0.5)
shot(relayout-0.5-again hyper4 --lab 1 --script "${axes}" --relayout-t 0.5)
shot(relayout-9 hyper4 --lab 1 --script "${axes}" --relayout-t 9)
shot(relayout-snap hyper4 --lab 1 --script "${axes}")
if(NOT hash_relayout-0.5 STREQUAL hash_relayout-0.5-again)
  message(FATAL_ERROR "two relayout captures at 0.5 s differ")
endif()
if(hash_relayout-0.5 STREQUAL hash_relayout-9)
  message(FATAL_ERROR "the relayout does not move")
endif()
if(NOT hash_relayout-9 STREQUAL hash_relayout-snap)
  message(FATAL_ERROR "a finished relayout differs from the snapped layout")
endif()

# The 5D arrival: in flight, then booted.
shot(arrive-0.8 5d --lab 1 --script "${move5d}" --relayout-t 0.8)
shot(arrive-9 5d --lab 1 --script "${move5d}" --relayout-t 9)
if(hash_arrive-0.8 STREQUAL hash_arrive-9)
  message(FATAL_ERROR "the 5d arrival does not move")
endif()

# --relayout-t needs the lab.
execute_process(COMMAND "${GUI}" hyper4 ${common} --relayout-t 1
  --shot "${OUT_DIR}/lab-multislab/x.ppm"
  WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(rc EQUAL 0)
  message(FATAL_ERROR "--relayout-t without --lab 1 was accepted")
endif()
