// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The TOML spelling of a piece's authored body and icon (M7.3.4), shared by the loader
// and the editor's document so the two can never disagree about it.
//
//   [piece.model]
//   segments = 20
//   symmetry = "kfold"        # full | mirror | kfold
//   k = 5                     # kfold only
//   profile = [[0, 0], [330, 0], ...]
//     [[piece.model.element]]
//     at = [250, 810]         # radius, height
//     outline = [[0, 0], [90, 130], ...]
//
//   [piece.icon]
//   fills = [[[50, 10], [80, 90], [20, 90]]]
//   cuts = []
//
// Every number is integer permille, exactly as `assets::` holds it. Neither table enters
// `VariantId`: how a piece looks is not what it is.

#include <toml++/toml.hpp>

#include "assets/piece_model.hpp"
#include "base/result.hpp"

namespace cb {

[[nodiscard]] Result<assets::PieceModel> pieceModelFromToml(const toml::table& t);
[[nodiscard]] Result<assets::IconModel> pieceIconFromToml(const toml::table& t);

[[nodiscard]] toml::table pieceModelToToml(const assets::PieceModel& m);
[[nodiscard]] toml::table pieceIconToToml(const assets::IconModel& m);

}  // namespace cb
