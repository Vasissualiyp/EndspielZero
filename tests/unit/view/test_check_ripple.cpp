// SPDX-License-Identifier: GPL-3.0-or-later
//
// Check ripple (M23.10 section 4): the pure wavefront model.
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "view/check_ripple.hpp"

using namespace cb::view;
using Catch::Approx;

TEST_CASE("checkRipple: quiet before the front arrives and after the ripple has died",
          "[unit][view][ripple]") {
  for (int dist : {0, 2, 6, 11}) {
    const float arrival = static_cast<float>(dist) / kRippleCellsPerSecond;
    CHECK(checkRipple(dist, arrival - 0.3f, 1.0f).amp == 0.0f);
    CHECK(checkRipple(dist, -1.0f, 1.0f).amp == 0.0f);
    CHECK(checkRipple(dist, kRippleSeconds, 1.0f).amp == 0.0f);
    CHECK(checkRipple(dist, kRippleSeconds + 3.0f, 1.0f).tint == 0.0f);
  }
  CHECK(checkRipple(-1, 0.1f, 1.0f).amp == 0.0f);  // unreachable
}

TEST_CASE("checkRipple: peaks at the arrival time and falls with distance",
          "[unit][view][ripple]") {
  for (int dist : {0, 2, 6}) {
    const float arrival = static_cast<float>(dist) / kRippleCellsPerSecond;
    const RippleCell peak = checkRipple(dist, arrival, 1.0f);
    CHECK(peak.amp > 0.1f);
    CHECK(peak.phase == Approx(0.0f).margin(1e-5f));
    CHECK(peak.amp >= checkRipple(dist, arrival + 0.05f, 1.0f).amp);
    if (dist > 0) CHECK(peak.amp >= checkRipple(dist, arrival - 0.05f, 1.0f).amp);
    if (dist > 0) CHECK(checkRipple(dist, arrival - 0.04f, 1.0f).phase < 0.0f);
    CHECK(checkRipple(dist, arrival + 0.04f, 1.0f).phase > 0.0f);
  }
  CHECK(checkRipple(0, 0.0f, 1.0f).amp == Approx(1.0f));
  CHECK(checkRipple(0, 0.0f, 1.0f).tint == Approx(kRippleTintMax));
  CHECK(checkRipple(6, 6.0f / kRippleCellsPerSecond, 1.0f).amp ==
        Approx(1.0f / (1.0f + kRippleFalloff * 6.0f)));
  CHECK(checkRipple(1, 1.0f / kRippleCellsPerSecond, 1.0f).amp >
        checkRipple(6, 6.0f / kRippleCellsPerSecond, 1.0f).amp);
}

TEST_CASE("checkRipple: calm has none, and the same inputs give the same answer",
          "[unit][view][ripple]") {
  for (float age : {0.0f, 0.1f, 0.3f}) {
    const RippleCell c = checkRipple(0, age, 0.0f);
    CHECK(c.amp == 0.0f);
    CHECK(c.tint == 0.0f);
    CHECK(c.phase == 0.0f);
  }
  const RippleCell a = checkRipple(3, 0.2f, 1.0f);
  const RippleCell b = checkRipple(3, 0.2f, 1.0f);
  CHECK(a.amp == b.amp);
  CHECK(a.phase == b.phase);
}
