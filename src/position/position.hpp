// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "base/bit_words.hpp"
#include "position/move.hpp"
#include "position/piece.hpp"

namespace cb {

/// A position: a flat cell array plus per-colour occupancy plus the small amount
/// of state that is not on the board. One allocation per position, no nested
/// containers, no per-piece objects (ARCH section 5).
/// A position holds a pointer to its variant, so **the VariantSpec must outlive
/// every Position and MoveGen that refers to it, and must not be moved while they
/// exist.** Own the spec somewhere stable (a unique_ptr, or a member constructed
/// before the objects that reference it) rather than passing it around by value.
/// **[INVARIANT]**
class Position {
 public:
  explicit Position(const VariantSpec& v);

  /// The variant's declared starting position.
  static Position startPosition(const VariantSpec& v);

  [[nodiscard]] const VariantSpec& variant() const noexcept { return *v_; }
  [[nodiscard]] std::uint32_t cellCount() const noexcept { return v_->dims.cellCount(); }

  [[nodiscard]] Piece at(CellId c) const noexcept { return cells_[c]; }
  [[nodiscard]] bool isEmpty(CellId c) const noexcept { return cells_[c].empty(); }
  [[nodiscard]] const BitWords& occupancy() const noexcept { return occAll_; }
  [[nodiscard]] const BitWords& occupancy(Color c) const noexcept {
    return occ_[static_cast<std::size_t>(c)];
  }

  [[nodiscard]] Color sideToMove() const noexcept { return side_; }
  [[nodiscard]] CellId epTarget() const noexcept { return ep_; }
  [[nodiscard]] CellId epVictim() const noexcept { return epVictim_; }
  [[nodiscard]] std::uint8_t castleRights() const noexcept { return castleRights_; }
  [[nodiscard]] std::int32_t halfmoveClock() const noexcept { return halfmove_; }
  [[nodiscard]] std::int32_t fullmoveNumber() const noexcept { return fullmove_; }
  [[nodiscard]] std::uint64_t hash() const noexcept { return hash_; }

  // ---- setup (not for use during play) ------------------------------------
  void clear();
  void place(CellId c, PieceTypeId type, Color color);
  void removeAt(CellId c);
  void setSideToMove(Color c);
  void setEpTarget(CellId target, CellId victim = kInvalidCell);
  void setCastleRights(std::uint8_t mask);
  void setClocks(std::int32_t halfmove, std::int32_t fullmove);
  /// Set or clear the moved bit of the piece on a cell (setup, e.g. reading FEN-N). The hash
  /// follows. A cell with no piece, or a type nothing asks "has it moved?" about, is left alone.
  void setMoved(CellId c, bool moved);

  // ---- custom fields -------------------------------------------------------
  [[nodiscard]] std::int32_t pieceField(int index, CellId c) const {
    return pieceFields_[static_cast<std::size_t>(index)][c];
  }
  [[nodiscard]] std::int32_t cellField(int index, CellId c) const {
    return cellFields_[static_cast<std::size_t>(index)][c];
  }
  /// Set a field, recording the previous value in \`u\` when one is supplied so that the
  /// change can be undone exactly.
  void setPieceField(int index, CellId c, std::int32_t value, Undo* u = nullptr);
  void setCellField(int index, CellId c, std::int32_t value, Undo* u = nullptr);

  /// Overwrite a cell with an exact piece (possibly empty), recording the previous
  /// occupant. This is the primitive every rule effect that adds, removes or
  /// transforms a piece is built from.
  void setCell(CellId c, Piece p, Undo* u = nullptr);

  /// Recompute the hash from scratch. The incremental path must always agree with
  /// this; a property test asserts it after every move (M1.3).
  [[nodiscard]] std::uint64_t computeHash() const;

  // ---- play ---------------------------------------------------------------
  /// Apply a move, filling `u` with what is needed to reverse it exactly.
  void make(const Move& m, Undo& u);
  /// Reverse the given move. Requires the `u` produced by the matching make().
  void unmake(const Move& m, const Undo& u);

  /// Cell of the first royal piece of `c`, or kInvalidCell. Variants may have
  /// none (checkers) or several; movegen's legality rule handles both.
  [[nodiscard]] CellId findRoyal(Color c) const;

  /// Internal consistency of cells, occupancy and hash. Debug-only assertion
  /// surface: cheap to call after every mutation in tests, never in release.
  [[nodiscard]] bool validate(std::string* why = nullptr) const;

 private:
  void addPiece(CellId c, Piece p);
  void removePiece(CellId c);

  const VariantSpec* v_;
  std::vector<Piece> cells_;
  /// One column per declared field, indexed by cell. Empty when a variant declares none.
  std::vector<std::vector<std::int32_t>> pieceFields_;
  std::vector<std::vector<std::int32_t>> cellFields_;
  BitWords occ_[kNumColors];
  BitWords occAll_;
  Color side_{Color::White};
  CellId ep_{kInvalidCell};
  CellId epVictim_{kInvalidCell};
  std::uint8_t castleRights_{0};
  std::int32_t halfmove_{0};
  std::int32_t fullmove_{1};
  std::uint64_t hash_{0};
};

}  // namespace cb
