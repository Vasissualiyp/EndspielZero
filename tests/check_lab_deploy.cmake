# SPDX-License-Identifier: GPL-3.0-or-later
# M24.5: a game happens in the lab. The deploy is pinned with --deploy-t, the stow with
# --stow-t; the lab-off game frame must not see any of it.
file(MAKE_DIRECTORY "${OUT_DIR}/lab-deploy")
set(common standard --theme manifold --width 640 --height 360)
function(shot name)
  execute_process(COMMAND "${GUI}" ${common} ${ARGN}
    --shot "${OUT_DIR}/lab-deploy/${name}.ppm"
    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} capture failed: ${err}")
  endif()
  file(SHA256 "${OUT_DIR}/lab-deploy/${name}.ppm" h)
  set(hash_${name} ${h} PARENT_SCOPE)
endfunction()
# The same for another variant (`common` names standard).
function(shot_of name variant)
  execute_process(COMMAND "${GUI}" ${variant} --theme manifold --width 640 --height 360
    ${ARGN} --shot "${OUT_DIR}/lab-deploy/${name}.ppm"
    WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} capture failed: ${err}")
  endif()
  file(SHA256 "${OUT_DIR}/lab-deploy/${name}.ppm" h)
  set(hash_${name} ${h} PARENT_SCOPE)
endfunction()

# Four ages of the deploy are four different pictures: the menu dissolving, the slab
# carried, the lock, the booted game.
set(ages 0.1 1.0 1.6 3.2)
foreach(t IN LISTS ages)
  shot(deploy-${t} --lab 1 --deploy-t ${t})
endforeach()
foreach(a IN LISTS ages)
  foreach(b IN LISTS ages)
    if(NOT a STREQUAL b AND hash_deploy-${a} STREQUAL hash_deploy-${b})
      message(FATAL_ERROR "deploy frames at ${a} s and ${b} s are identical")
    endif()
  endforeach()
endforeach()

# Determinism: the same age twice is the same picture.
shot(deploy-1.0-again --lab 1 --deploy-t 1.0)
if(NOT hash_deploy-1.0 STREQUAL hash_deploy-1.0-again)
  message(FATAL_ERROR "two deploy captures at 1.0 s differ")
endif()

# Settled and booted: the last lock (3.5 s, M25.3's heavier slab) plus the 1.8 s boot is
# 5.3 s, so 5.6 s equals the long-settled game, and that is a plain lab game.
shot(deploy-5.6 --lab 1 --deploy-t 5.6)
shot(deploy-9 --lab 1 --deploy-t 9)
shot(lab-game --lab 1)
if(NOT hash_deploy-5.6 STREQUAL hash_deploy-9)
  message(FATAL_ERROR "the deploy has not settled by 5.6 s")
endif()
if(NOT hash_deploy-9 STREQUAL hash_lab-game)
  message(FATAL_ERROR "a settled deploy differs from the lab game")
endif()

# The stow: shutting down, travelling and home are different pictures.
shot(stow-0.5 --lab 1 --stow-t 0.5)
shot(stow-1.5 --lab 1 --stow-t 1.5)
shot(stow-9 --lab 1 --stow-t 9)
if(hash_stow-0.5 STREQUAL hash_stow-1.5 OR hash_stow-1.5 STREQUAL hash_stow-9)
  message(FATAL_ERROR "stow frames do not move")
endif()

# M25.3: a game in the shape view deploys flat; the shape forms only once the slab has
# locked. Mid-travel the torus frame is the flat sheet; settled it is the formed shape.
shot_of(torus-travel torus --geometry --lab 1 --deploy-t 2.2)
shot_of(torus-travel-again torus --geometry --lab 1 --deploy-t 2.2)
shot_of(torus-settled torus --geometry --lab 1 --deploy-t 9)
if(NOT hash_torus-travel STREQUAL hash_torus-travel-again)
  message(FATAL_ERROR "two shape-view deploy captures at 2.2 s differ")
endif()
if(hash_torus-travel STREQUAL hash_torus-settled)
  message(FATAL_ERROR "the shape-view deploy does not move")
endif()

# The lab off: a game frame is the one captures have always made (the default is off).
shot(game-lab-off --lab 0)
shot(game-default)
if(NOT hash_game-lab-off STREQUAL hash_game-default)
  message(FATAL_ERROR "--lab 0 game frame differs from the default capture")
endif()
if(hash_game-lab-off STREQUAL hash_lab-game)
  message(FATAL_ERROR "the lab game frame shows no lab")
endif()

# The rig flags need the lab.
execute_process(COMMAND "${GUI}" ${common} --deploy-t 1 --shot "${OUT_DIR}/lab-deploy/x.ppm"
  WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
if(rc EQUAL 0)
  message(FATAL_ERROR "--deploy-t without --lab 1 was accepted")
endif()
