// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "variant/variant.hpp"

namespace cb::view {

/// The board-boot timeline of M23.10 section 3, in seconds since boot start. All pure.
inline constexpr float kBootSeconds = 1.8f;
inline constexpr float kBootCalmSeconds = 0.4f;   ///< calm: one simple fade
inline constexpr float kBootBezelSeconds = 0.25f;
inline constexpr float kBootBackdropSeconds = 0.6f;
inline constexpr float kBootCellOnStart = 0.10f;  ///< cell power-on: start + span * d
inline constexpr float kBootCellOnSpan = 0.70f;
inline constexpr float kBootDarkLevel = 0.08f;    ///< a cell's light before its time
inline constexpr float kBootOvershoot = 1.35f;
inline constexpr float kBootOvershootSeconds = 0.06f;
inline constexpr float kBootFlickerFloor = 0.5f;
inline constexpr float kBootFlickerSeconds = 0.04f;
inline constexpr float kBootPieceStart = 0.55f;   ///< piece reveal: start + span * d
inline constexpr float kBootPieceSpan = 0.75f;
inline constexpr float kBootHeatRamp = 0.15f;
inline constexpr float kBootRevealSeconds = 0.30f;
inline constexpr float kBootHeatEase = 0.15f;

/// What one cell and the piece on it are doing at `bootAge`.
struct BootState {
  float power{1};    ///< cell light multiplier: dark, then the overshoot, then 1
  float flicker{1};  ///< the two short dips after power-on
  float onAge{-1};   ///< seconds since power-on (drives the living transition); < 0 = none
  float reveal{1};   ///< the piece's hologram reveal height; >= 1 = fully shown
  float heat{0};     ///< extra footprint heat ahead of and during the reveal
  float bezel{1};    ///< the lit rim's progress, 0..1
  float backdrop{1}; ///< the room glow's ramp, 0..1
};

/// `d` is the cell's normalised boot order (`bootCellOrder`), `intensity` the shared
/// presentation intensity; below one half the sequence is the calm 0.4 s fade. `seed`
/// varies the flicker timing per cell. At `bootAge >= kBootSeconds` everything is at its
/// normal level whatever the intensity.
[[nodiscard]] BootState bootState(float bootAge, float d, float intensity,
                                  std::uint32_t seed = 0);

/// Normalised boot order of every cell, 0 at the near edge to 1 at the far one. A box
/// board uses rank (layer first, lexicographically, on boards above two axes). A glued
/// board uses king-step graph distance from the first rank, so wraps count as adjacency.
[[nodiscard]] std::vector<float> bootCellOrder(const VariantSpec& v);

}  // namespace cb::view
