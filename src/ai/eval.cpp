// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/eval.hpp"

#include <algorithm>

#include "base/rng.hpp"

namespace cb::ai {
namespace {

constexpr int kSamples = 48;
/// value = kBase + kPerMove * average moves. Calibrated so standard chess comes out in
/// the familiar order and roughly the familiar ratios (pawn ~1, knight ~3, rook ~5,
/// queen ~9); the absolute scale is arbitrary, the ordering is what search needs.
constexpr int kBase = 40;
constexpr int kPerMove = 40;

}  // namespace

Evaluator::Evaluator(const VariantSpec& v)
    : v_(&v), gen_(v), values_(v.pieces.size(), 0), mobility_(v.pieces.size(), 0) {
  const MoveGen& gen = gen_;
  const std::uint32_t cells = v.dims.cellCount();
  if (cells == 0) return;
  Rng rng(v.variantId() ^ 0xA1A1A1A1ULL);
  // Blockers at about half the opening's density, so a slider is measured on a board
  // that looks like the middle of a game rather than an empty one.
  const std::uint32_t blockers =
      std::min<std::uint32_t>(cells / 2, static_cast<std::uint32_t>(v.start.size() / 4));
  // A blocker that cannot itself be captured or moved through is the plainest stand-in
  // for "something is there": the mover's own colour.
  PieceTypeId blockerType = kNoPiece;
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    if (!v.pieces[i].royal) {
      blockerType = static_cast<PieceTypeId>(i);
      break;
    }
  }
  MoveList moves(v.moveUpperBound());
  for (std::size_t t = 1; t < v.pieces.size(); ++t) {
    if (v.pieces[t].royal) continue;
    std::int64_t total = 0;
    for (int s = 0; s < kSamples; ++s) {
      Position p(v);
      const CellId at = rng.below(cells);
      p.place(at, static_cast<PieceTypeId>(t), Color::White);
      for (std::uint32_t b = 0; b < blockers && blockerType != kNoPiece; ++b) {
        const CellId c = rng.below(cells);
        if (p.isEmpty(c)) p.place(c, blockerType, Color::White);
      }
      moves.clear();
      gen.generatePseudoLegalFor(p, Color::White, /*includeCastles=*/false, moves);
      for (const Move& m : moves) {
        if (m.from == at) ++total;
      }
    }
    mobility_[t] = static_cast<int>(total * 100 / kSamples);
    values_[t] = kBase + static_cast<int>(total * kPerMove / kSamples);
  }
}

int Evaluator::evaluate(const Position& p) const {
  const Color us = p.sideToMove();
  int score = 0;
  for (CellId c = 0; c < p.cellCount(); ++c) {
    const Piece piece = p.at(c);
    if (piece.empty()) continue;
    const int v = pieceValue(piece.type);
    score += piece.colorOf() == us ? v : -v;
  }
  // One buffer per thread, reused: the opponent searches on a worker while a script or
  // a test may search on the main thread, and a leaf must not allocate.
  thread_local MoveList moves(0);
  if (moves.capacity() < v_->moveUpperBound()) moves.reserve(v_->moveUpperBound());
  moves.clear();
  gen_.generatePseudoLegalFor(p, us, /*includeCastles=*/false, moves);
  const auto ours = static_cast<int>(moves.size());
  moves.clear();
  gen_.generatePseudoLegalFor(p, opponent(us), /*includeCastles=*/false, moves);
  const auto theirs = static_cast<int>(moves.size());
  return score + kMobilityWeight * (ours - theirs);
}

}  // namespace cb::ai
