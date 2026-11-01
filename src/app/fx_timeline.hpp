// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "view/game_end_fx.hpp"

namespace cb::app {

class Session;

/// Stamps a session's presentation events (M23.10) with presentation time. The session
/// only counts what happened; this is owned by the front end that has a clock, and every
/// answer it gives is a pure function of the times it was told. Captures that never call
/// `observe` see no event at all, which is the settled board.
class FxTimeline {
 public:
  /// Call once per frame. `eligible`: the session is on the game screen and not in the
  /// editor. `swallow`: events happening now are not to play at all (the trailer has its
  /// own transitions). An event that arrives while neither holds waits for `eligible`.
  void observe(const Session* session, double now, bool eligible, bool swallow);

  /// Seconds into the boot, or nullopt when there is none running.
  [[nodiscard]] std::optional<float> bootAge(double now) const;
  [[nodiscard]] bool booting(double now) const { return bootAge(now).has_value(); }
  /// The first click, key or engine move completes the boot at once.
  void skipBoot() noexcept { bootStart_.reset(); }

  /// The check ripple (M23.10 section 4): seconds since the check, and where it started.
  /// Nullopt before it and once it has died out.
  struct Ripple {
    float age;
    CellId origin;
  };
  [[nodiscard]] std::optional<Ripple> ripple(double now) const;
  /// The game-end hold (section 5): a finished game stays powered down for as long as it
  /// is decided, so this has no end. `age` counts from the end of the game.
  struct End {
    float age;
    view::EndFx fx;
  };
  [[nodiscard]] std::optional<End> end(double now) const;
  /// The power-up after an undo out of a finished game, for `kPowerUpSeconds`.
  [[nodiscard]] std::optional<End> powerUp(double now) const;
  /// Whether any of the above is running (what the frame loop keeps redrawing for).
  [[nodiscard]] bool animating(double now) const;

 private:
  std::uint64_t sessionId_{0};
  std::uint64_t bootSeen_{0};
  std::optional<double> bootStart_;
  std::uint64_t checkSeen_{0};
  std::uint64_t endSeen_{0};
  std::uint64_t powerUpSeen_{0};
  std::optional<double> rippleStart_;
  CellId rippleOrigin_{0};
  std::optional<double> endStart_;
  std::optional<view::EndFx> endFx_;
  std::optional<double> powerUpStart_;
  std::optional<view::EndFx> powerUpFx_;
};

}  // namespace cb::app
