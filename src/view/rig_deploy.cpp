// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/rig_deploy.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <tuple>

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
  return std::max(0.0f,
                  1 - (sinceLock - kClampPulseRise) / (kClampPulseEnd - kClampPulseRise));
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

bool layoutFits(const Bounds& b, const RigXform& x, const DeployVolume& v) noexcept {
  const float m = kFitMarginCells;
  const auto lo = x.toLab({b.minX - m, b.minY - m, b.minZ - m});
  const auto hi = x.toLab({b.maxX + m, b.maxY + m, b.maxZ + m});
  constexpr float e = 1e-4f;
  return lo.x >= v.min.x - e && lo.y >= v.min.y - e && lo.z >= v.min.z - e &&
         hi.x <= v.max.x + e && hi.y <= v.max.y + e && hi.z <= v.max.z + e;
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
    while (g < axes.size() &&
           std::hypot(axes[g].x - axis.x, axes[g].y - axis.y) > kShareMastCells)
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

float departureStep(int slots) noexcept {
  return std::min(kDeployStagger,
                  kDeployDepartureSpan / static_cast<float>(std::max(1, slots - 1)));
}

float deployLastLock(int slots) noexcept {
  const int n = std::max(1, slots);
  return kDeployFirstDeparture + departureStep(n) * static_cast<float>(n - 1) +
         kSlabLockSeconds;
}

float stowTravelSeconds(int slots) noexcept {
  const int n = std::max(1, slots);
  return departureStep(n) * static_cast<float>(n - 1) + kSlabLockSeconds;
}

namespace {
constexpr float kCellsPerLabMetre = 20.0f;
/// Two masts this close in x (metres) hang from one bridge.
constexpr float kSameColumnMetres = 1e-3f;

/// The arms a group's hardware spans: its merged feet, or its own mast foot.
std::vector<Vec3> armsOf(const MastGroup& g) {
  return g.rack() ? g.arms : std::vector<Vec3>{g.mastFoot};
}
struct Span {
  float minX, maxX, minY, maxY;
};
Span spanOf(const MastGroup& g) {
  Span s{g.mastFoot.x, g.mastFoot.x, g.mastFoot.y, g.mastFoot.y};
  for (const auto& a : armsOf(g)) {
    s.minX = std::min(s.minX, a.x);
    s.maxX = std::max(s.maxX, a.x);
    s.minY = std::min(s.minY, a.y);
    s.maxY = std::max(s.maxY, a.y);
  }
  return s;
}
/// Does the pair break a spacing rule? (board units; `m` = board units per metre)
bool offends(const MastGroup& a, const MastGroup& b, float m) noexcept {
  const float dx = std::abs(a.mastFoot.x - b.mastFoot.x);
  const float dy = std::abs(a.mastFoot.y - b.mastFoot.y);
  if (dx <= kSameColumnMetres * m) {
    if (dy < kBridgeSpacingMetres * m) return true;
  } else if (dx < kColumnSpacingMetres * m) {
    return true;
  }
  if (!a.rack() && !b.rack()) return false;
  // A rack's T-bars span its arms: two spans (grown by half a column) must not overlap.
  const float pad = .5f * kColumnSpacingMetres * m;
  const Span p = spanOf(a), q = spanOf(b);
  return p.minX - pad < q.maxX + pad && q.minX - pad < p.maxX + pad &&
         p.minY - pad < q.maxY + pad && q.minY - pad < p.maxY + pad;
}
}  // namespace

bool spacingHolds(const std::vector<MastGroup>& groups, float labScale) noexcept {
  const float m = kCellsPerLabMetre * labScale;
  for (std::size_t i = 0; i < groups.size(); ++i)
    for (std::size_t j = i + 1; j < groups.size(); ++j)
      if (offends(groups[i], groups[j], m)) return false;
  return true;
}

std::vector<MastGroup> routeGroups(std::vector<MastGroup> groups, const DeployInfo&,
                                   float labScale) {
  const float m = kCellsPerLabMetre * labScale;
  for (;;) {
    std::size_t bi = 0, bj = 0;
    float best = 1e30f;
    for (std::size_t i = 0; i < groups.size(); ++i)
      for (std::size_t j = i + 1; j < groups.size(); ++j) {
        if (!offends(groups[i], groups[j], m)) continue;
        const float d = std::hypot(groups[i].mastFoot.x - groups[j].mastFoot.x,
                                   groups[i].mastFoot.y - groups[j].mastFoot.y);
        if (d < best) {
          best = d;
          bi = i;
          bj = j;
        }
      }
    if (best >= 1e29f) break;
    MastGroup& a = groups[bi];
    const MastGroup b = std::move(groups[bj]);
    auto arms = armsOf(a);
    for (const auto& arm : armsOf(b)) arms.push_back(arm);
    a.slabs.insert(a.slabs.end(), b.slabs.begin(), b.slabs.end());
    a.arms = std::move(arms);
    float lo = 1e30f, hi = -1e30f, far = -1e30f, foot = 1e30f;
    for (const auto& arm : a.arms) {
      lo = std::min(lo, arm.x);
      hi = std::max(hi, arm.x);
      far = std::max(far, arm.y);
      foot = std::min(foot, arm.z);
    }
    a.mastFoot = {(lo + hi) * .5f, far, foot};
    groups.erase(groups.begin() + static_cast<std::ptrdiff_t>(bj));
  }
  std::stable_sort(groups.begin(), groups.end(),
                   [](const MastGroup& a, const MastGroup& b) {
                     const auto first = [](const MastGroup& g) {
                       return *std::min_element(g.slabs.begin(), g.slabs.end());
                     };
                     return first(a) < first(b);
                   });
  return groups;
}

Departures departureOrder(const std::vector<MastGroup>& groups, const Vec3& magazine) {
  // Columns by x; each column's distance is its nearest mast's.
  std::vector<std::pair<float, float>> columns;  // (x, distance)
  std::vector<std::size_t> columnOf(groups.size());
  for (std::size_t g = 0; g < groups.size(); ++g) {
    const auto& f = groups[g].mastFoot;
    const float d = std::hypot(f.x - magazine.x, f.y - magazine.y);
    std::size_t c = 0;
    while (c < columns.size() && std::abs(columns[c].first - f.x) > 1e-3f) ++c;
    if (c == columns.size())
      columns.emplace_back(f.x, d);
    else
      columns[c].second = std::min(columns[c].second, d);
    columnOf[g] = c;
  }
  std::vector<std::size_t> order(columns.size());
  for (std::size_t c = 0; c < order.size(); ++c) order[c] = c;
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return columns[a].second < columns[b].second;
  });
  std::vector<int> rank(columns.size());
  for (std::size_t i = 0; i < order.size(); ++i) rank[order[i]] = static_cast<int>(i);
  Departures out;
  out.count = static_cast<int>(columns.size());
  for (std::size_t g = 0; g < groups.size(); ++g) out.slot.push_back(rank[columnOf[g]]);
  return out;
}

