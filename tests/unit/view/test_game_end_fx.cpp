// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game-end power-down and power-up (M23.10 section 5): the pure models.
#include <algorithm>
#include <array>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "view/game_end_fx.hpp"

using namespace cb::view;
using Catch::Approx;

TEST_CASE("shutdownStarts: decisive order, king pause and calm stagger",
          "[unit][view][endfx]") {
  const EndFx end{EndKind::Decisive, cb::Color::White};
  const std::array pieces{ShutdownPiece{cb::Color::White, false, 1, 0},
                          ShutdownPiece{cb::Color::White, false, 4, 1},
                          ShutdownPiece{cb::Color::White, false, 2, 2},
                          ShutdownPiece{cb::Color::White, true, 0, 3},
                          ShutdownPiece{cb::Color::Black, false, 3, 4},
                          ShutdownPiece{cb::Color::Black, true, 5, 5}};
  const std::array<std::size_t, 6> order{1, 2, 0, 3, 4, 5};
  for (float intensity : {0.0f, 1.0f}) {
    const auto starts = shutdownStarts(end, pieces, 6.0f, intensity);
    REQUIRE(starts.size() == pieces.size());
    const float stagger = intensity < 0.5f ? kCalmShutdownStagger : kShutdownStagger;
    for (std::size_t k = 0; k < order.size(); ++k)
      CHECK(starts[order[k]] ==
            Approx(6.0f + static_cast<float>(k) * stagger +
                   (k == 5 && intensity >= 0.5f ? kKingPause : 0.0f)));
    CHECK(shutdownComplete(starts, intensity) ==
          Approx(starts[5] + (intensity < 0.5f ? kCalmShutdownFade : kCollapseSeconds)));
  }
}

TEST_CASE("shutdownStarts: spread cap, draw ties and missing armies",
          "[unit][view][endfx]") {
  std::vector<ShutdownPiece> army;
  for (std::uint32_t c = 0; c < 32; ++c)
    army.push_back({c % 2 == 0 ? cb::Color::White : cb::Color::Black, false, 3, c});
  army.push_back({cb::Color::White, true, 0, 32});
  army.push_back({cb::Color::Black, true, 5, 33});
  EndFx end{EndKind::Decisive, cb::Color::White};
  auto starts = shutdownStarts(end, army, 6.0f, 1.0f);
  CHECK(*std::max_element(starts.begin(), starts.end() - 1) -
            *std::min_element(starts.begin(), starts.end()) <=
        kShutdownSpread + 1e-5f);
  end.kind = EndKind::Draw;
  starts = shutdownStarts(end, army, 6.0f, 1.0f);
  CHECK(starts[0] < starts[2]);   // same side and distance: lower cell
  CHECK(starts[30] < starts[1]);  // equal distance: all white before black
  CHECK(starts[32] == starts[33]);
  CHECK(starts[32] == Approx(starts[31] + kShutdownSpread / 31.0f));
  army = {{cb::Color::Black, true, 0, 4}};
  end.kind = EndKind::Decisive;
  CHECK(shutdownStarts(end, army, 6.0f, 1.0f)[0] == 6.0f);
  army = {{cb::Color::Black, false, 2, 5}, {cb::Color::Black, false, 1, 4}};
  starts = shutdownStarts(end, army, 6.0f, 1.0f);
  CHECK(starts[1] == 6.0f);
  CHECK(starts[0] == Approx(6.0f + kShutdownStagger));
  CHECK(shutdownStarts(end, {}, 6.0f, 1.0f).empty());
}

