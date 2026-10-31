// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/panel_alive.hpp"

#include <algorithm>
#include <cmath>

namespace cb::render {

std::uint32_t panelHash(std::uint32_t x) noexcept {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}

float panelUnit(std::uint32_t x) noexcept {
  return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

float glitchModulus(std::uint32_t ledCells) noexcept {
  return std::max(8.0f, std::floor(static_cast<float>(ledCells) * kGlitchSparsity));
}

PanelGlitch panelGlitch(std::uint32_t seed16, float seconds, float modulus) noexcept {
  PanelGlitch g;
  const auto slot = static_cast<std::uint32_t>(std::floor(seconds / kGlitchSlot));
  const std::uint32_t slotHash = panelHash(slot * 0x9e3779b9u ^ 0x51ed270bu);
  const std::uint32_t m =
      std::max<std::uint32_t>(static_cast<std::uint32_t>(modulus), 1u);
  if (seed16 % m != (slotHash >> 8) % m) return g;
  const std::uint32_t event = panelHash(slotHash ^ (seed16 * 0x68bc21ebu));
  g.start = 0.05f + 1.15f * panelUnit(panelHash(event ^ 0x1234567u));
  g.duration = kGlitchMinSeconds + (kGlitchMaxSeconds - kGlitchMinSeconds) *
                                       panelUnit(panelHash(event ^ 0x89abcdefu));
  const float age = seconds - static_cast<float>(slot) * kGlitchSlot - g.start;
  if (age <= 0.0f || age >= g.duration) return g;
  g.on = true;
  g.u = age / g.duration;
  g.kind = panelHash(event ^ 0x31415926u) % 4u;
  g.lane = panelHash(event ^ 0x27182818u);
  return g;
}

EmitterTransition emitterTransition(std::uint32_t seed16, std::uint32_t ix,
                                    std::uint32_t iy) noexcept {
  const std::uint32_t h =
      panelHash(seed16 ^ (ix * 0x1f3d5b79u) ^ (iy * 0x9e3779b1u) ^ 0x7a3c5e91u);
  const float cls = panelUnit(h);
  const float a = panelUnit(panelHash(h ^ 0x85ebca6bu));
  const float b = panelUnit(panelHash(h ^ 0xc2b2ae35u));
  EmitterTransition t;
  if (cls < 0.80f) {  // the prompt majority: done by 120 ms
    t.delay = 0.040f * a;
    t.duration = 0.040f + 0.040f * b;
  } else if (cls < 0.98f) {  // the lagging tail: 2-4x as long
    t.delay = 0.060f + 0.100f * a;
    t.duration = 0.120f + 0.200f * b;
  } else {  // a very few lazy emitters drift in over ~0.4-0.6 s
    t.delay = 0.100f + 0.100f * a;
    t.duration = 0.300f + 0.100f * b;
  }
  return t;
}

float emitterProgress(const EmitterTransition& t, float age) noexcept {
  const float x = std::clamp((age - t.delay) / std::max(t.duration, 1e-4f), 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

EmitterFault emitterFault(std::uint32_t seed16, std::uint32_t slot,
                          float seconds) noexcept {
  EmitterFault f;
  if (panelHash(seed16 ^ 0x1b873593u) % 6u != 0u) return f;  // about one cell in six
  static constexpr std::uint32_t kPeriods[9]{8, 10, 12, 15, 20, 24, 30, 40, 60};
  const std::uint32_t h =
      panelHash(seed16 * 0x2c1b3c6du ^ slot * 0x297a2d39u ^ 0x6a09e667u);
  const std::uint32_t period = kPeriods[h % 9u];
  const float p = static_cast<float>(period);
  const float x = seconds + p * panelUnit(panelHash(h ^ 0x1111u));
  const float raw = std::floor(x / p);
  const auto epoch = static_cast<std::uint32_t>(raw) % (120u / period);
  const float phase = x / p - raw;
  const std::uint32_t e = panelHash(h ^ (epoch * 0x9e3779b9u) ^ 0x5bd1e995u);
  if (e % 3u != 0u) return f;  // a third of the periods carry a fault
  const auto smooth = [](float a, float b, float v) {
    const float t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  };
  f.active = true;
  f.ix = 2u + (e >> 4) % 20u;
  f.iy = 2u + (e >> 12) % 20u;
  f.kind = (e >> 20) % 3u;
  // A fault occupies the first 70 % of its period, so none is ever permanent.
  const float w = phase / 0.7f;
  f.env = w >= 1.0f ? 0.0f : smooth(0.0f, 0.15f, w) * (1.0f - smooth(0.85f, 1.0f, w));
  if (f.kind == 0u) {
    f.level = 0.12f;
  } else if (f.kind == 1u) {
    f.level = 1.7f;
  } else {
    // Irregular blinks, about 1-3 per second: six ticks a second, a quarter of them dark.
    const auto tick = static_cast<std::uint32_t>(std::floor(seconds * 6.0f));
    const std::uint32_t r = panelHash(tick ^ e);
    f.level = r % 4u == 0u ? 0.12f : (r % 9u == 0u ? 1.6f : 1.0f);
  }
  return f;
}

}  // namespace cb::render
