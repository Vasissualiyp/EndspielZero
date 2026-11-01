// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "variant/variant.hpp"

namespace cb::view {

/// Board-adjacency distance, in king steps, from the nearest of `sources` to every cell
/// (-1 where unreachable). Steps run through the geometry's own seams, so on a torus the
/// ripple of M23.10 wraps and on a Klein bottle it twists. A step moves along one or two
/// axes. Shared by the boot order, the check ripple and the power-down wave.
[[nodiscard]] std::vector<int> cellDistances(const VariantSpec& v,
                                             std::span<const CellId> sources);

}  // namespace cb::view
