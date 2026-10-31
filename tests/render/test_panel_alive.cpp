// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN
#include <algorithm>
#include <cmath>
#include <vector>

#include "render/panel_alive.hpp"

using namespace cb::render;

TEST_CASE("cell glitches are bounded in duration, rate and simultaneity",
          "[render][led][alive]") {
  constexpr std::uint32_t kCells = 64;
  const float modulus = glitchModulus(kCells);
  // Seeds as the renderer makes them: an arbitrary 16-bit value per cell.
  std::vector<std::uint32_t> seeds;
  for (std::uint32_t c = 0; c < kCells; ++c) seeds.push_back(1 + panelHash(c) % 65535u);
  int starts = 0;
  int maxSimultaneous = 0;
  float minDur = 1e9f, maxDur = 0;
  std::vector<bool> was(kCells, false);
  const float step = 0.01f;
  for (float t = 0; t < 120.0f; t += step) {
    int now = 0;
    for (std::uint32_t c = 0; c < kCells; ++c) {
      const PanelGlitch g = panelGlitch(seeds[c], t, modulus);
      if (!g.on) {
        was[c] = false;
        continue;
      }
      ++now;
      if (!was[c]) ++starts;
      was[c] = true;
      minDur = std::min(minDur, g.duration);
      maxDur = std::max(maxDur, g.duration);
      CHECK(g.u >= 0.0f);
      CHECK(g.u < 1.0f);
      CHECK(g.kind < 4u);
    }
    maxSimultaneous = std::max(maxSimultaneous, now);
  }
  CHECK(minDur >= kGlitchMinSeconds - 1e-4f);
  CHECK(maxDur <= kGlitchMaxSeconds + 1e-4f);
  // "At most a few per board per ~10 s": over 120 s that is well below 40 and the field
  // is not dead either.
  CHECK(starts > 6);
  CHECK(starts < 60);
  CHECK(maxSimultaneous <= 2);
}

TEST_CASE("glitches are pure in time and never straddle the clock wrap",
          "[render][led][alive]") {
  const float modulus = glitchModulus(64);
  for (std::uint32_t seed : {1u, 777u, 40000u}) {
    for (float t : {0.3f, 10.7f, 55.1f}) {
      const auto a = panelGlitch(seed, t, modulus);
      const auto b = panelGlitch(seed, t, modulus);
      CHECK(a.on == b.on);
      CHECK(a.kind == b.kind);
    }
    // Everything is finished before the wrap at 120 s.
    for (float t = 119.7f; t < 120.0f; t += 0.01f)
      CHECK_FALSE(panelGlitch(seed, t, modulus).on);
  }
}

TEST_CASE("per-emitter colour change: most land within 120 ms, none after 0.6 s",
          "[render][led][alive]") {
  int total = 0, done120 = 0, lagging = 0, lazy = 0;
  float latest = 0;
  for (std::uint32_t seed = 1; seed <= 40; ++seed) {
    for (std::uint32_t y = 0; y < 24; ++y) {
      for (std::uint32_t x = 0; x < 24; ++x) {
        const auto t = emitterTransition(seed * 977u, x, y);
        ++total;
        latest = std::max(latest, t.end());
        if (t.end() <= 0.120f + 1e-4f)
          ++done120;
        else if (t.end() <= 0.50f)
          ++lagging;
        if (t.end() >= 0.40f) ++lazy;
        CHECK(t.delay >= 0.0f);
        CHECK(t.duration > 0.0f);
      }
    }
  }
  const float fast = static_cast<float>(done120) / static_cast<float>(total);
  CHECK(fast > 0.76f);
  CHECK(fast < 0.84f);
  CHECK(latest <= kTransitionMaxSeconds + 1e-4f);
  const float tail = static_cast<float>(total - done120) / static_cast<float>(total);
  CHECK(tail > 0.10f);  // a visible 10-20 % lag
  CHECK(tail < 0.24f);
  CHECK(lazy > 0);                                                      // some drift
  CHECK(static_cast<float>(lazy) / static_cast<float>(total) < 0.06f);  // but very few
  (void)lagging;
}

