// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/hud_fx.hpp"

#include <algorithm>
#include <cmath>

namespace cb::view {

namespace {
constexpr bool calm(float intensity) noexcept { return intensity < 0.5f; }
}  // namespace

TurnFlip turnFlip(float age, float intensity) noexcept {
  if (calm(intensity)) return {1.0f, 1.0f};
  const float u = std::clamp(age / kTurnFlipSeconds, 0.0f, 1.0f);
  const bool dark = age >= kTurnFlickerStart && age < kTurnFlickerEnd;
  return {u * u * (3.0f - 2.0f * u), dark ? 0.15f : 1.0f};
}

float thinkingDot(float age, int dot, float intensity) noexcept {
  if (calm(intensity)) return 1.0f;
  constexpr float kTwoPi = 6.2831853f;
  const float phase =
      (age - static_cast<float>(dot) * kThinkStaggerSeconds) / kThinkDotSeconds;
  return 0.15f + 0.85f * (0.5f + 0.5f * std::cos(kTwoPi * phase));
}

float checkSpike(float age, float intensity) noexcept {
  if (calm(intensity) || age < 0.0f || age >= kCheckSpikeSeconds) return 0.0f;
  const float u = 1.0f - age / kCheckSpikeSeconds;
  return u * u;
}

LedgerRowFx ledgerRowFx(float age, float intensity) noexcept {
  if (calm(intensity)) return {1.0f, 0.0f};
  const float reveal = std::clamp(age / kRowRevealSeconds, 0.0f, 1.0f);
  const float rest = 1.0f - std::clamp(age / kRowShiftSeconds, 0.0f, 1.0f);
  return {reveal, rest * rest * rest};
}

}  // namespace cb::view
