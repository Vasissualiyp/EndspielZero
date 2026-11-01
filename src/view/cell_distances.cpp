// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/cell_distances.hpp"

#include <vector>

namespace cb::view {

namespace {

/// Every king step of up to two axes: the unit vectors and their pairwise diagonals.
std::vector<Direction> kingSteps(std::uint8_t dims) {
  std::vector<Direction> out;
  for (std::uint8_t a = 0; a < dims; ++a) {
    for (int sa : {-1, 1}) {
      std::array<std::int16_t, kMaxDims> vec{};
      vec[a] = static_cast<std::int16_t>(sa);
      out.push_back(Direction::make(vec, dims));
      for (std::uint8_t b = static_cast<std::uint8_t>(a + 1); b < dims; ++b) {
        for (int sb : {-1, 1}) {
          std::array<std::int16_t, kMaxDims> diag{};
          diag[a] = static_cast<std::int16_t>(sa);
          diag[b] = static_cast<std::int16_t>(sb);
          out.push_back(Direction::make(diag, dims));
        }
      }
    }
  }
  return out;
}

}  // namespace

std::vector<int> cellDistances(const VariantSpec& v, std::span<const CellId> sources) {
  const std::uint32_t count = v.dims.cellCount();
  std::vector<int> dist(count, -1);
  std::vector<CellId> frontier;
  for (const CellId s : sources) {
    if (s < count && dist[s] < 0) {
      dist[s] = 0;
      frontier.push_back(s);
    }
  }
  const std::vector<Direction> steps = kingSteps(v.dims.dims());
  std::vector<CellId> next;
  for (int level = 1; !frontier.empty(); ++level) {
    next.clear();
    for (const CellId cell : frontier) {
      for (const Direction& d : steps) {
        Walker w = v.geom.start(cell, d);
        if (!v.geom.step(w) || w.cell >= count || dist[w.cell] >= 0) continue;
        dist[w.cell] = level;
        next.push_back(w.cell);
      }
    }
    frontier.swap(next);
  }
  return dist;
}

}  // namespace cb::view
