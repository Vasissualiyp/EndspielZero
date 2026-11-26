// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/neural_vr.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "view/rig_move.hpp"

namespace cb::view {
namespace {
float clamp01(float x) noexcept {
  return std::clamp(x, 0.f, 1.f);
}
float smooth(float x) noexcept {
  x = clamp01(x);
  return x * x * (3.f - 2.f * x);
}
constexpr float kTwoPi = 6.2831853f;
/// The code's pulse period: it breathes twice in its 1.5 s.
constexpr float kCodePulseSeconds = 0.75f;

VrState inside() noexcept {
  VrState s;
  s.room = false;
  s.horizon = 1;
  return s;
}
}  // namespace

int deployedSlabCount(const std::vector<Placement>& placements) {
  return static_cast<int>(slabsOf(placements, 0).size());
}

bool needsVr(const std::vector<Placement>& placements, int rackBays) {
  return deployedSlabCount(placements) > rackBays;
}

bool needsVr(const VariantSpec& variant, const ViewConfig& cfg, int rackBays) {
  return needsVr(layout(variant.dims, cfg), rackBays);
}

VrState vrEnter(float age, float intensity) noexcept {
  if (!std::isfinite(age)) return inside();
  age = std::max(age, 0.f);
  if (intensity < .5f) {
    if (age >= kVrEnterCalmSeconds) return inside();
    VrState s;
    s.done = false;
    s.slabAge = 1e3f;  // a calm deploy hangs the slab at once
    const float half = kVrEnterCalmSeconds * .5f;
    if (age < half) {
      s.room = true;
      s.code = 1;
      s.horizon = 0;
      s.dark = smooth(age / half);
    } else {
      s.dark = 1.f - smooth((age - half) / half);
    }
    return s;
  }
  if (age >= kVrEnterSeconds) return inside();
  VrState s;
  s.done = false;
  s.horizon = 0;
  const float shut = kVrLidsCloseAt + kVrLidsCloseSeconds;
  const float closing = smooth((age - kVrLidsCloseAt) / kVrLidsCloseSeconds);
  if (age < shut) {
    s.room = true;
    s.slabAge = age;
    if (age >= kVrSlabLock) {
      const float since = age - kVrSlabLock;
      const float pulse = .66f + .34f * std::cos(kTwoPi * since / kCodePulseSeconds);
      s.code = smooth(since / kVrCodeRise) * pulse * (1.f - closing);
    }
    if (age >= kVrHandshakeAt && age < kVrHandshakeAt + kVrHandshakeSeconds)
      s.handshake = age - kVrHandshakeAt;
    s.lidOpen = 1.f - closing;
    s.dark = closing;
    return s;
  }
  // Behind closed lids the room is gone; the implant's picture opens out of the dark.
  s.room = false;
  const float open = smooth((age - shut - kVrDarkSeconds) / kVrHorizonSeconds);
  s.horizon = open;
  s.dark = 1.f - open;
  s.lidOpen = open;
  return s;
}

VrState vrLeave(float age, float intensity) noexcept {
  // `horizon` stays open: the space fades to dark rather than closing.
  VrState s;
  s.room = true;
  if (!std::isfinite(age)) return s;
  age = std::max(age, 0.f);
  if (intensity < .5f) {
    if (age >= kVrLeaveCalmSeconds) return s;
    s.done = false;
    const float half = kVrLeaveCalmSeconds * .5f;
    if (age < half) {
      s.room = false;
      s.horizon = 1;
      s.dark = smooth(age / half);
    } else {
      s.dark = 1.f - smooth((age - half) / half);
    }
    return s;
  }
  if (age >= kVrLeaveSeconds) return s;
  s.done = false;
  // The slab is already hanging at the centre, its code shown, behind the dark.
  s.code = 1;
  if (age < kVrLeaveDark) {
    s.room = false;
    s.horizon = 1;
    s.dark = smooth(age / kVrLeaveDark);
    s.lidOpen = 1.f - s.dark;
    return s;
  }
  const float open = smooth((age - kVrLeaveDark) / kVrLidsOpenSeconds);
  s.lidOpen = open;
  s.dark = 1.f - open;
  if (age >= kVrLeaveHandshakeAt && age < kVrLeaveHandshakeAt + kVrHandshakeSeconds)
    s.handshake = age - kVrLeaveHandshakeAt;
  s.code = 1.f - smooth((age - kVrCodeFadeAt) / kVrCodeFadeSeconds);
  return s;
}

float vrBlinkSwitch(bool codeStep, float intensity) noexcept {
  if (intensity < .5f) return .18f;
  return (codeStep ? kVrBlinkCode : 0.f) + kVrBlinkClose;
}

float vrBlinkSeconds(bool codeStep, float intensity) noexcept {
  if (intensity < .5f) return .36f;
  return vrBlinkSwitch(codeStep, intensity) + kVrBlinkDark + kVrBlinkOpen;
}

VrState vrBlink(float age, bool codeStep, float intensity) noexcept {
  VrState s;
  s.room = true;
  s.horizon = 0;
  if (!std::isfinite(age)) return s;
  age = std::max(age, 0.f);
  if (age >= vrBlinkSeconds(codeStep, intensity)) return s;
  s.done = false;
  const float sw = vrBlinkSwitch(codeStep, intensity);
  if (intensity < .5f) {
    s.dark = age < sw ? smooth(age / sw) : 1.f - smooth((age - sw) / sw);
    return s;
  }
  const float c = codeStep ? kVrBlinkCode : 0.f;
  const float closing = smooth((age - c) / kVrBlinkClose);
  if (codeStep) s.code = smooth(age / .15f) * (1.f - closing);
  if (age < sw) {
    s.lidOpen = 1.f - closing;
    return s;
  }
  s.lidOpen = smooth((age - sw - kVrBlinkDark) / kVrBlinkOpen);
  return s;
}

bool neuralCodeModule(int i, int j, int n, float seconds, std::uint32_t seed) noexcept {
  if (i < 0 || j < 0 || i >= n || j >= n) return false;
  // QR finder squares: a lit 7 x 7 ring, a dark gap, a lit 3 x 3 core, a dark margin.
  for (const auto& [ox, oy] :
       {std::pair{0, 0}, std::pair{n - 7, 0}, std::pair{0, n - 7}}) {
    const int x = i - ox, y = j - oy;
    if (x < -1 || y < -1 || x > 7 || y > 7) continue;
    if (x < 0 || y < 0 || x > 6 || y > 6) return false;
    if (x == 0 || x == 6 || y == 0 || y == 6) return true;
    return x >= 2 && x <= 4 && y >= 2 && y <= 4;
  }
  // A Turing-like labyrinth: two domain-warped waves multiplied, plus a slow diagonal
  // drift, thresholded. Features run several modules wide, so the code reads as blobs and
  // stripes rather than noise, and drifts at well under a radian a second.
  const float s = static_cast<float>(seed % 997u) * .37f;
  const float x = static_cast<float>(i) * .32f + s, y = static_cast<float>(j) * .32f;
  const float t = seconds;
  const float v = std::sin(1.9f * x + 1.3f * std::sin(1.1f * y + .23f * t)) *
                      std::sin(1.7f * y + 1.1f * std::sin(1.3f * x - .19f * t)) +
                  .35f * std::sin(.9f * (x + y) + .3f * t);
  return v > 0;
}

Vec3 vrRelayoutPoint(const Vec3& from, const Vec3& to, float age,
                     float intensity) noexcept {
  if (intensity < .5f || !(age < kVrRelayoutSeconds) || length(to - from) < 1e-6f)
    return to;
  const float x = clamp01(age / kVrRelayoutSeconds);
  const float e = rigEase(x);
  Vec3 p = from + (to - from) * e;
  p.z += kVrRelayoutLift * std::sin(3.14159265f * x);
  return p;
}

bool VrSwitch::request(bool next, bool code, double now) noexcept {
  if (next == target) return false;
  target = next;
  codeStep = code;
  started = now;
  return true;
}
bool VrSwitch::advance(double now, float intensity) noexcept {
  if (shown == target ||
      now - started < static_cast<double>(vrBlinkSwitch(codeStep, intensity)))
    return false;
  shown = target;
  return true;
}
bool VrSwitch::active(double now, float intensity) const noexcept {
  const double age = now - started;
  return age >= 0 && age < static_cast<double>(vrBlinkSeconds(codeStep, intensity));
}
void VrSwitch::skip(double now, float intensity) noexcept {
  started = now - static_cast<double>(vrBlinkSeconds(codeStep, intensity)) - .001;
}
std::vector<Placement> vrRelayoutPlacements(const std::vector<Placement>& from,
                                            const std::vector<Placement>& to, float age,
                                            float intensity) {
  std::unordered_map<CellId, Vec3> old;
  for (const auto& p : from) old.emplace(p.cell, Vec3{p.x, p.y, p.z});
  auto out = to;
  for (auto& p : out) {
    const Vec3 end{p.x, p.y, p.z};
    const auto it = old.find(p.cell);
    const Vec3 start = it == old.end() ? end - Vec3{0, 0, 4} : it->second;
    const auto at = vrRelayoutPoint(start, end, age, intensity);
    p.x = at.x;
    p.y = at.y;
    p.z = at.z;
  }
  return out;
}

}  // namespace cb::view
