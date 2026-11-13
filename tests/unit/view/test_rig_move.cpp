// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <numbers>
#include "support/variants.hpp"
#include "view/rig_move.hpp"
using namespace cb::view;
using Catch::Approx;
namespace {
constexpr float pi = std::numbers::pi_v<float>;
void same(const OrbitCamera& a, const OrbitCamera& b) {
  CHECK(a.target.x == Approx(b.target.x).margin(1e-5));
  CHECK(a.target.y == Approx(b.target.y).margin(1e-5));
  CHECK(a.target.z == Approx(b.target.z).margin(1e-5));
  CHECK(a.distance == Approx(b.distance).margin(1e-5));
  CHECK(std::remainder(a.yaw - b.yaw, 2 * pi) == Approx(0).margin(1e-5));
  CHECK(a.pitch == Approx(b.pitch).margin(1e-5));
  CHECK(a.fovY == Approx(b.fovY).margin(1e-5));
  CHECK(a.nearZ == b.nearZ);
  CHECK(a.farZ == b.farZ);
  CHECK(a.roll == b.roll);
  CHECK(a.orthographic == b.orthographic);
}
}  // namespace
TEST_CASE("rig camera endpoints and geometric distance with lift", "[unit][view][rig]") {
  OrbitCamera a, b;
  a.target = {1, 2, 3};
  b.target = {4, 8, 12};
  a.distance = 4;
  b.distance = 16;
  b.yaw = 2;
  b.pitch = .2f;
  b.fovY = 1.1f;
  same(blendCamera(a, b, 0), a);
  same(blendCamera(a, b, 1), b);
  const auto mid = blendCamera(a, b, .5f);
  CHECK(mid.distance == Approx(8 + kRigLift * 10));
  CHECK(mid.target.y == Approx(5));
  b.nearZ = 2;
  b.farZ = 500;
  b.roll = .3f;
  b.orthographic = true;
  const auto lens = blendCamera(a, b, .4f);
  CHECK(lens.nearZ == b.nearZ);
  CHECK(lens.farZ == b.farZ);
  CHECK(lens.roll == b.roll);
  CHECK(lens.orthographic);
}
TEST_CASE("rig yaw takes the short way through zero", "[unit][view][rig]") {
  OrbitCamera a, b;
  a.yaw = 350 * pi / 180;
  b.yaw = 10 * pi / 180;
  CHECK(std::remainder(blendCamera(a, b, .5f).yaw, 2 * pi) == Approx(0).margin(1e-5));
}
TEST_CASE("rig easing settles past arrival and has exact held endpoints",
          "[unit][view][rig]") {
  CHECK(rigEase(-1) == 0);
  CHECK(rigEase(0) == 0);
  CHECK(rigEase(1) == 1);
  CHECK(rigEase(2) == 1);
  float high = 0, last = 0;
  for (int i = 0; i <= 1000; ++i) {
    const float x = static_cast<float>(i) / 1000;
    const float y = rigEase(x);
    high = std::max(high, y);
    if (x <= .7f) CHECK(y >= last);
    last = y;
  }
  CHECK(high > 1);
  CHECK(high <= 1.02f);
}
TEST_CASE("rig duration is symmetric bounded and angular speed is explicit",
          "[unit][view][rig]") {
  OrbitCamera a, b;
  CHECK(rigMoveSeconds(a, a, .05f) == kRigMinSeconds);
  CHECK(rigAngularSpeed(a, a, .4f, .8f) == 0);
  b.yaw = a.yaw + pi / 2;
  CHECK(rigAngularSpeed(a, b, .4f, .8f) > 0);
  CHECK(rigAngularSpeed(a, b, -.1f, .8f) == 0);
  CHECK(rigAngularSpeed(a, b, .9f, .8f) == 0);
  b.target = {100, 20, 30};
  b.pitch = .2f;
  CHECK(rigMoveSeconds(a, b, .05f) == Approx(rigMoveSeconds(b, a, .05f)));
  CHECK(rigMoveSeconds(a, b, 1) == kRigMaxSeconds);
}

TEST_CASE("rack slabs ease out only for selected slots", "[unit][view][rig]") {
  CHECK(slabSlide(0, 1, 0, 1) == 0);
  CHECK(slabSlide(0, 1, .125f, 1) == Approx(.03f));
  CHECK(slabSlide(0, 1, .25f, 1) == .06f);
  CHECK(slabSlide(0, 1, 1, 1) == .06f);
  CHECK(slabSlide(1, 1, 1, 1) == 0);
  CHECK(slabSlide(0, 0, 1, 1) == 0);
  CHECK(slabSlide(0, 1, 0, 0) == .06f);
  CHECK(slabSlide(1, 1, 0, 0) == 0);
}

TEST_CASE("rack count follows flat sections of the layout and caps at eight",
          "[unit][view][rig]") {
  for (const auto& item : {std::pair{"standard", 1},
                           {"cylinder", 1},
                           {"torus", 1},
                           {"cube5", 5},
                           {"hyper4", 8}}) {
    const auto spec = cb::test::loadVariant(item.first);
    CHECK(rackSlabCount(spec.dims) == item.second);
  }
}
