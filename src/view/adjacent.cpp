// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/adjacent.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cb::view {

namespace {

/// Fold the covering-space coordinate `q` back onto the board through the glued faces,
/// composing the face transforms in the order the engine crosses them (Geometry::
/// stepBoundary: lowest axis first). Returns false when a face on the way is not glued.
bool fold(const Geometry& g, const DimSpec& d, Coord q, Transform& acc) {
  const int guard = 2 * kMaxDims;
  for (int k = 0; k < guard; ++k) {
    int axis = -1;
    Side side = Side::Max;
    for (std::uint8_t a = 0; a < d.dims(); ++a) {
      if (q.c[a] >= d.extent(a)) {
        axis = a;
        side = Side::Max;
        break;
      }
      if (q.c[a] < 0) {
        axis = a;
        side = Side::Min;
        break;
      }
    }
    if (axis < 0) return true;
    const auto a = static_cast<std::uint8_t>(axis);
    if (g.boundaryKind(a, side) != BoundaryKind::Periodic) return false;
    const Transform* xf = g.faceTransform(a, side);
    if (xf == nullptr) return false;
    q = xf->applyToCoord(q);
    acc = xf->compose(acc);
  }
  return false;
}

}  // namespace

std::vector<AdjacentCopy> adjacentCopies(const VariantSpec& v, const ViewConfig& cfg,
                                         int rings) {
  std::vector<AdjacentCopy> out;
  if (rings < 1 || cfg.screenAxes.size() != 2) return out;
  rings = std::min(rings, 3);
  const DimSpec& d = v.dims;
  const std::uint8_t au = cfg.screenAxes[0];
  const std::uint8_t av = cfg.screenAxes[1];

  bool any = false;
  for (int su = -rings; su <= rings; ++su) {
    for (int sv = -rings; sv <= rings; ++sv) {
      AdjacentCopy c;
      c.stepU = static_cast<std::int8_t>(su);
      c.stepV = static_cast<std::int8_t>(sv);
      c.order = static_cast<std::uint8_t>(std::max(std::abs(su), std::abs(sv)));
      // The copy-local frame is the central one, shifted out by whole boards.
      c.viaSeams = Transform::identity(d.dims());
      c.viaSeams.off[au] = static_cast<std::int16_t>(su * d.extent(au));
      c.viaSeams.off[av] = static_cast<std::int16_t>(sv * d.extent(av));
      if (su != 0 || sv != 0) {
        // A representative cell in the middle of the copy; which faces it crosses, and in
        // what order, is the same for every cell of it.
        Coord q(d.dims());
        for (std::uint8_t a = 0; a < d.dims(); ++a) {
          q.c[a] = static_cast<std::int16_t>(d.extent(a) / 2);
        }
        q.c[au] = static_cast<std::int16_t>(q.c[au] + su * d.extent(au));
        q.c[av] = static_cast<std::int16_t>(q.c[av] + sv * d.extent(av));
        Transform acc = Transform::identity(d.dims());
        if (!fold(v.geom, d, q, acc)) continue;
        c.viaSeams = acc.compose(c.viaSeams);
        any = true;
      }
      out.push_back(c);
    }
  }
  if (!any) return {};

  std::sort(out.begin(), out.end(), [](const AdjacentCopy& a, const AdjacentCopy& b) {
    if (a.order != b.order) return a.order > b.order;
    if (a.stepU != b.stepU) return a.stepU < b.stepU;
    return a.stepV < b.stepV;
  });
  return out;
}

Placement copyPlacement(const AdjacentCopy& copy, const Placement& central,
                        const VariantSpec& v, const ViewConfig& cfg) {
  Placement p = central;
  p.x += static_cast<float>(copy.stepU * v.dims.extent(cfg.screenAxes[0]));
  p.y += static_cast<float>(copy.stepV * v.dims.extent(cfg.screenAxes[1]));
  p.cell = sourceCell(copy, central.cell, v);
  return p;
}

CellId sourceCell(const AdjacentCopy& copy, CellId copyLocalCell, const VariantSpec& v) {
  return v.dims.toCell(copy.viaSeams.applyToCoord(v.dims.toCoord(copyLocalCell)));
}

float adjacentAlpha(int order) noexcept {
  if (order <= 0) return 1.0f;
  return kAdjacentAlpha * std::pow(kAdjacentFalloff, static_cast<float>(order - 1));
}

std::vector<AdjacentCopy> fitAdjacent(std::vector<AdjacentCopy> copies,
                                      std::size_t perBoard, std::size_t maxInstances) {
  int keep = 0;
  for (const AdjacentCopy& c : copies) keep = std::max(keep, static_cast<int>(c.order));
  const auto boards = [&](int rings) {
    return static_cast<std::size_t>(
        std::count_if(copies.begin(), copies.end(), [&](const AdjacentCopy& c) {
          return c.order >= 1 && c.order <= rings;
        }));
  };
  while (keep > 0 && boards(keep) * perBoard > maxInstances) --keep;
  std::erase_if(copies, [&](const AdjacentCopy& c) { return c.order > keep; });
  return copies;
}

std::vector<AdjacentToken> adjacentTokens(const PositionView& snapshot,
                                          const std::vector<AdjacentCopy>& copies,
                                          const std::vector<Placement>& central,
                                          const ViewConfig& cfg, float reveal,
                                          CellId skip) {
  std::vector<AdjacentToken> out;
  if (reveal <= 0.0f) return out;
  const VariantSpec& v = snapshot.variant();
  for (const AdjacentCopy& copy : copies) {
    if (copy.order == 0) continue;
    const float alpha = reveal * adjacentAlpha(copy.order);
    for (const Placement& pl : central) {
      const Placement at = copyPlacement(copy, pl, v, cfg);
      if (at.cell == skip) continue;
      const Piece piece = snapshot.at(at.cell);
      if (piece.empty()) continue;
      out.push_back({piece, at, copy.order, alpha});
    }
  }
  return out;
}

}  // namespace cb::view
