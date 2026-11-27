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
constexpr std::array<float, 4> kIdentity{0, 0, 0, 1};

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
/// Zero velocity and acceleration at both ends (smootherstep).
float smoother(float x) noexcept {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * x * (x * (x * 6 - 15) + 10);
}
/// Integral of smootherstep, normalized to a broad velocity curve. The load creeps
/// off, cruises with a small continuing acceleration, peaks late (70%), then brakes.
/// Position, velocity and acceleration agree at every join.
float smoothIntegral(float x) noexcept {
  x = std::clamp(x, 0.f, 1.f);
  return x * x * x * x * (2.5f - 3 * x + x * x);
}
float heavyEase(float x) noexcept {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  if (x < .2f) return .18f * smoothIntegral(x / .2f) / .715f;
  if (x < .7f)
    return (.09f + .9f * (x - .2f) + .05f * smoothIntegral((x - .2f) / .5f)) / .715f;
  return (.565f + (x - .7f) - .3f * smoothIntegral((x - .7f) / .3f)) / .715f;
}
float smoothVelocity(float x) noexcept {
  if (x <= 0 || x >= 1) return 0;
  return 30 * x * x * (1 - x) * (1 - x);
}
float heavyAccel(float x) noexcept {
  if (x < .2f) return .9f / (.2f * .715f) * smoothVelocity(x / .2f);
  if (x < .7f) return .1f / (.5f * .715f) * smoothVelocity((x - .2f) / .5f);
  return -smoothVelocity((x - .7f) / .3f) / (.3f * .715f);
}
constexpr float kCellsPerMetre = 20.0f;
/// The swing's response window: the critically damped kernel w^2 t e^(-w t) is cut where
/// less than 2.2 % of its weight is left.
constexpr float kSwingWindow = 5.7f / kSlabSwingOmega;
constexpr int kSwingSamples = 48;

/// The lean the travel's acceleration asks for at path time `u`, in radians, positive
/// with the slab's top tipped back against the travel.
float leanDemand(const SlabPathSpec& s, float u) noexcept {
  const float x = (u - kSlabTravelStart) / (kSlabTravelEnd - kSlabTravelStart);
  if (x <= 0 || x >= 1) return 0;
  const float d =
      std::hypot(s.hangCentre.x - s.pickCentre.x, s.hangCentre.y - s.pickCentre.y);
  const float span = kSlabTravelEnd - kSlabTravelStart;
  const float acceleration =
      heavyAccel(x) * d / (kCellsPerMetre * s.labScale * span * span);
  // 1.25 m/s² asks for two degrees: visible even at the new gantry speeds.
  return (2.f / 1.25f) * std::numbers::pi_v<float> / 180 * acceleration;
}
/// The slab's lean at path time `u`: the demand through a critically damped response,
/// causal in the move's own time (a stow plays the path backwards).
float swingAngle(const SlabPathSpec& s, float u) noexcept {
  const float w = kSlabSwingOmega, ds = kSwingWindow / kSwingSamples;
  const float mass = 1 - std::exp(-w * kSwingWindow) * (1 + w * kSwingWindow);
  float sum = 0;
  for (int k = 0; k < kSwingSamples; ++k) {
    const float tau = (static_cast<float>(k) + .5f) * ds;
    sum +=
        w * w * tau * std::exp(-w * tau) * leanDemand(s, s.reversed ? u + tau : u - tau);
  }
  return sum * ds / mass;
}
std::array<float, 4> qmul(const std::array<float, 4>& a,
                          const std::array<float, 4>& b) noexcept {
  return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
          a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
          a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
          a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}
