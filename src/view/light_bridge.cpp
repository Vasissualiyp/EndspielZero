// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/light_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

#include "view/seams.hpp"

namespace cb::view {
namespace {
struct Slab {
  std::uint32_t slice{0};
  float minX{0}, maxX{0}, minY{0}, maxY{0}, z{0};
  [[nodiscard]] Vec3 centre() const noexcept {
    return {(minX + maxX) * .5f, (minY + maxY) * .5f, z};
  }
};
long faceKey(float z) noexcept {
  return std::lround(static_cast<double>(z) * 1000);
}
}  // namespace

std::vector<LightBridge> lightBridges(const VariantSpec& v, const ViewConfig& cfg,
                                      const std::vector<Placement>& p,
                                      const Theme& theme) {
  std::vector<LightBridge> out;
  if (v.geom.isBox() || p.empty()) return out;
  // One slab per (slice, face height), as the rig hangs them.
  std::map<std::pair<std::uint32_t, long>, std::uint32_t> slabOf;
  std::vector<Slab> slabs;
  std::map<CellId, std::size_t> placed;
  for (std::size_t i = 0; i < p.size(); ++i) placed[p[i].cell] = i;
  std::vector<std::uint32_t> slabOfPlacement(p.size());
  for (std::size_t i = 0; i < p.size(); ++i) {
    const auto key = std::pair{p[i].slice, faceKey(p[i].z)};
    auto it = slabOf.find(key);
    if (it == slabOf.end()) {
      it = slabOf.emplace(key, 0).first;
      slabs.push_back({p[i].slice, p[i].x, p[i].x, p[i].y, p[i].y, p[i].z});
    }
    (void)it;
  }
  // Number the slabs in (slice, height) order, like `slabsOf`.
  {
    std::uint32_t n = 0;
    std::vector<Slab> ordered(slabs.size());
    for (auto& [key, index] : slabOf) {
      index = n;
      ordered[n] = {key.first, 1e30f, -1e30f, 1e30f, -1e30f, 0};
      ++n;
    }
    for (std::size_t i = 0; i < p.size(); ++i) {
      const auto s = slabOf.at({p[i].slice, faceKey(p[i].z)});
      slabOfPlacement[i] = s;
      auto& slab = ordered[s];
      slab.minX = std::min(slab.minX, p[i].x);
      slab.maxX = std::max(slab.maxX, p[i].x);
      slab.minY = std::min(slab.minY, p[i].y);
      slab.maxY = std::max(slab.maxY, p[i].y);
      slab.z = p[i].z;
    }
    const float m = .5f + kSlabBezelMargin;
    for (auto& s : ordered) {
      s.minX -= m;
      s.maxX += m;
      s.minY -= m;
      s.maxY += m;
    }
    slabs = std::move(ordered);
  }

  const std::uint8_t dims = v.dims.dims();
  const std::size_t drawn = std::min<std::size_t>(cfg.screenAxes.size(), 2);
  std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint8_t>, bool> seen;
  for (std::uint8_t ax = 0; ax < dims; ++ax) {
    bool onScreen = false;
    for (std::size_t i = 0; i < drawn; ++i)
      onScreen = onScreen || cfg.screenAxes[i] == ax;
    if (onScreen) continue;
    const Transform* xf = v.geom.faceTransform(ax, Side::Max);
    if (xf == nullptr) continue;
    const auto face = static_cast<std::int16_t>(v.dims.extent(ax) - 1);
    // Which way the axis runs in the layout: a +1 step between two placed cells.
    Vec3 dir{};
    for (std::size_t i = 0; i < p.size() && length(dir) < 1e-6f; ++i) {
      Coord c = v.dims.toCoord(p[i].cell);
      if (c.c[ax] >= face) continue;
      ++c.c[ax];
      const auto it = placed.find(v.dims.toCell(c));
      if (it == placed.end()) continue;
      const auto& q = p[it->second];
      dir = Vec3{q.x - p[i].x, q.y - p[i].y, q.z - p[i].z};
    }
    std::array<std::int16_t, kMaxDims> step{};
    step[ax] = 1;
    const Direction outward = Direction::make(step, dims);
    const float span = static_cast<float>(std::max<int>(dims - 1, 1));
    const Rgba color = seamRampColor(theme, static_cast<float>(ax) / span);
    for (std::size_t i = 0; i < p.size(); ++i) {
      const CellId c = p[i].cell;
      if (v.dims.toCoord(c).c[ax] != face) continue;
      Walker w = v.geom.start(c, outward);
      if (!v.geom.step(w) || w.cell == c) continue;
      const auto other = placed.find(w.cell);
      if (other == placed.end()) continue;
      const auto sa = slabOfPlacement[i], sb = slabOfPlacement[other->second];
      if (sa == sb || seen.count({sa, sb, ax}) != 0) continue;
      seen[{sa, sb, ax}] = true;
      const Slab& A = slabs[sa];
      const Slab& B = slabs[sb];
      const float gx = std::max({0.0f, B.minX - A.maxX, A.minX - B.maxX});
      const float gy = std::max({0.0f, B.minY - A.maxY, A.minY - B.maxY});
      const float gz = std::abs(A.z - B.z);
      if (std::sqrt(gx * gx + gy * gy + gz * gz) <= kBridgeMinGap) continue;
      LightBridge br;
      br.sliceA = A.slice;
      br.sliceB = B.slice;
      br.slabA = sa;
      br.slabB = sb;
      br.axis = ax;
      br.color = color;
      const Vec3 ca = A.centre(), cb = B.centre();
      const float ax_ = std::abs(dir.x), ay_ = std::abs(dir.y), az_ = std::abs(dir.z);
      if (az_ >= ax_ && az_ >= ay_) {
        // A stack of levels: both near (-Y) edges, the side the player stands on,
        // arching out towards them.
        br.a = {ca.x, A.minY, ca.z};
        br.b = {cb.x, B.minY, cb.z};
        const float s = length(br.b - br.a);
        br.control = (br.a + br.b) * .5f + Vec3{0, -.25f * s, 0};
      } else {
        if (ax_ >= ay_) {
          const bool up = dir.x >= 0;
          br.a = {up ? A.maxX : A.minX, ca.y, ca.z};
          br.b = {up ? B.minX : B.maxX, cb.y, cb.z};
        } else {
          const bool up = dir.y >= 0;
          br.a = {ca.x, up ? A.maxY : A.minY, ca.z};
          br.b = {cb.x, up ? B.minY : B.maxY, cb.z};
        }
        const float s = length(br.b - br.a);
        br.control = (br.a + br.b) * .5f + Vec3{0, 0, .25f * s};
      }
      out.push_back(br);
    }
  }
  std::sort(out.begin(), out.end(), [](const LightBridge& x, const LightBridge& y) {
    return std::tie(x.slabA, x.slabB, x.axis) < std::tie(y.slabA, y.slabB, y.axis);
  });
  return out;
}

Vec3 bridgePoint(const LightBridge& b, float u) noexcept {
  const float t = std::clamp(u, 0.0f, 1.0f), s = 1 - t;
  return b.a * (s * s) + b.control * (2 * s * t) + b.b * (t * t);
}

Vec3 bridgeRide(const BridgeRide& r, float progress) noexcept {
  const float t = std::clamp(progress, 0.0f, 1.0f);
  if (t <= kBridgeEnter) return r.start + (r.a - r.start) * (t / kBridgeEnter);
  if (t >= kBridgeLeave)
    return r.b + (r.end - r.b) * ((t - kBridgeLeave) / (1 - kBridgeLeave));
  LightBridge b;
  b.a = r.a;
  b.b = r.b;
  b.control = r.control;
  return bridgePoint(b, (t - kBridgeEnter) / (kBridgeLeave - kBridgeEnter));
}

}  // namespace cb::view
