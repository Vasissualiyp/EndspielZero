# SPDX-License-Identifier: GPL-3.0-or-later
cb_layer(endspielzero_base LEVEL 0
  SOURCES result.cpp zobrist.cpp
  DEPS)
target_link_libraries(endspielzero_base PUBLIC cb_flags)
