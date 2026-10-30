// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "position/position.hpp"

namespace cb::view {

/// An immutable copy of everything a renderer needs to draw a position.
///
/// The renderer never touches a live Position: the engine would then have to
/// synchronise with the GPU, and a half-applied move would be drawable. Instead the
/// engine hands over a snapshot, which is a flat copy of the cell array plus the
/// small amount of state that is not on the board (ARCH section 10). **[INVARIANT]**
///
/// The variant is held by pointer, not copied - it is immutable and must outlive both
/// the position and the snapshot anyway (see Position's lifetime note).
class PositionView {
 public:
  static PositionView capture(const Position& p);

  [[nodiscard]] const VariantSpec& variant() const noexcept { return *variant_; }
  [[nodiscard]] Piece at(CellId c) const noexcept { return cells_[c]; }
  [[nodiscard]] std::uint32_t cellCount() const noexcept {
    return static_cast<std::uint32_t>(cells_.size());
  }
  [[nodiscard]] std::span<const Piece> cells() const noexcept { return cells_; }
  [[nodiscard]] Color sideToMove() const noexcept { return side_; }
  [[nodiscard]] std::uint64_t hash() const noexcept { return hash_; }
  [[nodiscard]] CellId epTarget() const noexcept { return ep_; }

  /// Cells the UI is currently highlighting: a selected piece and its legal
  /// destinations. Part of the snapshot so the renderer has one source of truth for a
  /// frame and cannot disagree with the engine about what is legal.
  [[nodiscard]] std::span<const CellId> highlighted() const noexcept {
    return highlight_;
  }
  void setHighlighted(std::vector<CellId> cells) { highlight_ = std::move(cells); }
  /// Hover aids (M20): where the piece under Ctrl can go, and the faint hint of what
  /// attacks / defends the hovered square (the pieces, and the cells their rays cross).
  /// All sorted and unique; empty when the aid is off or nothing is hovered.
  [[nodiscard]] std::span<const CellId> peekMoves() const noexcept { return peek_; }
  [[nodiscard]] std::span<const CellId> hintAttackers() const noexcept { return hintFrom_; }
  [[nodiscard]] std::span<const CellId> hintPath() const noexcept { return hintPath_; }
  [[nodiscard]] std::span<const CellId> hintDefenders() const noexcept { return defFrom_; }
  [[nodiscard]] std::span<const CellId> hintDefendPath() const noexcept { return defPath_; }
  /// The cell under the pointer, drawn a little brighter always; kInvalidCell if none.
  [[nodiscard]] CellId hover() const noexcept { return hover_; }
  void setHover(CellId c) noexcept { hover_ = c; }
  void setPeekMoves(std::vector<CellId> c) { peek_ = std::move(c); }
  void setHint(std::vector<CellId> attackers, std::vector<CellId> path,
               std::vector<CellId> defenders, std::vector<CellId> defendPath) {
    hintFrom_ = std::move(attackers);
    hintPath_ = std::move(path);
    defFrom_ = std::move(defenders);
    defPath_ = std::move(defendPath);
  }
  [[nodiscard]] CellId selected() const noexcept { return selected_; }
  void setSelected(CellId c) noexcept { selected_ = c; }

 private:
  const VariantSpec* variant_{nullptr};
  std::vector<Piece> cells_;
  std::vector<CellId> highlight_;
  std::vector<CellId> peek_, hintFrom_, hintPath_, defFrom_, defPath_;
  CellId selected_{kInvalidCell};
  CellId hover_{kInvalidCell};
  Color side_{Color::White};
  CellId ep_{kInvalidCell};
  std::uint64_t hash_{0};
};

}  // namespace cb::view
