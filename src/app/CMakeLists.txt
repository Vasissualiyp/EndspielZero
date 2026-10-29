# SPDX-License-Identifier: GPL-3.0-or-later
#
# Interaction logic with no front end in it: input is actions, output is a snapshot. The
# SDL window and the tests drive exactly the same object.
cb_layer(endspielzero_app LEVEL 95
  SOURCES session.cpp settings.cpp shell.cpp editor.cpp hotseat.cpp overture.cpp game_runner.cpp embed.cpp camera_track.cpp
  DEPS endspielzero_io endspielzero_game endspielzero_view endspielzero_audio)
