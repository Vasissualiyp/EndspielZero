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
/// Past this many cells the royal tables are not built: the mobility table costs a move
/// generation per cell, and the distance table is cells squared bytes.
constexpr std::uint32_t kMaxMobilityCells = 4096;
constexpr std::uint32_t kMaxDistCells = 1024;

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
  buildRoyalTables();
}

void Evaluator::buildRoyalTables() {
  const VariantSpec& v = *v_;
  const std::uint32_t cells = v.dims.cellCount();
  if (cells == 0 || cells > kMaxMobilityCells) return;
  // The mop-up term is about "the" king; a variant with none, or with several kinds of
  // royal piece, has no such thing.
  PieceTypeId royal = kNoPiece;
  for (std::size_t t = 1; t < v.pieces.size(); ++t) {
    if (!v.pieces[t].royal) continue;
    if (royal != kNoPiece) return;
    royal = static_cast<PieceTypeId>(t);
  }
  if (royal == kNoPiece) return;

  // The royal's move graph on an empty board: an edge per move it has from a cell.
  std::vector<std::vector<CellId>> next(cells);
  MoveList moves(v.moveUpperBound());
  Position p(v);
  for (CellId c = 0; c < cells; ++c) {
    p.place(c, royal, Color::White);
    moves.clear();
    gen_.generatePseudoLegalFor(p, Color::White, /*includeCastles=*/false, moves);
    p.removeAt(c);
    for (const Move& m : moves) {
      if (m.from == c && m.to < cells && m.to != c) next[c].push_back(m.to);
    }
    std::sort(next[c].begin(), next[c].end());
    next[c].erase(std::unique(next[c].begin(), next[c].end()), next[c].end());
  }
  royalMobility_.assign(cells, 0);
  for (CellId c = 0; c < cells; ++c) {
    const auto n = std::min<std::size_t>(next[c].size(), 255);
    royalMobility_[c] = static_cast<std::uint8_t>(n);
    maxRoyalMobility_ = std::max(maxRoyalMobility_, static_cast<int>(n));
  }

  if (cells <= kMaxDistCells) {
    dist_.assign(static_cast<std::size_t>(cells) * cells, 255);
    std::vector<CellId> queue(cells);
    for (CellId src = 0; src < cells; ++src) {
      std::uint8_t* row = &dist_[static_cast<std::size_t>(src) * cells];
      std::size_t head = 0;
      std::size_t tail = 0;
      row[src] = 0;
      queue[tail++] = src;
      while (head < tail) {
        const CellId at = queue[head++];
        if (row[at] >= 254) continue;
        for (const CellId to : next[at]) {
          if (row[to] != 255) continue;
          row[to] = static_cast<std::uint8_t>(row[at] + 1);
          maxDist_ = std::max(maxDist_, static_cast<int>(row[to]));
          queue[tail++] = to;
        }
      }
    }
  }

  // "Winning on material" means about a piece of middling worth: the middle of the
  // variant's own piece values (a bishop in standard chess). Whether that really is a
  // win the search finds out; the term only steers.
  std::vector<int> worth;
  for (std::size_t t = 1; t < v.pieces.size(); ++t) {
    if (!v.pieces[t].royal && values_[t] > 0) worth.push_back(values_[t]);
  }
  if (worth.empty()) return;
  std::sort(worth.begin(), worth.end());
  mopGap_ = worth[worth.size() / 2];
  royalType_ = royal;
}

bool Evaluator::mopUp(const Position& p, Color us, int ourMaterial, int theirMaterial,
                      int& out) const {
  // Only a lone royal is mopped up; the stronger side needs enough to finish with.
  Color strong = us;
  if (theirMaterial == 0 && ourMaterial >= mopGap_) {
    strong = us;
  } else if (ourMaterial == 0 && theirMaterial >= mopGap_) {
    strong = opponent(us);
  } else {
    return false;
  }
  const CellId strongKing = p.findRoyal(strong);
  const CellId weakKing = p.findRoyal(opponent(strong));
  if (strongKing == kInvalidCell || weakKing == kInvalidCell) return false;
  int bonus = kMopEdgeWeight * (maxRoyalMobility_ - royalMobility(weakKing));
  if (!dist_.empty()) {
    const auto cells = static_cast<std::size_t>(royalMobility_.size());
    const int d = dist_[static_cast<std::size_t>(strongKing) * cells + weakKing];
    bonus += kMopDistWeight * (maxDist_ - std::min(d, maxDist_));
  }
  out = strong == us ? bonus : -bonus;
  return true;
}

int Evaluator::evaluate(const Position& p) const {
  const Color us = p.sideToMove();
  int ours = 0;
  int theirs = 0;
  for (CellId c = 0; c < p.cellCount(); ++c) {
    const Piece piece = p.at(c);
    if (piece.empty()) continue;
    const int v = pieceValue(piece.type);
    if (piece.colorOf() == us) {
      ours += v;
    } else {
      theirs += v;
    }
  }
  const int score = ours - theirs;
  // A lone royal against an army: the mobility term would reward the lone king's
  // freedom, which is exactly what the win has to take away, so the mop-up term (drive it
  // to the edge, bring the own king up) replaces it.
  int mop = 0;
  if (royalType_ != kNoPiece && mopUp(p, us, ours, theirs, mop)) return score + mop;
  // One buffer per thread, reused: the opponent searches on a worker while a script or
  // a test may search on the main thread, and a leaf must not allocate.
  thread_local MoveList moves(0);
  if (moves.capacity() < v_->moveUpperBound()) moves.reserve(v_->moveUpperBound());
  moves.clear();
  gen_.generatePseudoLegalFor(p, us, /*includeCastles=*/false, moves);
  const auto oursMoves = static_cast<int>(moves.size());
  moves.clear();
  gen_.generatePseudoLegalFor(p, opponent(us), /*includeCastles=*/false, moves);
  const auto theirMoves = static_cast<int>(moves.size());
  return score + kMobilityWeight * (oursMoves - theirMoves);
}

}  // namespace cb::ai
