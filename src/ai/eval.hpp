// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "game/game.hpp"

namespace cb::ai {

/// A position's worth, derived from the variant rather than written for it (M10.3).
///
/// Pieces are user-defined, so there is no table of piece values to consult. Instead a
/// piece is worth what it can *do*: its average number of moves, sampled on random
/// positions of the variant's own board - its own size, its own seams, its own number of
/// axes - with a scatter of blockers at about half the starting density. A knight is
/// worth less on a 4x4x4 torus than on an 8x8 box for exactly the reason a player would
/// give, and nobody had to say so.
///
/// Integer throughout (M10.4's no-float rule starts here): values are centipawn-like
/// units, deterministic for a given variant on any machine.
class Evaluator {
 public:
  explicit Evaluator(const VariantSpec& v);

  /// The derived material value of a piece type; 0 for a royal piece, which is never
  /// traded and is scored by the game's result instead.
  [[nodiscard]] int pieceValue(PieceTypeId t) const noexcept {
    return t < values_.size() ? values_[t] : 0;
  }

  /// The average number of moves the sampling found for a type, times 100 - the input
  /// the value is derived from, exposed for tests and for curious authors.
  [[nodiscard]] int mobilityCenti(PieceTypeId t) const noexcept {
    return t < mobility_.size() ? mobility_[t] : 0;
  }

  /// The position as seen by the side to move: its material minus the opponent's, plus
  /// a little for every move it has over the opponent. Positive is good for the mover.
  /// Mobility is the one positional idea that means the same thing on every board - it
  /// is what makes the derived values above - and without it a material-only opponent
  /// shuffles a knight back and forth because nothing tells it development is worth
  /// anything. Everything else - check, mate, hanging pieces - is the search's job.
  [[nodiscard]] int evaluate(const Position& p) const;

  /// Per pseudo-legal move of difference. Small beside material: a pawn is ~80.
  static constexpr int kMobilityWeight = 3;

 private:
  const VariantSpec* v_;
  MoveGen gen_;
  std::vector<int> values_;
  std::vector<int> mobility_;
};

}  // namespace cb::ai
