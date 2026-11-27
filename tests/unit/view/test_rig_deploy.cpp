// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include <random>

#include "view/rig_deploy.hpp"

using namespace cb;
using namespace cb::view;
using Catch::Approx;

namespace {
/// The shipped [deploy] table in lab-world cells: metres minus gantry.centre, times 20.
DeployVolume shippedVolume() {
  DeployVolume v;
  v.min = {-1.45f * 20, (-.40f - .6f) * 20, (.45f - 1.05f) * 20};
  v.max = {1.45f * 20, (1.60f - .6f) * 20, (2.05f - 1.05f) * 20};
  v.railZ = (2.46f - 1.05f) * 20;
  v.mastEdgeGap = .06f * 20;
  v.yokeDrop = .035f * 20;
  return v;
}
/// An n x n board as one slice at the given origin and face height.
void addBoard(std::vector<Placement>& out, int n, float ox, float oy, float z,
              std::uint32_t slice) {
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x)
      out.push_back({static_cast<CellId>(out.size()),
                     ox + static_cast<float>(x) - .5f * static_cast<float>(n - 1),
                     oy + static_cast<float>(y) - .5f * static_cast<float>(n - 1), z,
                     slice});
}
std::vector<Placement> standard() {
  std::vector<Placement> p;
  addBoard(p, 8, 0, 0, 0, 0);
  return p;
}
std::vector<Placement> cube5() {
  std::vector<Placement> p;
  for (std::uint32_t s = 0; s < 5; ++s)
    addBoard(p, 5, 0, 0, 2.5f * static_cast<float>(s), s);
  return p;
}
constexpr float kBezel = .28f;
float qlen(const std::array<float, 4>& q) {
  return std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
}
SlabPathSpec samplePath() {
  SlabPathSpec s;
  s.bayCentre = {47.4f, 16.3f, 5.f};
  s.bayQuat = {.5f, .5f, .5f, .5f};
  s.pickCentre = {29.f, 16.3f, 5.f};
  s.hangCentre = {0, 0, 0};
  s.departure = .2f;
  s.mastOffset = {0, 5.48f, 0};
  s.yokeBelow = kSlabBelowFace + .7f;
  s.railZ = 28.2f;
  s.liftZ = 15.f;
  s.parkCarriage = {0, 0, 28.2f};
  return s;
}
}  // namespace

TEST_CASE("placeLayout centres a standard board on the anchor at scale one",
          "[unit][view][rig]") {
  const auto b = boundsOf(standard());
  const auto x = placeLayout(b, shippedVolume());
  CHECK(x.labScale == 1.0f);
  const auto origin = x.toLab({b.centerX(), b.centerY(), b.minZ});
  CHECK(origin.x == Approx(0).margin(1e-6));
  CHECK(origin.y == Approx(0).margin(1e-6));
  CHECK(origin.z == Approx(0).margin(1e-6));
  // The bounds' bottom maps to the anchor's z.
  CHECK(x.toLab({0, 0, b.minZ}).z == Approx(0).margin(1e-6));
  for (const Vec3 p : {Vec3{1, 2, 3}, Vec3{-40, 7.5f, -2}, Vec3{.25f, -.5f, 9}}) {
    const auto back = x.toLab(x.toBoard(p));
    CHECK(back.x == Approx(p.x).margin(1e-5));
    CHECK(back.y == Approx(p.y).margin(1e-5));
    CHECK(back.z == Approx(p.z).margin(1e-5));
  }
  const auto c = placeLayout(boundsOf(cube5()), shippedVolume());
  CHECK(c.labScale == 1.0f);
  CHECK(c.toLab({0, 0, 0}).z == Approx(0).margin(1e-6));
}

TEST_CASE("placeLayout scales the lab up around a layout too wide for the volume",
          "[unit][view][rig]") {
  const auto v = shippedVolume();
  // Three times the volume's x half-extent, including its margins.
  const float margin = .08f * 20;
  const float half = 3 * v.max.x - margin;
  Bounds b{-half, -1, 0, half, 1, 0};
  const auto x = placeLayout(b, v);
  CHECK(x.labScale == Approx(3.0f).epsilon(1e-4));
  // Every corner of the scaled layout lies inside the volume.
  for (const float cx : {b.minX - margin, b.maxX + margin}) {
    const auto lab = x.toLab({cx, 0, 0});
    CHECK(lab.x >= v.min.x - 1e-3f);
    CHECK(lab.x <= v.max.x + 1e-3f);
  }
}

