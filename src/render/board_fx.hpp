// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "render/board_renderer.hpp"
#include "view/game_end_fx.hpp"

namespace cb::app {
class Session;
}

namespace cb::render {

/// What the board's presentation effects (M23.10) are doing this frame. Everything is an
/// age in seconds, so the same state renders the same picture.
struct BoardFxState {
  /// Seconds into the boot sequence, if one is running.
  std::optional<float> bootAge;
  /// The check ripple (section 4): seconds since the check, and the king it runs from.
  std::optional<float> rippleAge;
  CellId rippleOrigin{kInvalidCell};
  /// The finished game (section 5): `endAge` seconds since it ended, or `powerUpAge` since
  /// an undo took it back; `end` says how it ended and who lost.
  std::optional<float> endAge;
  std::optional<float> powerUpAge;
  view::EndFx end;
  /// The check colour the ripple tints toward (the theme's `blood`).
  float tint[3]{0.83f, 0.28f, 0.23f};
  /// The shared presentation intensity; below one half every effect is its calm form.
  float intensity{1.0f};
  /// Whether the cells are LED panels. Other materials take the calm form of every effect.
  bool ledCells{true};

  [[nodiscard]] bool any() const noexcept {
    return bootAge || rippleAge || endAge || powerUpAge;
  }
};

/// Write the effects into `set.cellFx` and `set.fxGlobals`. Pure: the same session,
/// state and set give the same bytes. A no-op (leaving the set neutral) when nothing runs.
void applyBoardFx(InstanceSet& set, const app::Session& session, const BoardFxState& state);

}  // namespace cb::render
