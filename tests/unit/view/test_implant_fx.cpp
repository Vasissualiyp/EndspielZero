// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "view/implant_fx.hpp"

using namespace cb::view;
using Catch::Approx;

TEST_CASE("implant heartbeat is a calm-safe bounded double pulse",
          "[unit][view][implant]") {
  for (int i = -200; i <= 200; ++i) {
    const double t = static_cast<double>(i) / 100.0;
    CHECK(heartbeat(t, 0) == 1);
    CHECK(heartbeat(t, 1) >= 1);
    CHECK(heartbeat(t, 1) <= 1.200001f);
  }
  for (int i = -2; i <= 2; ++i) {
    CHECK(heartbeat(i, 1) == Approx(1.20f));
    CHECK(heartbeat(i, 1) > heartbeat(i - .02, 1));
    CHECK(heartbeat(i, 1) > heartbeat(i + .02, 1));
    CHECK(heartbeat(i + .25, 1) > heartbeat(i + .20, 1));
    CHECK(heartbeat(i + .25, 1) < heartbeat(i, 1));
  }
}

TEST_CASE("implant saccadic suppression follows angular speed", "[unit][view][implant]") {
  CHECK(saccadicDip(0, 1) == 0);
  CHECK(saccadicDip(3, 1) == Approx(.4f));
  CHECK(saccadicDip(9, 1) == Approx(.4f));
  CHECK(saccadicDip(9, 0) == 0);
  CHECK(saccadicDip(1.5f, 1) == Approx(.2f));
}

TEST_CASE("implant panels form dissolve and leave a short afterimage",
          "[unit][view][implant]") {
  CHECK(implantForming(0, 1).form == 0);
  CHECK(implantForming(-1, 1).form == 0);
  CHECK(implantForming(.45f, 1).form == 1);
  CHECK(implantForming(1, 1).form == 1);
  CHECK(implantForming(.2f, 0).form == 1);
  CHECK(implantLeaving(0, 1).dissolve == 0);
  CHECK(implantLeaving(.22f, 1).dissolve == 1);
  CHECK(implantLeaving(.22001f, 1).afterimage == Approx(.06f));
  CHECK(implantLeaving(.4f, 1).afterimage > 0);
  CHECK(implantLeaving(.58f, 1).afterimage == 0);
  CHECK_FALSE(implantLeaving(.58f, 1).visible());
  CHECK_FALSE(implantLeaving(1, 1).visible());
  CHECK(implantLeaving(.2f, 0).dissolve == 1);
  CHECK(implantLeaving(.1f, 0).afterimage == 0);
}

TEST_CASE("neural heartbeat rate settles without losing integrated phase (M25.1)",
          "[unit][view][implant][neural]") {
  CHECK(heartbeatRate(-20) == 60);
  CHECK(heartbeatRate(0) == 60);
  CHECK(heartbeatRate(20) == 80);
  CHECK(heartbeatRate(55) == 110);
  HeartbeatPhase phase;
  phase.advance(0, .4);
  CHECK(phase.cycles == Approx(.4));
  const double before = phase.cycles;
  phase.advance(50, 0);
  CHECK(phase.cycles == before);
  phase.advance(50, 1.0 / 60);
  CHECK(phase.cycles > before);
  CHECK(phase.cycles - before < 110.0 / 3600);
  phase.advance(50, 30);
  CHECK(phase.bpm == Approx(110).margin(.01));
  phase.advance(0, 10);
  CHECK(phase.bpm < 63);
  HeartbeatPhase whole, split;
  whole.advance(35, 2);
  for (int i = 0; i < 120; ++i) split.advance(35, 1.0 / 60);
  CHECK(whole.cycles == Approx(split.cycles).margin(1e-6));
  CHECK(whole.bpm == Approx(split.bpm).margin(1e-4));
  whole.advance(0, -1);
  CHECK(whole.cycles == Approx(split.cycles).margin(1e-6));
}
