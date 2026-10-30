// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "render/piece_mesh.hpp"

namespace cb::render {

/// The flat board's pieces, as polygon tables.
///
/// Seen from straight above a model is a blob, so the flat view draws icons instead.
/// They are *tables* rather than a switch full of drawing calls for two reasons: a
/// variant could eventually ship its own, and a table can be checked by a test - that
/// every archetype has one, that nothing escapes its box, that the cheap set is
/// actually cheap.
///
/// Coordinates are in a 100x100 box with y pointing down, the way a glyph is authored.
/// Every outline is straight-segment only: that is the visual language, and it is also
/// exactly what ImDrawList fills without curves or tessellation.
struct IconPoint {
  float x{0}, y{0};
};

/// One closed outline.
using IconPoly = std::span<const IconPoint>;

/// Which set to draw.
enum class IconStyle : std::uint8_t {
  /// Faceted silhouettes: a dozen or so points each, with cut-outs for detail.
  Faceted,
  /// Two or three convex shapes each, never more than eight points. Kept because the
  /// flat view exists for machines that would rather not work hard, and because some
  /// players simply want the plainer picture.
  Primitive,
};

std::string_view iconStyleName(IconStyle s) noexcept;
/// Parse a stored setting. An unknown name is the default rather than an error.
IconStyle iconStyleFromName(std::string_view name) noexcept;

struct PieceIcon {
  /// Outlines drawn in the piece's own colour.
  std::span<const IconPoly> fills;
  /// Outlines drawn in the colour behind the piece, cutting detail out of the
  /// silhouette - a knight's eye, a slot in a rook's base. Empty for the cheap set.
  std::span<const IconPoly> cuts;
};

/// The icon for an archetype. Every archetype has one in every style; Cell and Portal
/// are not pieces and come back empty.
PieceIcon pieceIcon(IconStyle style, Archetype shape);

/// The icon a piece of a variant draws with: its authored `[piece.icon]` (M7.3.4) when it
/// has one - in either style, since an author draws one icon - and otherwise its
/// archetype's. The returned spans stay valid for the life of the program: authored
/// icons are converted once and kept, keyed by their content.
PieceIcon pieceIconFor(IconStyle style, const PieceTypeDef& piece);

}  // namespace cb::render
