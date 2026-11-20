// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/variants.hpp"
#include "view/light_bridge.hpp"
#include "view/move_anim.hpp"

using namespace cb;
using namespace cb::view;
using Catch::Approx;

namespace {
std::vector<LightBridge> bridgesOf(const char* name,
                                   std::vector<Placement>* out = nullptr) {
  const auto v = test::loadVariant(name);
  const auto cfg = ViewConfig::forBoard(v.dims);
  const auto p = layout(v.dims, cfg);
  if (out) *out = p;
  return lightBridges(v, cfg, p, Theme::manifold());
}
}  // namespace

TEST_CASE("a wrapping level stack has one light bridge, first level to last",
          "[unit][view][rig]") {
  std::vector<Placement> p;
  const auto b = bridgesOf("torus3d", &p);
  REQUIRE(b.size() == 1);
  float lo = 1e9f, hi = -1e9f;
  for (const auto& c : p) {
    lo = std::min(lo, c.z);
    hi = std::max(hi, c.z);
  }
  // From the top level (it leaves upward through the level seam) to the bottom one.
  CHECK(b[0].a.z == Approx(hi));
  CHECK(b[0].b.z == Approx(lo));
  CHECK(b[0].axis == 2);
  // The beam stands off the stack on its near (-Y) side, arching out towards the player.
  float minY = 1e9f;
  for (const auto& c : p) minY = std::min(minY, c.y);
  CHECK(b[0].a.y < minY);
  CHECK(b[0].control.y < b[0].a.y);
  CHECK(bridgePoint(b[0], 0).z == Approx(b[0].a.z));
  CHECK(bridgePoint(b[0], 1).z == Approx(b[0].b.z));
}

TEST_CASE("nothing glued that does not touch: no light bridges", "[unit][view][rig]") {
  CHECK(bridgesOf("cube5").empty());
  CHECK(bridgesOf("standard").empty());
  CHECK(bridgesOf("torus").empty());  // its seams are drawn axes, on the slab itself
}

TEST_CASE("a grid axis that wraps bridges its far slices, arching over the rest",
          "[unit][view][rig]") {
  const auto b = bridgesOf("t6");
  REQUIRE_FALSE(b.empty());
  bool grid = false;
  for (const auto& br : b) {
    CHECK(br.slabA != br.slabB);
    if (br.axis < 3) continue;
    grid = true;
    // The control point is above both endpoints.
    CHECK(br.control.z > br.a.z);
    CHECK(br.control.z > br.b.z);
    CHECK(br.a.z == Approx(br.b.z));
  }
  CHECK(grid);
}

TEST_CASE("a piece crossing a light bridge rides it", "[unit][view][rig]") {
  const VariantSpec v = test::loadVariant("standard");
  const auto cfg = ViewConfig::forBoard(v.dims);
  const auto placements = layout(v.dims, cfg);
  MovePath path;
  path.from = 8;
  path.to = 16;
  path.steps.push_back({8, 16, StepKind::Interior, {}, 0, Side::Max});
  MoveAnimation anim;
  anim.start(cfg, placements, SeamMap{}, Theme::manifold(), path, 0.2f);
  REQUIRE(anim.active());
  BridgeRide ride;
  ride.start = {0, 0, 0};
  ride.a = {1, 0, 0};
  ride.control = {2, 0, 3};
  ride.b = {3, 0, 0};
  ride.end = {4, 0, 0};
  anim.setBridge(ride);
  const auto at = [&](float t) {
    anim.setProgress(t);
    const auto s = anim.sample();
    return Vec3{s.x, s.y, s.z};
  };
  const auto near = [](const Vec3& a, const Vec3& b) { return length(a - b) < 1e-4f; };
  CHECK(near(at(0), ride.start));
  CHECK(near(at(.2f), ride.a));
  CHECK(near(at(.5f), Vec3{2, 0, 1.5f}));  // the Bezier's middle
  CHECK(near(at(.8f), ride.b));
  CHECK(near(at(1), ride.end));
  anim.setProgress(.5f);
  CHECK(anim.onBridge());
  anim.setProgress(.1f);
  CHECK_FALSE(anim.onBridge());
  // A new move forgets the bridge.
  anim.start(cfg, placements, SeamMap{}, Theme::manifold(), path, 0.2f);
  CHECK_FALSE(anim.hasBridge());
}
