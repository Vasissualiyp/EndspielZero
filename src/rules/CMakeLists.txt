# SPDX-License-Identifier: GPL-3.0-or-later
#
# The rule VM sits above the position and below move generation. It deliberately cannot
# see a move generator: anything needing one (such as "does this piece have a capture")
# is asked through a callback the orchestrating layer supplies, which keeps the
# dependency pointing down.
cb_layer(endspielzero_rules LEVEL 45
  SOURCES expr.cpp rule.cpp vm.cpp validate.cpp
  DEPS endspielzero_position)