TEST_CASE("hologramShutdown: dropouts, falling reveal, flash and stable endpoints",
          "[unit][view][endfx]") {
  for (std::uint32_t seed : {0u, 1u, 77u}) {
    const auto before = hologramShutdown(-1.0f, seed, 1.0f);
    CHECK(before.opacity == 1.0f);
    CHECK(before.reveal == 1.0f);
    CHECK(before.scanJitter == 0.0f);
    CHECK(before.baseHeat == 0.0f);
    bool dropout = false;
    for (int tick = 0; tick < 150; ++tick) {
      const auto h = hologramShutdown(static_cast<float>(tick) / 1000.0f, seed, 1.0f);
      dropout = dropout || h.opacity == 0.15f;
      CHECK(h.scanJitter == 1.0f);
    }
    CHECK(dropout);
    float previous = 1.0f;
    for (int tick = 15; tick <= 55; ++tick) {
      const float age = static_cast<float>(tick) / 100.0f;
      const auto h = hologramShutdown(age, seed, 1.0f);
      CHECK(h.reveal <= previous);
      CHECK(h.reveal >= 0.0f);
      const auto again = hologramShutdown(age, seed, 1.0f);
      CHECK(h.opacity == again.opacity);
      CHECK(h.reveal == again.reveal);
      CHECK(h.scanJitter == again.scanJitter);
      CHECK(h.baseHeat == again.baseHeat);
      previous = h.reveal;
    }
    CHECK(hologramShutdown(0.6f, seed, 1.0f).baseHeat > 0.0f);
    for (float age : {0.7f, 5.0f, 3600.0f}) {
      const auto h = hologramShutdown(age, seed, 1.0f);
      CHECK(h.gone());
      CHECK(h.opacity == 0.0f);
      CHECK(h.baseHeat == 0.0f);
    }
    for (float age : {-1.0f, 0.0f, 0.3f, 0.6f, 3600.0f}) {
      const auto h = hologramShutdown(age, seed, 0.0f);
      CHECK(h.baseHeat == 0.0f);
      CHECK(h.scanJitter == 0.0f);
      CHECK(h.reveal == 1.0f);
    }
    CHECK(hologramShutdown(0.6f, seed, 0.0f).opacity == 0.0f);
  }
}

TEST_CASE("hologramUnshutdown: undo reveals the pieces and settles neutral",
          "[unit][view][endfx]") {
  for (float intensity : {0.0f, 1.0f}) {
    CHECK(hologramUnshutdown(-0.1f, 77u, intensity).reveal == 0.0f);
    const auto h = hologramUnshutdown(0.35f, 77u, intensity);
    CHECK(h.opacity == 1.0f);
    CHECK(h.reveal == 1.0f);
    CHECK(h.scanJitter == 0.0f);
    CHECK(h.baseHeat == 0.0f);
  }
  CHECK(hologramUnshutdown(0.0f, 77u, 1.0f).reveal == 0.0f);
  CHECK(hologramUnshutdown(0.0f, 77u, 0.0f).opacity == 0.0f);
}

TEST_CASE("shutdown timing: after the wave, standby starts dim and pulses or stays calm",
          "[unit][view][endfx]") {
  CHECK(shutdownBegins(EndKind::Decisive, 9, 1.0f) == Approx(3.0f + 3.0f + 0.6f));
  CHECK(shutdownBegins(EndKind::Draw, 9, 1.0f) == Approx(6.0f));
  CHECK(shutdownBegins(EndKind::Decisive, 9, 0.0f) == Approx(5.0f));
  for (EndKind kind : {EndKind::Decisive, EndKind::Draw}) {
    CHECK(standbyLevel(kind, 9.9f, 10.0f, 1.0f) == 0.0f);
    CHECK(standbyLevel(kind, 10.0f, 10.0f, 1.0f) == Approx(0.35f));
    const float period = kind == EndKind::Decisive ? 2.0f : 4.0f;
    CHECK(standbyLevel(kind, 10.0f + period * 0.5f, 10.0f, 1.0f) == Approx(1.0f));
    for (float age : {10.0f, 10.3f, 11.0f, 60.0f, 3600.0f}) {
      CHECK(standbyLevel(kind, age, 10.0f, 1.0f) >= 0.35f);
      CHECK(standbyLevel(kind, age, 10.0f, 1.0f) <= 1.0f);
      CHECK(standbyLevel(kind, age, 10.0f, 0.0f) == 0.8f);
    }
  }
}

TEST_CASE("powerDown: full light until the front arrives, the hold level after",
          "[unit][view][endfx]") {
  for (int dist : {0, 1, 4, 9}) {
    const float arrival = kEndDelaySeconds + static_cast<float>(dist) / kPowerDownCellsPerSecond;
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
    for (float age : {kEndDelaySeconds + kDrawDimSeconds, kEndDelaySeconds + kDrawDimSeconds + 4.0f}) {
      CHECK(powerDown(EndKind::Draw, dist, age, 1.0f).power == Approx(kDrawHold));
    }
  }
}

