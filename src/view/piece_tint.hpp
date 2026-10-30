// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>

#include "view/snapshot.hpp"
#include "view/theme.hpp"

namespace cb::view {

/// How strongly a piece is tinted, and towards what (M20). One rule for every way a piece
/// is drawn - the 3-D board, the shape view and the flat tokens - so they cannot disagree.
///
/// A hovered piece takes a slight accent tint; the selected piece (clicked, about to move)
/// a stronger one; `travelling` is the same strong tint for the piece while it is in
/// flight, so the tint lasts until the move has finished. A piece that attacks or defends
/// the hovered square under Ctrl is tinted in that hint's colour.
inline constexpr float kHoverPieceTint = 0.22f;
inline constexpr float kSelectedPieceTint = 0.55f;
inline constexpr float kHintPieceTint = 0.50f;

[[nodiscard]] inline Rgba tintedPiece(Rgba base, const PositionView& p, CellId cell,
                                      bool travelling, const Theme& t) {
  const auto in = [&](std::span<const CellId> s) {
    return std::find(s.begin(), s.end(), cell) != s.end();
  };
  const auto mix = [](const Rgba& a, const Rgba& b, float k) {
    return Rgba{a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, a.a};
  };
  if (travelling || (cell != kInvalidCell && cell == p.selected())) {
    return mix(base, t.ember, kSelectedPieceTint);
  }
  if (cell == kInvalidCell) return base;
  if (in(p.hintAttackers())) return mix(base, t.blood, kHintPieceTint);
  if (in(p.hintDefenders())) return mix(base, t.peek, kHintPieceTint);
  if (cell == p.hover()) return mix(base, t.ember, kHoverPieceTint);
  return base;
}

}  // namespace cb::view
