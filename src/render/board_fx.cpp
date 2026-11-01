// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/board_fx.hpp"

#include <algorithm>

#include "app/session.hpp"
#include "view/board_boot.hpp"

namespace cb::render {

namespace {

std::uint32_t cellSeed(std::uint32_t cell) {
  std::uint32_t x = cell * 0x9e3779b1u + 0x7f4a7c15u;
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  return x;
}

}  // namespace

void applyBoardFx(InstanceSet& set, const app::Session& session, const BoardFxState& state) {
  set.cellFx.clear();
  set.fxGlobals = view::FxGlobals{};
  if (!state.any()) return;
  const view::PositionView& pos = session.snapshot();
  const std::uint32_t count = pos.cellCount();
  set.cellFx.assign(static_cast<std::size_t>(count) + 1, view::CellFx{});

  if (state.bootAge) {
    const std::vector<float>& order = session.bootOrder();
    const float intensity = state.ledCells ? state.intensity : 0.0f;
    for (CellId c = 0; c < count; ++c) {
      const view::BootState b =
          view::bootState(*state.bootAge, order[c], intensity, cellSeed(c));
      view::CellFx& fx = set.cellFx[c + 1];
      fx.power = b.power;
      fx.flicker = b.flicker;
      fx.reveal = pos.at(c).empty() ? 1.0f : b.reveal;
      fx.heat = pos.at(c).empty() ? 0.0f : b.heat;
      // -2: not yet on (the shader keeps it quiet); -1 no transition; otherwise the age.
      fx.onAge = b.onAge < 0.0f && b.power < 0.5f ? -2.0f : b.onAge;
    }
    const view::BootState g = view::bootState(*state.bootAge, 0.0f, intensity);
    set.fxGlobals.bezel = g.bezel;
    set.fxGlobals.backdrop = g.backdrop;
  }
}

}  // namespace cb::render
