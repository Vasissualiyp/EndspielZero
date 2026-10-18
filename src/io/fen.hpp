// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "base/result.hpp"
#include "position/position.hpp"

namespace cb {

/// FEN-N: a generalized position serialization that is byte-identical to standard
/// FEN for an ordinary 2-D 8x8 board, so the enormous existing corpus of chess
/// positions is directly usable as test data (M1.7).
///
/// The generalization: cells are written in row-major order with axis 1
/// descending (matching FEN's rank 8 first) and axis 0 ascending. Axis 1 groups
/// are separated by '/', axis 2 groups by '|', axis 3 by '||', and so on.
///
/// An optional seventh field lists the cells of the *unmoved* pieces (or "-" for none), for
/// the piece types some move of which depends on that. Without it, a piece has moved unless
/// it stands where the variant's start position put one - which is what a standard FEN
/// assumes of pawns - and `toFen` writes the field only when a position is not that.
std::string toFen(const Position& p);

/// Parse FEN-N against a variant. Accepts standard FEN for 2-D variants.
Result<Position> fromFen(const VariantSpec& v, std::string_view fen);

}  // namespace cb
