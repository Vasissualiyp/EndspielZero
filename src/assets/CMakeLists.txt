# SPDX-License-Identifier: GPL-3.0-or-later
#
# Authored content: the vector description of a piece's body and of its flat icon.
# Integer-only and dependency-free on purpose - a model is saved, shared and eventually
# signed, so two machines have to agree on it byte for byte. It sits below the editor and
# below the renderer because both need it, and above nothing but base.
cb_layer(endspielzero_assets LEVEL 25
  SOURCES piece_model.cpp
  DEPS endspielzero_base)
