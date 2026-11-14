// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/rig_deploy.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "view/rig_move.hpp"

namespace cb::view {
namespace {
constexpr float kFitMarginCells = 0.08f * 20.0f;  ///< 0.08 m per side
constexpr float kShareMastCells = 0.5f;
constexpr float kSettleDamping = 3.0f;
constexpr std::array<float, 4> kIdentity{0, 0, 0, 1};

float easeInOut(float x) noexcept {
  x = std::clamp(x, 0.0f, 1.0f);
  const float v = -2 * x + 2;
  return x < .5f ? 4 * x * x * x : 1 - v * v * v / 2;
}
Vec3 lerp3(const Vec3& a, const Vec3& b, float f) noexcept {
  return a + (b - a) * f;
}
std::array<float, 4> slerp(std::array<float, 4> a, const std::array<float, 4>& b,
                           float f) noexcept {
  float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (d < 0) {
    for (float& c : a) c = -c;
    d = -d;
  }
  float wa = 1 - f, wb = f;
  if (d < .9995f) {
    const float th = std::acos(std::clamp(d, -1.0f, 1.0f));
    const float s = std::sin(th);
    wa = std::sin((1 - f) * th) / s;
    wb = std::sin(f * th) / s;
  }
  std::array<float, 4> out{};
  float n = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    out[i] = wa * a[i] + wb * b[i];
    n += out[i] * out[i];
  }
  n = std::sqrt(n);
  for (float& c : out) c /= n;
  return out;
}
float clampPulse(float sinceLock) noexcept {
  if (sinceLock < 0) return 0;
  if (sinceLock < kClampPulseRise) return sinceLock / kClampPulseRise;
  return std::max(0.0f, 1 - (sinceLock - kClampPulseRise) /
                                (kClampPulseEnd - kClampPulseRise));
}
}  // namespace

Vec3 RigXform::toBoard(const Vec3& w) const noexcept {
  return labOrigin + w * labScale;
}
Vec3 RigXform::toLab(const Vec3& b) const noexcept {
  return (b - labOrigin) * (1.0f / labScale);
}

RigXform placeLayout(const Bounds& b, const DeployVolume& v) noexcept {
  RigXform x;
  x.labOrigin = {b.centerX(), b.centerY(), b.minZ};
  const float hx = (b.maxX - b.minX) * .5f + kFitMarginCells;
  const float hy = (b.maxY - b.minY) * .5f + kFitMarginCells;
  const float up = b.maxZ - b.minZ + kFitMarginCells;
  const float ax = std::min(-v.min.x, v.max.x), ay = std::min(-v.min.y, v.max.y);
  float s = 1;
  if (ax > 0) s = std::max(s, hx / ax);
  if (ay > 0) s = std::max(s, hy / ay);
  if (v.max.z > 0) s = std::max(s, up / v.max.z);
  if (-v.min.z > 0) s = std::max(s, kFitMarginCells / -v.min.z);
  x.labScale = s;
  return x;
}

std::vector<SlabSpec> slabsOf(const std::vector<Placement>& placements,
                              float bezelMargin) {
  // One slab per flat level of a slice: a 3-D board is one slice of stacked levels.
  std::map<std::pair<std::uint32_t, long>, SlabSpec> bySlice;
  for (const Placement& p : placements) {
    auto [it, fresh] =
        bySlice.try_emplace({p.slice, std::lround(static_cast<double>(p.z) * 1000)});
    SlabSpec& s = it->second;
    if (fresh) {
      s = {p.slice, p.x, p.x, p.y, p.y, p.z};
      continue;
    }
    s.minX = std::min(s.minX, p.x);
    s.maxX = std::max(s.maxX, p.x);
    s.minY = std::min(s.minY, p.y);
    s.maxY = std::max(s.maxY, p.y);
    s.z = std::min(s.z, p.z);
  }
  std::vector<SlabSpec> out;
  out.reserve(bySlice.size());
  const float m = .5f + bezelMargin;
  for (auto& [key, s] : bySlice) {
    s.minX -= m;
    s.maxX += m;
    s.minY -= m;
    s.maxY += m;
    out.push_back(s);
  }
  return out;
}

