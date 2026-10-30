// SPDX-License-Identifier: GPL-3.0-or-later
//
// The trailer's resting camera drift (M13.9 revision): it must stay small, smooth and
// switch-offable, because the complaint it answers was a trailer camera that jittered and
// rotated far too much.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include "view/morph.hpp"
#include "view/trailer_camera.hpp"

using namespace cb::view;

TEST_CASE("steady sway never strays more than a few degrees", "[unit][view][trailer]") {
  OrbitCamera base;
  base.yaw = 0.4f;
  base.pitch = 0.9f;
  const SwayConfig cfg;
  float maxYaw = 0.0f;
  float maxPitch = 0.0f;
  for (float t = 0.0f; t < 400.0f; t += 0.25f) {
    const OrbitCamera c = steadySway(base, t, 1.0f, cfg);
    maxYaw = std::max(maxYaw, std::abs(c.yaw - base.yaw));
    maxPitch = std::max(maxPitch, std::abs(c.pitch - base.pitch));
  }
  CHECK(maxYaw <= cfg.yawAmplitude + 1e-4f);
  CHECK(maxYaw >
        0.5f * cfg.yawAmplitude);  // it does move: a still image is the other mode
  CHECK(maxPitch <= cfg.pitchAmplitude + 1e-4f);
  // "Very little" is a number: under 6 degrees of yaw in the worst case.
  CHECK(maxYaw < 0.105f);
}

TEST_CASE("steady sway at zero weight is the base camera exactly",
          "[unit][view][trailer]") {
  OrbitCamera base;
  base.yaw = 1.3f;
  base.pitch = 0.7f;
  base.distance = 12.0f;
  for (float t : {0.0f, 3.3f, 17.0f, 99.0f}) {
    const OrbitCamera c = steadySway(base, t, 0.0f);
    CHECK(c.yaw == base.yaw);
    CHECK(c.pitch == base.pitch);
    CHECK(c.distance == base.distance);
  }
}

TEST_CASE("steady sway is smooth: no frame steps further than a hair",
          "[unit][view][trailer]") {
  OrbitCamera base;
  const float dt = 1.0f / 60.0f;
  OrbitCamera prev = steadySway(base, 0.0f, 1.0f);
  float worst = 0.0f;
  for (float t = dt; t < 120.0f; t += dt) {
    const OrbitCamera c = steadySway(base, t, 1.0f);
    worst = std::max({worst, std::abs(c.yaw - prev.yaw), std::abs(c.pitch - prev.pitch)});
    prev = c;
  }
  CHECK(worst < 2e-4f);  // ~0.01 degrees per frame at 60 fps
}

TEST_CASE("steady sway leaves target, distance and lens alone", "[unit][view][trailer]") {
  OrbitCamera base;
  base.target = {1.0f, 2.0f, 3.0f};
  base.distance = 9.0f;
  const OrbitCamera c = steadySway(base, 7.0f, 1.0f);
  CHECK(c.target.x == 1.0f);
  CHECK(c.target.y == 2.0f);
  CHECK(c.target.z == 3.0f);
  CHECK(c.distance == 9.0f);
  CHECK(c.fovY == base.fovY);
}

TEST_CASE("zooming out never pushes the board past the far plane",
          "[unit][view][camera]") {
  // `frame` sets farZ from the framing distance; a player then zooms out well beyond it.
  // The target must still land inside the depth range [0,1], or the shape vanishes.
  OrbitCamera cam;
  cam.farZ = 60.0f;
  for (float d : {10.0f, 80.0f, 400.0f, 3000.0f}) {
    cam.distance = d;
    const Mat4 vp = cam.viewProj(1.6f);
    const Vec3 t = cam.target;
    const float z = vp[2] * t.x + vp[6] * t.y + vp[10] * t.z + vp[14];
    const float w = vp[3] * t.x + vp[7] * t.y + vp[11] * t.z + vp[15];
    CAPTURE(d);
    REQUIRE(w > 0.0f);
    CHECK(z / w >= 0.0f);
    CHECK(z / w <= 1.0f);
  }
}

// ---- the eased morph (M13.9 revision)
// --------------------------------------------------

#include "view/morph.hpp"

TEST_CASE("an eased morph starts and ends gently and takes the span's time",
          "[unit][view][morph]") {
  using namespace cb::view;
  EasedMorph m;
  float v = 0.0f;
  const float dt = 1.0f / 60.0f;
  // 0 -> 1 at speed 1 takes 1 / kFormMorphRate = 0.5 s.
  std::vector<float> trace;
  for (int i = 0; i < 60 && v < 1.0f; ++i) {
    v = advanceEasedMorph(m, v, 1.0f, dt, 1.0f);
    trace.push_back(v);
  }
  REQUIRE(v == 1.0f);
  CHECK(trace.size() >= 29);
  CHECK(trace.size() <= 32);
  // Gentle start: the first step is far smaller than the average speed's; monotone.
  CHECK(trace.front() < 0.25f * dt * kFormMorphRate);
  for (std::size_t i = 1; i < trace.size(); ++i) CHECK(trace[i] >= trace[i - 1]);
  // Gentle end too.
  const float lastStep = trace.back() - trace[trace.size() - 2];
  CHECK(lastStep < 0.25f * dt * kFormMorphRate);
  // The peak speed is the smoothstep's 1.5x average, never more.
  float peak = 0.0f;
  for (std::size_t i = 1; i < trace.size(); ++i)
    peak = std::max(peak, trace[i] - trace[i - 1]);
  CHECK(peak < 1.6f * dt * kFormMorphRate);
}

