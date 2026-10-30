// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "view/camera.hpp"

using namespace cb;
using Catch::Matchers::WithinAbs;

namespace {

float angleBetween(const view::Vec3& a, const view::Vec3& b) {
  return std::acos(std::clamp(view::dot(a, b), -1.0f, 1.0f));
}

view::Vec3 forwardOf(const view::OrbitCamera& c) {
  view::Vec3 f{}, r{}, u{};
  c.basis(f, r, u);
  return f;
}

}  // namespace

TEST_CASE("slerpCamera ends on its endpoints", "[view]") {
  view::OrbitCamera a;
  a.target = {1.0f, 2.0f, 3.0f};
  a.distance = 9.0f;
  a.yaw = 0.4f;
  a.pitch = 0.7f;
  a.roll = 0.2f;
  view::OrbitCamera b;
  b.target = {-2.0f, 0.5f, 1.0f};
  b.distance = 4.0f;
  b.yaw = -2.1f;
  b.pitch = -0.3f;
  b.roll = -0.9f;

  const view::OrbitCamera at0 = view::slerpCamera(a, b, 0.0f);
  const view::OrbitCamera at1 = view::slerpCamera(a, b, 1.0f);
  for (const auto& [got, want] : {std::pair{at0, a}, std::pair{at1, b}}) {
    CHECK_THAT(view::length(got.eye() - want.eye()), WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(view::length(got.target - want.target), WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(view::length(forwardOf(got) - forwardOf(want)), WithinAbs(0.0f, 1e-3f));
  }
}

TEST_CASE("slerpCamera's target travels a straight line", "[view]") {
  // M17.23: `target` is the authored state and the one that must be interpolated,
  // matching `OrbitCamera`'s own target-primary representation. Interpolating `eye` and
  // back-solving a target - what this used to do - let an in-between camera aim away from
  // both subjects when their targets differ.
  view::OrbitCamera a;
  a.target = {1.0f, -2.0f, 0.5f};
  a.distance = 10.0f;
  a.yaw = 0.3f;
  a.pitch = 0.8f;
  view::OrbitCamera b;
  b.target = {-2.5f, 1.5f, 0.0f};
  b.distance = 4.0f;
  b.yaw = -2.5f;
  b.pitch = -0.6f;
  const view::Vec3 from = a.target;
  const view::Vec3 delta = b.target - from;
  view::Vec3 prevEye = a.eye();
  for (int i = 0; i <= 50; ++i) {
    const float t = static_cast<float>(i) / 50.0f;
    const view::OrbitCamera c = view::slerpCamera(a, b, t);
    CHECK_THAT(view::length(c.target - (from + delta * t)), WithinAbs(0.0f, 1e-3f));
    // The eye is no longer on a straight line - the target is - but it stays a smooth,
    // continuous function of `t`: no step jumps a whole camera's distance.
    if (i > 0) CHECK(view::length(c.eye() - prevEye) < 2.0f);
    prevEye = c.eye();
  }
}

TEST_CASE("slerpCamera keeps both endpoints' subjects in frame", "[view]") {
  // M17.23 regression: the Align/Approach/Return blend
  // (`src/render/shape_sequence.cpp`) always passes two cameras with *different* targets
  // - a far, board-centred player camera and a close, piece-centred chase camera. If the
  // blend lets the aim drift off both, the board leaves the frame entirely; a captured
  // torus clip went flat-background for several frames during Return. Both subjects must
  // stay projectable for the whole blend.
  view::OrbitCamera player;  // settled, far, looking at the board centre
  player.target = {0.0f, 0.0f, 0.0f};
  player.distance = 9.0f;
  player.yaw = 0.5f;
  player.pitch = 0.9f;
  view::OrbitCamera chase;  // close, piece-centred, the real chase magnitudes
  chase.target = {2.2f, -1.0f, 0.0f};
  chase.distance = 3.2f;
  chase.yaw = -2.4f;
  chase.pitch = 0.3f;

  constexpr float kAspect = 16.0f / 9.0f;
  constexpr float kW = 1280.0f;
  constexpr float kH = 720.0f;
  constexpr float kSlack = 0.4f;
  const view::Vec3 offsets[7] = {{0.0f, 0.0f, 0.0f},    {kSlack, 0.0f, 0.0f},
                                 {-kSlack, 0.0f, 0.0f}, {0.0f, kSlack, 0.0f},
                                 {0.0f, -kSlack, 0.0f}, {0.0f, 0.0f, kSlack},
                                 {0.0f, 0.0f, -kSlack}};
  auto anyVisible = [&](const view::OrbitCamera& c, const view::Vec3& p) {
    for (const view::Vec3& o : offsets)
      if (c.project(p + o, kAspect, kW, kH).visible) return true;
    return false;
  };

  for (int i = 0; i <= 100; ++i) {
    const float t = static_cast<float>(i) / 100.0f;
    const view::OrbitCamera c = view::slerpCamera(player, chase, t);
    INFO("t = " << t);
    CHECK(anyVisible(c, player.target));
    CHECK(anyVisible(c, chase.target));
  }
}

TEST_CASE("slerpCamera turns the shortest way, never the long way", "[view]") {
  // The defect this replaces: interpolating the eye *direction* (or yaw/pitch/roll)
  // separately let the view swing through the origin for near-opposite cameras - a 720
  // degree spin. A quaternion slerp turns along the geodesic, so the forward sweeps the
  // short arc and no step is large.
  view::OrbitCamera a;
  a.target = {0.0f, 0.0f, 0.0f};
  a.distance = 8.0f;
  a.yaw = -0.2f;
  a.pitch = 1.2f;  // steeply down
  a.roll = 0.0f;
  view::OrbitCamera b;
  b.target = {0.0f, 0.0f, 0.0f};
  b.distance = 8.0f;
  b.yaw = 2.9f;
  b.pitch = -1.1f;  // steeply up, nearly opposite
  b.roll = 2.0f;

  const float total = angleBetween(forwardOf(a), forwardOf(b));
  float swept = 0.0f;
  float worst = 0.0f;
  view::Vec3 prev = forwardOf(a);
  const int steps = 200;
  for (int i = 1; i <= steps; ++i) {
    const view::Vec3 f =
        forwardOf(view::slerpCamera(a, b, static_cast<float>(i) / steps));
    const float d = angleBetween(prev, f);
    swept += d;
    worst = std::max(worst, d);
    prev = f;
  }
  // The sweep is the geodesic, not the long way round, and it is finely subdivided.
  CHECK_THAT(swept, WithinAbs(total, 0.05f));
  CHECK(worst < 0.2f);
}

// ---- the camera spline (M13.9 revision): smooth motion through keyframes
// ----------------

namespace {

cb::view::OrbitCamera camAt(float yaw, float pitch, float dist, float tx = 0.0f) {
  cb::view::OrbitCamera c;
  c.yaw = yaw;
  c.pitch = pitch;
  c.distance = dist;
  c.target = {tx, 0.0f, 0.0f};
  return c;
}

float angleBetween(const cb::view::OrbitCamera& a, const cb::view::OrbitCamera& b) {
  const cb::view::Vec3 ea = cb::view::normalize(a.eye() - a.target);
  const cb::view::Vec3 eb = cb::view::normalize(b.eye() - b.target);
  return std::atan2(cb::view::length(cb::view::cross(ea, eb)), cb::view::dot(ea, eb));
}

}  // namespace

TEST_CASE("the camera spline passes through its waypoints and holds past the ends",
          "[unit][view][spline]") {
  using namespace cb::view;
  const std::vector<CameraWaypoint> keys{{0.0f, camAt(0.0f, 0.8f, 10.0f)},
                                         {2.0f, camAt(1.0f, 0.5f, 14.0f, 3.0f)},
                                         {5.0f, camAt(0.2f, 1.0f, 9.0f, -2.0f)}};
  for (const CameraWaypoint& k : keys) {
    const OrbitCamera c = splineCamera(keys, k.time);
    CAPTURE(k.time);
    CHECK(angleBetween(c, k.camera) < 1e-3f);
    CHECK(std::abs(c.distance - k.camera.distance) < 1e-3f);
    CHECK(std::abs(c.target.x - k.camera.target.x) < 1e-3f);
  }
  CHECK(angleBetween(splineCamera(keys, -5.0f), keys.front().camera) < 1e-3f);
  CHECK(angleBetween(splineCamera(keys, 99.0f), keys.back().camera) < 1e-3f);
  CHECK(splineCamera({}, 1.0f).distance == OrbitCamera{}.distance);
  CHECK(splineCamera({keys[1]}, 7.0f).distance == keys[1].camera.distance);
}

TEST_CASE("the camera spline is smooth where a chain of eased legs is not",
          "[unit][view][spline]") {
  using namespace cb::view;
  // Four keyframes along a quarter circle of yaw: a smooth path has no corner at the
  // middle ones, so the turning rate just before and after a keyframe agree.
  std::vector<CameraWaypoint> keys;
  for (int i = 0; i < 5; ++i) {
    keys.push_back(
        {static_cast<float>(i), camAt(0.4f * static_cast<float>(i), 0.8f, 10.0f)});
  }
  const float h = 0.01f;
  for (float t : {1.0f, 2.0f, 3.0f}) {
    const float before =
        angleBetween(splineCamera(keys, t - 2 * h), splineCamera(keys, t - h));
    const float after =
        angleBetween(splineCamera(keys, t + h), splineCamera(keys, t + 2 * h));
    CAPTURE(t);
    CHECK(before > 0.0f);
    CHECK(std::abs(before - after) < 0.05f * before);  // C1: rate is continuous
  }
  // And along the whole path no frame turns much more than the others (no jitter).
  float lo = 1e9f, hi = 0.0f;
  for (float t = 0.0f; t < 3.99f; t += 0.02f) {
    const float step = angleBetween(splineCamera(keys, t), splineCamera(keys, t + 0.02f));
    if (t > 0.3f && t < 3.7f) {
      lo = std::min(lo, step);
      hi = std::max(hi, step);
    }
  }
  CHECK(hi < 1.5f * lo);
}

TEST_CASE("the camera spline turns the short way round", "[unit][view][spline]") {
  using namespace cb::view;
  // Yaws just either side of the wrap: the path between them must not spin the long way.
  const std::vector<CameraWaypoint> keys{{0.0f, camAt(3.0f, 0.8f, 10.0f)},
                                         {1.0f, camAt(-3.0f, 0.8f, 10.0f)}};
  const OrbitCamera mid = splineCamera(keys, 0.5f);
  const float toA = angleBetween(mid, keys[0].camera);
  CHECK(toA < 0.2f);  // half of the ~0.28 rad the short way, not ~3 rad
}

TEST_CASE("scalar spline is smooth and passes through its points",
          "[unit][view][spline]") {
  using namespace cb::view;
  const std::vector<std::pair<float, float>> pts{
      {0.0f, 0.0f}, {1.0f, 1.0f}, {2.0f, 0.0f}, {4.0f, 1.0f}};
  for (const auto& p : pts)
    CHECK(std::abs(splineScalar(pts, p.first) - p.second) < 1e-5f);
  CHECK(splineScalar(pts, -1.0f) == 0.0f);
  CHECK(splineScalar(pts, 9.0f) == 1.0f);
  const float h = 1e-3f;
  const float d1 = (splineScalar(pts, 1.0f) - splineScalar(pts, 1.0f - h)) / h;
  const float d2 = (splineScalar(pts, 1.0f + h) - splineScalar(pts, 1.0f)) / h;
  CHECK(std::abs(d1 - d2) < 0.02f);
}

TEST_CASE("the near clip hides what is closer to the eye than it, and only that", "[view]") {
  view::OrbitCamera cam;
  cam.target = view::Vec3{0, 0, 0};
  cam.distance = 10.0f;
  const view::Vec3 eye = cam.eye();
  const view::Vec3 fwd = view::normalize(cam.target - eye);
  const auto clipZ = [&](const view::OrbitCamera& c, float ahead) {
    const view::Vec3 p = eye + fwd * ahead;
    const view::Mat4 vp = c.viewProj(1.0f);
    const float z = vp[2] * p.x + vp[6] * p.y + vp[10] * p.z + vp[14];
    const float w = vp[3] * p.x + vp[7] * p.y + vp[11] * p.z + vp[15];
    return z / w;  // Vulkan depth: inside the frustum when in [0, 1]
  };
  const view::OrbitCamera far = cam.withNearClip(0.6f);
  // 0.3 ahead of the eye: drawn with the old 0.1 plane, clipped with a 0.6 one.
  CHECK(clipZ(cam, 0.3f) > 0.0f);
  CHECK(clipZ(far, 0.3f) < 0.0f);
  // Past the plane both draw it.
  CHECK(clipZ(far, 1.0f) > 0.0f);
  // A camera that already clips further is not pulled nearer.
  view::OrbitCamera wide = cam;
  wide.nearZ = 2.0f;
  CHECK(wide.withNearClip(0.5f).nearZ == 2.0f);
}
