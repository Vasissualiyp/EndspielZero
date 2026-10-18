// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "position/piece.hpp"

namespace cb {

/// Set in `Move::castleIndex` when the move is a compound move rather than a castle template.
inline constexpr std::uint8_t kCompoundBit = 0x80;

enum class MoveFlag : std::uint8_t {
  None = 0,
  Capture = 1 << 0,
  Promotion = 1 << 1,
  Castle = 1 << 2,
  EnPassant = 1 << 3,
  /// Left an en-passant target behind (a pawn's double step, generalized).
  LeavesEnPassant = 1 << 4,
};

inline std::uint8_t operator|(MoveFlag a, MoveFlag b) {
  return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) |
                                   static_cast<std::uint8_t>(b));
}
inline bool has(std::uint8_t flags, MoveFlag f) {
  return (flags & static_cast<std::uint8_t>(f)) != 0;
}

/// One move. `captureCell` is separate from `to` because an en-passant capture
/// removes a piece from a cell the mover never occupies - and because a future
/// variant may capture at a distance.
struct Move {
  CellId from{kInvalidCell};
  CellId to{kInvalidCell};
  CellId captureCell{kInvalidCell};
  /// The cell an opponent may capture through next move, if this move leaves one.
  CellId epTarget{kInvalidCell};
  /// The piece that becomes vulnerable to that en-passant capture. Recorded
  /// rather than derived as "one cell behind the target", which is meaningless
  /// once the board wraps or has more axes.
  CellId epVictim{kInvalidCell};
  PieceTypeId promoteTo{kNoPiece};
  std::uint8_t flags{0};
  /// A legacy castle template's index, or `kCompoundBit | i` for the variant's i-th compound
  /// move; 0xFF for neither.
  std::uint8_t castleIndex{0xFF};
  /// Which of the mover's atoms made this move (0xFF for none): `make` reads the marks it
  /// leaves from there. Not part of a move's identity.
  std::uint8_t atomIndex{0xFF};
  /// A two-piece move (castling): where the partner starts and where it is carried.
  CellId partnerFrom{kInvalidCell};
  CellId partnerTo{kInvalidCell};

  [[nodiscard]] bool isCapture() const noexcept { return has(flags, MoveFlag::Capture); }
  [[nodiscard]] bool isCastle() const noexcept { return has(flags, MoveFlag::Castle); }

  friend bool operator==(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.captureCell == b.captureCell &&
           a.promoteTo == b.promoteTo && a.flags == b.flags &&
           a.castleIndex == b.castleIndex;
  }
};

static_assert(std::is_trivially_copyable_v<Move>);

/// Total order on moves, for canonical output and for deduplication.
inline bool moveLess(const Move& a, const Move& b) {
  if (a.from != b.from) return a.from < b.from;
  if (a.to != b.to) return a.to < b.to;
  if (a.captureCell != b.captureCell) return a.captureCell < b.captureCell;
  if (a.promoteTo != b.promoteTo) return a.promoteTo < b.promoteTo;
  if (a.flags != b.flags) return a.flags < b.flags;
  return a.castleIndex < b.castleIndex;
}

/// One custom-field value as it was before a move changed it.
struct FieldChange {
  bool cellField{false};  ///< false = a piece field
  std::uint16_t index{0};
  CellId cell{kInvalidCell};
  std::int32_t oldValue{0};
};

/// State that a move destroys and unmake must restore. Kept beside the move
/// rather than inside it so a Move stays a value that can be stored cheaply.
///
/// The two vectors stay empty - and therefore allocate nothing - for a variant with no
/// custom fields and no rules, which is why standard chess keeps its no-allocation
/// property while an exploding variant can still be undone exactly.
/// A named tag on a cell for a number of plies (M19). The en-passant target is the tag "ep"
/// on the passed cell for one ply, carrying the piece it exposes; a "destroyed" square is a
/// blocking tag on a landing's surroundings. `owner` is who made the move that left it - an
/// en-passant mark can only be used by the other side.
struct Mark {
  CellId cell{kInvalidCell};
  std::uint16_t tag{0};
  std::uint16_t ttl{0};  ///< plies left; 0 is forever
  std::uint8_t owner{0};
  CellId victim{kInvalidCell};

  friend bool operator==(const Mark&, const Mark&) = default;
};

struct Undo {
  Piece captured{};
  /// The marks before the move, restored exactly by unmake.
  std::vector<Mark> marksBefore;
  std::uint8_t castleRightsBefore{0};
  std::int32_t halfmoveBefore{0};
  std::uint64_t hashBefore{0};
  /// The mover's flags before the move (its moved bit), which unmake puts back.
  std::uint8_t moverFlags{0};
  /// The partner's flags before a two-piece move (its moved bit).
  std::uint8_t partnerFlags{0};
  /// Cells a rule effect changed, with what stood there before.
  std::vector<std::pair<CellId, Piece>> cellRestores;
  std::vector<FieldChange> fieldChanges;
};

}  // namespace cb