TEST_CASE("slabsOf measures each slice's bezel", "[unit][view][rig]") {
  const auto slabs = slabsOf(standard(), kBezel);
  REQUIRE(slabs.size() == 1);
  CHECK(slabs[0].minX == Approx(-3.5f - .5f - kBezel));
  CHECK(slabs[0].maxY == Approx(3.5f + .5f + kBezel));
  CHECK(slabs[0].z == 0.0f);
  const auto five = slabsOf(cube5(), kBezel);
  REQUIRE(five.size() == 5);
  for (std::uint32_t s = 0; s < 5; ++s) CHECK(five[s].slice == s);
  CHECK(five[4].z == Approx(10.0f));
  // A 3-D board is one layout slice of flat levels: one slab per level.
  auto levels = cube5();
  for (auto& p : levels) p.slice = 0;
  const auto stacked = slabsOf(levels, kBezel);
  REQUIRE(stacked.size() == 5);
  CHECK(stacked[0].z == 0.0f);
  CHECK(stacked[4].z == Approx(10.0f));
}

TEST_CASE("mast groups: standard, cube5 and a 2x2 grid", "[unit][view][rig]") {
  const auto v = shippedVolume();
  {
    const auto p = standard();
    const auto slabs = slabsOf(p, kBezel);
    const auto x = placeLayout(boundsOf(p), v);
    const auto groups = mastGroups(slabs, x, v);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].slabs == std::vector<std::uint32_t>{0});
    CHECK(groups[0].mastFoot.x == Approx(0).margin(1e-5));
    CHECK(groups[0].mastFoot.y == Approx(4 + kBezel + v.mastEdgeGap));
    CHECK(groups[0].mastFoot.z ==
          Approx(slabs[0].z - kSlabBelowFace - v.yokeDrop).margin(1e-5));
    CHECK(groups[0].mastTopZ == Approx(x.toBoard({0, 0, v.railZ}).z));
  }
  {
    const auto p = cube5();
    const auto slabs = slabsOf(p, kBezel);
    const auto groups = mastGroups(slabs, placeLayout(boundsOf(p), v), v);
    REQUIRE(groups.size() == 1);
    // Top to bottom.
    CHECK(groups[0].slabs == std::vector<std::uint32_t>{4, 3, 2, 1, 0});
    CHECK(groups[0].mastFoot.z ==
          Approx(slabs[0].z - kSlabBelowFace - v.yokeDrop).margin(1e-5));
  }
  {
    std::vector<Placement> p;
    addBoard(p, 4, 0, 0, 0, 0);
    addBoard(p, 4, 7, 0, 0, 1);
    addBoard(p, 4, 0, 7, 0, 2);
    addBoard(p, 4, 7, 7, 0, 3);
    const auto slabs = slabsOf(p, kBezel);
    const auto groups = mastGroups(slabs, placeLayout(boundsOf(p), v), v);
    REQUIRE(groups.size() == 4);
    for (std::uint32_t g = 0; g < 4; ++g) CHECK(groups[g].slabs == std::vector{g});
  }
}

TEST_CASE("the slab path waits in its bay, then hangs and clamps", "[unit][view][rig]") {
  const auto s = samplePath();
  const auto bay = slabDeploy(s, s.departure - .05f, 1);
  CHECK(bay.inBay);
  CHECK_FALSE(bay.clamped);
  CHECK(bay.centre.x == s.bayCentre.x);
  CHECK(bay.centre.y == s.bayCentre.y);
  CHECK(bay.centre.z == s.bayCentre.z);
  CHECK(bay.quat == s.bayQuat);
  const auto locked = slabDeploy(s, s.departure + kSlabLockSeconds + 1e-4f, 1);
  CHECK_FALSE(locked.inBay);
  CHECK(locked.clamped);
  CHECK(locked.centre.x == Approx(s.hangCentre.x).margin(1e-4));
  CHECK(locked.centre.y == Approx(s.hangCentre.y).margin(1e-4));
  CHECK(locked.centre.z == Approx(s.hangCentre.z).margin(1e-4));
  CHECK(slabDeploy(s, s.departure + kSlabLockSeconds + .015f, 1).clampPulse > .3f);
  CHECK(slabDeploy(s, s.departure + kSlabLockSeconds + .03f, 1).clampPulse ==
        Approx(1).margin(1e-3));
  CHECK(slabDeploy(s, s.departure + kSlabLockSeconds + .25f, 1).clampPulse == 0.0f);
  const auto rest = slabDeploy(s, 9, 1);
  CHECK(rest.clamped);
  CHECK(rest.clampPulse == 0);
  CHECK(rest.quat == std::array<float, 4>{0, 0, 0, 1});
}

