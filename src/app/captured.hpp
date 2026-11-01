// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
/// not one position, and for a game set up from a FEN (its start is not kept). Ends with the history, so an undo shortens it.
[[nodiscard]] CapturedPieces capturedBySide(const Game& game);

}  // namespace cb::app
