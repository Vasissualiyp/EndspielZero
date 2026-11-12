// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <span>

#include "variant/variant.hpp"
#include "view/layout.hpp"
#include "view/theme.hpp"

namespace cb::view {

/// What a player needs to know about one edge of one cell that is not really an edge.
///
/// A glued board is unreadable while every seam looks the same: a cylinder and a
/// Moebius band draw an identical pair of cyan rails, and the one fact that
/// distinguishes them - *where you come out* - is exactly the fact the rails hide.
///
/// So a seam is coloured by the **portal it belongs to**, not by the fact that it is a
/// seam. Both ends of one identification get the same colour, which is picked from a
/// ramp along the seam. On a cylinder the ramp then runs the same way down both edges;
/// on a Moebius band it runs in opposite directions, and the twist is visible before a
/// piece ever crosses it. A Klein bottle shows one matched pair and one reversed pair,
/// which is precisely what a Klein bottle is.
///
/// Mirrors are excluded from the ramp and drawn silver: nothing is on the other side,
/// so a hue that promises a destination would be a lie.
enum class SeamKind : std::uint8_t { Glued, Mirror };

struct SeamFace {
  CellId cell{kInvalidCell};
  /// Index into ViewConfig::screenAxes - which *drawn* axis this face is on.
  std::uint8_t screenAxis{0};
  Side side{Side::Max};
  SeamKind kind{SeamKind::Glued};
  Rgba color{};
  /// Where a piece leaving through this face arrives. Equal to `cell` for a mirror,
  /// which is what a reflection is: you arrive back where you left.
  CellId partner{kInvalidCell};
};

/// Every seam face of the drawn faces, sorted by cell.
///
/// Only the first two screen axes are included: those are the faces a board drawn flat
/// actually shows. A gluing on a grid axis - a periodic timeline, say - is real and the
/// engine honours it, but it joins whole sub-boards rather than cell edges and wants a
/// different drawing, which is M6 work.
class SeamMap {
 public:
  static SeamMap build(const VariantSpec& v, const ViewConfig& cfg, const Theme& theme);

  [[nodiscard]] bool empty() const noexcept { return faces_.empty(); }
  [[nodiscard]] const std::vector<SeamFace>& faces() const noexcept { return faces_; }
  /// The faces of one cell, or an empty span. O(log n) - the list is sorted by cell.
  [[nodiscard]] std::span<const SeamFace> at(CellId cell) const;

 private:
  std::vector<SeamFace> faces_;
};

/// The portal ramp, exposed so the same colour can be used by anything that has to
/// agree with the board: the move animation's portals, a legend, a minimap.
Rgba seamRampColor(const Theme& theme, float t);

/// One line of the on-screen seam legend: a drawn axis that is glued, and the colour its
/// edges are drawn in. The legend is what makes "the a-file and the h-file are the same
/// edge" sayable in the corner rather than something a player has to be told (M14.4).
struct SeamLegendEntry {
  std::string axis;  ///< the axis's own name, e.g. "file"
  Rgba color;
  /// Index into ViewConfig::screenAxes of the faces this entry stands for. The
  /// hover highlight (M23.12 section 1) selects faces by it.
  std::uint8_t screenAxis{0};
};

/// One entry per glued drawn axis, or empty when nothing is glued. Mirrors are excluded -
/// they are silver and lead nowhere, so a hue that promised a destination would be a lie.
[[nodiscard]] std::vector<SeamLegendEntry> seamLegend(const SeamMap& seams,
                                                      const ViewConfig& cfg,
                                                      const DimSpec& dims);

}  // namespace cb::view