TEST_CASE("the slab path is continuous, carried and unit", "[unit][view][rig]") {
  const auto s = samplePath();
  // The rails' x/y range: the frame rails at +-1.7 m in x and -0.6 .. 1.8 m in y.
  const Bounds rails{-34, -24, 0, 34, 24, 0};
  auto last = slabDeploy(s, 0, 1);
  float lowest = 1e9f;
  for (int ms = 1; static_cast<float>(ms) / 1000 <= s.departure + kSlabLockSeconds + .3f;
       ++ms) {
    const float t = static_cast<float>(ms) / 1000.0f;
    const auto p = slabDeploy(s, t, 1);
    CAPTURE(t);
    CHECK(length(p.centre - last.centre) <= .6f);
    CHECK(p.mastLength >= 0);
    CHECK(qlen(p.quat) == Approx(1).margin(1e-4));
    CHECK(p.carriage.x >= rails.minX);
    CHECK(p.carriage.x <= rails.maxX);
    CHECK(p.carriage.y >= rails.minY);
    CHECK(p.carriage.y <= rails.maxY);
    CHECK(p.carriage.z == s.railZ);
    const float settle = t - s.departure - kSlabLowerEnd;
    if (settle >= 0) lowest = std::min(lowest, p.centre.z);
    last = p;
  }
  // The overshoot dips 1-3 mm (0.02-0.06 cells) once, and ends at rest (M25.3).
  CHECK(lowest < s.hangCentre.z - .02f);
  CHECK(lowest >= s.hangCentre.z - .06f - 1e-5f);
  CHECK(last.centre.z == Approx(s.hangCentre.z).margin(1e-6));
}

TEST_CASE("calm deploys straight to the final pose", "[unit][view][rig]") {
  const auto s = samplePath();
  for (const float t : {0.f, .1f, 1.f, 5.f}) {
    const auto p = slabDeploy(s, t, .3f);
    CHECK(p.clamped);
    CHECK_FALSE(p.inBay);
    CHECK(p.clampPulse == 0);
    CHECK(p.centre.z == Approx(s.hangCentre.z));
  }
}

TEST_CASE("the orbit stays inside the room", "[unit][view][rig]") {
  const Bounds room{-40, -60, -21, 40, 50, 30};
  const float inset = 3;
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> u(0, 1);
  for (int i = 0; i < 200; ++i) {
    OrbitCamera c;
    c.target = {-20 + 40 * u(rng), -20 + 40 * u(rng), -5 + 10 * u(rng)};
    c.yaw = 6.28f * u(rng);
    c.pitch = -1.4f + 2.8f * u(rng);
    const float wanted = 5 + 300 * u(rng);
    c.distance = clampOrbitDistance(c, wanted, room, inset);
    CHECK(c.distance <= wanted);
    const auto e = c.eye();
    CHECK(e.x >= room.minX + inset - 1e-3f);
    CHECK(e.x <= room.maxX - inset + 1e-3f);
    CHECK(e.y >= room.minY + inset - 1e-3f);
    CHECK(e.y <= room.maxY - inset + 1e-3f);
    CHECK(e.z >= room.minZ + inset - 1e-3f);
    CHECK(e.z <= room.maxZ - inset + 1e-3f);
  }
  OrbitCamera free;
  free.distance = 10;
  CHECK(clampOrbitDistance(free, 10, room, inset) == 10);
}