Vec3 qrotate(const std::array<float, 4>& q, const Vec3& v) noexcept {
  const Vec3 axis{q[0], q[1], q[2]};
  const auto t = cross(axis, v) * 2;
  return v + t * q[3] + cross(axis, t);
}
/// The settle: one dip below the hang and back, starting and ending at rest.
float settleDip(float u) noexcept {
  const float start = kSlabLowerEnd - .1f;
  if (u <= start || u >= kSlabLockSeconds) return 0;
  const float s =
      std::sin(std::numbers::pi_v<float> * (u - start) / (kSlabLockSeconds - start));
  return kSlabOvershoot * s * s * s * s;
}
/// How far the slab has dropped as the bay's latch lets go.
float latchDrop(float u) noexcept {
  constexpr float start = kSlabTakeUpEnd;
  return kSlabDrop * smoother((u - start) / (kSlabBreakFreeEnd - start));
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
  const float slide =
      heavyEase((u - kSlabBreakFreeEnd) / (kSlabSlideEnd - kSlabBreakFreeEnd));
  const float lift = smoother((u - kSlabLiftStart) / (kSlabLiftEnd - kSlabLiftStart));
  const float railProgress =
      heavyEase((u - kSlabTravelStart) / (kSlabTravelEnd - kSlabTravelStart));
  const float lower = smoother((u - kSlabLowerStart) / (kSlabLowerEnd - kSlabLowerStart));
  p.inBay = u < 0;
  p.centre = lerp3(s.bayCentre, s.pickCentre, slide);
  p.centre.x += (s.hangCentre.x - s.pickCentre.x) * railProgress;
  p.centre.y += (s.hangCentre.y - s.pickCentre.y) * railProgress;
  p.centre.z += (s.liftZ - s.pickCentre.z + kSlabDrop) * lift +
                (s.hangCentre.z - s.liftZ) * lower - latchDrop(u) - settleDip(u);
  // The slab remains edge-on until it has slid clear of its bay. Rotation overlaps
  // the continuing rise and rail travel instead of introducing another stop.
  const float tilt = smoother((u - kSlabTravelStart) / (kSlabTiltEnd - kSlabTravelStart));
  p.quat = tilt <= 0 ? s.bayQuat : slerp(s.bayQuat, kIdentity, tilt);
  const float reach = s.departure + kSlabTakeUpEnd;
  const float take = reach > 0 ? smoother(std::max(0.f, t) / reach) : 1.f;
  // Once attached, the carriage tracks the load inside the rail's pick limit.
  const Vec3 carried{std::lerp(s.pickCentre.x, s.hangCentre.x, railProgress),
                     std::lerp(s.pickCentre.y, s.hangCentre.y, railProgress), p.centre.z};
  const Vec3 target = carriageAt(carried);
  p.carriage = {std::lerp(s.parkCarriage.x, target.x, take),
                std::lerp(s.parkCarriage.y, target.y, take), s.railZ};
  p.mastLength = std::lerp(mastTo(s.liftZ), mastTo(p.centre.z), take);
  // Inertia: the slab rocks on its yoke against the carriage's acceleration.
  if (const float lean = swingAngle(s, u) * tilt; lean != 0) {
    const Vec3 travel{s.hangCentre.x - s.pickCentre.x, s.hangCentre.y - s.pickCentre.y,
                      0};
    if (length(travel) > 1e-4f) {
      const Vec3 axis = normalize(cross(travel, Vec3{0, 0, 1}));
      const float h = lean * .5f;
      const std::array<float, 4> q{axis.x * std::sin(h), axis.y * std::sin(h),
                                   axis.z * std::sin(h), std::cos(h)};
      const Vec3 boss{p.centre.x, p.centre.y, p.centre.z - s.yokeBelow};
      p.centre = boss + qrotate(q, p.centre - boss);
      p.quat = qmul(q, p.quat);
    }
  }
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

namespace {
/// 0 before `t` reaches 0, 1 from `span` on, smootherstep between (exact at both ends).
float ramp(float t, float span) noexcept {
  if (t <= 0) return 0;
  if (t >= span) return 1;
  return std::min(1.0f, smoother(t / span));
}
}  // namespace

float deployFormGate(float age, float lastLock, float intensity) noexcept {
  if (intensity < .5f) return 1;
  return ramp(age - lastLock, kRigFormSeconds);
}

float stowFormGate(float age, float intensity) noexcept {
  if (intensity < .5f) return 0;
  return 1 - ramp(age, kRigUnformSeconds);
}

float stowLead(float shutdown, bool formed) noexcept {
  return formed ? std::max(shutdown, kRigUnformSeconds) : shutdown;
}

float relayoutFormGate(float age, float lead, float seconds, float intensity) noexcept {
  if (intensity < .5f) return 1;
  if (lead > 0 && age < lead) return 1 - ramp(age, lead);
  return ramp(age - lead - seconds, kRigFormSeconds);
}

float relayoutTravelSeconds(float distanceCells) noexcept {
  const float metres = std::abs(distanceCells) / kCellsPerLabMetre;
  return std::max({.7f, 1.4f * metres / .35f, std::sqrt(11.81f * metres / .85f)});
}

float relayoutSeconds(const RelayoutSpec& s) noexcept {
  return relayoutTravelSeconds(length(s.to - s.from)) +
         .5f * (kRelayoutLiftSeconds + kRelayoutLowerSeconds) + kRelayoutSettleSeconds;
}

RelayoutPose slabRelayout(const RelayoutSpec& s, float t, float intensity) noexcept {
  const float end = relayoutSeconds(s);
  const bool still =
      length(s.to - s.from) < 1e-6f && std::abs(s.toYaw - s.fromYaw) < 1e-6f;
  if (intensity < .5f || t >= end || still) return {s.to, s.toYaw, true};
  if (t <= 0) return {s.from, s.fromYaw, false};
  const float travel = relayoutTravelSeconds(length(s.to - s.from));
  const float start = .5f * kRelayoutLiftSeconds;
  const float f = heavyEase((t - start) / travel);
  RelayoutPose p;
  p.centre = lerp3(s.from, s.to, f);
  p.centre.z += s.lift * (smoother(t / kRelayoutLiftSeconds) -
                          smoother((t - travel) / kRelayoutLowerSeconds));
  const float settleStart = end - kRelayoutSettleSeconds - .1f;
  if (t > settleStart) {
    const float dip =
        std::sin(std::numbers::pi_v<float> * (t - settleStart) / (end - settleStart));
    p.centre.z -= kSlabOvershoot * dip * dip * dip * dip;
  }
  p.yaw =
      s.fromYaw + std::remainder(s.toYaw - s.fromYaw, 2 * std::numbers::pi_v<float>) * f;
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
