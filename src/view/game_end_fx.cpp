// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/game_end_fx.hpp"

#include <algorithm>
#include <cmath>

namespace cb::view {

namespace {

float smooth(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

float unit(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

constexpr float kTwoPi = 6.2831853f;

float settledOpacity(HologramSide side) {
  switch (side) {
    case HologramSide::Loser: return kLoserOpacity;
    case HologramSide::Draw: return kDrawOpacity;
    case HologramSide::Winner: break;
  }
  return 1.0f;
}

float settledGain(HologramSide side) {
  return side == HologramSide::Winner ? kWinnerGain : 1.0f;
}

}  // namespace

float holdLevel(EndKind kind) noexcept {
  return kind == EndKind::Draw ? kDrawHold : kDecisiveHold;
}

PowerCell powerDown(EndKind kind, int dist, float age, float intensity) {
  PowerCell p;
  const float hold = holdLevel(kind);
  if (!(age > 0.0f)) return p;
  if (intensity < 0.5f) {
    p.power = 1.0f + (hold - 1.0f) * smooth(age / kCalmFadeSeconds);
    return p;
  }
  if (kind == EndKind::Draw) {
    p.power = 1.0f + (hold - 1.0f) * smooth(age / kDrawDimSeconds);
    // One soft unison flicker on the way down; it is over well before the hold.
    const float x = (age - 0.30f) / 0.12f;
    if (x > 0.0f && x < 1.0f) p.power *= 1.0f - 0.18f * std::sin(3.14159265f * x);
    p.power = std::max(p.power, hold);
    return p;
  }
  const float arrival = static_cast<float>(std::max(dist, 0)) / kPowerDownCellsPerSecond;
  const float a = age - arrival;
  if (a <= 0.0f) return p;
  p.glitch = a >= kPowerDownGlitchSeconds ? 0.0f : 1.0f - a / kPowerDownGlitchSeconds;
  p.power = 1.0f + (hold - 1.0f) * smooth(a / kPowerDownFallSeconds);
  return p;
}

PowerCell powerUp(EndKind kind, float age, float intensity, std::uint32_t seed) {
  PowerCell p;
  const float hold = holdLevel(kind);
  if (!(age > 0.0f)) {
    p.power = hold;
    return p;
  }
  if (age >= kPowerUpSeconds) return p;
  if (intensity < 0.5f) {
    p.power = hold + (1.0f - hold) * smooth(age / kPowerUpSeconds);
    return p;
  }
  const float half = 0.5f * kPowerUpSeconds;
  p.power = age < half
                ? hold + (kPowerUpOvershoot - hold) * smooth(age / half)
                : kPowerUpOvershoot + (1.0f - kPowerUpOvershoot) * smooth((age - half) / half);
  // Each cell comes back to life with a short burst of its own, a little after the rise.
  const float start = 0.05f + 0.20f * unit(seed);
  p.glitch = std::clamp(1.0f - (age - start) / 0.2f, 0.0f, 1.0f) * (age >= start ? 1.0f : 0.0f);
  return p;
}

HologramEnd hologramEnd(HologramSide side, float age, std::uint32_t seed, float intensity) {
  HologramEnd h;
  const float opacity = settledOpacity(side);
  const float gain = settledGain(side);
  if (!(age > 0.0f)) return h;
  if (intensity < 0.5f) {
    const float f = smooth(age / kCalmFadeSeconds);
    h.opacity = 1.0f + (opacity - 1.0f) * f;
    h.gain = 1.0f + (gain - 1.0f) * f;
    return h;
  }
  switch (side) {
    case HologramSide::Winner:
      h.gain = 1.0f + (gain - 1.0f) * smooth(age / 0.5f);
      return h;
    case HologramSide::Draw:
      h.opacity = 1.0f + (opacity - 1.0f) * smooth(age / kDrawDimSeconds);
      return h;
    case HologramSide::Loser:
      break;
  }
  h.opacity = 1.0f + (opacity - 1.0f) * smooth(age / kLoserSeconds);
  // The scan is irregular while it destabilises and only a little after.
  h.scanJitter = smooth(age / 0.25f) * (1.0f - 0.8f * smooth((age - kLoserSeconds) / 0.4f));
  // One or two short dropouts inside the destabilising second.
  const int drops = 1 + static_cast<int>(seed % 2u);
  for (int i = 0; i < drops; ++i) {
    const std::uint32_t s = seed * 2654435761u + static_cast<std::uint32_t>(i) * 0x9e3779b9u;
    const float start = 0.15f + 0.65f * unit(s ^ 0x1234567u) * (i == 0 ? 0.5f : 1.0f);
    const float len = 0.05f + 0.03f * unit(s ^ 0x7654321u);
    if (age >= start && age < start + len) h.opacity = 0.3f;
  }
  // Settled: a slow shimmer, 0.3 Hz.
  const float shimmer = smooth((age - 0.9f) / 0.3f);
  h.opacity += 0.04f * shimmer * std::sin(kTwoPi * 0.3f * age + kTwoPi * unit(seed ^ 0xabcdefu));
  return h;
}

HologramEnd hologramRestore(HologramSide side, float age) {
  HologramEnd h;
  const float f = smooth(age / kPowerUpSeconds);
  h.opacity = settledOpacity(side) + (1.0f - settledOpacity(side)) * f;
  h.gain = settledGain(side) + (1.0f - settledGain(side)) * f;
  return h;
}

}  // namespace cb::view