std::vector<MastGroup> mastGroups(const std::vector<SlabSpec>& slabs, const RigXform& x,
                                  const DeployInfo& d) {
  std::vector<MastGroup> groups;
  std::vector<Vec3> axes;
  for (std::uint32_t i = 0; i < slabs.size(); ++i) {
    const SlabSpec& s = slabs[i];
    const Vec3 axis{(s.minX + s.maxX) * .5f, s.maxY + d.mastEdgeGap, 0};
    std::size_t g = 0;
    while (g < axes.size() && std::hypot(axes[g].x - axis.x, axes[g].y - axis.y) >
                                  kShareMastCells)
      ++g;
    if (g == axes.size()) {
      axes.push_back(axis);
      groups.emplace_back();
    }
    groups[g].slabs.push_back(i);
  }
  const float top = x.toBoard({0, 0, d.railZ}).z;
  for (std::size_t g = 0; g < groups.size(); ++g) {
    auto& list = groups[g].slabs;
    std::stable_sort(list.begin(), list.end(), [&](std::uint32_t a, std::uint32_t b) {
      return slabs[a].z > slabs[b].z;
    });
    const float lowest = slabs[list.back()].z;
    groups[g].mastFoot = {axes[g].x, axes[g].y, lowest - kSlabBelowFace - d.yokeDrop};
    groups[g].mastTopZ = top;
  }
  return groups;
}

SlabPose slabDeploy(const SlabPathSpec& s, float t, float intensity) noexcept {
  SlabPose p;
  const auto mastTo = [&](float centreZ) {
    return std::max(0.0f, s.railZ - (centreZ - s.yokeBelow));
  };
  const auto carriageAt = [&](const Vec3& centre) {
    return Vec3{centre.x + s.mastOffset.x, centre.y + s.mastOffset.y, s.railZ};
  };
  if (intensity < .5f || t - s.departure >= kSlabLockSeconds) {
    p.centre = s.hangCentre;
    p.quat = kIdentity;
    p.carriage = carriageAt(s.hangCentre);
    p.mastLength = mastTo(s.hangCentre.z);
    p.clamped = true;
    p.inBay = false;
    if (intensity >= .5f) p.clampPulse = clampPulse(t - s.departure - kSlabLockSeconds);
    return p;
  }
  const float u = t - s.departure;
  const float liftLength = mastTo(s.liftZ);
  p.inBay = u < 0;
  p.quat = s.bayQuat;
  if (u < kSlabSlideEnd) {
    // The carriage runs from where it was parked to above the pick-up point while the
    // slab slides out of its slot, lowering its mast to meet it.
    const float reach = s.departure + kSlabSlideEnd;
    const float f = reach > 0 ? rigEase(std::max(0.0f, t) / reach) : 1.0f;
    const Vec3 pick = carriageAt(s.pickCentre);
    p.carriage = {std::lerp(s.parkCarriage.x, pick.x, f),
                  std::lerp(s.parkCarriage.y, pick.y, f), s.railZ};
    const float slide = u < 0 ? 0.0f : rigEase(u / kSlabSlideEnd);
    p.centre = lerp3(s.bayCentre, s.pickCentre, slide);
    p.mastLength = std::lerp(liftLength, mastTo(s.pickCentre.z), slide);
    return p;
  }
  if (u < kSlabTiltEnd) {
    // Tilt from edge-on to face-up while the mast shortens to just under the rail.
    const float f = easeInOut((u - kSlabSlideEnd) / (kSlabTiltEnd - kSlabSlideEnd));
    p.quat = slerp(s.bayQuat, kIdentity, f);
    p.centre = {s.pickCentre.x, s.pickCentre.y, std::lerp(s.pickCentre.z, s.liftZ, f)};
  } else if (u < kSlabTravelEnd) {
    const float f = rigEase((u - kSlabTiltEnd) / (kSlabTravelEnd - kSlabTiltEnd));
    p.quat = kIdentity;
    p.centre = {std::lerp(s.pickCentre.x, s.hangCentre.x, f),
                std::lerp(s.pickCentre.y, s.hangCentre.y, f), s.liftZ};
  } else if (u < kSlabLowerEnd) {
    const float f = easeInOut((u - kSlabTravelEnd) / (kSlabLowerEnd - kSlabTravelEnd));
    p.quat = kIdentity;
    p.centre = {s.hangCentre.x, s.hangCentre.y, std::lerp(s.liftZ, s.hangCentre.z, f)};
  } else {
    // A damped 2.5 mm overshoot downwards and back.
    const float v = (u - kSlabLowerEnd) / (kSlabLockSeconds - kSlabLowerEnd);
    p.quat = kIdentity;
    p.centre = s.hangCentre;
    p.centre.z -= kSlabOvershoot * std::exp(-kSettleDamping * v) *
                  std::sin(std::numbers::pi_v<float> * v * 2);
  }
  p.carriage = carriageAt(p.centre);
  p.mastLength = mastTo(p.centre.z);
  return p;
}

