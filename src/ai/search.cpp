// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/search.hpp"

#include <algorithm>
#include <vector>

namespace cb::ai {
namespace {

class Searcher {
 public:
  Searcher(Game& g, const Evaluator& e, const SearchOptions& o)
      : g_(g), eval_(e), opt_(o) {}

  /// The score of the current position for its side to move.
  int search(int depth, int alpha, int beta, int ply) {
    ++nodes_;
    const GameResult r = g_.result();
    if (r != GameResult::InProgress) return terminal(r, ply);
    if (exhausted()) return eval_.evaluate(g_.position());
    if (depth <= 0) return quiesce(opt_.quiescence, alpha, beta, ply);

    std::vector<Move> moves = g_.legalMoves();
    order(moves);
    int best = -kMate * 2;
    for (const Move& m : moves) {
      const int score = child(m, depth - 1, alpha, beta, ply);
      if (score > best) best = score;
      alpha = std::max(alpha, score);
      if (alpha >= beta) break;
    }
    return best;
  }

  /// Captures only, with the option of standing pat: the side to move need not capture,
  /// so the static score is a floor.
  int quiesce(int depth, int alpha, int beta, int ply) {
    const int stand = eval_.evaluate(g_.position());
    if (depth <= 0 || exhausted()) return stand;
    if (stand >= beta) return stand;
    alpha = std::max(alpha, stand);
    std::vector<Move> moves;
    for (const Move& m : g_.legalMoves()) {
      if (m.isCapture() ||
          (m.flags & static_cast<std::uint8_t>(MoveFlag::Promotion)) != 0)
        moves.push_back(m);
    }
    order(moves);
    int best = stand;
    for (const Move& m : moves) {
      ++nodes_;
      const Color mover = g_.position().sideToMove();
      if (!g_.play(m).has_value()) continue;
      int score = 0;
      const GameResult r = g_.result();
      const bool sameSide = g_.position().sideToMove() == mover;
      if (r != GameResult::InProgress) {
        score = sameSide ? terminal(r, ply + 1) : -terminal(r, ply + 1);
      } else if (sameSide) {
        score = quiesce(depth - 1, alpha, beta, ply + 1);
      } else {
        score = -quiesce(depth - 1, -beta, -alpha, ply + 1);
      }
      (void)g_.undo();
      best = std::max(best, score);
      alpha = std::max(alpha, score);
      if (alpha >= beta) break;
    }
    return best;
  }

  /// Play `m`, search what follows, undo. When the same side is still to move - a rule
  /// that keeps the turn (a multi-jump), or a temporal turn with boards still to answer
  /// on - what follows is searched from the same side, not negated.
  int child(const Move& m, int depth, int alpha, int beta, int ply) {
    const Color mover = g_.position().sideToMove();
    if (!g_.play(m).has_value()) return -kMate * 2;
    int score = 0;
    if (g_.position().sideToMove() == mover) {
      score = search(depth, alpha, beta, ply + 1);
    } else {
      score = -search(depth, -beta, -alpha, ply + 1);
    }
    (void)g_.undo();
    return score;
  }

  /// Captures first, the most valuable victim with the least valuable attacker; then
  /// promotions; then the rest in the generator's own (deterministic) order.
  void order(std::vector<Move>& moves) const {
    const Position& p = g_.position();
    const auto key = [&](const Move& m) {
      int k = 0;
      if (m.isCapture() && m.captureCell != kInvalidCell) {
        k += 10 * eval_.pieceValue(p.at(m.captureCell).type) -
             eval_.pieceValue(p.at(m.from).type) + 100000;
      }
      if ((m.flags & static_cast<std::uint8_t>(MoveFlag::Promotion)) != 0) {
        k += 50000 + eval_.pieceValue(m.promoteTo);
      }
      return k;
    };
    std::stable_sort(moves.begin(), moves.end(),
                     [&](const Move& a, const Move& b) { return key(a) > key(b); });
  }

  /// The side to move's view of a finished game. A nearer mate scores higher, so the
  /// search plays the quickest win and puts off a loss.
  int terminal(GameResult r, int ply) const {
    if (r == GameResult::Draw) return 0;
    const Color mover = g_.position().sideToMove();
    const bool moverWon = (r == GameResult::WhiteWins) == (mover == Color::White);
    return moverWon ? kMate - ply : -(kMate - ply);
  }

  [[nodiscard]] std::uint64_t nodes() const noexcept { return nodes_; }

  [[nodiscard]] bool exhausted() const noexcept {
    return nodes_ >= opt_.maxNodes ||
           (opt_.stop != nullptr && opt_.stop->load(std::memory_order_relaxed));
  }

 private:
  Game& g_;
  const Evaluator& eval_;
  const SearchOptions& opt_;
  std::uint64_t nodes_{0};
};

}  // namespace

SearchResult chooseMove(const Game& game, const Evaluator& eval,
                        const SearchOptions& options) {
  SearchResult out;
  Game g = game;  // searched on a copy: the caller's game and its history are untouched
  std::vector<Move> moves = g.legalMoves();
  if (moves.empty() || g.result() != GameResult::InProgress) return out;
  Searcher s(g, eval, options);
  s.order(moves);
  int alpha = -kMate * 2;
  const int beta = kMate * 2;
  out.best = moves.front();
  out.score = alpha;
  out.found = true;
  for (const Move& m : moves) {
    const int score = s.child(m, std::max(0, options.depth - 1), alpha, beta, 0);
    // Strictly better only: among equals the first in order wins, which is what makes
    // the choice reproducible.
    if (score > out.score) {
      out.score = score;
      out.best = m;
    }
    alpha = std::max(alpha, score);
  }
  out.nodes = s.nodes();
  return out;
}

}  // namespace cb::ai
