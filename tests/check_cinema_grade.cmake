# SPDX-License-Identifier: GPL-3.0-or-later
#
# M18.3: `--cinema` adds a vignette and a grade; ordinary play must not. Render the torus
# as its own shape - the ring leaves the frame centre as page, so a corner-vs-centre
# reading is a clean measurement of the vignette - once with `--cinema` and once without,
# and assert the cinema corners are measurably darker than its centre while the ungraded
# frame stays flat. Invoked as:
#   cmake -DGUI=... -DWORKDIR=... -DOUT_CINEMA=... -DOUT_PLAIN=... -P this.cmake

function(shot out)
  execute_process(COMMAND "${GUI}" torus --geometry --width 640 --height 360
                          --shot "${out}" ${ARGN}
                  WORKING_DIRECTORY "${WORKDIR}"
                  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "shot ${out} failed (${rc}): ${err}")
  endif()
endfunction()

# The PPM size, from writePpm's `P6\n<w> <h>\n255\n` header.
function(ppm_size path w h)
  file(READ "${path}" header LIMIT 32)
  string(REGEX MATCH "^P6[ \t\r\n]+([0-9]+)[ \t\r\n]+([0-9]+)" _ "${header}")
  if(NOT CMAKE_MATCH_1 OR NOT CMAKE_MATCH_2)
    message(FATAL_ERROR "not a binary PPM: '${header}'")
  endif()
  set(${w} ${CMAKE_MATCH_1} PARENT_SCOPE)
  set(${h} ${CMAKE_MATCH_2} PARENT_SCOPE)
endfunction()

# Average luminance of a `side`-squared block at (x0, y0), in thousandths of a level.
# Decoding the handful of pixels straight out of the hex dump keeps this script free of
# any tool the test machine might not have.
function(block_luma path w h x0 y0 side out)
  file(READ "${path}" hex HEX)
  set(prefix "P6\n${w} ${h}\n255\n")
  string(LENGTH "${prefix}" offset)
  set(acc 0)
  set(n 0)
  math(EXPR yend "${y0} + ${side} - 1")
  math(EXPR xend "${x0} + ${side} - 1")
  foreach(y RANGE ${y0} ${yend})
    foreach(x RANGE ${x0} ${xend})
      math(EXPR idx "((${y} * ${w}) + ${x}) * 3 + ${offset}")
      math(EXPR hi "${idx} * 2")
      string(SUBSTRING "${hex}" ${hi} 2 rhex)
      math(EXPR hi "${idx} * 2 + 2")
      string(SUBSTRING "${hex}" ${hi} 2 ghex)
      math(EXPR hi "${idx} * 2 + 4")
      string(SUBSTRING "${hex}" ${hi} 2 bhex)
      math(EXPR r "0x${rhex}")
      math(EXPR g "0x${ghex}")
      math(EXPR b "0x${bhex}")
      math(EXPR lum "(299 * ${r} + 587 * ${g} + 114 * ${b}) / 1000")
      math(EXPR acc "${acc} + ${lum}")
      math(EXPR n "${n} + 1")
    endforeach()
  endforeach()
  math(EXPR avg "${acc} / ${n}")
  set(${out} ${avg} PARENT_SCOPE)
endfunction()

# Corner average and frame-centre average, each as thousandths of a level.
function(readings path out_corner out_centre)
  ppm_size("${path}" w h)
  set(side 8)
  math(EXPR lastx "${w} - ${side}")
  math(EXPR lasty "${h} - ${side}")
  math(EXPR midx "${w} / 2 - ${side} / 2")
  math(EXPR midy "${h} / 2 - ${side} / 2")
  block_luma("${path}" ${w} ${h} 0 0 ${side} tl)
  block_luma("${path}" ${w} ${h} ${lastx} 0 ${side} tr)
  block_luma("${path}" ${w} ${h} 0 ${lasty} ${side} bl)
  block_luma("${path}" ${w} ${h} ${lastx} ${lasty} ${side} br)
  block_luma("${path}" ${w} ${h} ${midx} ${midy} ${side} centre)
  math(EXPR corner "(${tl} + ${tr} + ${bl} + ${br}) / 4")
  set(${out_corner} ${corner} PARENT_SCOPE)
  set(${out_centre} ${centre} PARENT_SCOPE)
endfunction()

shot("${OUT_CINEMA}" --cinema)
shot("${OUT_PLAIN}")

readings("${OUT_CINEMA}" cinema_corner cinema_centre)
readings("${OUT_PLAIN}" plain_corner plain_centre)

message(STATUS "cinema: corner=${cinema_corner} centre=${cinema_centre}")
message(STATUS "plain : corner=${plain_corner} centre=${plain_centre}")

# The vignette darkens the page in the corners. On the dark manifold theme the
# page is only ~11/255 to begin with, so a 30-level gap (calibrated for the
# former light theme) is impossible. The graded centre must still be measurably
# brighter than its corners, and the ungraded page must remain flat.
math(EXPR dark "${cinema_centre} - ${cinema_corner}")
if(dark LESS 2)
  message(FATAL_ERROR "cinema corners are only ${dark}/1000 darker than the centre")
endif()

# Ordinary play carries neither the vignette nor the grade: corner and centre page are the
# same brightness as each other.
math(EXPR spread "${plain_corner} - ${plain_centre}")
if(spread LESS 0)
  math(EXPR spread "0 - ${spread}")
endif()
if(spread GREATER 6)
  message(FATAL_ERROR "non-cinema frame is not flat: corner=${plain_corner} centre=${plain_centre}")
endif()

message(STATUS "cinema vignette present (${dark}/1000); ungraded frame flat (${spread}/1000)")
