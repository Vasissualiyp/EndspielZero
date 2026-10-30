# SPDX-License-Identifier: GPL-3.0-or-later
#
# The baseline opponent (M10.2-M10.3): a derived evaluator and a small alpha-beta search
# over the game's own legality, so every variant - rules, seams, timelines - gets a legal
# opponent with no per-variant code. Integer-only, like the core it sits on.
cb_layer(endspielzero_ai LEVEL 85
  SOURCES eval.cpp search.cpp
  DEPS endspielzero_game)
