# SPDX-License-Identifier: GPL-3.0-or-later
#
# The multiverse sits above the position and below the game: it decides which boards
# exist, which timelines are active, and where the present line is - policy over the
# ordinary lattice axes (ADR-0007), with no move generation of its own.
cb_layer(endspielzero_temporal LEVEL 70
  SOURCES timeline.cpp multiverse.cpp
  DEPS endspielzero_position)
