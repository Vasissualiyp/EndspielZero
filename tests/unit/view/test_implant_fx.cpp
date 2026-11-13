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
    CHECK(heartbeat(t, 1) <= 1.020001f);
  }
  for (int i = -2; i <= 2; ++i) {
    CHECK(heartbeat(i, 1) == Approx(1.02f));
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
