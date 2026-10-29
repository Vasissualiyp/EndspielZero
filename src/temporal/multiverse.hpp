// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <optional>
#include <set>
#include <vector>

#include "pieces/atom.hpp"
#include "temporal/timeline.hpp"

namespace cb::temporal {

/// A board's place in the multiverse: a turn on a timeline.
struct BoardKey {
  Turn turn{0};
  LineId line{0};
  friend bool operator==(const BoardKey& a, const BoardKey& b) {
    return a.turn == b.turn && a.line == b.line;
  }
  friend bool operator<(const BoardKey& a, const BoardKey& b) {
    return a.line != b.line ? a.line < b.line : a.turn < b.turn;
  }
};

/// The multiverse's board bookkeeping, from the EndspielZero 5D policy worked out with the
/// project owner. It knows which boards exist, whose move each one is, where the present
/// is, and what a move does to the multiverse. It holds no positions and generates no
/// moves: the game layer copies board state according to the plan this returns.
///
/// The move policy, in the owner's words:
///  - A move always creates a board one turn later on the *source's* timeline, with the
///    moving piece gone from it.
///  - If the destination board has no future board yet (it is the latest on its line),
///    the arrival also advances that line: the destination one turn later gains the
///    piece.
///  - If the destination already has a future (it is in the past), the new state goes on
///    a fresh timeline branching from the destination.
class Multiverse {
 public:
  Multiverse(TemporalPolicy policy, Turn maxTurn, LineId maxLineCoord);

  [[nodiscard]] const TemporalPolicy& policy() const noexcept { return policy_; }
  [[nodiscard]] const TimelineModel& timelines() const noexcept { return lines_; }
  [[nodiscard]] const std::set<BoardKey>& live() const noexcept { return live_; }
  [[nodiscard]] BoardKey origin() const { return BoardKey{0, policy_.originLine}; }
  [[nodiscard]] bool exists(const BoardKey& b) const { return live_.count(b) != 0; }
  [[nodiscard]] bool playable(const BoardKey& b) const;
  [[nodiscard]] Color sideToMove(const BoardKey& b) const;

  /// The present column: the live boards on active timelines at the earliest turn.
  [[nodiscard]] std::vector<BoardKey> present() const;
  /// The player to move: the side of the present boards. White before anything shifts.
  [[nodiscard]] Color mover() const;
  /// Present boards this player has not committed a move on yet.
  [[nodiscard]] std::vector<BoardKey> unansweredPresent() const;

  /// Plan a move from `source` to `destination`. Returns the boards that must be created
  /// (source's future first), or nullopt if the source is not playable or the multiverse
  /// has no room. The caller copies board state into them.
  std::optional<std::vector<BoardKey>> apply(const BoardKey& source,
                                             const BoardKey& destination);

 private:
  void add(const BoardKey& b, Color side);
  [[nodiscard]] BoardKey futureOf(const BoardKey& b) const {
    return BoardKey{static_cast<Turn>(b.turn + 1), b.line};
  }

  Turn maxTurn_{0};
  LineId maxLineCoord_{0};
  TemporalPolicy policy_;
  TimelineModel lines_;
  std::set<BoardKey> live_;
  std::map<BoardKey, Color> side_;
  std::set<BoardKey> answered_;
};

}  // namespace cb::temporal
