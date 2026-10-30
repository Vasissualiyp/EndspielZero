// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/search.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "base/rng.hpp"

namespace cb::ai {
namespace {

/// A score this close to kMate is a forced mate, and is stored in the table relative to
/// the node rather than to the root, so a transposition at another ply reads it right.
constexpr int kMateBound = kMate - 1000;

enum class Bound : std::uint8_t { None, Exact, Lower, Upper };

/// One slot of the transposition table: the full key (no false hits), the score with its
/// bound, how deep it was searched, and the move that was best, by what identifies it.
struct TtEntry {
  std::uint64_t key{0};
  std::int32_t score{0};
  CellId from{kInvalidCell};
  CellId to{kInvalidCell};
  PieceTypeId promoteTo{kNoPiece};
  std::int8_t depth{0};
  Bound bound{Bound::None};
};

class Searcher {
 public:
  Searcher(Game& g, const Evaluator& e, const SearchOptions& o)
      : g_(g), eval_(e), opt_(o) {
    // Sized once from the budget: about two slots per node the search may visit, so the
    // table never fills and is never reallocated. A power of two so the index is a mask.
    std::size_t want = 1024;
    while (want < 2 * opt_.maxNodes && want < (std::size_t{1} << 20)) want <<= 1;
    tt_.assign(want, TtEntry{});
    // The hash does not cover a temporal variant's boards, so there a table hit may be a
    // different game: it can order moves (it is checked against the legal list) but never
    // cut the search off.
    cutoffs_ = !g_.isTemporal();
  }

  /// The score of the current position for its side to move. If the budget runs out the
  /// value is meaningless and `aborted()` is set; every caller returns at once.
  int search(int depth, int alpha, int beta, int ply) {
    ++nodes_;
    const GameResult r = g_.result();
    if (r != GameResult::InProgress) return terminal(r, ply);
    // Back to a position already seen, in the game or on this line: a draw, so a side
    // that is winning does not shuffle and a side that is losing may.
    if (g_.repetitionCount() >= 2) return 0;
    if (exhausted()) {
      aborted_ = true;
      return 0;
    }
    if (depth <= 0) return quiesce(opt_.quiescence, alpha, beta, ply);

    const std::uint64_t key = g_.currentHash();
    TtEntry& slot = tt_[key & (tt_.size() - 1)];
    const bool hit = slot.key == key && slot.bound != Bound::None;
    if (hit && cutoffs_ && slot.depth >= depth) {
      const int stored = fromTable(slot.score, ply);
      if (slot.bound == Bound::Exact) return stored;
      if (slot.bound == Bound::Lower && stored >= beta) return stored;
      if (slot.bound == Bound::Upper && stored <= alpha) return stored;
    }

    std::vector<Move> moves = g_.legalMoves();
    order(moves);
    if (hit) promote(moves, slot.from, slot.to, slot.promoteTo);
    const int alpha0 = alpha;
    int best = -kMate * 2;
    const Move* bestMove = nullptr;
    for (const Move& m : moves) {
      const int score = child(m, depth - 1, alpha, beta, ply);
      if (aborted_) return 0;
      if (score > best) {
        best = score;
        bestMove = &m;
      }
      alpha = std::max(alpha, score);
      if (alpha >= beta) break;
    }
    store(key, best, depth, ply,
          best <= alpha0 ? Bound::Upper
          : best >= beta ? Bound::Lower
                         : Bound::Exact,
          bestMove);
    return best;
  }

