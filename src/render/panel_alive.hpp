// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace cb::render {

/// Integer hash shared bit-for-bit with `panelHash` in `board.frag`.
[[nodiscard]] std::uint32_t panelHash(std::uint32_t x) noexcept;
/// 24-bit unit float in [0,1), shared with `panelUnit` in `board.frag`.
[[nodiscard]] float panelUnit(std::uint32_t x) noexcept;

/// Glitch slot length in seconds: 120 / 1.5 is whole, so no event straddles the 120 s
/// clock wrap.
inline constexpr float kGlitchSlot = 1.5f;
/// Event duration bounds, seconds (spec: ~80-250 ms).
inline constexpr float kGlitchMinSeconds = 0.08f;
inline constexpr float kGlitchMaxSeconds = 0.25f;
/// Expected glitching cells per slot is `cells / modulus`; the modulus is this many times
/// the LED cell count, i.e. about one glitch per two slots (a few per 10 s).
inline constexpr float kGlitchSparsity = 2.0f;

/// The modulus pushed to the shader for a board of `ledCells` panels.
[[nodiscard]] float glitchModulus(std::uint32_t ledCells) noexcept;

/// One cell glitch: kind 0 row shift, 1 tear, 2 channel offset, 3 dropout.
struct PanelGlitch {
  bool on{false};
  float start{0};     ///< seconds from slot start
  float duration{0};  ///< seconds
  float u{0};         ///< 0..1 through the event (valid when `on`)
  std::uint32_t kind{0};
  std::uint32_t lane{0};
};
/// Mirror of `panelGlitch` in `board.frag`. Pure in its arguments.
[[nodiscard]] PanelGlitch panelGlitch(std::uint32_t seed16, float seconds,
                                      float modulus) noexcept;

/// Per-emitter colour-change timing (living colour change). Seeded by the cell seed and
/// the emitter's grid index; mirrored by `board.frag`.
struct EmitterTransition {
  float delay{0};
  float duration{0};
  [[nodiscard]] float end() const noexcept { return delay + duration; }
};
[[nodiscard]] EmitterTransition emitterTransition(std::uint32_t seed16, std::uint32_t ix,
                                                  std::uint32_t iy) noexcept;
/// No emitter is still moving after this many seconds.
inline constexpr float kTransitionMaxSeconds = 0.60f;
/// 0..1 progress of one emitter at `age` seconds after the change (smoothstepped).
[[nodiscard]] float emitterProgress(const EmitterTransition& t, float age) noexcept;

/// Wandering emitter faults (dead, stuck-bright or slowly blinking single emitters). No
/// emitter is permanently defective: each cell has up to two fault slots, a slot's fault
/// lives one `period` (8-60 s, a divisor of 120 so the clock wrap is continuous), fades
/// in and out, and the next period it is elsewhere (or absent). Pure in `seconds`.
struct EmitterFault {
  bool active{false};
  std::uint32_t ix{0}, iy{0};  ///< emitter grid index within the 24 x 24 cell
  std::uint32_t kind{0};       ///< 0 dead, 1 stuck bright, 2 blinking
  float env{0};                ///< fade in/out, 0..1
  float level{1};              ///< brightness multiplier now, before `env` is applied
};
inline constexpr std::uint32_t kFaultSlots = 2;
[[nodiscard]] EmitterFault emitterFault(std::uint32_t seed16, std::uint32_t slot,
                                        float seconds) noexcept;

}  // namespace cb::render