TEST_CASE("walls between the eye and the board fade", "[unit][view][rig]") {
  const Vec3 eye{0, -30, 10}, target{0, 0, 0};
  const Bounds across{-5, -16, -5, 5, -14, 20};
  CHECK(wallFade(across, eye, target, 2) == Approx(kWallFadeFloor));
  const Bounds far{30, 30, 30, 40, 40, 40};
  CHECK(wallFade(far, eye, target, 2) == 1.0f);
  // It eases over the pad: a box moved away step by step fades back monotonically.
  float last = 0;
  for (int i = 0; i <= 40; ++i) {
    const float off = -2 + .1f * static_cast<float>(i);
    const Bounds side{off, -16, -5, off + 4, -14, 20};
    const float f = wallFade(side, eye, target, 2);
    CHECK(f >= last - 1e-6f);
    CHECK(f - last <= .2f);
    last = f;
  }
  CHECK(last == 1.0f);
}

// ---------------------------------------------------------------------------------------
// M24.6: the multi-slab gantry.

namespace {
/// A row of `n` boards of `size` cells, `pitch` apart along X, at labScale 1.
std::vector<Placement> row(int n, int size, float pitch) {
  std::vector<Placement> p;
  for (int s = 0; s < n; ++s)
    addBoard(p, size, pitch * static_cast<float>(s), 0, 0, static_cast<std::uint32_t>(s));
  return p;
}
float metres(float boardCells, float labScale) {
  return boardCells / (20.0f * labScale);
}
}  // namespace

TEST_CASE("routing keeps standard, cube5 and a spaced grid as they were",
          "[unit][view][rig]") {
  const auto v = shippedVolume();
  for (const auto& p : {standard(), cube5()}) {
    const auto slabs = slabsOf(p, kBezel);
    const auto x = placeLayout(boundsOf(p), v);
    const auto groups = mastGroups(slabs, x, v);
    const auto routed = routeGroups(groups, v, x.labScale);
    REQUIRE(routed.size() == 1);
    CHECK(routed[0].slabs == groups[0].slabs);
    CHECK_FALSE(routed[0].rack());
    CHECK(routed[0].mastFoot.x == groups[0].mastFoot.x);
    CHECK(routed[0].mastFoot.y == groups[0].mastFoot.y);
    CHECK(routed[0].mastFoot.z == groups[0].mastFoot.z);
    CHECK(spacingHolds(routed, x.labScale));
  }
  // 4x4 boards 7 cells apart: 0.35 m columns and 0.35 m on one bridge - no merge.
  std::vector<Placement> grid;
  addBoard(grid, 4, 0, 0, 0, 0);
  addBoard(grid, 4, 7, 0, 0, 1);
  addBoard(grid, 4, 0, 7, 0, 2);
  addBoard(grid, 4, 7, 7, 0, 3);
  const auto x = placeLayout(boundsOf(grid), v);
  const auto routed = routeGroups(mastGroups(slabsOf(grid, kBezel), x, v), v, x.labScale);
  CHECK(routed.size() == 4);
  CHECK(spacingHolds(routed, x.labScale));
}

TEST_CASE("masts closer than the spacings merge into one rack with T-bar arms",
          "[unit][view][rig]") {
  const auto v = shippedVolume();
  // Four 4x4 boards 6 cells apart: 0.30 m < 0.34 m between columns.
  const auto p = row(4, 4, 6);
  const auto slabs = slabsOf(p, kBezel);
  const auto x = placeLayout(boundsOf(p), v);
  const auto groups = mastGroups(slabs, x, v);
  REQUIRE(groups.size() == 4);
  CHECK_FALSE(spacingHolds(groups, x.labScale));
  const auto routed = routeGroups(groups, v, x.labScale);
  REQUIRE(routed.size() == 1);
  CHECK(routed[0].rack());
  CHECK(routed[0].arms.size() == 4);
  CHECK(routed[0].slabs.size() == 4);
  // The rack mast stands at the middle of its arms, on their line.
  CHECK(routed[0].mastFoot.x == Approx(9.0f).margin(1e-4));
  CHECK(routed[0].mastFoot.y == Approx(groups[0].mastFoot.y));
  CHECK(spacingHolds(routed, x.labScale));
  // Every slab is still carried exactly once.
  std::vector<std::uint32_t> all = routed[0].slabs;
  std::sort(all.begin(), all.end());
  CHECK(all == std::vector<std::uint32_t>{0, 1, 2, 3});
}

