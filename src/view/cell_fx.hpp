// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cb::view {

/// One cell's share of a board presentation effect (M23.10): boot, check ripple and the
/// game-end power-down all write the same record, and the board shader reads it through
/// the instance's cell index. Twelve floats, laid out as three vec4s so the C++ struct and
/// the std430 `CellFx` in board.frag agree byte for byte. The default is "no effect".
struct CellFx {
  // vec4 a
  float power{1};         ///< multiplies the cell's light; 1 = normal
  float flicker{1};       ///< further multiplier (boot flicker dips), 1 = none
  float reveal{1};        ///< standing piece's reveal height 0..1, >= 1 = fully shown
  float heat{0};          ///< extra footprint heat, added to the standing one
  // vec4 b
  float rippleAmp{0};     ///< check ripple strength at this cell
  float ripplePhase{0};   ///< signed wavefront phase, -1..1
  float tintMix{0};       ///< tint toward `FxGlobals::tint`, 0..1
  float glitch{0};        ///< forced glitch burst 0..1 (power-down)
  // vec4 c
  float opacity{1};       ///< the standing piece's hologram opacity
  float scanJitter{0};    ///< the standing piece's scan irregularity
  float gain{1};          ///< the standing piece's emission gain
  float onAge{-1};        ///< seconds since the cell powered on; < 0 = no transition
};
static_assert(sizeof(CellFx) == 48);

/// Per-frame values that are not per cell.
struct FxGlobals {
  float bezel{1};      ///< how much of the board's lit rim has drawn on, 0..1
  float backdrop{1};   ///< scales the board's spill into the room, 0..1
  float tint[3]{1.0f, 0.2f, 0.2f};  ///< the colour `CellFx::tintMix` mixes toward
  /// The HUD's panels (M23.10 section 7): edges trace in with the bezel, power-down dims
  /// them. Both 0..1; 1 is normal.
  float hudDraw{1};
  float hudLevel{1};
};

}  // namespace cb::view
