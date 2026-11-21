// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/implant_fx.hpp"

#include <algorithm>
#include <cmath>

namespace cb::view {
namespace {
float smooth(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t * t * (3.f - 2.f * t);
}
}  // namespace
float heartbeatRate(float tension) noexcept {
  return std::isfinite(tension) ? std::clamp(kHeartbeatBpm + tension, 60.f, 110.f) : 60.f;
}
void HeartbeatPhase::advance(float tension, double dt) noexcept {
  if (!std::isfinite(dt) || dt <= 0) return;
  const double target = heartbeatRate(tension);
  const double difference = static_cast<double>(bpm) - target;
  const double decay = std::exp(-dt / 3.0);
  cycles += (target * dt + difference * 3.0 * (1.0 - decay)) / 60.0;
  bpm = static_cast<float>(target + difference * decay);
}
float heartbeat(double seconds, float intensity) noexcept {
  if (intensity < .5f) return 1;
  const auto gaussian = [](double phase) {
    phase -= std::floor(phase + .5);
    return std::exp(-phase * phase / .0016);
  };
  const double phase = seconds * static_cast<double>(kHeartbeatBpm) / 60.0;
  return 1.f + kHeartbeatAmplitude *
                   static_cast<float>(
                       gaussian(phase) +
                       .6 * gaussian(phase - static_cast<double>(kHeartbeatSecond *
                                                                 kHeartbeatBpm) /
                                                 60.0));
}
float saccadicDip(float radPerSecond, float intensity) noexcept {
  return intensity < .5f ? 0.f
                         : kSaccadeMaxDip * smooth(radPerSecond / kSaccadeFullSpeed);
}
bool ImplantPanelState::visible() const noexcept {
  return dissolve < 1.f || afterimage > 0.f;
}
ImplantPanelState implantForming(float age, float intensity) noexcept {
  return {smooth(age / (intensity < .5f ? .2f : kImplantFormSeconds)), 0, 0};
}
ImplantPanelState implantLeaving(float age, float intensity) noexcept {
  if (intensity < .5f) return {1, smooth(age / .2f), 0};
  const float after =
      age >= kImplantDissolveSeconds
          ? kImplantAfterimageLevel * (1.f - smooth((age - kImplantDissolveSeconds) /
                                                    kImplantAfterimageSeconds))
          : 0.f;
  return {1, smooth(age / kImplantDissolveSeconds), after};
}
}  // namespace cb::view