TEST_CASE("powerDown: the glitch is only near arrival, never below the hold, calm has none",
          "[unit][view][endfx]") {
  const int dist = 4;
  const float arrival = kEndDelaySeconds + static_cast<float>(dist) / kPowerDownCellsPerSecond;
  CHECK(powerDown(EndKind::Decisive, dist, arrival - 0.01f, 1.0f).glitch == 0.0f);
  CHECK(powerDown(EndKind::Decisive, dist, arrival + 0.01f, 1.0f).glitch > 0.8f);
  CHECK(powerDown(EndKind::Decisive, dist, arrival + kPowerDownGlitchSeconds + 0.01f, 1.0f)
            .glitch == 0.0f);
  for (EndKind kind : {EndKind::Decisive, EndKind::Draw}) {
    for (float intensity : {0.0f, 1.0f}) {
      float lowest = 1.0f;
      for (float age = 0.0f; age < 12.0f; age += 0.02f)
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
  CHECK(powerDown(EndKind::Decisive, 0, kEndDelaySeconds + kCalmFadeSeconds, 0.0f).power ==
        Approx(kDecisiveHold));
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
    for (float age = 4.0f; age < 12.0f; age += 0.1f) {
      const HologramEnd l = hologramEnd(HologramSide::Loser, age, seed, 1.0f);
      CHECK(l.opacity == Approx(kLoserOpacity).margin(0.05f));
    }
    // One or two dropouts to 0.3 within the first second, each short.
    int dropped = 0;
    bool in = false;
    for (float age = 0.0f; age < kLoserSeconds; age += 0.002f) {
      const bool low = hologramEnd(HologramSide::Loser, age, seed, 1.0f).opacity <= 0.3f + 1e-4f;
      if (low && !in) ++dropped;
      in = low;
    }
    CHECK(dropped >= 1);
    CHECK(dropped <= 2);
    CHECK(hologramEnd(HologramSide::Loser, 1.0f, seed, 1.0f).scanJitter > 0.0f);
    // The winner stays solid.
    const HologramEnd w = hologramEnd(HologramSide::Winner, 6.0f, seed, 1.0f);
    CHECK(w.opacity == 1.0f);
    CHECK(w.gain == Approx(kWinnerGain));
    const HologramEnd d = hologramEnd(HologramSide::Draw, 6.0f, seed, 1.0f);
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
  for (float age = 0.0f; age < 5.0f; age += 0.01f) {
    const HologramEnd l = hologramEnd(HologramSide::Loser, age, 5u, 0.0f);
    CHECK(l.scanJitter == 0.0f);
    CHECK(l.opacity >= kLoserOpacity - 1e-6f);
  }
  CHECK(hologramEnd(HologramSide::Loser, 5.0f, 5u, 0.0f).opacity == Approx(kLoserOpacity));
}

TEST_CASE("hologramRestore: back to solid in the power-up time", "[unit][view][endfx]") {
  CHECK(hologramRestore(HologramSide::Loser, 0.0f).opacity == Approx(kLoserOpacity));
  CHECK(hologramRestore(HologramSide::Loser, kPowerUpSeconds).opacity == Approx(1.0f));
  CHECK(hologramRestore(HologramSide::Winner, kPowerUpSeconds).gain == Approx(1.0f));
}

TEST_CASE("powerDown: the board is untouched for the whole delay, and settles slowly after it",
          "[unit][view][endfx]") {
  for (EndKind kind : {EndKind::Decisive, EndKind::Draw})
    for (float intensity : {0.0f, 1.0f})
      for (int dist : {0, 3, 9})
        for (float age = 0.0f; age <= kEndDelaySeconds; age += 0.05f) {
          const PowerCell p = powerDown(kind, dist, age, intensity);
          CHECK(p.power == 1.0f);
          CHECK(p.glitch == 0.0f);
        }
  // The hold arrives only after the delay, the wave to the cell, and its fall.
  const int dist = 6;
  const float reach = kEndDelaySeconds + static_cast<float>(dist) / kPowerDownCellsPerSecond;
  CHECK(powerDown(EndKind::Decisive, dist, reach + 0.5f * kPowerDownFallSeconds, 1.0f).power >
        kDecisiveHold + 0.05f);
  CHECK(powerDown(EndKind::Decisive, dist, reach + kPowerDownFallSeconds + 0.001f, 1.0f).power ==
        Approx(kDecisiveHold));
  // A draw dims over 3 s and calm fades over 2 s, both after the delay.
  CHECK(powerDown(EndKind::Draw, 0, kEndDelaySeconds + 0.5f * kDrawDimSeconds, 1.0f).power >
        kDrawHold + 0.05f);
  CHECK(powerDown(EndKind::Draw, 0, kEndDelaySeconds + kDrawDimSeconds + 0.01f, 1.0f).power ==
        Approx(kDrawHold));
  CHECK(powerDown(EndKind::Decisive, 0, kEndDelaySeconds + 0.5f * kCalmFadeSeconds, 0.0f)
            .power > kDecisiveHold + 0.05f);
}
