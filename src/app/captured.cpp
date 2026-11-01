// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/captured.hpp"

namespace cb::app {

CapturedPieces capturedBySide(const Game& game) {
  CapturedPieces out;
  if (game.isTemporal()) return out;
  Position pos = game.startPosition();
  for (const Move& m : game.moveHistory()) {
    // A history that does not belong to this start (a FEN-set game keeps none of its own)
    // is refused before `make` can trip over it.
    const Piece mover = pos.at(m.from);
    if (mover.empty() || mover.colorOf() != pos.sideToMove()) return {};
    if (m.isCapture() && m.captureCell != kInvalidCell) {
      const Piece taken = pos.at(m.captureCell);
      if (!taken.empty()) {
        (pos.sideToMove() == Color::White ? out.byWhite : out.byBlack).push_back(taken);
      }
    }
    Undo u;
    pos.make(m, u);
  }
  // A game set up from a FEN does not remember its start, so the replay would not land on
  // the position being played: show nothing rather than a wrong tray.
  if (pos.hash() != game.currentHash()) return {};
  return out;
}

}  // namespace cb::app
