# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Catch2 3 REQUIRED)

file(GLOB_RECURSE CB_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/render/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/property/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/golden/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/perft/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/arch/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/oracle/*.cpp)

add_executable(endspielzero_tests ${CB_TEST_SOURCES})
target_link_libraries(endspielzero_tests PRIVATE
  Catch2::Catch2WithMain cb_flags ${CB_ALL_LAYER_TARGETS})
target_include_directories(endspielzero_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(endspielzero_tests PRIVATE
  CB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  CB_BINARY_DIR="${CMAKE_BINARY_DIR}")

# The renderer is optional, so its tests compile to nothing when Vulkan is absent - the
# rest of the suite still builds and passes on a machine with no graphics stack.
if(TARGET endspielzero_render)
  target_compile_definitions(endspielzero_tests PRIVATE CB_HAVE_VULKAN=1)
endif()

# Aggregate, label-addressable suites: `ctest -L unit` is the fast loop.
# Labels are added as the first test of that kind lands, so an empty label can
# never silently "pass". golden/perft joined in M1.
foreach(label unit property golden perft arch render)
  add_test(NAME ${label} COMMAND endspielzero_tests "[${label}]~[slow]")
  set_tests_properties(${label} PROPERTIES LABELS ${label})
endforeach()

# The deep node counts are minutes of work in Release and over an hour in Debug, so
# they are their own `slow` label rather than a tax on every `ctest --preset dev`. The
# test presets exclude that label; ask for it with `ctest --preset release-slow`.
add_test(NAME perft-slow COMMAND endspielzero_tests "[perft][slow]")
set_tests_properties(perft-slow PROPERTIES LABELS "slow;perft" TIMEOUT 3600)
add_test(NAME property-slow COMMAND endspielzero_tests "[property][slow]")
set_tests_properties(property-slow PROPERTIES LABELS "slow;property" TIMEOUT 3600)
set_tests_properties(property PROPERTIES TIMEOUT 900)

# The interface, end to end: the real binary renders one frame - board, rails, ledger,
# promotion state - into an image with no display present, and fails if the Vulkan
# validation layers say anything. It is the only automated check that the panels and the
# board agree about where the board is, and it needs no compositor to run.
if(TARGET endspielzero_gui)
  # Every screen, not just the board: a menu that fails to lay out, or a panel that makes
  # the validation layers complain, is caught here rather than by someone opening it.
  foreach(screen board welcome menu newgame quit pause settings editor info pieces
                 watch)
    add_test(NAME gui-${screen}
      COMMAND endspielzero_gui standard
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-${screen}.ppm
              --screen ${screen} --script "click e2"
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-${screen} PROPERTIES LABELS "render;gui" TIMEOUT 120
      ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
  # The grid floor takes both paths: the shell's background list and the board's
  # early list. Exercise the actual alternate-theme pipelines with validation on.
  foreach(screen board newgame)
    add_test(NAME gui-manifold-${screen}
      COMMAND endspielzero_gui standard --theme manifold --screen ${screen}
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-manifold-${screen}.ppm
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-manifold-${screen} PROPERTIES LABELS "render;gui"
      TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
  add_test(NAME gui-visual-time
    COMMAND ${CMAKE_COMMAND} -DGUI=$<TARGET_FILE:endspielzero_gui>
            -DWORKDIR=${PROJECT_SOURCE_DIR}
            -DOUT_DIR=${CMAKE_BINARY_DIR}/render-captures
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_visual_time.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-visual-time PROPERTIES LABELS "render;gui" TIMEOUT 180
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  add_test(NAME gui-live-controls
    COMMAND ${CMAKE_COMMAND} -DGUI=$<TARGET_FILE:endspielzero_gui>
            -DWORKDIR=${PROJECT_SOURCE_DIR}
            -DOUT_DIR=${CMAKE_BINARY_DIR}/render-captures
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_live_controls.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-live-controls PROPERTIES LABELS "render;gui" TIMEOUT 180
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  add_test(NAME gui-capture-hud
    COMMAND ${CMAKE_COMMAND} -DGUI=$<TARGET_FILE:endspielzero_gui>
            -DWORKDIR=${PROJECT_SOURCE_DIR}
            -DOUT_DIR=${CMAKE_BINARY_DIR}/render-captures
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_capture_hud.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-capture-hud PROPERTIES LABELS "render;gui" TIMEOUT 180
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # The play board as its own shape (M17): a glued variant drawn on its surface, still
  # validation-clean and still a picture. `standard --geometry` is a no-op, checked in the
  # scene tests rather than here because it is about geometry, not about the window.
  add_test(NAME gui-geometry
    COMMAND endspielzero_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-geometry.ppm
            --geometry --script "click h1"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-geometry PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  foreach(variant standard torus)
    if(variant STREQUAL "torus")
      set(woodFlags --geometry)
    else()
      set(woodFlags)
    endif()
    add_test(NAME gui-wood-${variant}
      COMMAND endspielzero_gui ${variant} --width 640 --height 360
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-wood-${variant}.ppm
              ${woodFlags} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-wood-${variant} PROPERTIES LABELS "render;gui" TIMEOUT 120
      ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
  add_test(NAME gui-wood-contrast
    COMMAND ${CMAKE_COMMAND} -DGUI=$<TARGET_FILE:endspielzero_gui>
            -DWORKDIR=${PROJECT_SOURCE_DIR}
            -DOUT_DIR=${CMAKE_BINARY_DIR}/render-captures
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_wood.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-wood-contrast PROPERTIES LABELS "render;gui" TIMEOUT 180
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  add_test(NAME gui-onboarding-scale
    COMMAND ${CMAKE_COMMAND} -DGUI=$<TARGET_FILE:endspielzero_gui>
            -DWORKDIR=${PROJECT_SOURCE_DIR}
            -DOUT_DIR=${CMAKE_BINARY_DIR}/render-captures
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_ui_scale.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-onboarding-scale PROPERTIES LABELS "render;gui" TIMEOUT 180
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # The eversion flag at its halfway point: with the revised invert this is the rest
  # shape (the swap happens past 0.5), so it is a plain validation capture of the pose.
  add_test(NAME gui-evert
    COMMAND endspielzero_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-evert.ppm
            --geometry --evert 0.5
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-evert PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # M18.6: a mid-morph frame - the board part-way between flat and its shape - is a real
  # mesh, not a half-built one: validation-clean and a picture at 0.5.
  add_test(NAME gui-formed
    COMMAND endspielzero_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-formed.ppm
            --geometry --formed 0.5
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-formed PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # And slid round itself: half a cell along each axis, which is where no square sits
  # where the unposed board put it and every one of them is still on the surface.
  add_test(NAME gui-slide
    COMMAND endspielzero_gui klein
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-slide.ppm
            --geometry --slide 2.5 --slide-v 1.5
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-slide PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # Fully inside out: what was the hole is now the outside, and every piece stands on the
  # outside with it (M17.7). Validation-clean and a picture, not a blank or a tangle.
  add_test(NAME gui-invert
    COMMAND endspielzero_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-invert.ppm
            --geometry --evert 1
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-invert PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # A ghosted board: the far side of the ring shows through the near side (M17.10).
  add_test(NAME gui-ghost
    COMMAND endspielzero_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-ghost.ppm
            --geometry --ghost 0.4
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-ghost PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # The shapes above two dimensions (M17.12): `torus3d`'s nested shells and `hyper4`'s
  # tesseract, validation-clean and a picture.
  foreach(shape torus3d hyper4)
    add_test(NAME gui-geometry-${shape}
      COMMAND endspielzero_gui ${shape}
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-geometry-${shape}.ppm
              --geometry
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-geometry-${shape} PROPERTIES LABELS "render;gui"
      TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
  # M17.22: slide, invert and ghost now reach the D >= 3 stacked shapes too. Each is a
  # validation-clean frame of the documented behaviour stated through the ordinary flags.
  foreach(case torus3d-slide torus3d-invert torus3d-ghost hyper4-invert hyper4-ghost)
    string(REPLACE "-" ";" parts "${case}")
    list(GET parts 0 shape)
    list(GET parts 1 pose)
    if(pose STREQUAL "slide")
      set(poseFlags --slide 1 --slide-v 2)
    elseif(pose STREQUAL "invert")
      set(poseFlags --evert 1)
    else()  # ghost
      set(poseFlags --ghost 0.4)
    endif()
    add_test(NAME gui-geometry-${case}
      COMMAND endspielzero_gui ${shape}
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-geometry-${case}.ppm
              --geometry ${poseFlags}
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-geometry-${case} PROPERTIES LABELS "render;gui"
      TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
  # M12.6: the notation runner. A flat game and a glued shape game both export a
  # validation-clean numbered sequence; the shape one exercises the M17.19 choreography
  # through the runner. Small resolution and a low frame rate keep the suite quick.
  add_test(NAME gui-play-flat
    COMMAND endspielzero_gui --play ${PROJECT_SOURCE_DIR}/tests/data/games/standard.cbgame
            --follow route --cinema --clip ${CMAKE_BINARY_DIR}/render-captures/play-flat
            --fps 8 --width 400 --height 250
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-play-flat PROPERTIES LABELS "render;gui" TIMEOUT 300
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  add_test(NAME gui-play-shape
    COMMAND endspielzero_gui --play ${PROJECT_SOURCE_DIR}/tests/data/games/torus.cbgame
            --follow route --shape-follow chase --geometry --cinema
            --clip ${CMAKE_BINARY_DIR}/render-captures/play-shape
            --fps 6 --dwell 0.2 --width 400 --height 250
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-play-shape PROPERTIES LABELS "render;gui" TIMEOUT 300
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # The determinism contract, end to end: the same shape clip twice, compared frame by
  # frame. `|` separates the common args because a CMake list splits on `;`.
  add_test(NAME gui-play-deterministic
    COMMAND ${CMAKE_COMMAND}
            -DGUI=$<TARGET_FILE:endspielzero_gui>
            -DWORKDIR=${PROJECT_SOURCE_DIR}
            -DCOMMON=--play|${PROJECT_SOURCE_DIR}/tests/data/games/torus.cbgame|--follow|route|--shape-follow|chase|--geometry|--cinema|--fps|6|--dwell|0.2|--width|400|--height|250
            -DOUT_A=${CMAKE_BINARY_DIR}/render-captures/play-det-a
            -DOUT_B=${CMAKE_BINARY_DIR}/render-captures/play-det-b
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_clip_deterministic.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-play-deterministic PROPERTIES LABELS "render;gui" TIMEOUT 600
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # M12.8: the interactive watching state reached headlessly, so the mode is reviewable
  # the way every screen is. The flat game shows the ordinary HUD; the glued one is shown
  # as its shape with the follow choreography. Both must carry the STOP WATCHING control
  # and stay validation-clean.
  add_test(NAME gui-watch-state-flat
    COMMAND endspielzero_gui --watch opera
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-watch-state-flat.ppm
            --width 640 --height 400
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-watch-state-flat PROPERTIES LABELS "render;gui" TIMEOUT 180
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  add_test(NAME gui-watch-state-shape
    COMMAND endspielzero_gui --watch torus-wrap
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-watch-state-shape.ppm
            --width 640 --height 400
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-watch-state-shape PROPERTIES LABELS "render;gui" TIMEOUT 180
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # M18.3: cinema adds a vignette and a grade, ordinary play does not. The torus as its
  # own shape leaves the ring's centre as page, so corner-vs-centre reads the vignette
  # cleanly; the ungraded frame must stay flat.
  add_test(NAME gui-cinema-grade
    COMMAND ${CMAKE_COMMAND}
            -DGUI=$<TARGET_FILE:endspielzero_gui>
            -DWORKDIR=${PROJECT_SOURCE_DIR}
            -DOUT_CINEMA=${CMAKE_BINARY_DIR}/render-captures/gui-cinema-grade-cinema.ppm
            -DOUT_PLAIN=${CMAKE_BINARY_DIR}/render-captures/gui-cinema-grade-plain.ppm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_cinema_grade.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-cinema-grade PROPERTIES LABELS "render;gui" TIMEOUT 180
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # And a validation-clean `--shot --cinema` on a flat board and a shape.
  foreach(variant standard torus)
    add_test(NAME gui-cinema-${variant}
      COMMAND endspielzero_gui ${variant}
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-cinema-${variant}.ppm
              --cinema
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-cinema-${variant} PROPERTIES LABELS "render;gui"
      TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
  # The capture default resolution is 1920x1080 (M12.6); a regression to 1440x900 is
  # caught by reading the PPM header.
  add_test(NAME gui-default-resolution
    COMMAND ${CMAKE_COMMAND}
            -DGUI=$<TARGET_FILE:endspielzero_gui>
            -DWORKDIR=${PROJECT_SOURCE_DIR}
            -DOUT=${CMAKE_BINARY_DIR}/render-captures/gui-default-resolution.ppm
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_default_resolution.cmake
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-default-resolution PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # M18.5: a capture must not need an audio device. The capture path never initialises
  # audio, and the interactive mixer degrades to silence when a device is missing; this
  # runs with an audio driver that does not exist, so a regression that made a capture open
  # a device - or that made a failed open fatal - would fail here rather than in CI.
  add_test(NAME gui-capture-no-audio
    COMMAND endspielzero_gui standard
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-capture-no-audio.ppm
            --script "click e2"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-capture-no-audio PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIODRIVER=endspielzero-does-not-exist")
endif()
