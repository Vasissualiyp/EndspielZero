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
  /// Full-width plies: 2 is "my move and the best answer to it".
  int depth{2};
  /// Further plies of captures only, past `depth`, so a trade is seen to its end instead
  /// of being cut off half way - which is exactly what "hanging a piece" looks like.
  int quiescence{4};
  /// A hard bound on positions visited, so a variant with an enormous move count still
  /// answers. Nodes, not time: a budget in nodes gives the same move on every machine.
  std::uint64_t maxNodes{60'000};
  /// Set from another thread to stop early: the search returns the best move found so
  /// far as though its node budget had run out. Null means "never asked to stop".
  const std::atomic<bool>* stop{nullptr};
};

struct SearchResult {
  Move best{};
  /// From the mover's side; a mate found is near +/- kMate.
  int score{0};
  std::uint64_t nodes{0};
  /// False only when there was nothing to play.
  bool found{false};
};

inline constexpr int kMate = 1'000'000;

/// Choose a move for the side to move in `game`.
///
/// Negamax with alpha-beta and a capture-only quiescence, over the game's own legality
/// (`Game::legalMoves`, `play`, `undo`), so every rule, seam and timeline the game knows
/// is respected with no code here - atomic captures explode, forced captures are forced,
/// a temporal move branches. The game is copied, never touched. Deterministic: the same
/// game and options give the same move on any machine, ties broken by move order.
[[nodiscard]] SearchResult chooseMove(const Game& game, const Evaluator& eval,
                                      const SearchOptions& options = {});

}  // namespace cb::ai
