// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "view/wake.hpp"

using namespace cb::view;
using Catch::Approx;

namespace {
// Blink closed instants: start + closing time.
constexpr float kBlink1Closed = 1.9f + .18f;
constexpr float kBlink2Closed = 2.6f + .12f;
}  // namespace

TEST_CASE("wake: the eyes are closed for the first 0.6 s", "[unit][view][wake]") {
  for (float a = 0; a < .6f; a += .01f) {
    const auto w = wakeState(a, 1);
    CHECK(w.lidOpen == 0);
    CHECK(w.lidGlow > .0f);
    CHECK(w.lidGlow <= .03f);
    CHECK_FALSE(w.done);
    CHECK(w.handshake == -1);
    CHECK(w.roomUp == 0);
  }
  // The closed lids overlap, so no row of the frame survives between them.
  const auto closed = wakeState(0, 1);
  CHECK(closed.topEdge + closed.bottomEdge > 1.f);
  CHECK(1.f - closed.bottomEdge < closed.topEdge);
}

TEST_CASE("wake: the opening hesitates once at about 40 percent", "[unit][view][wake]") {
  const float held = wakeState(1.1f, 1).lidOpen;
  CHECK(held == Approx(.4f).margin(.02));
  for (float a = 1.06f; a <= 1.18f; a += .005f) CHECK(wakeState(a, 1).lidOpen == Approx(held).margin(.01));
  CHECK(wakeState(1.6f, 1).lidOpen == Approx(1.f));
  // Rises before and after the hold.
  CHECK(wakeState(.9f, 1).lidOpen > 0);
  CHECK(wakeState(.9f, 1).lidOpen < held);
  CHECK(wakeState(1.4f, 1).lidOpen > held);
}

TEST_CASE("wake: the upper lid leads the lower", "[unit][view][wake]") {
  const auto w = wakeState(.9f, 1);
  const float topProgress = 1.f - w.topEdge / wakeState(0, 1).topEdge;
  const float bottomProgress = 1.f - w.bottomEdge / wakeState(0, 1).bottomEdge;
  CHECK(topProgress > bottomProgress);
  CHECK(w.curve == 1.f);
}

TEST_CASE("wake: each blink shuts the eyes fully", "[unit][view][wake]") {
  CHECK(wakeState(kBlink1Closed, 1).lidOpen < .02f);
  CHECK(wakeState(kBlink2Closed, 1).lidOpen < .02f);
  CHECK(wakeState(kBlink1Closed, 1).lidGlow == 0);
  // The lids are open between the blinks and well after the second.
  CHECK(wakeState(2.45f, 1).lidOpen == Approx(1.f));
  CHECK(wakeState(3.2f, 1).lidOpen == Approx(1.f));
  // The second blink is quicker.
  const auto openFrac = [](float from, float to) {
    int n = 0;
    for (float a = from; a < to; a += .002f) n += wakeState(a, 1).lidOpen < .5f;
    return static_cast<float>(n) * .002f;  // seconds spent more than half shut
  };
  CHECK(openFrac(2.6f, 2.9f) < openFrac(1.9f, 2.4f));
  // The lids reach the common height just below the centre (0.47 H).
  const auto closed = wakeState(kBlink1Closed, 1);
  CHECK(closed.topEdge >= .47f);
  CHECK(1.f - closed.bottomEdge <= .47f);
}

TEST_CASE("wake: the blur only clears, the vision comes up, and the room follows",
          "[unit][view][wake]") {
  float lastBlur = 1;
  float lastEv = -10;
  for (float a = 0; a <= 5.f; a += .005f) {
    const auto w = wakeState(a, 1);
    CHECK(w.blurFraction <= lastBlur + 1e-6f);
    CHECK(w.exposureEv >= lastEv - 1e-6f);
    lastBlur = w.blurFraction;
    lastEv = w.exposureEv;
  }
  CHECK(wakeState(0, 1).blurFraction == Approx(.025f));
  CHECK(wakeState(0, 1).exposureEv == Approx(-1.5f));
  CHECK(wakeState(0, 1).saturation < 1.f);
  for (float a = 2.9f; a <= 5.f; a += .05f) {
    CHECK(wakeState(a, 1).blurFraction == 0);
    CHECK(wakeState(a, 1).exposureEv == 0);
    CHECK(wakeState(a, 1).saturation == Approx(1.f));
  }
  CHECK(wakeState(1.9f, 1).blurFraction == Approx(.018f).margin(.001));
  CHECK(wakeState(2.0f, 1).roomUp == 0);
  CHECK(wakeState(2.6f, 1).roomUp > 0);
  CHECK(wakeState(2.6f, 1).roomUp < 1);
  CHECK(wakeState(3.2f, 1).roomUp == 1);
}

