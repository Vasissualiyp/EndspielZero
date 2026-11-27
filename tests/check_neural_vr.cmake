# SPDX-License-Identifier: GPL-3.0-or-later
# M25.5 the neural VR: a game with more slabs than the rack holds hands off to the
# implant's space - deterministic captures of the entering and leaving handoffs, the
# space itself, a relayout gliding, and the 2-D implant picture. Lab off is untouched.
file(MAKE_DIRECTORY "${OUT_DIR}/neural-vr")
set(common --theme manifold --width 480 --height 270)
function(shot name)
  execute_process(COMMAND "${GUI}" ${common} ${ARGN}
    --shot "${OUT_DIR}/neural-vr/${name}.ppm"
    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} capture failed: ${err}")
  endif()
  file(SHA256 "${OUT_DIR}/neural-vr/${name}.ppm" h)
  set(hash_${name} ${h} PARENT_SCOPE)
endfunction()
function(differ a b why)
  if(hash_${a} STREQUAL hash_${b})
    message(FATAL_ERROR "${a} and ${b} are the same picture: ${why}")
  endif()
endfunction()
function(same a b why)
  if(NOT hash_${a} STREQUAL hash_${b})
    message(FATAL_ERROR "${a} and ${b} differ: ${why}")
  endif()
endfunction()

# The space: hyper4 and t6 go there by count, reproducibly; --vr 0 keeps the rig.
foreach(v hyper4 t6)
  shot(${v}-vr ${v} --lab 1)
  shot(${v}-vr-again ${v} --lab 1)
  shot(${v}-rig ${v} --lab 1 --vr 0)
  same(${v}-vr ${v}-vr-again "two captures of the implant's space")
  differ(${v}-vr ${v}-rig "the space is not the rig")
endforeach()
# --vr 1 puts a game that fits the rack there too.
shot(standard-rig standard --lab 1)
shot(standard-vr standard --lab 1 --vr 1)
differ(standard-rig standard-vr "--vr 1 starts inside")

# Entering follows the physical stand-in's lock. Keep samples in the same
# semantic phases when that shared gantry path is retimed (M25.3b).
include("${CMAKE_CURRENT_LIST_DIR}/rig_timing.cmake")
rig_milliseconds(rig_deploy kDeployFirstDeparture departure_ms)
rig_milliseconds(rig_deploy kSlabLockSeconds travel_ms)
rig_milliseconds(neural_vr kVrLidsCloseSeconds close_ms)
rig_milliseconds(neural_vr kVrDarkSeconds dark_ms)
rig_milliseconds(neural_vr kVrHorizonSeconds horizon_ms)
# kVrLidsCloseAt = kVrSlabLock + 1.5 s, kVrHandshakeAt = lock + 0.8 s.
math(EXPR lock_ms "${departure_ms} + ${travel_ms}")
math(EXPR code_ms "${lock_ms} + 1000")
math(EXPR black_ms "${lock_ms} + 1500 + ${close_ms} + ${dark_ms} / 2")
math(EXPR opened_ms "${lock_ms} + 1500 + ${close_ms} + ${dark_ms} + ${horizon_ms} + 1000")
rig_seconds(${code_ms} code_age)
rig_seconds(${black_ms} black_age)
rig_seconds(${opened_ms} opened_age)
shot(enter-travel hyper4 --lab 1 --vr-t 1.0)
shot(enter-code hyper4 --lab 1 --vr-t ${code_age})
shot(enter-black hyper4 --lab 1 --vr-t ${black_age})
shot(enter-open hyper4 --lab 1 --vr-t ${opened_age})
shot(enter-code-again hyper4 --lab 1 --vr-t ${code_age})
same(enter-code enter-code-again "the handoff is pure in its age")
differ(enter-travel enter-code "the code shows after the lock")
differ(enter-code enter-black "the eyes have closed")
differ(enter-black enter-open "the horizon has opened")

# Leaving: the space fades, the eyes open on the room and the code, the slab stows.
foreach(t 0.1 1.5 4.0)
  shot(leave-${t} hyper4 --lab 1 --stow-t ${t})
endforeach()
differ(leave-0.1 leave-1.5 "the room is back")
differ(leave-1.5 leave-4.0 "the slab is on its way home")

# A relayout glides: mid-glide differs from the end, and the end is the snapped layout.
set(axes "axes file,aeon vertical")
shot(relayout-0.3 hyper4 --lab 1 --script "${axes}" --relayout-t 0.3)
shot(relayout-9 hyper4 --lab 1 --script "${axes}" --relayout-t 9)
shot(relayout-snap hyper4 --lab 1 --script "${axes}")
differ(relayout-0.3 relayout-9 "the boards move")
same(relayout-9 relayout-snap "a finished glide lands on the layout")

# 2D: the implant's own picture, physical or VR.
shot(hyper4-flat hyper4 --lab 1 --flat)
shot(standard-flat standard --lab 1 --flat)
differ(hyper4-flat hyper4-vr "2D is its own picture")
differ(standard-flat standard-rig "2D leaves the room")

# Lab off: the capture it always was.
shot(hyper4-off hyper4 --lab 0)
shot(hyper4-default hyper4)
same(hyper4-off hyper4-default "--lab 0 is the default capture")

# Full darkness includes the neural HUD/check overlay, even on T6 (starts in check).
set(common --theme manifold --width 96 --height 54)
shot(black-t6 t6 --lab 1 --vr-t ${black_age})
file(READ "${OUT_DIR}/neural-vr/black-t6.ppm" black HEX)
string(LENGTH "P6\n96 54\n255\n" header)
math(EXPR start "${header} * 2")
string(SUBSTRING "${black}" ${start} -1 pixels)
if(pixels MATCHES "[1-9a-fA-F]")
  message(FATAL_ERROR "VR darkness still shows the scene or neural HUD")
endif()

# The UI clock must keep the second half of a calm handoff, including on leave when
# the destination menu would normally stop drawing the outgoing game immediately.
foreach(mode enter leave)
  if(mode STREQUAL "enter")
    set(flag --vr-t)
  else()
    set(flag --stow-t)
  endif()
  foreach(age 0.3 0.45 0.6)
    shot(calm-${mode}-${age} hyper4 --lab 1 --presentation 0 ${flag} ${age})
  endforeach()
  same(calm-${mode}-0.3 black-t6 "calm midpoint must also be fully black")
  differ(calm-${mode}-0.3 calm-${mode}-0.45 "calm handoff visibly reopens")
  differ(calm-${mode}-0.45 calm-${mode}-0.6 "calm handoff has not finished at 0.45 s")
endforeach()
