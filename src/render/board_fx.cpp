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

void applyBoardFx(InstanceSet& set, const app::Session& session,
                  const BoardFxState& state) {
  set.cellFx.clear();
  set.fxGlobals = view::FxGlobals{};
  set.standbyLed.reset();
  if (!state.any()) return;
  const view::PositionView& pos = session.snapshot();
  const std::uint32_t count = pos.cellCount();
  set.cellFx.assign(static_cast<std::size_t>(count) + 1, view::CellFx{});

  const float fxIntensity = state.ledCells ? state.intensity : 0.0f;
  set.fxGlobals.tint[0] = state.tint[0];
  set.fxGlobals.tint[1] = state.tint[1];
  set.fxGlobals.tint[2] = state.tint[2];

  const auto bootCell = [&](CellId c, float age, float intensity) {
    const view::BootState b =
        view::bootState(age, session.bootOrder()[c], intensity, cellSeed(c));
    view::CellFx& fx = set.cellFx[c + 1];
    fx.power = b.power;
    fx.flicker = b.flicker;
    fx.reveal = pos.at(c).empty() ? 1.0f : b.reveal;
    fx.heat = pos.at(c).empty() ? 0.0f : b.heat;
    // -2: not yet on (the shader keeps it quiet); -1 no transition; otherwise the age.
    fx.onAge = b.onAge < 0.0f && b.power < 0.5f ? -2.0f : b.onAge;
  };
  // Slabs on their own boot clock (M24.6), over whatever the rest of the board does.
  const auto bootSlabs = [&] {
    const float intensity = state.ledCells && !state.shortBoot ? state.intensity : 0.0f;
    const float stretch = state.shortBoot ? view::kBootSeconds / kShortBootSeconds : 1.0f;
    for (CellId c = 0; c < count && c < state.cellBootAge.size(); ++c)
      if (state.cellBootAge[c] >= 0.0f)
        bootCell(c, state.cellBootAge[c] * stretch, intensity);
  };

  if (state.bootAge) {
    const float intensity = state.ledCells ? state.intensity : 0.0f;
    for (CellId c = 0; c < count; ++c) bootCell(c, *state.bootAge, intensity);
    const view::BootState g = view::bootState(*state.bootAge, 0.0f, intensity);
    set.fxGlobals.bezel = g.bezel;
    set.fxGlobals.backdrop = g.backdrop;
    bootSlabs();
    return;  // the boot replaces whatever the board was doing
  }

  using view::EndKind;
  using view::HologramSide;
  const auto sideOf = [&](Piece p) {
    if (state.end.kind == EndKind::Draw) return HologramSide::Draw;
    return p.colorOf() == state.end.loser ? HologramSide::Loser : HologramSide::Winner;
  };

  if (state.endAge && !state.reviewing && state.end.origin != kInvalidCell) {
    const std::vector<int>& dist = session.distancesFrom(state.end.origin);
    const bool decisive = state.end.kind == EndKind::Decisive;
    // The last move's two cells stay readable in the hold, whatever the power.
    CellId lastFrom = kInvalidCell, lastTo = kInvalidCell;
    if (const auto last = session.shownLastMove()) {
      lastFrom = last->first;
      lastTo = last->second;
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
          view::kEndDelaySeconds + (decisive ? static_cast<float>(std::max(dist[c], 0)) /
                                                   view::kPowerDownCellsPerSecond
                                             : 0.0f);
      const view::HologramEnd h = view::hologramEnd(
          sideOf(piece), *state.endAge - arrival, cellSeed(c), fxIntensity);
      fx.opacity = h.opacity;
      fx.scanJitter = h.scanJitter;
      fx.gain = h.gain;
    }

    // A second cache lookup may move the cached vectors, so finish reading dist first.
    const int maxDist = dist.empty() ? 0 : *std::max_element(dist.begin(), dist.end());
    const auto& shutdownDist = decisive ? dist : session.distancesFrom(count / 2);
    std::vector<view::ShutdownPiece> pieces;
    for (CellId c = 0; c < count; ++c) {
      const Piece piece = pos.at(c);
      if (!piece.empty())
        pieces.push_back({piece.colorOf(), pos.variant().pieces[piece.type].royal,
                          shutdownDist[c], c});
    }
    const float firstStart = view::shutdownBegins(state.end.kind, maxDist, fxIntensity);
    const auto starts =
        state.compressedShutdown
            ? std::vector<float>{}
            : view::shutdownStarts(state.end, pieces, firstStart, fxIntensity);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
      const CellId c = pieces[i].cell;
      const float age = state.compressedShutdown ? (state.holdProgress - 0.40f) / 0.55f *
                                                       view::kCollapseSeconds
                                                 : *state.endAge - starts[i];
      const auto h = view::hologramShutdown(age, cellSeed(c), fxIntensity);
      auto& fx = set.cellFx[c + 1];
      fx.opacity *= h.opacity;
      fx.reveal = std::min(fx.reveal, h.reveal);
      fx.scanJitter = std::min(1.0f, fx.scanJitter + h.scanJitter);
      fx.heat = std::max(fx.heat, h.baseHeat);
      // The mated king's cell keeps its light only until its own projector fails.
      if (decisive && state.end.mated && c == state.end.origin && age >= 0.0f) {
        const float x = std::clamp(age / view::kPowerDownFallSeconds, 0.0f, 1.0f);
        fx.power =
            1.0f + (view::holdLevel(state.end.kind) - 1.0f) * x * x * (3.0f - 2.0f * x);
      }
    }
    const float complete =
        starts.empty() ? firstStart : view::shutdownComplete(starts, fxIntensity);
    if (!state.compressedShutdown && !state.powerUpAge && *state.endAge >= complete) {
      set.fxGlobals.standby =
          view::standbyLevel(state.end.kind, *state.endAge, complete, fxIntensity);
      const auto* color = decisive ? state.winnerColor : state.drawColor;
      std::copy_n(color, 3, set.fxGlobals.standbyColor);
      const CellId royal =
          decisive ? session.game().position().findRoyal(opponent(state.end.loser))
                   : kInvalidCell;
      set.fxGlobals.standbyCell = royal == kInvalidCell ? state.end.origin : royal;
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
      const auto restored =
          view::hologramUnshutdown(*state.powerUpAge, cellSeed(c), fxIntensity);
      fx.opacity *= restored.opacity;
      fx.reveal = std::min(fx.reveal, restored.reveal);
      fx.scanJitter = std::min(1.0f, fx.scanJitter + restored.scanJitter);
    }
  }

  if (state.rippleAge && state.rippleOrigin != kInvalidCell) {
    const std::vector<int>& dist = session.distancesFrom(state.rippleOrigin);
    for (CellId c = 0; c < count; ++c) {
      const view::RippleCell r =
          view::checkRipple(dist[c], *state.rippleAge, fxIntensity);
      view::CellFx& fx = set.cellFx[c + 1];
      fx.rippleAmp = r.amp;
      fx.ripplePhase = r.phase;
      fx.tintMix = r.tint;
    }
  }
  bootSlabs();
  if (set.fxGlobals.standbyCell != kInvalidCell)
    set.standbyLed = BoardRenderer::buildStandbyLed(
        set, view::layout(pos.variant().dims, session.viewConfig()));
}

}  // namespace cb::render
