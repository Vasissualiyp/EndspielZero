// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>

#include "ai/eval.hpp"
#include "game/game.hpp"

namespace cb::ai {

/// How hard to look. The defaults are the demo floor (M10.3): a move that is always
/// legal, takes what it can and does not hang a piece to an immediate reply.
struct SearchOptions {
  /// The deepest iteration of the iterative deepening, in full-width plies. The default
  /// is a cap that is never reached: `maxNodes` is what actually bounds the search.
  int depth{64};
  /// Further plies of captures only, past `depth`, so a trade is seen to its end instead
  /// of being cut off half way - which is exactly what "hanging a piece" looks like.
  int quiescence{4};
  /// A hard bound on positions visited, so a variant with an enormous move count still
  /// answers. Nodes, not time: a budget in nodes gives the same move on every machine.
  std::uint64_t maxNodes{60'000};
  /// Set from another thread to stop early: the search returns the best move found so
  /// far as though its node budget had run out. Null means "never asked to stop".
  const std::atomic<bool>* stop{nullptr};
  /// The difficulty level this was made for (optionsForLevel), 0 for none. Only seeds the
  /// root noise.
  int level{0};
  /// Root noise: each root move's score gets a deterministic pseudo-random offset in
  /// [-noise, +noise] (never on a mate score), so a low level sometimes plays the
  /// second-best move. 0 is the engine at full strength.
  int noise{0};
};

inline constexpr int kMinLevel = 1;
inline constexpr int kMaxLevel = 5;
inline constexpr int kDefaultLevel = 3;

struct SearchResult {
  Move best{};
  /// From the mover's side; a mate found is near +/- kMate.
  int score{0};
  std::uint64_t nodes{0};
  /// False only when there was nothing to play.
  bool found{false};
};

inline constexpr int kMate = 1'000'000;

/// The options of difficulty level 1 (a beginner can win) to 5 (it converts won
/// endgames); out of range is clamped. Level 4 and 5 differ only in budget (60k and 100k
/// nodes: about 4 and 7 seconds in a Release build on a standard board); the mop-up
/// evaluation term is on at every level.
[[nodiscard]] SearchOptions optionsForLevel(int level);

/// Choose a move for the side to move in `game`.
///
/// Iterative deepening over negamax with alpha-beta, a transposition table and a
/// capture-only quiescence, over the game's own legality
/// (`Game::legalMoves`, `play`, `undo`), so every rule, seam and timeline the game knows
/// is respected with no code here - atomic captures explode, forced captures are forced,
/// a temporal move branches. A position that has already occurred scores as a draw. The
/// answer is that of the deepest iteration the budget let finish. The game is copied,
/// never touched. Deterministic: the same
/// game and options give the same move on any machine, ties broken by move order.
[[nodiscard]] SearchResult chooseMove(const Game& game, const Evaluator& eval,
                                      const SearchOptions& options = {});

}  // namespace cb::ai
