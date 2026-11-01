# SPDX-License-Identifier: GPL-3.0-or-later
#
# Presentation, not engine: this is the first layer allowed to use floating point,
# because world-space positions are inherently continuous. The arch test's no-float
# rule covers the core layers only, and that boundary is the point.
cb_layer(endspielzero_view LEVEL 60
  SOURCES layout.cpp snapshot.cpp camera.cpp seams.cpp move_anim.cpp move_camera.cpp
  board_boot.cpp cell_distances.cpp check_ripple.cpp game_end_fx.cpp
  DEPS endspielzero_position)