float deployLastLock(int groups) noexcept {
  if (groups <= 0 || groups > kMaxTravellingGroups) return kStaticLockSeconds;
  return kDeployFirstDeparture + kDeployStagger * static_cast<float>(groups - 1) +
         kSlabLockSeconds;
}

float stowTravelSeconds(int groups) noexcept {
  if (groups <= 0 || groups > kMaxTravellingGroups) return kStaticLockSeconds;
  return kDeployStagger * static_cast<float>(groups - 1) + kSlabLockSeconds;
}

float clampOrbitDistance(const OrbitCamera& c, float wanted, const Bounds& room,
                         float inset) noexcept {
  OrbitCamera unit = c;
  unit.distance = 1;
  const Vec3 dir = unit.eye() - unit.target;
  const float lo[3]{room.minX + inset, room.minY + inset, room.minZ + inset};
  const float hi[3]{room.maxX - inset, room.maxY - inset, room.maxZ - inset};
  const float at[3]{c.target.x, c.target.y, c.target.z};
  const float dv[3]{dir.x, dir.y, dir.z};
  float limit = wanted;
  for (int a = 0; a < 3; ++a) {
    if (dv[a] > 1e-6f)
      limit = std::min(limit, (hi[a] - at[a]) / dv[a]);
    else if (dv[a] < -1e-6f)
      limit = std::min(limit, (lo[a] - at[a]) / dv[a]);
  }
  return limit >= wanted ? wanted : std::max(0.0f, limit);
}

float wallFade(const Bounds& part, const Vec3& eye, const Vec3& target,
               float pad) noexcept {
  const float lo[3]{part.minX, part.minY, part.minZ};
  const float hi[3]{part.maxX, part.maxY, part.maxZ};
  const float e[3]{eye.x, eye.y, eye.z};
  const float d[3]{target.x - eye.x, target.y - eye.y, target.z - eye.z};
  // Exact segment/box intersection first (slab test), so a blocking part is the floor.
  float t0 = 0, t1 = 1;
  bool hit = true;
  for (int a = 0; a < 3 && hit; ++a) {
    if (std::abs(d[a]) < 1e-9f) {
      hit = e[a] >= lo[a] && e[a] <= hi[a];
    } else {
      float ta = (lo[a] - e[a]) / d[a], tb = (hi[a] - e[a]) / d[a];
      if (ta > tb) std::swap(ta, tb);
      t0 = std::max(t0, ta);
      t1 = std::min(t1, tb);
      hit = t0 <= t1;
    }
  }
  if (hit) return kWallFadeFloor;
  // Distance from a convex box along a segment is convex: a ternary search finds it.
  const auto dist = [&](float t) {
    float sq = 0;
    for (int a = 0; a < 3; ++a) {
      const float p = e[a] + d[a] * t;
      const float out = std::max({lo[a] - p, 0.0f, p - hi[a]});
      sq += out * out;
    }
    return std::sqrt(sq);
  };
  float a = 0, b = 1;
  for (int i = 0; i < 80; ++i) {
    const float m1 = a + (b - a) / 3, m2 = b - (b - a) / 3;
    if (dist(m1) < dist(m2))
      b = m2;
    else
      a = m1;
  }
  const float s = std::min({dist(0), dist(1), dist((a + b) * .5f)});
  if (!(pad > 0) || s >= pad) return 1.0f;
  const float x = s / pad;
  return kWallFadeFloor + (1 - kWallFadeFloor) * x * x * (3 - 2 * x);
}

}  // namespace cb::view
