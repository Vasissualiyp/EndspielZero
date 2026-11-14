// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cb::view {
/// M24.7: waking up. Starting the app is the player opening their eyes in the lab, after
/// which the implant comes online. Everything below is a pure function of the launch age.
inline constexpr float kWakeSeconds = 5.0f;
/// Intensity below this takes the calm form: one opening, no blinks, no handshake.
inline constexpr float kWakeCalmBelow = .5f;

struct WakeState {
  float lidOpen{1};        ///< 0 closed .. 1 fully open (both lids)
  float topEdge{0};        ///< fraction of screen height covered by the upper lid at centre
  float bottomEdge{0};     ///< fraction covered by the lower lid at centre
  float curve{0};          ///< how much the lid edges bow (corners open last), 0..1
  float feather{0.04f};    ///< edge softness, fraction of screen height
  float blurFraction{0};   ///< defocus radius as a fraction of screen height
  float exposureEv{0};     ///< 0 normal, negative darker
  float saturation{1};
  float lidGlow{0};        ///< warm glow through closed lids, 0..0.03
  float roomUp{1};         ///< 0..1: projector power-on progress
  float handshake{-1};     ///< seconds into the handshake, or -1 when it is not running
  float menuFade{1};       ///< 0..1: how visible the implant layer's menu is (calm cross-fade)
  bool done{true};
};
[[nodiscard]] WakeState wakeState(float age, float intensity) noexcept;
}  // namespace cb::view
