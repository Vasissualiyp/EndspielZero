// SPDX-License-Identifier: GPL-3.0-or-later
//
// The projector handoff (M23.9 D): a move hands the hologram from panel to panel. These
// pin the timing the player sees - per-hop opacity, the trail, the heat ahead of the
// leading copy - as pure functions of progress, so a --move-t capture reproduces.
#include <algorithm>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "view/move_anim.hpp"

using namespace cb;
using namespace cb::view;
using Catch::Approx;

namespace {

MovePath slider(int hops) {
  MovePath p;
  p.from = 100;
  for (int i = 1; i <= hops; ++i) {
    PathStep s;
    s.from = static_cast<CellId>(100 + i - 1);
    s.to = static_cast<CellId>(100 + i);
    p.steps.push_back(s);
  }
  p.to = static_cast<CellId>(100 + hops);
  return p;
}

const HandoffCopy* copyAt(const HandoffState& s, CellId c) {
  for (const auto& x : s.copies)
    if (x.cell == c) return &x;
  return nullptr;
}
float heatAt(const HandoffState& s, CellId c) {
  for (const auto& x : s.heat)
    if (x.cell == c) return x.heat;
  return 0;
}

}  // namespace

TEST_CASE("handoff starts as the standing piece and ends as one solid destination",
          "[view][handoff]") {
  const MovePath path = slider(4);
  const auto a = handoffState(path, 0.0f);
  REQUIRE(a.copies.size() == 1);
  CHECK(a.copies[0].cell == 100);
  CHECK(a.copies[0].opacity == Approx(1.0f));
  CHECK(a.copies[0].scan == Approx(0.0f));

  const auto z = handoffState(path, 1.0f);
  REQUIRE(z.copies.size() == 1);
  CHECK(z.copies[0].cell == 104);
  CHECK(z.copies[0].destination);
  CHECK(z.copies[0].opacity == Approx(1.0f));
  CHECK(z.copies[0].scan == Approx(0.0f));
}

TEST_CASE("exactly one solid head at every moment, nothing faint ahead of it",
          "[view][handoff]") {
  for (const int hops : {1, 2, 4, 7}) {
    const MovePath path = slider(hops);
    const float h = kHandoffSpan / static_cast<float>(hops);
    for (float t = 0; t <= 1.0f; t += 0.002f) {
      const auto s = handoffState(path, t);
      float best = 0;
      int solid = 0;
      int head = -1;
      for (const auto& c : s.copies) {
        best = std::max(best, c.opacity);
        if (c.opacity >= 0.999f) {
          ++solid;
          head = static_cast<int>(c.cell) - 100;
        }
      }
      // After the head's own brief rise there is always a (near) solid copy; during a
      // crossfade at most the old and new heads overlap, never a third.
      CHECK(best >= 0.8f);
      CHECK(solid <= 1);
      // No copy ahead of the head, ever: every copy with opacity > 0 is at or behind the
      // newest cell the head has reached.
      const int reached = std::min(hops, static_cast<int>(t / h));
      for (const auto& c : s.copies) CHECK(static_cast<int>(c.cell) - 100 <= reached);
      // Once the head's brief rise is over it is the one solid copy.
      if (t >= h * static_cast<float>(reached) + kHandoffMaxRise + 1e-4f) {
        CHECK(solid == 1);
        CHECK(head == reached);
      }
    }
  }
}

TEST_CASE("the head rises almost at once and the destination is solid on landing",
          "[view][handoff]") {
  const MovePath path = slider(4);
  const float h = kHandoffSpan / 4.0f;
  // Well under a hop after the head lands on cell 2 its copy is fully solid.
  const auto s = handoffState(path, 2.0f * h + 0.3f * h);
  REQUIRE(copyAt(s, 102));
  CHECK(copyAt(s, 102)->opacity == Approx(1.0f));
  // The cell it left is at ghost level or below, and scan dominated.
  const auto later = handoffState(path, 2.0f * h + 0.9f * h);
  REQUIRE(copyAt(later, 101));
  CHECK(copyAt(later, 101)->opacity <= kHandoffGhostOpacity + 1e-4f);
  CHECK(copyAt(later, 101)->scan > 0.5f);
  // The destination is solid once reached, with no late resolve.
  const auto landed = handoffState(path, kHandoffSpan + kHandoffMaxRise);
  REQUIRE(copyAt(landed, 104));
  CHECK(copyAt(landed, 104)->opacity == Approx(1.0f));
  CHECK(copyAt(landed, 104)->scan == Approx(0.0f));
  // Trails are short: two hops after being left, a copy is gone.
  CHECK(copyAt(handoffState(path, 1.0f), 101) == nullptr);
}

