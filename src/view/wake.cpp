// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/wake.hpp"

#include <algorithm>
#include <cmath>

namespace cb::view {
namespace {
float clamp01(float x) noexcept { return std::clamp(x, 0.f, 1.f); }
float smooth(float x) noexcept {
  x = clamp01(x);
  return x * x * (3.f - 2.f * x);
}
/// 0 before `a`, 1 after `b`, smooth between.
float ramp(float age, float a, float b) noexcept { return smooth((age - a) / (b - a)); }
float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

/// A blink: closes over `close` seconds starting at `start`, then opens over `open`.
float blink(float age, float start, float close, float open) noexcept {
  if (age <= start || age >= start + close + open) return 1.f;
  if (age < start + close) return 1.f - smooth((age - start) / close);
  return smooth((age - start - close) / open);
}

// The lids overlap when shut, so no row of the frame survives between them; they meet at
// 0.47 H, slightly below the centre line. The upper lid travels farther and 1.35x faster.
constexpr float kTopClosed = .55f, kBottomClosed = .61f, kUpperSpeed = 1.35f;
constexpr float kBlink1Start = 1.9f, kBlink1Close = .18f, kBlink1Open = .22f;
constexpr float kBlink2Start = 2.6f, kBlink2Close = .12f, kBlink2Open = .16f;
constexpr float kHandshakeStart = 3.0f;

void setLids(WakeState& w) noexcept {
  w.topEdge = kTopClosed * (1.f - std::min(1.f, kUpperSpeed * w.lidOpen));
  w.bottomEdge = kBottomClosed * (1.f - w.lidOpen);
  w.curve = 1.f - smooth((w.lidOpen - .85f) / .15f);
  // Fully open means no lid anywhere: the corners must not stay covered by the bow.
  if (w.lidOpen >= 1.f) w.curve = 0.f;
}
}  // namespace

WakeState wakeState(float age, float intensity) noexcept {
  WakeState w;
  if (!std::isfinite(age)) return w;
  age = std::max(age, 0.f);
  if (intensity < kWakeCalmBelow) {
    constexpr float kEnd = 1.2f;
    if (age >= kEnd) return w;
    w.done = false;
    w.lidOpen = ramp(age, 0, .6f);
    w.blurFraction = .025f * (1.f - ramp(age, 0, .8f));
    w.exposureEv = lerp(-1.5f, 0, ramp(age, 0, .8f));
    w.saturation = lerp(.7f, 1.f, ramp(age, 0, .8f));
    w.lidGlow = .03f * (1.f - ramp(age, 0, .2f));
    w.roomUp = ramp(age, 0, .8f);
    w.menuFade = ramp(age, .8f, kEnd);
    setLids(w);
    return w;
  }
  if (age >= kWakeSeconds) return w;
  w.done = false;

  // Lids: closed, one opening with a hesitation at 40 %, then two blinks.
  float open;
  if (age < .6f) {
    open = 0;
  } else if (age < 1.05f) {
    open = .4f * smooth((age - .6f) / .45f);
  } else if (age < 1.2f) {
    open = .4f;
  } else if (age < 1.6f) {
    open = lerp(.4f, 1.f, smooth((age - 1.2f) / .4f));
  } else {
    open = 1.f;
  }
  open = std::min(open, blink(age, kBlink1Start, kBlink1Close, kBlink1Open));
  open = std::min(open, blink(age, kBlink2Start, kBlink2Close, kBlink2Open));
  w.lidOpen = open;
  setLids(w);

  // The vision clears in steps: the opening, and then each blink takes it closer to sharp.
  const float opening = ramp(age, .6f, 1.6f);
  const float b1 = ramp(age, kBlink1Start, kBlink1Start + kBlink1Close + kBlink1Open);
  const float b2 = ramp(age, kBlink2Start, kBlink2Start + kBlink2Close + kBlink2Open);
  w.blurFraction = .025f;
  w.blurFraction = lerp(w.blurFraction, .018f, opening);
  w.blurFraction = lerp(w.blurFraction, .010f, b1);
  w.blurFraction = lerp(w.blurFraction, 0.f, b2);
  w.exposureEv = -1.5f;
  w.exposureEv = lerp(w.exposureEv, -.8f, opening);
  w.exposureEv = lerp(w.exposureEv, -.4f, b1);
  w.exposureEv = lerp(w.exposureEv, 0.f, b2);
  w.saturation = lerp(.7f, 1.f, ramp(age, kBlink2Start, 2.9f));
  // A pure-black lid needs the glow to be exactly 0 once it ends, and during blinks.
  w.lidGlow = age < .8f ? .03f * (.8f + .2f * std::sin(2.1f * age)) * (1.f - ramp(age, .6f, .8f)) : 0.f;
  w.roomUp = ramp(age, 2.0f, 3.2f);
  w.handshake = age >= kHandshakeStart ? age - kHandshakeStart : -1.f;
  w.menuFade = age >= kHandshakeStart ? 1.f : 0.f;
  return w;
}
}  // namespace cb::view
