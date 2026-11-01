// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/board_boot.hpp"

#include <algorithm>
#include <cmath>

#include "view/cell_distances.hpp"

namespace cb::view {

namespace {

float smooth(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

float unit(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

/// A dip of depth `1 - floor` and width `kBootFlickerSeconds` starting at `start`.
float dip(float age, float start) {
  const float x = (age - start) / kBootFlickerSeconds;
  if (x <= 0.0f || x >= 1.0f) return 1.0f;
  return 1.0f - (1.0f - kBootFlickerFloor) * (1.0f - std::abs(2.0f * x - 1.0f));
}

}  // namespace

BootState bootState(float bootAge, float d, float intensity, std::uint32_t seed) {
  BootState s;
  if (!(bootAge < kBootSeconds)) return s;  // settled (also NaN)
  if (bootAge < 0.0f) bootAge = 0.0f;
  d = std::clamp(d, 0.0f, 1.0f);
  s.bezel = std::clamp(bootAge / kBootBezelSeconds, 0.0f, 1.0f);
  s.backdrop = smooth(bootAge / kBootBackdropSeconds);

  if (intensity < 0.5f) {
    // Calm: cells and pieces come up together, no flicker, overshoot or sweep.
    const float f = smooth(bootAge / kBootCalmSeconds);
    s.power = kBootDarkLevel + (1.0f - kBootDarkLevel) * f;
    s.reveal = f;
    return s;
  }

  const float on = kBootCellOnStart + kBootCellOnSpan * d;
  if (bootAge < on) {
    s.power = kBootDarkLevel;
  } else {
    const float a = bootAge - on;
    s.onAge = a;
    s.power = a < kBootOvershootSeconds ? kBootOvershoot : 1.0f;
    const float f1 = kBootOvershootSeconds + 0.06f * unit(seed ^ 0x9e3779b9u);
    const float f2 = f1 + kBootFlickerSeconds + 0.03f + 0.05f * unit(seed ^ 0x85ebca6bu);
    s.flicker = std::min(dip(a, f1), dip(a, f2));
  }

  const float start = kBootPieceStart + kBootPieceSpan * d;
  const float done = start + kBootRevealSeconds;
  if (bootAge < start) {
    s.reveal = 0.0f;
    s.heat = smooth((bootAge - (start - kBootHeatRamp)) / kBootHeatRamp);
  } else if (bootAge < done) {
    s.reveal = (bootAge - start) / kBootRevealSeconds;
    s.heat = 1.0f;
  } else {
    s.reveal = 1.0f;
    s.heat = 1.0f - smooth((bootAge - done) / kBootHeatEase);
  }
  return s;
}

std::vector<float> bootCellOrder(const VariantSpec& v) {
  const std::uint32_t count = v.dims.cellCount();
  std::vector<float> order(count, 0.0f);
  if (count == 0) return order;
  const std::uint8_t n = v.dims.dims();
  if (v.geom.isBox()) {
    // Layer (axes 2.., axis 2 most significant) then rank (axis 1).
    std::vector<double> key(count, 0.0);
    double hi = 0.0;
    for (CellId c = 0; c < count; ++c) {
      const Coord p = v.dims.toCoord(c);
      if (n < 2) {
        key[c] = p.c[0];
      } else {
        double layer = 0.0;
        for (std::uint8_t a = 2; a < n; ++a) layer = layer * v.dims.extent(a) + p.c[a];
        key[c] = layer * v.dims.extent(1) + p.c[1];
      }
      hi = std::max(hi, key[c]);
    }
    for (CellId c = 0; c < count; ++c)
      order[c] = hi > 0.0 ? static_cast<float>(key[c] / hi) : 0.0f;
    return order;
  }
  std::vector<CellId> sources;
  for (CellId c = 0; c < count; ++c) {
    const Coord p = v.dims.toCoord(c);
    if (n < 2 ? p.c[0] == 0 : p.c[1] == 0) sources.push_back(c);
  }
  const std::vector<int> dist = cellDistances(v, sources);
  int hi = 0;
  for (const int x : dist) hi = std::max(hi, x);
  for (CellId c = 0; c < count; ++c)
    order[c] = hi > 0 && dist[c] > 0 ? static_cast<float>(dist[c]) / static_cast<float>(hi)
                                     : 0.0f;
  return order;
}

}  // namespace cb::view
