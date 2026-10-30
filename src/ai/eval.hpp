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

  /// The mop-up term's weights: per royal move the lone royal is short of the best cell,
  /// and per royal step of distance between the two royals beyond the nearest. Together
  /// they stay well under a minor piece but far above the mobility term, so the search
  /// always has a gradient to climb: edge the lone king, bring the own king up.
  static constexpr int kMopEdgeWeight = 18;
  static constexpr int kMopDistWeight = 9;

  /// Whether the mop-up term has what it needs: exactly one royal piece type, and a
  /// board small enough to tabulate. Exposed for tests.
  [[nodiscard]] bool hasMopUp() const noexcept { return royalType_ != kNoPiece; }
  /// The number of pseudo-legal moves of a lone royal on `c` of an empty board; 0 when
  /// there is no mop-up table.
  [[nodiscard]] int royalMobility(CellId c) const noexcept {
    return c < royalMobility_.size() ? royalMobility_[c] : 0;
  }

 private:
  /// The mop-up score for the side to move `us`, or 0 when the position does not call
  /// for it (see eval.cpp). The materials are each colour's non-royal total.
  [[nodiscard]] bool mopUp(const Position& p, Color us, int ourMaterial,
                           int theirMaterial, int& out) const;
  void buildRoyalTables();

  const VariantSpec* v_;
  MoveGen gen_;
  std::vector<int> values_;
  std::vector<int> mobility_;
  /// The sole royal piece type, or kNoPiece when the variant has none or several (no
  /// mop-up then).
  PieceTypeId royalType_{kNoPiece};
  std::vector<std::uint8_t> royalMobility_;  ///< per cell, lone royal on an empty board
  int maxRoyalMobility_{0};
  /// dist_[a * cells + b]: royal moves from a to b, 255 when unreachable. Empty when the
  /// board has more than kMaxDistCells cells.
  std::vector<std::uint8_t> dist_;
  int maxDist_{0};
  /// The material gap at which a side is "winning on material" for the mop-up term.
  int mopGap_{0};
};

}  // namespace cb::ai
