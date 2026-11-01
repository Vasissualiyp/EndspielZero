// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cb::view {

/// The check ripple of M23.10 section 4: one interference wave from the checked king,
/// travelling by board adjacency (`cellDistances`), so on a torus it wraps through the
/// seams. All pure in an event age.
inline constexpr float kRippleCellsPerSecond = 14.0f;
inline constexpr float kRippleSeconds = 0.9f;
inline constexpr float kRippleSigma = 0.06f;       ///< width of the wavefront in time
inline constexpr float kRippleFalloff = 0.35f;     ///< amplitude is 1 / (1 + falloff * dist)
inline constexpr float kRippleTintMax = 0.35f;     ///< tint at the king's own cell

/// What the wave does to one cell. `amp` is 0..1 (the Gaussian envelope over distance
/// falloff), `phase` the signed position within the wavefront, -1 before the peak to +1
/// after it: the shader dips the light first and lets it recover, and sweeps the emitter
/// rows across the cell with it. `tint` is the `CellFx::tintMix`.
struct RippleCell {
  float amp{0};
  float phase{0};
  float tint{0};
};

/// `dist` is the cell's king-step distance from the king (< 0 = unreachable), `age` the
/// seconds since the check, `intensity` the shared presentation intensity. Calm (below one
/// half) has no ripple: the check cue alone says "check". Zero before the front arrives
/// and from `kRippleSeconds` on.
[[nodiscard]] RippleCell checkRipple(int dist, float age, float intensity);

}  // namespace cb::view