TEST_CASE("an eased morph retargets from where it is, and notices outside changes",
          "[unit][view][morph]") {
  using namespace cb::view;
  EasedMorph m;
  float v = 0.0f;
  const float dt = 1.0f / 60.0f;
  for (int i = 0; i < 10; ++i) v = advanceEasedMorph(m, v, 1.0f, dt, 1.0f);
  const float mid = v;
  REQUIRE(mid > 0.0f);
  REQUIRE(mid < 1.0f);
  // A new target mid-flight departs from the current value, not from the old start.
  v = advanceEasedMorph(m, v, 0.0f, dt, 1.0f);
  CHECK(std::abs(v - mid) < 0.02f);
  // Someone sets the value from outside (a seek, a keyframe): the morph carries on from
  // there instead of snapping back to its own track.
  v = 0.8f;
  v = advanceEasedMorph(m, v, 0.8f, dt, 1.0f);
  CHECK(v == 0.8f);
  v = advanceEasedMorph(m, v, 1.0f, dt, 1.0f);
  CHECK(v > 0.79f);
  CHECK(v < 0.9f);
  // Nothing to do when already there.
  EasedMorph still;
  CHECK(advanceEasedMorph(still, 0.5f, 0.5f, dt, 1.0f) == 0.5f);
}

TEST_CASE("nearestLap picks the equivalent slide nearest the reference", "[view]") {
  CHECK(nearestLap(125.521f, 0.0f, 8.0f) == Catch::Approx(-2.479f).margin(1e-3));
  CHECK(nearestLap(126.825f, 0.0f, 16.0f) == Catch::Approx(-1.175f).margin(1e-3));
  CHECK(nearestLap(-28.2959f, 0.0f, 8.0f) == Catch::Approx(3.7041f).margin(1e-3));
  CHECK(nearestLap(-3.9f, 3.9f, 8.0f) == Catch::Approx(4.1f).margin(1e-3));
  CHECK(nearestLap(5.0f, 0.0f, 0.0f) == 5.0f);
  CHECK(nearestLap(0.0f, 0.0f, 8.0f) == 0.0f);
}

TEST_CASE("the pose hands over to the keys over one move, never at once", "[view]") {
  const float inf = std::numeric_limits<float>::infinity();
  CHECK(poseHandoverWeight(54.0f, 54.0f, 53.0f) == 0.0f);
  CHECK(poseHandoverWeight(54.5f, 54.0f, 53.0f) == Catch::Approx(0.5f).margin(1e-4));
  CHECK(poseHandoverWeight(55.0f, 54.0f, 53.0f) == 1.0f);
  CHECK(poseHandoverWeight(20.0f, 20.0f, 21.0f) == 0.0f);
  CHECK(poseHandoverWeight(21.0f, 20.0f, 21.0f) == 1.0f);
  CHECK(poseHandoverWeight(30.0f, 20.0f, 36.0f) == 0.0f);
  CHECK(poseHandoverWeight(35.5f, 20.0f, 36.0f) == Catch::Approx(0.5f).margin(1e-4));
  CHECK(poseHandoverWeight(100.0f, 20.0f, inf) == 0.0f);
}

TEST_CASE("the keyframe spline never flips the camera between two adjacent frames",
          "[view]") {
  // The poses of a real 3-D torus track (yaw, pitch, roll, distance), whose two middle keys
  // sit at the horizon: the squad blend used to change sign between 60.89 and 60.92 and spin
  // the camera half a turn in one frame.
  struct Key {
    float t, yaw, pitch, roll, dist;
  };
  const Key keys[] = {{53.0f, 1.02421f, 0.51093f, 0.0f, 48.5342f},
                      {56.0f, 1.4247f, 0.411133f, -0.0817869f, 50.6472f},
                      {59.0f, 2.19983f, 0.05f, 0.0f, 14.7956f},
                      {63.0f, 0.0529981f, 0.4275f, 0.0f, 13.8957f}};
  std::vector<CameraWaypoint> path;
  for (const Key& k : keys) {
    OrbitCamera c;
    c.target = Vec3{3.5f, 3.5f, 0.7f};
    c.yaw = k.yaw;
    c.pitch = k.pitch;
    c.roll = k.roll;
    c.distance = k.dist;
    path.push_back({k.t, c});
  }
  const auto fwd = [](const OrbitCamera& c) { return normalize(c.target - c.eye()); };
  Vec3 prev = fwd(splineCamera(path, 53.01f));
  float worst = 0.0f;
  for (float t = 53.02f; t < 62.99f; t += 0.02f) {
    const Vec3 cur = fwd(splineCamera(path, t));
    worst = std::max(worst, std::acos(std::clamp(dot(prev, cur), -1.0f, 1.0f)));
    prev = cur;
  }
  // A move of 0.02 time units is a small turn; a flip is about 3 rad.
  CHECK(worst < 0.1f);
}