TEST_CASE("the spacing rules: columns 0.34 m apart, carriages on a bridge 0.32 m",
          "[unit][view][rig]") {
  const auto group = [](float x, float y) {
    MastGroup g;
    g.mastFoot = {x, y, 0};
    return g;
  };
  // At labScale 1 a metre is 20 cells.
  CHECK(spacingHolds({group(0, 0), group(6.8f, 0)}, 1));
  CHECK_FALSE(spacingHolds({group(0, 0), group(6.7f, 0)}, 1));
  CHECK(spacingHolds({group(0, 0), group(0, 6.4f)}, 1));
  CHECK_FALSE(spacingHolds({group(0, 0), group(0, 6.3f)}, 1));
  // The rules are in metres: twice the lab scale needs twice the board distance.
  CHECK_FALSE(spacingHolds({group(0, 0), group(6.8f, 0)}, 2));
  CHECK(metres(13.6f, 2) == Approx(.34f));
}

TEST_CASE("departures: nearest the magazine first, a column together, within 1.4 s",
          "[unit][view][rig]") {
  const auto group = [](float x, float y) {
    MastGroup g;
    g.mastFoot = {x, y, 0};
    return g;
  };
  const Vec3 magazine{40, 0, 0};
  const auto d =
      departureOrder({group(0, 0), group(20, 0), group(20, 10), group(-20, 0)}, magazine);
  CHECK(d.count == 3);
  CHECK(d.slot == std::vector<int>{1, 0, 0, 2});
  CHECK(departureStep(1) == Approx(.12f));
  CHECK(departureStep(4) == Approx(.12f));
  CHECK(departureStep(20) == Approx(kDeployDepartureSpan / 19));
  for (const int n : {1, 2, 4, 11, 40, 200}) {
    CAPTURE(n);
    const float last =
        kDeployFirstDeparture + departureStep(n) * static_cast<float>(n - 1);
    CHECK(last <= 1.4f + 1e-5f);
    CHECK(deployLastLock(n) == Approx(last + kSlabLockSeconds));
  }
  // No static fallback: any number of columns travels.
  CHECK(deployLastLock(1) == Approx(kDeployFirstDeparture + kSlabLockSeconds));
  CHECK(stowTravelSeconds(1) == Approx(kSlabLockSeconds));
}

TEST_CASE("a relayout lifts, travels, turns, lowers and settles", "[unit][view][rig]") {
  RelayoutSpec s;
  s.from = {0, 0, 0};
  s.to = {10, -4, 2};
  s.fromYaw = 0;
  s.toYaw = std::numbers::pi_v<float> / 2;
  const float end = relayoutSeconds(s);
  CHECK(end ==
        Approx(.5f * (kRelayoutLiftSeconds + kRelayoutLowerSeconds) +
               relayoutTravelSeconds(length(s.to - s.from)) + kRelayoutSettleSeconds));
  CHECK(relayoutTravelSeconds(0) == Approx(.7f));
  CHECK(relayoutTravelSeconds(200) == Approx(40.f));
  // Exact endpoints.
  const auto a = slabRelayout(s, 0, 1);
  CHECK(a.centre.x == s.from.x);
  CHECK(a.centre.y == s.from.y);
  CHECK(a.centre.z == s.from.z);
  CHECK(a.yaw == s.fromYaw);
  CHECK_FALSE(a.settled);
  const auto b = slabRelayout(s, end, 1);
  CHECK(b.centre.x == s.to.x);
  CHECK(b.centre.y == s.to.y);
  CHECK(b.centre.z == s.to.z);
  CHECK(b.yaw == Approx(s.toYaw));
  CHECK(b.settled);
  // Lift and travel overlap: at half lift the rail travel has not yet started.
  const auto lifted = slabRelayout(s, .5f * kRelayoutLiftSeconds, 1);
  CHECK(lifted.centre.z == Approx(s.from.z + .5f * s.lift));
  CHECK(lifted.yaw == Approx(s.fromYaw));
  // Continuous: no jump bigger than 0.6 cells or 0.1 rad in 1 ms.
  auto last = a;
  for (int ms = 1; ms <= static_cast<int>(end * 1000) + 5; ++ms) {
    const auto p = slabRelayout(s, static_cast<float>(ms) / 1000.0f, 1);
    CAPTURE(ms);
    CHECK(length(p.centre - last.centre) <= .6f);
    CHECK(std::abs(p.yaw - last.yaw) <= .1f);
    last = p;
  }
  // A slab that does not move does not bob, and calm is the final pose.
  RelayoutSpec still;
  still.from = still.to = {3, 3, 3};
  CHECK(slabRelayout(still, .2f, 1).centre.z == 3.0f);
  CHECK(slabRelayout(s, .1f, .3f).centre.x == s.to.x);
  CHECK(slabRelayout(s, .1f, .3f).settled);
}

