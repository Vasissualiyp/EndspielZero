// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
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
      out.push_back({static_cast<CellId>(out.size()), ox + static_cast<float>(x) -
                                                          .5f * static_cast<float>(n - 1),
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
  for (std::uint32_t s = 0; s < 5; ++s) addBoard(p, 5, 0, 0, 2.5f * static_cast<float>(s), s);
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
  for (int ms = 1; ms <= 2500; ++ms) {
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
  // The overshoot dips, but never more than 2.5 mm (0.05 cells), and ends at rest.
  CHECK(lowest < s.hangCentre.z);
  CHECK(lowest >= s.hangCentre.z - .05f - 1e-5f);
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