float relayoutTravelSeconds(float distanceCells) noexcept {
  return 0.5f + 0.4f * std::clamp(distanceCells / kCellsPerLabMetre, 0.0f, 1.0f);
}

float relayoutSeconds(const RelayoutSpec& s) noexcept {
  return kRelayoutLiftSeconds + relayoutTravelSeconds(length(s.to - s.from)) +
         kRelayoutLowerSeconds + kRelayoutSettleSeconds;
}

RelayoutPose slabRelayout(const RelayoutSpec& s, float t, float intensity) noexcept {
  RelayoutPose p;
  const float end = relayoutSeconds(s);
  const bool still =
      length(s.to - s.from) < 1e-6f && std::abs(s.toYaw - s.fromYaw) < 1e-6f;
  if (intensity < .5f || t >= end || still) {
    p.centre = s.to;
    p.yaw = s.toYaw;
    p.settled = intensity < .5f || t >= end;
    return p;
  }
  // The turn takes the short way round.
  float turn = std::remainder(s.toYaw - s.fromYaw, 2 * std::numbers::pi_v<float>);
  const float travel = relayoutTravelSeconds(length(s.to - s.from));
  const float t1 = kRelayoutLiftSeconds, t2 = t1 + travel,
              t3 = t2 + kRelayoutLowerSeconds;
  const Vec3 up{0, 0, s.lift};
  if (t <= 0) {
    p.centre = s.from;
    p.yaw = s.fromYaw;
  } else if (t < t1) {
    p.centre = s.from + up * easeInOut(t / t1);
    p.yaw = s.fromYaw;
  } else if (t < t2) {
    const float f = rigEase((t - t1) / travel);
    p.centre = lerp3(s.from, s.to, f) + up;
    p.yaw = s.fromYaw + turn * f;
  } else if (t < t3) {
    p.centre = s.to + up * (1 - easeInOut((t - t2) / kRelayoutLowerSeconds));
    p.yaw = s.fromYaw + turn;
  } else {
    const float v = (t - t3) / kRelayoutSettleSeconds;
    p.centre = s.to;
    p.centre.z -= kSlabOvershoot * std::exp(-kSettleDamping * v) *
                  std::sin(std::numbers::pi_v<float> * v * 2);
    p.yaw = s.fromYaw + turn;
  }
  return p;
}

