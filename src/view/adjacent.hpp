// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>

#include "geometry/transform.hpp"
#include "variant/variant.hpp"
#include "view/layout.hpp"

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

}  // namespace cb::view
