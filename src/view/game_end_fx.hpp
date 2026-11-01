// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "pieces/atom.hpp"
#include "space/dims.hpp"

namespace cb::view {

/// The game-end power-down of M23.10 section 5, and the power-up when an undo takes the
/// game back out of it. All pure in an event age in seconds.
inline constexpr float kDecisiveHold = 0.30f;      ///< panel light after a decisive end
inline constexpr float kDrawHold = 0.55f;          ///< ... and after a draw
inline constexpr float kPowerDownCellsPerSecond = 9.0f;
inline constexpr float kPowerDownGlitchSeconds = 0.25f;
inline constexpr float kPowerDownFallSeconds = 0.25f;  ///< decisive: fall to the hold
inline constexpr float kDrawDimSeconds = 0.8f;
inline constexpr float kCalmFadeSeconds = 0.6f;
inline constexpr float kLoserSeconds = 1.0f;       ///< the loser's holograms destabilise
inline constexpr float kLoserOpacity = 0.65f;
inline constexpr float kDrawOpacity = 0.85f;
inline constexpr float kWinnerGain = 1.15f;
inline constexpr float kPowerUpSeconds = 0.5f;
inline constexpr float kPowerUpOvershoot = 1.15f;

enum class EndKind : std::uint8_t { Decisive, Draw };

/// A finished game as the presentation sees it. `origin` is where the wave starts: the
/// losing royal's cell, or the loser's last-moved piece without one. `mated`: the origin
/// holds a checkmated king, which keeps its full light.
struct EndFx {
  EndKind kind{EndKind::Decisive};
  Color loser{Color::White};
  CellId origin{kInvalidCell};
  bool mated{false};
};

/// A cell's light and forced glitch. `power` multiplies the cell's emission; `glitch`
/// is 0..1 (the tear/offset looks of M23.9, driven rather than random).
struct PowerCell {
  float power{1};
  float glitch{0};
};

/// The hold level of an end of this kind.
[[nodiscard]] float holdLevel(EndKind kind) noexcept;

/// `dist` is the cell's distance from the origin (< 0 treated as at the origin), `age`
/// seconds since the end. Decisive: 1 until the front arrives at `dist / 9`, a glitch burst
/// 1 -> 0 over 0.25 s, then a fall to the hold level, never below it. Draw: the whole
/// board dims to the hold over 0.8 s with one unison flicker, no glitch. Calm (intensity
/// below one half): a 0.6 s fade to the hold, no glitch.
[[nodiscard]] PowerCell powerDown(EndKind kind, int dist, float age, float intensity);

/// The cell's light when an undo takes the game out of the end: from the hold level,
/// one overshoot to 1.15, then 1; plus a short staggered glitch (`seed` per cell) that is
/// the cell coming back to life. Calm: a plain 0.5 s rise.
[[nodiscard]] PowerCell powerUp(EndKind kind, float age, float intensity,
                                std::uint32_t seed);

enum class HologramSide : std::uint8_t { Winner, Loser, Draw };

/// A standing piece's hologram once the game has ended.
struct HologramEnd {
  float opacity{1};
  float scanJitter{0};
  float gain{1};
};

/// `age` is seconds since the front reached the piece. Loser: over 1.0 s the scan jitters,
/// one or two dropouts (opacity 0.3 for 50..80 ms) and it settles at 0.65 with a slow
/// 0.3 Hz shimmer. Winner: solid, +15 % emission. Draw: both armies settle at 0.85. Calm:
/// a 0.6 s fade to the same levels with no jitter, dropouts or shimmer.
[[nodiscard]] HologramEnd hologramEnd(HologramSide side, float age, std::uint32_t seed,
                                      float intensity);

/// The same piece as an undo restabilises it: back to a solid, ungained hologram over 0.5 s.
[[nodiscard]] HologramEnd hologramRestore(HologramSide side, float age);

}  // namespace cb::view