std::vector<std::uint32_t> slabOfPlacements(const std::vector<Placement>& placements,
                                            const std::vector<SlabSpec>& slabs) {
  std::map<std::pair<std::uint32_t, long>, std::uint32_t> index;
  for (std::uint32_t i = 0; i < slabs.size(); ++i)
    index[{slabs[i].slice, std::lround(static_cast<double>(slabs[i].z) * 1000)}] = i;
  std::vector<std::uint32_t> out;
  out.reserve(placements.size());
  for (const auto& p : placements) {
    const auto it = index.find({p.slice, std::lround(static_cast<double>(p.z) * 1000)});
    out.push_back(it == index.end() ? 0u : it->second);
  }
  return out;
}

std::vector<SlabMatch> matchSlabs(const std::vector<Placement>& oldP,
                                  const std::vector<SlabSpec>& oldS,
                                  const std::vector<Placement>& newP,
                                  const std::vector<SlabSpec>& newS) {
  const auto oldOf = slabOfPlacements(oldP, oldS);
  const auto newOf = slabOfPlacements(newP, newS);
  std::map<CellId, std::size_t> oldAt;
  for (std::size_t i = 0; i < oldP.size(); ++i) oldAt[oldP[i].cell] = i;
  // Shared cells per (new, old) pair.
  std::map<std::pair<std::uint32_t, std::uint32_t>, int> shared;
  for (std::size_t i = 0; i < newP.size(); ++i)
    if (const auto it = oldAt.find(newP[i].cell); it != oldAt.end())
      ++shared[{newOf[i], oldOf[it->second]}];
  std::vector<std::tuple<int, std::uint32_t, std::uint32_t>> pairs;
  for (const auto& [k, n] : shared) pairs.emplace_back(-n, k.first, k.second);
  std::sort(pairs.begin(), pairs.end());
  std::vector<int> fromOf(newS.size(), -1);
  std::vector<bool> used(oldS.size(), false);
  for (const auto& [n, to, from] : pairs) {
    if (fromOf[to] >= 0 || used[from]) continue;
    fromOf[to] = static_cast<int>(from);
    used[from] = true;
  }
  const auto centre = [](const SlabSpec& s) {
    return std::pair{(s.minX + s.maxX) * .5f, (s.minY + s.maxY) * .5f};
  };
  std::vector<SlabMatch> out;
  for (std::uint32_t to = 0; to < newS.size(); ++to) {
    SlabMatch m{fromOf[to], static_cast<int>(to), 0};
    if (m.from >= 0) {
      // Best-fit rotation of the shared cells' offsets, old to new.
      const auto [ox, oy] = centre(oldS[static_cast<std::size_t>(m.from)]);
      const auto [nx, ny] = centre(newS[to]);
      double dot = 0, cross = 0;
      for (std::size_t i = 0; i < newP.size(); ++i) {
        if (newOf[i] != to) continue;
        const auto it = oldAt.find(newP[i].cell);
        if (it == oldAt.end() || oldOf[it->second] != static_cast<std::uint32_t>(m.from))
          continue;
        const auto& o = oldP[it->second];
        const double ax = o.x - ox, ay = o.y - oy, bx = newP[i].x - nx,
                     by = newP[i].y - ny;
        dot += ax * bx + ay * by;
        cross += ax * by - ay * bx;
      }
      if (std::abs(dot) + std::abs(cross) > 1e-9)
        m.yaw = static_cast<float>(std::atan2(cross, dot));
    }
    out.push_back(m);
  }
  for (std::uint32_t from = 0; from < oldS.size(); ++from)
    if (!used[from]) out.push_back({static_cast<int>(from), -1, 0});
  return out;
}

std::vector<Vec3> cablePoints(const Vec3& a, const Vec3& b, int segments, float sag) {
  // A catenary cosh(k(2u - 1)) with k = 1, scaled so its middle hangs `sag` x span.
  const float span = length(b - a);
  const float k = 1.0f, top = std::cosh(k);
  std::vector<Vec3> out;
  const int n = std::max(1, segments);
  for (int i = 0; i <= n; ++i) {
    const float u = static_cast<float>(i) / static_cast<float>(n);
    const float drop = sag * span * (top - std::cosh(k * (2 * u - 1))) / (top - 1);
    out.push_back(lerp3(a, b, u) - Vec3{0, 0, drop});
  }
  out.front() = a;
  out.back() = b;
  return out;
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
