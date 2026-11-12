// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/game_end_fx.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

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
  age -= kEndDelaySeconds;  // the final position is held first
  if (!(age > 0.0f)) return p;
  if (intensity < 0.5f) {
    p.power = 1.0f + (hold - 1.0f) * smooth(age / kCalmFadeSeconds);
    return p;
  }
  if (kind == EndKind::Draw) {
    p.power = 1.0f + (hold - 1.0f) * smooth(age / kDrawDimSeconds);
    // One soft unison flicker on the way down; it is over well before the hold.
    const float x = (age - 1.0f) / 0.12f;  // about a third of the way in
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
      h.gain = 1.0f + (gain - 1.0f) * smooth(age / 1.5f);
      return h;
    case HologramSide::Draw:
      h.opacity = 1.0f + (opacity - 1.0f) * smooth(age / kDrawDimSeconds);
      return h;
    case HologramSide::Loser:
      break;
  }
  h.opacity = 1.0f + (opacity - 1.0f) * smooth(age / kLoserSeconds);
  // The scan is irregular while it destabilises and only a little after.
  h.scanJitter =
      smooth(age / 0.75f) * (1.0f - 0.8f * smooth((age - kLoserSeconds) / 0.6f));
  // One or two short dropouts inside the destabilising second.
  const int drops = 1 + static_cast<int>(seed % 2u);
  for (int i = 0; i < drops; ++i) {
    const std::uint32_t s = seed * 2654435761u + static_cast<std::uint32_t>(i) * 0x9e3779b9u;
    // Spread across the destabilising window, the second always after the first half.
    const float start =
        0.45f + 1.0f * unit(s ^ 0x1234567u) + (i == 0 ? 0.0f : 0.9f);
    const float len = 0.05f + 0.03f * unit(s ^ 0x7654321u);
    if (age >= start && age < start + len) h.opacity = 0.3f;
  }
  // Settled: a slow shimmer, 0.3 Hz.
  const float shimmer = smooth((age - 0.9f * kLoserSeconds) / 0.9f);
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

float shutdownBegins(EndKind kind, int maxDist, float intensity) noexcept {
  if (intensity < 0.5f) return kEndDelaySeconds + kCalmFadeSeconds;
  if (kind == EndKind::Draw) return kEndDelaySeconds + kDrawDimSeconds;
  return kEndDelaySeconds +
         static_cast<float>(std::max(maxDist, 0)) / kPowerDownCellsPerSecond +
         kPowerDownGlitchSeconds;
}

std::vector<float> shutdownStarts(const EndFx& end, std::span<const ShutdownPiece> pieces,
                                  float firstStart, float intensity) {
  const bool draw = end.kind == EndKind::Draw;
  const auto group = [&](const ShutdownPiece& p) {
    if (draw) return p.royal ? 1 : 0;
    return (p.side == end.loser ? 0 : 2) + (p.royal ? 1 : 0);
  };
  const int finalGroup = draw ? 1 : 3;
  std::vector<std::size_t> order(pieces.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    const auto& p = pieces[a];
    const auto& q = pieces[b];
    const int g = group(p), h = group(q);
    if (g != h) return g < h;
    if (!p.royal && p.dist != q.dist)
      return draw || g == 0 ? p.dist > q.dist : p.dist < q.dist;
    if (draw && p.side != q.side) return p.side == Color::White;
    if (p.cell != q.cell) return p.cell < q.cell;
    return a < b;
  });
  const auto nonfinal = static_cast<std::size_t>(
      std::count_if(pieces.begin(), pieces.end(),
                    [&](const auto& p) { return group(p) != finalGroup; }));
  float stagger = intensity < 0.5f ? kCalmShutdownStagger : kShutdownStagger;
  if (nonfinal > 1)
    stagger = std::min(stagger, kShutdownSpread / static_cast<float>(nonfinal - 1));
  const float finalStart =
      firstStart + static_cast<float>(nonfinal) * stagger +
      (nonfinal > 0 && !draw && intensity >= 0.5f ? kKingPause : 0.0f);
  std::vector<float> starts(pieces.size());
  for (std::size_t k = 0; k < order.size(); ++k)
    starts[order[k]] = group(pieces[order[k]]) == finalGroup
                           ? finalStart
                           : firstStart + static_cast<float>(k) * stagger;
  return starts;
}

HologramShutdown hologramShutdown(float age, std::uint32_t seed, float intensity) {
  HologramShutdown h;
  if (age < 0.0f) return h;
  if (intensity < 0.5f) {
    h.opacity = 1.0f - smooth(age / kCalmShutdownFade);
    return h;
  }
  if (age >= kCollapseSeconds) return {0, 0, 0, 0};
  if (age >= 0.55f) {
    h.reveal = 0;
    h.baseHeat = std::clamp(1.0f - (age - 0.55f) / 0.15f, 0.0f, 1.0f);
    return h;
  }
  h.scanJitter = 1;
  if (age >= 0.15f) {
    const float x = (age - 0.15f) / 0.40f;
    h.reveal = std::clamp(1.0f - x * x, 0.0f, 1.0f);
  } else {
    const std::uint32_t drops = 2 + seed % 2;
    for (std::uint32_t i = 0; i < drops; ++i) {
      const float start = 0.01f + 0.13f * unit(seed ^ (0x51EDu + i * 0x9E37u));
      const float length = 0.03f + 0.02f * unit(seed ^ (0xD1CEu + i * 0x7F4Au));
      if (age >= start && age < start + length) h.opacity = 0.15f;
    }
  }
  return h;
}

HologramShutdown hologramUnshutdown(float age, std::uint32_t seed, float intensity) {
  if (age < 0.0f) return {1, 0, 0, 0};
  HologramShutdown h;
  if (age >= kRestoreRevealSeconds) return h;
  const float f = smooth(age / kRestoreRevealSeconds);
  if (intensity < 0.5f) {
    h.opacity = f;
  } else {
    h.reveal = f;
    h.scanJitter = 1.0f - f;
    const float start = 0.05f + 0.10f * unit(seed ^ 0xB007u);
    if (age >= start && age < start + 0.04f) h.opacity = 0.3f;
  }
  return h;
}

float shutdownComplete(std::span<const float> starts, float intensity) noexcept {
  if (starts.empty()) return 0.0f;  // caller supplies firstStart for an empty board
  return *std::max_element(starts.begin(), starts.end()) +
         (intensity < 0.5f ? kCalmShutdownFade : kCollapseSeconds);
}

float standbyLevel(EndKind kind, float age, float complete, float intensity) noexcept {
  if (age < complete) return 0.0f;
  if (intensity < 0.5f) return 0.8f;
  const float frequency = kind == EndKind::Decisive ? 0.5f : 0.25f;
  return std::clamp(
      0.35f + 0.65f * (0.5f + 0.5f * std::sin(kTwoPi * frequency * (age - complete) -
                                              kTwoPi * 0.25f)),
      0.35f, 1.0f);
}

}  // namespace cb::view
