// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/seams.hpp"

#include <algorithm>
#include <cmath>

namespace cb::view {
namespace {

/// HSV with hue in turns. The ramp has to sweep hues rather than interpolate two
/// colours: a straight RGB lerp from cyan to magenta passes through grey, and a grey
/// seam is a seam a player cannot match to its other end.
Rgba fromHsv(float hueTurns, float s, float v, float a = 1.0f) {
  const float h = (hueTurns - std::floor(hueTurns)) * 6.0f;
  const int sector = static_cast<int>(h);
  const float f = h - static_cast<float>(sector);
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));
  switch (sector) {
    case 0:
      return Rgba{v, t, p, a};
    case 1:
      return Rgba{q, v, p, a};
    case 2:
      return Rgba{p, v, t, a};
    case 3:
      return Rgba{p, q, v, a};
    case 4:
      return Rgba{t, p, v, a};
    default:
      return Rgba{v, p, q, a};
  }
}

/// One outward unit step along an axis.
Direction outward(std::uint8_t axis, Side side, std::uint8_t dims) {
  std::array<std::int16_t, kMaxDims> v{};
  v[axis] = side == Side::Max ? 1 : -1;
  return Direction::make(v, dims);
}

}  // namespace

Rgba seamRampColor(const Theme& theme, float t) {
  const float clamped = std::clamp(t, 0.0f, 1.0f);
  const float hue =
      theme.seamHueBegin + (theme.seamHueEnd - theme.seamHueBegin) * clamped;
  return fromHsv(hue, theme.seamSaturation, theme.seamValue);
}

SeamMap SeamMap::build(const VariantSpec& v, const ViewConfig& cfg, const Theme& theme) {
  SeamMap out;
  if (v.geom.isBox()) return out;

  const std::uint8_t dims = v.dims.dims();
  const std::size_t drawn = std::min<std::size_t>(cfg.screenAxes.size(), 2);

  for (std::size_t i = 0; i < drawn; ++i) {
    const std::uint8_t axis = cfg.screenAxes[i];
    // The other drawn axis is what a seam runs *along*, and therefore what the ramp is
    // keyed to. With only one drawn axis there is nowhere for a ramp to go.
    const int tangent = drawn > 1 ? static_cast<int>(cfg.screenAxes[1 - i]) : -1;
    const float tangentSpan =
        tangent >= 0 ? static_cast<float>(std::max<int>(
                           v.dims.extent(static_cast<std::size_t>(tangent)) - 1, 1))
                     : 1.0f;

    for (int s = 0; s < 2; ++s) {
      const Side side = s == 0 ? Side::Min : Side::Max;
      const Transform* xf = v.geom.faceTransform(axis, side);
      if (xf == nullptr) continue;
      const Direction dir = outward(axis, side, dims);
      const std::int16_t face =
          side == Side::Min ? 0 : static_cast<std::int16_t>(v.dims.extent(axis) - 1);

      for (CellId c = 0; c < v.dims.cellCount(); ++c) {
        const Coord co = v.dims.toCoord(c);
        if (co.c[axis] != face) continue;

        Walker w = v.geom.start(c, dir);
        if (!v.geom.step(w)) continue;  // an open face after all

        SeamFace f;
        f.cell = c;
        f.screenAxis = static_cast<std::uint8_t>(i);
        f.side = side;
        f.partner = w.cell;
        // A mirror is the identification that sends a cell to itself: the ray comes
        // straight back. Asking the geometry beats reading the declaration, because the
        // declaration is what a composed transform can quietly contradict.
        f.kind = w.cell == c ? SeamKind::Mirror : SeamKind::Glued;
        if (f.kind == SeamKind::Mirror) {
          f.color = theme.mirrorEdge;
        } else {
          // Both ends of one portal key off the same cell, so both get one colour.
          //
          // Two facts have to survive: *which* portal this is, and *where along the
          // seam* it sits. Hue carries the second - it ramps along the seam, so a
          // cylinder's two edges read as one gradient repeated - and brightness
          // carries the first, because a twisted seam pairs a cell on one face with a
          // cell on the same face, and hue alone would then give two different portals
          // the same colour. Each drawn axis gets its own stretch of the arc so a
          // Klein bottle's two seams cannot be confused with each other.
          const CellId canonical = std::min(c, w.cell);
          const Coord k = v.dims.toCoord(canonical);
          const float along =
              tangent >= 0 ? static_cast<float>(k.c[static_cast<std::size_t>(tangent)]) /
                                 tangentSpan
                           : 0.5f;
          const float slot = (static_cast<float>(i) + along) / static_cast<float>(drawn);
          f.color = seamRampColor(theme, slot);
          if (k.c[axis] != 0) {
            // The near end of this portal is the far face, not the near one: dim it, so
            // the pairing stays visible when hue cannot carry it.
            f.color =
                Rgba{f.color.r * 0.55f, f.color.g * 0.55f, f.color.b * 0.55f, f.color.a};
          }
        }
        out.faces_.push_back(f);
      }
    }
  }

  std::sort(out.faces_.begin(), out.faces_.end(),
            [](const SeamFace& a, const SeamFace& b) {
              if (a.cell != b.cell) return a.cell < b.cell;
              if (a.screenAxis != b.screenAxis) return a.screenAxis < b.screenAxis;
              return a.side < b.side;
            });
  return out;
}

std::span<const SeamFace> SeamMap::at(CellId cell) const {
  const auto lo =
      std::lower_bound(faces_.begin(), faces_.end(), cell,
                       [](const SeamFace& f, CellId c) { return f.cell < c; });
  const auto hi = std::upper_bound(
      lo, faces_.end(), cell, [](CellId c, const SeamFace& f) { return c < f.cell; });
  return {faces_.data() + (lo - faces_.begin()), static_cast<std::size_t>(hi - lo)};
}

std::vector<SeamLegendEntry> seamLegend(const SeamMap& seams, const ViewConfig& cfg,
                                        const DimSpec& dims) {
  std::vector<SeamLegendEntry> out;
  for (const SeamFace& f : seams.faces()) {
    if (f.kind != SeamKind::Glued) continue;  // a mirror leads nowhere; no hue to explain
    const std::string axis =
        static_cast<std::size_t>(f.screenAxis) < cfg.screenAxes.size()
            ? std::string(dims.name(cfg.screenAxes[f.screenAxis]))
            : std::string("?");
    bool seen = false;
    for (const SeamLegendEntry& e : out) {
      if (e.axis == axis) {
        seen = true;
        break;
      }
    }
    if (!seen) out.push_back({axis, f.color, f.screenAxis});
  }
  return out;
}

}  // namespace cb::view