TEST_CASE("slabs are matched across a layout change by their cells",
          "[unit][view][rig]") {
  // One 4x4 board; the new layout swaps its axes (a quarter turn) and moves it.
  std::vector<Placement> before, after;
  addBoard(before, 4, 0, 0, 0, 0);
  for (const auto& p : before) after.push_back({p.cell, 10 - p.y, 5 + p.x, 0, 0});
  // A second board only in the new layout.
  addBoard(after, 4, 30, 0, 0, 1);
  for (std::size_t i = 16; i < after.size(); ++i) after[i].cell += 100;
  const auto m =
      matchSlabs(before, slabsOf(before, kBezel), after, slabsOf(after, kBezel));
  REQUIRE(m.size() == 2);
  CHECK(m[0].from == 0);
  CHECK(m[0].to == 0);
  CHECK(m[0].yaw == Approx(std::numbers::pi_v<float> / 2).margin(1e-4));
  CHECK(m[1].from == -1);
  CHECK(m[1].to == 1);
}

TEST_CASE("a light-guide cable sags 8 % between its masts", "[unit][view][rig]") {
  const Vec3 a{0, 0, 10}, b{20, 0, 10};
  const auto pts = cablePoints(a, b);
  REQUIRE(pts.size() == 13);
  CHECK(length(pts.front() - a) < 1e-5f);
  CHECK(length(pts.back() - b) < 1e-5f);
  CHECK(pts[6].z == Approx(10 - .08f * 20).margin(.02f));
  for (std::size_t i = 1; i < pts.size(); ++i) CHECK(pts[i].x > pts[i - 1].x);
  for (const auto& p : pts) CHECK(p.z <= 10 + 1e-5f);
}

TEST_CASE("a layout fits the volume where the lab stands, or it does not",
          "[unit][view][rig]") {
  const auto v = shippedVolume();
  const auto b = boundsOf(standard());
  const auto x = placeLayout(b, v);
  CHECK(layoutFits(b, x, v));
  // The same board moved 10 cells along x still fits; 40 cells does not.
  Bounds moved = b;
  moved.minX += 10;
  moved.maxX += 10;
  CHECK(layoutFits(moved, x, v));
  moved.minX += 30;
  moved.maxX += 30;
  CHECK_FALSE(layoutFits(moved, x, v));
}

namespace {
/// The slab's tilt away from face-up, in degrees (only meaningful once it is face-up).
float tiltDegrees(const std::array<float, 4>& q) {
  return 2 * std::acos(std::clamp(std::abs(q[3]), 0.0f, 1.0f)) * 180 /
         std::numbers::pi_v<float>;
}
}  // namespace

TEST_CASE("heavy deploy including its trailing second completes within 9 s (M25.3b)",
          "[unit][view][rig][weight]") {
  CHECK(deployLastLock(1) >= 5.0f);
  CHECK(deployLastLock(1) + 1 <= 9.0f);
  for (const int n : {1, 2, 4, 11, 40, 200, 256}) {
    CAPTURE(n);
    CHECK(deployLastLock(n) + 1 <= 9.0f);
  }
  // The phases follow one another.
  CHECK(0 < kSlabTakeUpEnd);
  CHECK(kSlabTakeUpEnd < kSlabBreakFreeEnd);
  CHECK(kSlabBreakFreeEnd < kSlabSlideEnd);
  CHECK(kSlabSlideEnd < kSlabTiltEnd);
  CHECK(kSlabTiltEnd < kSlabTravelEnd);
  CHECK(kSlabTravelEnd < kSlabLowerEnd);
  CHECK(kSlabLowerEnd < kSlabLockSeconds);
}