TEST_CASE("heat warms ahead of the lead, never the whole path", "[view][handoff]") {
  const MovePath path = slider(6);
  const float h = kHandoffSpan / 6.0f;
  const auto s = handoffState(path, 2.0f * h + 0.01f);  // head at cell 102
  CHECK(heatAt(s, 103) > 0.2f);                         // one hop ahead is warming
  CHECK(heatAt(s, 106) == Approx(0.0f));                // the far end is still cold
  CHECK(heatAt(s, 105) == Approx(0.0f));
  // Never more than a few cells warm at once (no legal-move-style lighting of the path).
  for (float t = 0; t <= 1.0f; t += 0.01f) {
    int warm = 0;
    for (const auto& x : handoffState(path, t).heat)
      if (x.heat > kFootprintHeat + 0.05f) ++warm;
    CHECK(warm <= 4);
  }
}

TEST_CASE("heat cools behind the lead and settles at the footprint, without a pop",
          "[view][handoff]") {
  const MovePath path = slider(3);
  const auto end = handoffState(path, 1.0f);
  CHECK(heatAt(end, 100) == Approx(0.0f).margin(0.01));  // origin cooled
  CHECK(heatAt(end, 101) == Approx(0.0f).margin(0.01));
  CHECK(heatAt(end, 102) == Approx(0.0f).margin(0.01));
  CHECK(heatAt(end, 103) == Approx(kFootprintHeat).margin(0.01));  // = standing footprint
  // Continuity: no frame-to-frame jump larger than a fraction.
  float prev = 0;
  for (float t = 0; t <= 1.0f; t += 0.005f) {
    const float d = heatAt(handoffState(path, t), 103);
    CHECK(std::abs(d - prev) < 0.12f);
    prev = d;
  }
}

TEST_CASE("leaps, unexplained moves and calm mode are a single from-to handoff",
          "[view][handoff]") {
  MovePath leap = slider(3);
  leap.leap = true;
  MovePath odd = slider(3);
  odd.unexplained = true;
  const MovePath plain = slider(3);
  for (const MovePath* p : {&leap, &odd}) {
    const auto s = handoffState(*p, 0.5f);
    for (const auto& c : s.copies) CHECK((c.cell == 100 || c.cell == 103));
  }
  const auto calm = handoffState(plain, 0.5f, 0.0f);
  for (const auto& c : calm.copies) CHECK((c.cell == 100 || c.cell == 103));
  // Portal steps are ordinary hops: the far cell simply takes over.
  MovePath portal = slider(2);
  portal.steps[1].kind = StepKind::Portal;
  portal.steps[1].to = 7;
  portal.to = 7;
  bool sawFar = false;
  for (float t = 0; t <= 1; t += 0.02f)
    sawFar = sawFar || copyAt(handoffState(portal, t), 7);
  CHECK(sawFar);
}

TEST_CASE("an LED capture is a per-emitter colour pulse, pure in progress",
          "[view][handoff]") {
  CHECK_FALSE(capturePulse(0.2f, 0.6f).active);
  const auto held = capturePulse(kCaptureContact + 0.05f, 0.6f);
  CHECK(held.active);
  CHECK(held.held);
  CHECK(held.age > 0.0f);
  const auto settle = capturePulse(kCaptureRelease + 0.1f, 0.6f);
  CHECK(settle.active);
  CHECK_FALSE(settle.held);
  // It has run past the slowest emitter before the move ends.
  CHECK(capturePulse(1.0f, 0.6f).age >= 0.25f);
  const auto again = capturePulse(0.8f, 0.6f);
  CHECK(again.age == capturePulse(0.8f, 0.6f).age);
}