TEST_CASE("emitter progress is monotone, 0 before the change and 1 once settled",
          "[render][led][alive]") {
  const auto t = emitterTransition(1234, 5, 9);
  CHECK(emitterProgress(t, 0.0f) == 0.0f);
  CHECK(emitterProgress(t, kTransitionMaxSeconds) == 1.0f);
  float prev = 0;
  for (float age = 0; age <= 0.7f; age += 0.005f) {
    const float p = emitterProgress(t, age);
    CHECK(p >= prev - 1e-6f);
    prev = p;
  }
}

TEST_CASE("emitter faults wander: none permanent, they fade, and the wrap is continuous",
          "[render][led][alive]") {
  int faultyCells = 0;
  int relocated = 0;
  for (std::uint32_t seed = 1; seed <= 600; ++seed) {
    bool ever = false;
    for (std::uint32_t slot = 0; slot < kFaultSlots; ++slot) {
      float longestRun = 0, run = 0;
      std::uint32_t lastIx = 0, lastIy = 0;
      bool have = false;
      for (float t = 0; t < 240.0f; t += 0.25f) {
        const EmitterFault f = emitterFault(seed, slot, std::fmod(t, 120.0f));
        if (f.active && f.env > 0.0f) {
          ever = true;
          run += 0.25f;
          CHECK(f.env <= 1.0f);
          if (have && (f.ix != lastIx || f.iy != lastIy)) ++relocated;
          lastIx = f.ix;
          lastIy = f.iy;
          have = true;
        } else {
          run = 0.0f;
        }
        longestRun = std::max(longestRun, run);
      }
      CHECK(longestRun <= 0.7f * 60.0f + 0.5f);  // never a permanent fault
      // The clock wrap: the state just before and just after 120 s agree.
      const EmitterFault before = emitterFault(seed, slot, 119.99f);
      const EmitterFault after = emitterFault(seed, slot, 0.0f);
      CHECK(before.active == after.active);
    }
    if (ever) ++faultyCells;
  }
  CHECK(faultyCells > 60);   // a sixth of cells ever carry one
  CHECK(faultyCells < 300);  // but never most of the board
  CHECK(relocated > 0);      // faults reappear elsewhere
}

TEST_CASE("at any moment a handful of emitters are faulty on a 64-cell board",
          "[render][led][alive]") {
  int minActive = 1000, maxActive = 0;
  for (float t = 0; t < 120.0f; t += 1.0f) {
    int active = 0;
    for (std::uint32_t c = 0; c < 64; ++c) {
      const std::uint32_t seed = 1 + panelHash(c) % 65535u;
      for (std::uint32_t slot = 0; slot < kFaultSlots; ++slot)
        if (emitterFault(seed, slot, t).env > 0.5f) ++active;
    }
    minActive = std::min(minActive, active);
    maxActive = std::max(maxActive, active);
  }
  CHECK(maxActive >= 3);
  CHECK(maxActive <= 20);
  CHECK(minActive >= 0);
}

TEST_CASE("blinkers blink about one to three times a second", "[render][led][alive]") {
  // Find a blinking fault and count dark runs per second.
  for (std::uint32_t seed = 1; seed < 5000; ++seed) {
    for (std::uint32_t slot = 0; slot < kFaultSlots; ++slot) {
      for (float t0 = 0; t0 < 120.0f; t0 += 4.0f) {
        const EmitterFault f = emitterFault(seed, slot, t0 + 2.0f);
        if (!f.active || f.kind != 2u || f.env < 0.99f) continue;
        int blinks = 0;
        bool dark = false;
        for (float t = t0 + 1.0f; t < t0 + 3.0f; t += 0.02f) {
          const EmitterFault g = emitterFault(seed, slot, t);
          if (!g.active || g.kind != 2u) {
            dark = false;
            continue;
          }
          const bool now = g.level < 0.5f;
          if (now && !dark) ++blinks;
          dark = now;
        }
        const float perSecond = static_cast<float>(blinks) / 2.0f;
        CHECK(perSecond >= 0.5f);
        CHECK(perSecond <= 3.5f);
        return;
      }
    }
  }
  FAIL("no blinking fault found");
}
#endif
