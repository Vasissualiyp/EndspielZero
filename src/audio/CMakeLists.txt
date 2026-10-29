# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sound. A separate, optional layer: the engine, the CLI and the tests all build and pass
# without SDL3 present, and the game simply stays silent. SDL3 is already a hard dependency
# of the gfx shell (flake.nix), so this adds no new library.
find_package(SDL3 CONFIG)

cb_layer(endspielzero_audio LEVEL 93
  SOURCES audio.cpp
  DEPS endspielzero_base)

if(SDL3_FOUND)
  # Private: the SDL headers stay out of `app`/`render`, which only need the Sound enum
  # and the free `play`. The final executables link SDL3 through this dependency.
  target_link_libraries(endspielzero_audio PRIVATE SDL3::SDL3)
  target_compile_definitions(endspielzero_audio PRIVATE CB_HAVE_SDL_AUDIO=1)
endif()
