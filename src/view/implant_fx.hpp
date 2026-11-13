// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cb::view {
// BCI brief (M24-bci-look.md) 3.2/3.3/4: lock-in .45 s, let go .22 s, positive persistence
// of 6 % over .35 s, saccadic dip 40 % on the breadcrumb.
inline constexpr float kImplantFormSeconds = .45f;
inline constexpr float kImplantDissolveSeconds = .22f;
inline constexpr float kImplantAfterimageSeconds = .35f;
inline constexpr float kImplantAfterimageLevel = .06f;
inline constexpr float kHeartbeatAmplitude = .02f;
inline constexpr float kHeartbeatBpm = 60.f;
inline constexpr float kHeartbeatSecond = .25f;
inline constexpr float kSaccadeMaxDip = .40f;
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
