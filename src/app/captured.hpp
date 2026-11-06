// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "game/game.hpp"

namespace cb::app {

/// What each side has taken so far (M23.10 section 7), in capture order. `byWhite` holds
/// the pieces White captured (Black's pieces), `byBlack` the reverse.
struct CapturedPieces {
  std::vector<Piece> byWhite;
  std::vector<Piece> byBlack;
};

/// Replay the game's move history from its start position and note each captured piece.
/// A capture is read off the move's capture cell before the move is made, so en passant
/// and a capturing promotion are right. Empty for a temporal variant, whose boards are
/// not one position, and for a game set up from a FEN (its start is not kept). Ends with
/// the history, so an undo shortens it.
[[nodiscard]] CapturedPieces capturedBySide(const Game& game);

/// Whether the move history replays from `startPosition()` onto the position being
/// played. False for a temporal variant and for a game set up from a FEN (its start is
/// not kept), which is how review (M23.11 section 3b) knows it cannot rebuild a position.
[[nodiscard]] bool replayable(const Game& game);

/// The position after the first `ply` moves, rebuilt from the start. Empty when the game
/// is not `replayable` or `ply` is past the history. Never touches the game.
[[nodiscard]] std::optional<Position> positionAtPly(const Game& game, std::size_t ply);

}  // namespace cb::app