TEST_CASE("wake: the handshake starts at 3 s and the whole thing ends at 5 s",
          "[unit][view][wake]") {
  CHECK(wakeState(2.999f, 1).handshake == -1);
  CHECK(wakeState(3.0f, 1).handshake == Approx(0.f).margin(1e-5));
  CHECK(wakeState(4.0f, 1).handshake == Approx(1.f));
  CHECK_FALSE(wakeState(4.99f, 1).done);
  CHECK(wakeState(5.0f, 1).done);
  CHECK(wakeState(5.0f, 1).handshake == -1);
  const auto settled = wakeState(7.f, 1);
  CHECK(settled.done);
  CHECK(settled.lidOpen == 1);
  CHECK(settled.curve == 0);
  CHECK(settled.topEdge == 0);
  CHECK(settled.bottomEdge == 0);
  CHECK(settled.lidGlow == 0);
  CHECK(settled.blurFraction == 0);
  CHECK(settled.exposureEv == 0);
  CHECK(settled.saturation == 1);
  CHECK(settled.roomUp == 1);
  CHECK(settled.menuFade == 1);
}

TEST_CASE("wake: the menu belongs to the handshake", "[unit][view][wake]") {
  CHECK(wakeState(1.0f, 1).menuFade == 0);
  CHECK(wakeState(2.99f, 1).menuFade == 0);
  CHECK(wakeState(3.0f, 1).menuFade == 1);
  CHECK(wakeState(-1.f, 1).lidOpen == 0);
}

TEST_CASE("wake: the calm form has no blinks and no handshake", "[unit][view][wake]") {
  CHECK(wakeState(0, .3f).lidOpen == 0);
  CHECK(wakeState(.6f, .3f).lidOpen == Approx(1.f));
  float last = 0;
  for (float a = 0; a <= 1.2f; a += .002f) {
    const auto w = wakeState(a, .3f);
    CHECK(w.lidOpen >= last - 1e-6f);  // monotone: it never blinks
    last = w.lidOpen;
    CHECK(w.handshake == -1);
  }
  CHECK(wakeState(.8f, .3f).blurFraction == 0);
  CHECK(wakeState(.4f, .3f).blurFraction > 0);
  CHECK(wakeState(.79f, .3f).menuFade == 0);
  CHECK(wakeState(1.2f, .3f).menuFade == 1);
  CHECK_FALSE(wakeState(1.19f, .3f).done);
  CHECK(wakeState(1.2f, .3f).done);
  CHECK(wakeState(0, 0).done == false);
}

TEST_CASE("wake: every field is continuous but at the documented instants",
          "[unit][view][wake]") {
  const struct { float lo, hi; } ranges[]{{0, 1}, {0, 1.f}, {0, 1.f}, {0, 1.f}, {0, .04f}, {0, .025f},
                                          {-1.5f, 0}, {.7f, 1}, {0, .03f}, {0, 1}};
  for (const float intensity : {1.f, .3f}) {
    WakeState prev = wakeState(0, intensity);
    for (int i = 1; i <= 5200; ++i) {
      const float a = static_cast<float>(i) * .001f;
      const WakeState w = wakeState(a, intensity);
      const float cur[]{w.lidOpen, w.topEdge, w.bottomEdge, w.curve, w.feather,
                        w.blurFraction, w.exposureEv, w.saturation, w.lidGlow, w.roomUp};
      const float old[]{prev.lidOpen, prev.topEdge, prev.bottomEdge, prev.curve, prev.feather,
                        prev.blurFraction, prev.exposureEv, prev.saturation, prev.lidGlow,
                        prev.roomUp};
      for (std::size_t k = 0; k < std::size(cur); ++k)
        CHECK(std::abs(cur[k] - old[k]) <= (ranges[k].hi - ranges[k].lo) / 10.f + 1e-6f);
      // The documented instants: the handshake begins (3.0), the menu appears (3.0/0.8), done.
      prev = w;
    }
  }
}
