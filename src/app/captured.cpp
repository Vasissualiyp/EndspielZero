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

bool replayable(const Game& game) {
  if (game.isTemporal()) return false;
  const auto end = positionAtPly(game, game.plyCount());
  return end && end->hash() == game.currentHash();
}

std::optional<Position> positionAtPly(const Game& game, std::size_t ply) {
  if (game.isTemporal() || ply > game.plyCount()) return std::nullopt;
  Position pos = game.startPosition();
  const auto& moves = game.moveHistory();
  for (std::size_t i = 0; i < ply; ++i) {
    const Piece mover = pos.at(moves[i].from);
    if (mover.empty() || mover.colorOf() != pos.sideToMove()) return std::nullopt;
    Undo u;
    pos.make(moves[i], u);
  }
  return pos;
}

}  // namespace cb::app
