# SPDX-License-Identifier: GPL-3.0-or-later
find_package(tomlplusplus REQUIRED)

cb_layer(endspielzero_io LEVEL 90
  SOURCES notation.cpp fen.cpp variant_toml.cpp variant_doc.cpp ascii_board.cpp rule_parse.cpp
          game_file.cpp piece_model_toml.cpp rule_catalogue.cpp
  DEPS endspielzero_assets endspielzero_movegen endspielzero_view endspielzero_game endspielzero_rules
  SYSTEM_DEPS tomlplusplus::tomlplusplus)
