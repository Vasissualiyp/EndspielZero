// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cb::view {
inline constexpr float kImplantFormSeconds = .35f;
inline constexpr float kImplantDissolveSeconds = .25f;
inline constexpr float kImplantAfterimageSeconds = .30f;
inline constexpr float kImplantAfterimageLevel = .08f;
inline constexpr float kHeartbeatAmplitude = .02f;
inline constexpr float kHeartbeatBpm = 60.f;
inline constexpr float kHeartbeatSecond = .25f;
inline constexpr float kSaccadeMaxDip = .60f;
inline constexpr float kSaccadeFullSpeed = 3.f;

[[nodiscard]] float heartbeat(double seconds, float intensity) noexcept;
[[nodiscard]] float saccadicDip(float radPerSecond, float intensity) noexcept;

struct ImplantPanelState {
  float form{1};
  float dissolve{0};
  float afterimage{0};
  [[nodiscard]] bool visible() const noexcept;
};
[[nodiscard]] ImplantPanelState implantForming(float age, float intensity) noexcept;
[[nodiscard]] ImplantPanelState implantLeaving(float age, float intensity) noexcept;
}  // namespace cb::view
