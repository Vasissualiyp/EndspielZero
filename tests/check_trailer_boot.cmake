# SPDX-License-Identifier: GPL-3.0-or-later
# The trailer's first board powers on on the trailer's own clock (M23.10 section 3): a
# menu capture pinned inside the boot window is reproducible and differs from one taken
# after it, and a second pinned time inside the window differs again.
set(dir "${OUT_DIR}/trailer-boot")
file(MAKE_DIRECTORY "${dir}")
function(capture name t)
  execute_process(COMMAND "${GUI}" --screen menu --theme manifold --width 640 --height 400
                          --t ${t} --shot "${dir}/${name}.ppm"
                  WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE rc
                  OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "trailer boot capture ${name} failed: ${err}")
  endif()
  file(SHA256 "${dir}/${name}.ppm" hash)
  set(hash_${name} "${hash}" PARENT_SCOPE)
endfunction()
capture(early 0.6)
capture(early-again 0.6)
capture(mid 1.2)
capture(late 4.0)
if(NOT hash_early STREQUAL hash_early-again)
  message(FATAL_ERROR "a pinned trailer boot frame is not reproducible")
endif()
if(hash_early STREQUAL hash_late OR hash_mid STREQUAL hash_late OR hash_early STREQUAL hash_mid)
  message(FATAL_ERROR "the trailer boot did not change the board between 0.6, 1.2 and 4.0 s")
endif()
