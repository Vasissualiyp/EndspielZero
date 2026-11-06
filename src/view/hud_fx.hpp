// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cb::view {

/// Live HUD feedback (M23.11 section 4). Every function is pure in an event age in
/// seconds. Below half intensity (the calm form) the state changes at once: no fade, no
/// flicker, no pulse.
inline constexpr float kTurnFlipSeconds = 0.25f;
inline constexpr float kTurnFlickerStart = 0.08f;   ///< the LED goes dark here...
inline constexpr float kTurnFlickerEnd = 0.14f;     ///< ...and comes back here
inline constexpr float kThinkDotSeconds = 0.4f;     ///< one dot's pulse period
inline constexpr float kThinkStaggerSeconds = 0.13f;
inline constexpr float kCheckSpikeSeconds = 0.3f;
inline constexpr float kRowRevealSeconds = 0.2f;
inline constexpr float kRowShiftSeconds = 0.15f;

struct TurnFlip {
  float mix;  ///< 0 shows the side that moved, 1 the side to move
  float led;  ///< LED brightness: 1 except for the one flicker at the switch
};
[[nodiscard]] TurnFlip turnFlip(float age, float intensity) noexcept;
/// Brightness 0..1 of thinking dot `dot` (0..2) after `age` seconds of thinking.
[[nodiscard]] float thinkingDot(float age, int dot, float intensity) noexcept;
/// Extra brightness 0..1 of the CHECK text, from the ripple's start.
[[nodiscard]] float checkSpike(float age, float intensity) noexcept;
struct LedgerRowFx {
  float reveal;  ///< the new row's text shown left to right, 0..1
  float shift;   ///< the fraction of a row the older rows still have to rise, 1..0
};
[[nodiscard]] LedgerRowFx ledgerRowFx(float age, float intensity) noexcept;

}  // namespace cb::view
