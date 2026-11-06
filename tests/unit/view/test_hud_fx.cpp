// SPDX-License-Identifier: GPL-3.0-or-later
// Live HUD feedback (M23.11 section 4): pure timings in event age.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "view/hud_fx.hpp"

using namespace cb::view;
using Catch::Approx;

TEST_CASE("turn flip cross-fades over a quarter second with one LED flicker",
          "[render][hud-fx]") {
  CHECK(turnFlip(0, 1).mix == 0);
  CHECK(turnFlip(.125f, 1).mix == Approx(.5f));
  CHECK(turnFlip(.25f, 1).mix == 1);
  CHECK(turnFlip(5, 1).mix == 1);
  // Exactly one dark window, inside the flip; steady before and after.
  int dark = 0, edges = 0;
  bool was = false;
  for (int i = 0; i <= 100; ++i) {
    const bool off = turnFlip(static_cast<float>(i) * .0025f, 1).led < .5f;
    dark += off;
    edges += off != was;
    was = off;
  }
  CHECK(dark > 0);
  CHECK(edges == 2);
  CHECK(turnFlip(2, 1).led == 1);
  // Calm: the state changes at once and nothing flickers.
  CHECK(turnFlip(0, 0).mix == 1);
  CHECK(turnFlip(.1f, 0).led == 1);
}
TEST_CASE("thinking dots pulse for 0.4 s each, staggered by 0.13 s",
          "[render][hud-fx]") {
  for (int d = 0; d < 3; ++d) {
    const float t = static_cast<float>(d) * .13f;
    CHECK(thinkingDot(t, d, 1) == Approx(1).margin(1e-4));     // each peaks at its stagger
    CHECK(thinkingDot(t + .2f, d, 1) < .3f);                   // and is dim half a period on
    CHECK(thinkingDot(t + .4f, d, 1) == Approx(1).margin(1e-4));
  }
  CHECK(thinkingDot(0, 2, 1) < .4f);  // the last dot has not caught up yet
  CHECK(thinkingDot(0.07f, 1, 0) == 1);  // calm: lit steadily
}
TEST_CASE("check spike lasts 0.3 s from the ripple's start", "[render][hud-fx]") {
  CHECK(checkSpike(0, 1) == 1);
  CHECK(checkSpike(.15f, 1) > 0);
  CHECK(checkSpike(.15f, 1) < 1);
  CHECK(checkSpike(.3f, 1) == 0);
  CHECK(checkSpike(2, 1) == 0);
  CHECK(checkSpike(0, 0) == 0);
}
TEST_CASE("a new ledger row reveals over 0.2 s while older rows ease up over 0.15 s",
          "[render][hud-fx]") {
  CHECK(ledgerRowFx(0, 1).reveal == 0);
  CHECK(ledgerRowFx(.1f, 1).reveal == Approx(.5f));
  CHECK(ledgerRowFx(.2f, 1).reveal == 1);
  CHECK(ledgerRowFx(0, 1).shift == 1);
  CHECK(ledgerRowFx(.075f, 1).shift < 1);
  CHECK(ledgerRowFx(.075f, 1).shift > 0);
  CHECK(ledgerRowFx(.15f, 1).shift == 0);
  CHECK(ledgerRowFx(0, 0).reveal == 1);  // calm: instant
  CHECK(ledgerRowFx(0, 0).shift == 0);
}
