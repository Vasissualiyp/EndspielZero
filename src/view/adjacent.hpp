// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "geometry/transform.hpp"
#include "variant/variant.hpp"
#include "view/layout.hpp"
#include "view/snapshot.hpp"

namespace cb::view {

/// M22.1 the tiled view. A glued board's neighbours, seen through its edges, as a pure
/// function of the variant: no clock, no renderer, nothing that enters a game file. A copy
/// is a *view* of the same position, never a second game.

/// One board in the tiling.
struct AdjacentCopy {
  std::int8_t stepU{0};     ///< which neighbour: -R..+R along the first drawn axis
  std::int8_t stepV{0};     ///< ... and along the second
  std::uint8_t order{0};    ///< ring distance max(|stepU|, |stepV|); 0 is the centre
  /// Maps a copy-local coordinate (the cell's index within the copy as drawn) to the
  /// coordinate of the central-board cell it shows. Identity for the centre. It is the
  /// face transforms of the seams crossed, composed in the order crossed (file, then rank).
  Transform viaSeams;
};

/// The copies to draw for a 2-D glued variant: the central board plus every board within
/// `rings` steps through periodic faces, farthest ring first (so translucent boards blend
/// correctly), then by (stepU, stepV); the centre is last. Empty for a variant with no
/// periodic drawn face, and for any variant with other than two drawn axes. The count is
/// (2R+1)^2 on a torus and 2R+1 on a cylinder.
[[nodiscard]] std::vector<AdjacentCopy> adjacentCopies(const VariantSpec& v,
                                                       const ViewConfig& cfg, int rings);

/// Where a copy's cell is drawn: `central`'s placement (the copy-local cell) shifted by the
/// board's own extent along each stepped axis, naming the central-board cell it shows.
[[nodiscard]] Placement copyPlacement(const AdjacentCopy& copy, const Placement& central,
                                      const VariantSpec& v, const ViewConfig& cfg);

/// The central-board cell that copy-local cell `c` shows.
[[nodiscard]] CellId sourceCell(const AdjacentCopy& copy, CellId copyLocalCell,
                                const VariantSpec& v);

// M22.2 drawing the copies. The rules both views share - the 3-D board's instances and
// the flat board's tokens - so the two cannot disagree.

/// The first ring's opacity, and how much each further ring keeps of the one inside it:
/// order 1 is 0.85, 2 is 0.49, 3 is 0.29.
inline constexpr float kAdjacentAlpha = 0.85f;
inline constexpr float kAdjacentFalloff = 0.58f;
/// How far a copy's colours are mixed toward the background (`Theme::ink`), so the eye
/// reads the centre as the real board.
inline constexpr float kAdjacentDim = 0.35f;
/// The most copy instances one frame may draw. A large board drops its outermost rings
/// first, never part of a ring. An 8x8 torus at three rings (49 boards at two instances
/// per cell) fits.
inline constexpr std::size_t kAdjacentMaxInstances = 6400;

/// A copy's opacity by its ring: 1 for the centre, `kAdjacentAlpha *
/// kAdjacentFalloff^(order-1)` beyond it - strictly decreasing.
[[nodiscard]] float adjacentAlpha(int order) noexcept;

/// The one rule for where the tiled view is drawn: switched on, and not on the shape (a
/// shape is already cyclic). The front end and the interface read the same predicate.
[[nodiscard]] constexpr bool adjacentDrawn(bool showAdjacent, bool surface) noexcept {
  return showAdjacent && !surface;
}

/// `copies` with whole outer rings dropped until every copy but the centre fits
/// `perBoard` instances each within `maxInstances`. Order is kept.
[[nodiscard]] std::vector<AdjacentCopy> fitAdjacent(
    std::vector<AdjacentCopy> copies, std::size_t perBoard,
    std::size_t maxInstances = kAdjacentMaxInstances);

/// One flat token on a copy: the source cell's piece, drawn at the copy's place.
struct AdjacentToken {
  Piece piece;
  Placement at;           ///< where it is drawn; `at.cell` is the source cell
  std::uint8_t order{0};  ///< the copy's ring
  float alpha{0};         ///< `reveal * adjacentAlpha(order)`
};

/// The flat view's tokens on every copy but the centre, farthest ring first. `central`
/// is the central board's placements; `reveal` (0..1, already eased) scales every alpha
/// and 0 returns nothing; a piece on `skip` (the one in flight) is left out.
[[nodiscard]] std::vector<AdjacentToken> adjacentTokens(
    const PositionView& snapshot, const std::vector<AdjacentCopy>& copies,
    const std::vector<Placement>& central, const ViewConfig& cfg, float reveal,
    CellId skip = kInvalidCell);

}  // namespace cb::view