TEST_CASE("a heavy slab hesitates, drops off its latch, then starts slowly (M25.3)",
          "[unit][view][rig][weight]") {
  const auto s = samplePath();
  const float u0 = s.departure + kSlabTakeUpEnd, u1 = s.departure + kSlabBreakFreeEnd;
  CHECK(kSlabBreakFreeEnd - kSlabTakeUpEnd >= .12f);
  CHECK(kSlabBreakFreeEnd - kSlabTakeUpEnd <= .21f);
  const auto before = slabDeploy(s, u0, 1);
  CHECK(before.centre.z == Approx(s.bayCentre.z).margin(1e-5));
  // The carriage has arrived over the pick-up before the slab moves.
  CHECK(before.carriage.x == Approx(s.pickCentre.x + s.mastOffset.x).margin(1e-4));
  CHECK(before.carriage.y == Approx(s.pickCentre.y + s.mastOffset.y).margin(1e-4));
  for (float t = u0; t <= u1; t += .005f) {
    const auto p = slabDeploy(s, t, 1);
    CAPTURE(t);
    // No horizontal motion while it breaks free.
    CHECK(p.centre.x == Approx(s.bayCentre.x).margin(1e-5));
    CHECK(p.centre.y == Approx(s.bayCentre.y).margin(1e-5));
    CHECK(p.quat == s.bayQuat);
  }
  // The drop as the latch lets go: 5-10 mm (0.1-0.2 cells).
  const float drop = s.bayCentre.z - slabDeploy(s, u1, 1).centre.z;
  CHECK(drop >= .1f);
  CHECK(drop <= .2f);
  CHECK(kSlabDrop == Approx(drop).margin(1e-5));
  // Then a slow start: 50 ms into the slide it has moved under 1 % of the way out.
  const float out = std::abs(slabDeploy(s, u1 + .05f, 1).centre.x - s.bayCentre.x);
  CHECK(out < .01f * std::abs(s.pickCentre.x - s.bayCentre.x));
}

TEST_CASE("the slab's speed never jumps and peaks late in its travel (M25.3)",
          "[unit][view][rig][weight]") {
  const auto s = samplePath();
  constexpr float dt = 1e-3f;
  Vec3 prev = slabDeploy(s, 0, 1).centre, prevCarriage = slabDeploy(s, 0, 1).carriage;
  Vec3 v{}, vc{};
  float peak = 0, peakAt = 0, worst = 0, worstCarriage = 0;
  const float end = s.departure + kSlabLockSeconds + .3f;
  for (int i = 1; static_cast<float>(i) * dt <= end; ++i) {
    const float t = static_cast<float>(i) * dt;
    const auto p = slabDeploy(s, t, 1);
    const Vec3 nv = (p.centre - prev) * (1 / dt),
               nvc = (p.carriage - prevCarriage) * (1 / dt);
    if (i > 1) {
      worst = std::max(worst, length(nv - v) / dt);
      worstCarriage = std::max(worstCarriage, length(nvc - vc) / dt);
    }
    const float u = t - s.departure;
    if (u > kSlabTiltEnd && u < kSlabTravelEnd && length(nv) > peak) {
      peak = length(nv);
      peakAt = u;
    }
    v = nv;
    vc = nvc;
    prev = p.centre;
    prevCarriage = p.carriage;
  }
  // An instant velocity change of even 1 cell/s (5 cm/s) in one millisecond would read
  // as 1000 cells/s^2; every smooth start and stop here stays under 700.
  CAPTURE(worst, worstCarriage);
  CHECK(worst < 700.0f);
  CHECK(worstCarriage < 700.0f);
  CHECK(peakAt > kSlabTravelStart + .55f * (kSlabTravelEnd - kSlabTravelStart));
}

