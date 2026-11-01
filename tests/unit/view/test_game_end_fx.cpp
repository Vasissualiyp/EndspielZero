// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game-end power-down and power-up (M23.10 section 5): the pure models.
#include <algorithm>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "view/game_end_fx.hpp"

using namespace cb::view;
using Catch::Approx;

TEST_CASE("powerDown: full light until the front arrives, the hold level after",
          "[unit][view][endfx]") {
  for (int dist : {0, 1, 4, 9}) {
    const float arrival = static_cast<float>(dist) / kPowerDownCellsPerSecond;
    CHECK(powerDown(EndKind::Decisive, dist, arrival - 0.05f, 1.0f).power == 1.0f);
    CHECK(powerDown(EndKind::Decisive, dist, -1.0f, 1.0f).power == 1.0f);
    const float settle = arrival + kPowerDownFallSeconds + 0.001f;
    for (float age : {settle, settle + 0.5f, settle + 30.0f}) {
      const PowerCell p = powerDown(EndKind::Decisive, dist, age, 1.0f);
      CHECK(p.power == Approx(kDecisiveHold));
      CHECK(p.glitch == 0.0f);
    }
  }
  for (int dist : {0, 5}) {
    for (float age : {kDrawDimSeconds, kDrawDimSeconds + 4.0f}) {
      CHECK(powerDown(EndKind::Draw, dist, age, 1.0f).power == Approx(kDrawHold));
    }
  }
}

TEST_CASE("powerDown: the glitch is only near arrival, never below the hold, calm has none",
          "[unit][view][endfx]") {
  const int dist = 4;
  const float arrival = static_cast<float>(dist) / kPowerDownCellsPerSecond;
  CHECK(powerDown(EndKind::Decisive, dist, arrival - 0.01f, 1.0f).glitch == 0.0f);
  CHECK(powerDown(EndKind::Decisive, dist, arrival + 0.01f, 1.0f).glitch > 0.8f);
  CHECK(powerDown(EndKind::Decisive, dist, arrival + kPowerDownGlitchSeconds + 0.01f, 1.0f)
            .glitch == 0.0f);
  for (EndKind kind : {EndKind::Decisive, EndKind::Draw}) {
    for (float intensity : {0.0f, 1.0f}) {
      float lowest = 1.0f;
      for (float age = 0.0f; age < 6.0f; age += 0.01f)
        for (int d : {0, 3, 8}) {
          const PowerCell p = powerDown(kind, d, age, intensity);
          lowest = std::min(lowest, p.power);
          CHECK(p.power >= holdLevel(kind) - 1e-6f);
          CHECK(p.power <= 1.0f + 1e-6f);
          if (kind == EndKind::Draw || intensity < 0.5f) CHECK(p.glitch == 0.0f);
        }
      CHECK(lowest == Approx(holdLevel(kind)));
    }
  }
  CHECK(powerDown(EndKind::Decisive, 0, 0.6f, 0.0f).power == Approx(kDecisiveHold));
}

TEST_CASE("powerUp: from the hold, one overshoot, then normal", "[unit][view][endfx]") {
  CHECK(powerUp(EndKind::Decisive, 0.0f, 1.0f, 3u).power == Approx(kDecisiveHold));
  float peak = 0;
  for (float age = 0.0f; age < kPowerUpSeconds; age += 0.005f)
    peak = std::max(peak, powerUp(EndKind::Decisive, age, 1.0f, 3u).power);
  CHECK(peak == Approx(kPowerUpOvershoot).margin(0.01f));
  CHECK(powerUp(EndKind::Decisive, kPowerUpSeconds, 1.0f, 3u).power == 1.0f);
  CHECK(powerUp(EndKind::Draw, 5.0f, 1.0f, 3u).glitch == 0.0f);
  // Calm rises without the overshoot or any glitch.
  for (float age = 0.0f; age < kPowerUpSeconds; age += 0.01f) {
    const PowerCell p = powerUp(EndKind::Draw, age, 0.0f, 3u);
    CHECK(p.power <= 1.0f);
    CHECK(p.glitch == 0.0f);
  }
}

TEST_CASE("hologramEnd: loser destabilises to 0.65, winner is solid and brighter, draw 0.85",
          "[unit][view][endfx]") {
  for (std::uint32_t seed : {1u, 2u, 77u}) {
    // Settled, the loser shimmers about 0.65 within a few per cent.
    for (float age = 1.5f; age < 6.0f; age += 0.1f) {
      const HologramEnd l = hologramEnd(HologramSide::Loser, age, seed, 1.0f);
      CHECK(l.opacity == Approx(kLoserOpacity).margin(0.05f));
    }
    // One or two dropouts to 0.3 within the first second, each short.
    int dropped = 0;
    bool in = false;
    for (float age = 0.0f; age < 1.0f; age += 0.002f) {
      const bool low = hologramEnd(HologramSide::Loser, age, seed, 1.0f).opacity <= 0.3f + 1e-4f;
      if (low && !in) ++dropped;
      in = low;
    }
    CHECK(dropped >= 1);
    CHECK(dropped <= 2);
    CHECK(hologramEnd(HologramSide::Loser, 0.4f, seed, 1.0f).scanJitter > 0.0f);
    // The winner stays solid.
    const HologramEnd w = hologramEnd(HologramSide::Winner, 3.0f, seed, 1.0f);
    CHECK(w.opacity == 1.0f);
    CHECK(w.gain == Approx(kWinnerGain));
    const HologramEnd d = hologramEnd(HologramSide::Draw, 3.0f, seed, 1.0f);
    CHECK(d.opacity == Approx(kDrawOpacity));
    CHECK(d.gain == 1.0f);
  }
  // Before the front arrives nothing has changed.
  const HologramEnd none = hologramEnd(HologramSide::Loser, -0.5f, 1u, 1.0f);
  CHECK(none.opacity == 1.0f);
  CHECK(none.gain == 1.0f);
}

TEST_CASE("hologramEnd: calm fades to the same levels with no jitter or dropout",
          "[unit][view][endfx]") {
  for (float age = 0.0f; age < 3.0f; age += 0.01f) {
    const HologramEnd l = hologramEnd(HologramSide::Loser, age, 5u, 0.0f);
    CHECK(l.scanJitter == 0.0f);
    CHECK(l.opacity >= kLoserOpacity - 1e-6f);
  }
  CHECK(hologramEnd(HologramSide::Loser, 3.0f, 5u, 0.0f).opacity == Approx(kLoserOpacity));
}

TEST_CASE("hologramRestore: back to solid in the power-up time", "[unit][view][endfx]") {
  CHECK(hologramRestore(HologramSide::Loser, 0.0f).opacity == Approx(kLoserOpacity));
  CHECK(hologramRestore(HologramSide::Loser, kPowerUpSeconds).opacity == Approx(1.0f));
  CHECK(hologramRestore(HologramSide::Winner, kPowerUpSeconds).gain == Approx(1.0f));
}
