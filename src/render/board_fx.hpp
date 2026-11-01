// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "render/board_renderer.hpp"

namespace cb::app {
class Session;
}

namespace cb::render {

/// What the board's presentation effects (M23.10) are doing this frame. Everything is an
/// age in seconds, so the same state renders the same picture.
struct BoardFxState {
  /// Seconds into the boot sequence, if one is running.
  std::optional<float> bootAge;
  /// The shared presentation intensity; below one half every effect is its calm form.
  float intensity{1.0f};
  /// Whether the cells are LED panels. Other materials take the calm form of every effect.
  bool ledCells{true};

  [[nodiscard]] bool any() const noexcept { return bootAge.has_value(); }
};

/// Write the effects into `set.cellFx` and `set.fxGlobals`. Pure: the same session,
/// state and set give the same bytes. A no-op (leaving the set neutral) when nothing runs.
void applyBoardFx(InstanceSet& set, const app::Session& session, const BoardFxState& state);

}  // namespace cb::render