TEST_CASE("the slab lags its carriage by a few degrees, critically damped (M25.3)",
          "[unit][view][rig][weight]") {
  const auto s = samplePath();
  float most = 0;
  int signChanges = 0, lastSign = 0;
  for (float u = kSlabTiltEnd; u <= kSlabLockSeconds + .2f; u += .002f) {
    const auto p = slabDeploy(s, s.departure + u, 1);
    CAPTURE(u);
    const float a = tiltDegrees(p.quat);
    CHECK(a <= 3.0f);
    most = std::max(most, a);
    // Which way it leans along the travel: the slab's up axis tipped forward (+) or
    // back (-).
    const Vec3 up{2 * (p.quat[0] * p.quat[2] + p.quat[3] * p.quat[1]),
                  2 * (p.quat[1] * p.quat[2] - p.quat[3] * p.quat[0]), 0};
    const Vec3 travel = normalize(
        Vec3{s.hangCentre.x - s.pickCentre.x, s.hangCentre.y - s.pickCentre.y, 0});
    const float lean = dot(up, travel);
    const int sign = lean > 1e-3f ? 1 : lean < -1e-3f ? -1 : 0;
    if (sign != 0) {
      if (lastSign != 0 && sign != lastSign) ++signChanges;
      lastSign = sign;
    }
    // Accelerating away it leans back; the first lean is backwards.
    if (signChanges == 0 && lastSign != 0) CHECK(lastSign == -1);
  }
  CHECK(most >= .5f);
  // Critically damped: back while accelerating, forward while braking, and no ringing.
  CHECK(signChanges == 1);
  // Level again by the lock.
  CHECK(tiltDegrees(slabDeploy(s, s.departure + kSlabLockSeconds, 1).quat) < .01f);
  // The stow (the path reversed in time) lags its own motion, too: its first lean as it
  // sets off back towards the rack is towards the hang, i.e. forward along the deploy.
  auto r = s;
  r.departure = 0;
  r.reversed = true;
  int first = 0;
  for (float u = kSlabTravelEnd; u >= kSlabTiltEnd && first == 0; u -= .002f) {
    const auto p = slabDeploy(r, u, 1);
    const Vec3 up{2 * (p.quat[0] * p.quat[2] + p.quat[3] * p.quat[1]),
                  2 * (p.quat[1] * p.quat[2] - p.quat[3] * p.quat[0]), 0};
    const float lean = dot(up, normalize(Vec3{s.hangCentre.x - s.pickCentre.x,
                                              s.hangCentre.y - s.pickCentre.y, 0}));
    if (std::abs(lean) > 1e-3f) first = lean > 0 ? 1 : -1;
  }
  CHECK(first == 1);
}

TEST_CASE("the shape forms only on locked slabs (M25.3)", "[unit][view][rig][weight]") {
  // Deploy: flat until the last lock, then formed over kRigFormSeconds.
  const float lock = 3.4f;
  CHECK(deployFormGate(0, lock, 1) == 0);
  CHECK(deployFormGate(lock - 1e-3f, lock, 1) == 0);
  CHECK(deployFormGate(lock + kRigFormSeconds, lock, 1) == 1);
  CHECK(deployFormGate(99, lock, 1) == 1);
  float last = 0;
  for (float t = 0; t < lock + 2; t += .01f) {
    const float g = deployFormGate(t, lock, 1);
    CHECK(g >= last);
    CHECK(g - last <= .05f);
    last = g;
  }
  CHECK(deployFormGate(0, 0, .3f) == 1);  // calm: the slabs are hung at once
  // Stow: unformed before anything leaves.
  CHECK(stowFormGate(0, 1) == 1);
  CHECK(stowFormGate(kRigUnformSeconds, 1) == 0);
  CHECK(stowFormGate(99, 1) == 0);
  CHECK(stowFormGate(0, .3f) == 0);
  CHECK(stowLead(1.0f, false) == 1.0f);
  CHECK(stowLead(0.0f, false) == 0.0f);
  CHECK(stowLead(0.0f, true) == kRigUnformSeconds);
  CHECK(stowLead(1.0f, true) == std::max(1.0f, kRigUnformSeconds));
  // Relayout: unform over the lead, flat while the slabs move, form again after.
  const float lead = kRigUnformSeconds, move = 2;
  CHECK(relayoutFormGate(0, lead, move, 1) == 1);
  CHECK(relayoutFormGate(lead, lead, move, 1) == 0);
  CHECK(relayoutFormGate(lead + move - 1e-3f, lead, move, 1) == 0);
  CHECK(relayoutFormGate(lead + move + kRigFormSeconds, lead, move, 1) == 1);
  CHECK(relayoutFormGate(.5f, lead, move, .3f) == 1);
  CHECK(relayoutFormGate(.5f, 0, move, 1) == 0);
}
