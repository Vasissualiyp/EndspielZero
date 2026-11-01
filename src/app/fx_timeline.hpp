// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

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

 private:
  std::uint64_t sessionId_{0};
  std::uint64_t bootSeen_{0};
  std::optional<double> bootStart_;
};

}  // namespace cb::app