  /// Captures only, with the option of standing pat: the side to move need not capture,
  /// so the static score is a floor.
  int quiesce(int depth, int alpha, int beta, int ply) {
    const int stand = eval_.evaluate(g_.position());
    if (exhausted()) {
      aborted_ = true;
      return 0;
    }
    if (depth <= 0) return stand;
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
      if (aborted_) return 0;
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

  /// Move the legal move that matches the table's best to the front, if it is there.
  static void promote(std::vector<Move>& moves, CellId from, CellId to,
                      PieceTypeId promoteTo) {
    const auto it = std::find_if(moves.begin(), moves.end(), [&](const Move& m) {
      return m.from == from && m.to == to && m.promoteTo == promoteTo;
    });
    if (it != moves.end()) std::rotate(moves.begin(), it, it + 1);
  }

  [[nodiscard]] std::uint64_t nodes() const noexcept { return nodes_; }
  [[nodiscard]] bool aborted() const noexcept { return aborted_; }

  [[nodiscard]] bool exhausted() const noexcept {
    return nodes_ >= opt_.maxNodes ||
           (opt_.stop != nullptr && opt_.stop->load(std::memory_order_relaxed));
  }

 private:
  /// A mate score as the table keeps it (distance from this node) and back (from the
  /// root): the ply the node sits at is added or taken off.
  static int toTable(int score, int ply) {
    if (score > kMateBound) return score + ply;
    if (score < -kMateBound) return score - ply;
    return score;
  }
  static int fromTable(int score, int ply) {
    if (score > kMateBound) return score - ply;
    if (score < -kMateBound) return score + ply;
    return score;
  }

  void store(std::uint64_t key, int score, int depth, int ply, Bound bound,
             const Move* best) {
    TtEntry& e = tt_[key & (tt_.size() - 1)];
    e.key = key;
    e.score = toTable(score, ply);
    e.depth = static_cast<std::int8_t>(std::min(depth, 127));
    e.bound = bound;
    // A move that failed low proves nothing about which is best.
    const bool useful = best != nullptr && bound != Bound::Upper;
    e.from = useful ? best->from : kInvalidCell;
    e.to = useful ? best->to : kInvalidCell;
    e.promoteTo = useful ? best->promoteTo : kNoPiece;
  }

  Game& g_;
  const Evaluator& eval_;
  const SearchOptions& opt_;
  std::uint64_t nodes_{0};
  bool aborted_{false};
  bool cutoffs_{true};
  std::vector<TtEntry> tt_;
};

}  // namespace

SearchOptions optionsForLevel(int level) {
  SearchOptions o;
  o.level = std::clamp(level, kMinLevel, kMaxLevel);
  switch (o.level) {
    case 1:
      o.depth = 1;
      o.maxNodes = 2'000;
      o.noise = 200;
      break;
    case 2:
      o.depth = 2;
      o.maxNodes = 8'000;
      o.noise = 100;
      break;
    case 3:
      o.depth = 4;
      o.maxNodes = 30'000;
      o.noise = 30;
      break;
    case 4:
      o.maxNodes = 60'000;
      break;
    default:
      o.maxNodes = 100'000;
      break;
  }
  return o;
}

namespace {

/// The root noise of one move: a pseudo-random offset in [-noise, +noise] from the
/// position, the level and the move alone, so the same question always gets the same
/// answer. Never added to a mate score (the caller checks), so a mate is never missed.
int rootNoise(const SearchOptions& o, std::uint64_t positionHash, const Move& m) {
  if (o.noise <= 0) return 0;
  std::uint64_t seed = positionHash ^ (static_cast<std::uint64_t>(o.level) << 56);
  seed = seed * 0x9E3779B97F4A7C15ULL + m.from;
  seed = seed * 0x9E3779B97F4A7C15ULL + m.to;
  seed = seed * 0x9E3779B97F4A7C15ULL + m.promoteTo;
  Rng rng(seed);
  return static_cast<int>(rng.below(static_cast<std::uint32_t>(2 * o.noise + 1))) -
         o.noise;
}

}  // namespace

SearchResult chooseMove(const Game& game, const Evaluator& eval,
                        const SearchOptions& options) {
  SearchResult out;
  Game g = game;  // searched on a copy: the caller's game and its history are untouched
  std::vector<Move> moves = g.legalMoves();
  if (moves.empty() || g.result() != GameResult::InProgress) return out;
  Searcher s(g, eval, options);
  s.order(moves);
  // If not even depth 1 finishes, the first move in order (captures first) stands.
  out.best = moves.front();
  out.score = -kMate * 2;
  out.found = true;
  const std::uint64_t rootHash = g.currentHash();
  const int maxDepth = std::max(1, options.depth);
  // Iterative deepening: every depth is searched to the end before the next begins, so
  // the answer is always from a search that finished, and the previous depth's best move
  // goes first in the next - which is what makes alpha-beta prune. A depth the budget
  // cuts short is thrown away rather than compared with the finished ones.
  for (int depth = 1; depth <= maxDepth; ++depth) {
    int alpha = -kMate * 2;
    const int beta = kMate * 2;
    Move best = moves.front();
    int bestScore = alpha;
    for (const Move& m : moves) {
      // With noise every root score must be exact, not a bound, so the window is not
      // narrowed by the moves already searched.
      const int score =
          s.child(m, depth - 1, options.noise > 0 ? -kMate * 2 : alpha, beta, 0);
      if (s.aborted()) break;
      const int shown =
          std::abs(score) > kMateBound ? score : score + rootNoise(options, rootHash, m);
      // Strictly better only: among equals the first in order wins, which is what makes
      // the choice reproducible.
      if (shown > bestScore) {
        bestScore = shown;
        best = m;
      }
      alpha = std::max(alpha, shown);
    }
    if (s.aborted()) break;
    out.best = best;
    out.score = bestScore;
    // The best move first for the next depth; the rest keep their order.
    const auto at = std::find(moves.begin(), moves.end(), best);
    std::rotate(moves.begin(), at, at + 1);
    if (bestScore > kMateBound || moves.size() == 1) break;
  }
  out.nodes = s.nodes();
  return out;
}

}  // namespace cb::ai
