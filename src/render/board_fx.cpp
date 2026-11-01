// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/board_fx.hpp"

#include <algorithm>

#include "app/session.hpp"
#include "view/board_boot.hpp"
#include "view/check_ripple.hpp"

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

  const float fxIntensity = state.ledCells ? state.intensity : 0.0f;
  set.fxGlobals.tint[0] = state.tint[0];
  set.fxGlobals.tint[1] = state.tint[1];
  set.fxGlobals.tint[2] = state.tint[2];

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
    return;  // the boot replaces whatever the board was doing
  }

  using view::EndKind;
  using view::HologramSide;
  const auto sideOf = [&](Piece p) {
    if (state.end.kind == EndKind::Draw) return HologramSide::Draw;
    return p.colorOf() == state.end.loser ? HologramSide::Loser : HologramSide::Winner;
  };

  if (state.endAge && state.end.origin != kInvalidCell) {
    const std::vector<int>& dist = session.distancesFrom(state.end.origin);
    const bool decisive = state.end.kind == EndKind::Decisive;
    // The last move's two cells stay readable in the hold, whatever the power.
    CellId lastFrom = kInvalidCell, lastTo = kInvalidCell;
    if (!session.game().moveHistory().empty()) {
      lastFrom = session.game().moveHistory().back().from;
      lastTo = session.game().moveHistory().back().to;
    }
    for (CellId c = 0; c < count; ++c) {
      view::CellFx& fx = set.cellFx[c + 1];
      const view::PowerCell p =
          view::powerDown(state.end.kind, dist[c], *state.endAge, fxIntensity);
      fx.power = p.power;
      fx.glitch = p.glitch;
      if (c == lastFrom || c == lastTo) fx.power = std::max(fx.power, 0.6f);
      if (decisive && state.end.mated && c == state.end.origin) fx.power = 1.0f;
      const Piece piece = pos.at(c);
      if (piece.empty()) continue;
      const float arrival =
          decisive ? static_cast<float>(std::max(dist[c], 0)) / view::kPowerDownCellsPerSecond
                   : 0.0f;
      const view::HologramEnd h =
          view::hologramEnd(sideOf(piece), *state.endAge - arrival, cellSeed(c), fxIntensity);
      fx.opacity = h.opacity;
      fx.scanJitter = h.scanJitter;
      fx.gain = h.gain;
    }
  }

  if (state.powerUpAge) {
    for (CellId c = 0; c < count; ++c) {
      view::CellFx& fx = set.cellFx[c + 1];
      const view::PowerCell p =
          view::powerUp(state.end.kind, *state.powerUpAge, fxIntensity, cellSeed(c));
      fx.power = p.power;
      fx.glitch = p.glitch;
      const Piece piece = pos.at(c);
      if (piece.empty()) continue;
      const view::HologramEnd h = view::hologramRestore(sideOf(piece), *state.powerUpAge);
      fx.opacity = h.opacity;
      fx.gain = h.gain;
    }
  }

  if (state.rippleAge && state.rippleOrigin != kInvalidCell) {
    const std::vector<int>& dist = session.distancesFrom(state.rippleOrigin);
    for (CellId c = 0; c < count; ++c) {
      const view::RippleCell r = view::checkRipple(dist[c], *state.rippleAge, fxIntensity);
      view::CellFx& fx = set.cellFx[c + 1];
      fx.rippleAmp = r.amp;
      fx.ripplePhase = r.phase;
      fx.tintMix = r.tint;
    }
  }
}

}  // namespace cb::render
