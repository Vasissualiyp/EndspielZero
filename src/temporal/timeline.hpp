// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace cb::temporal {

/// A timeline index along the multiverse axis.
using LineId = std::int16_t;

/// A half-turn index along the time axis. Board 0 is the starting position; every move
/// creates a board one half-turn to its right.
using Turn = std::int16_t;

/// Who a timeline belongs to. The original timeline belongs to nobody in particular.
enum class BranchOwner : std::uint8_t { Original, White, Black };

/// The rules that make one time-travel variant different from another, as data.
///
/// None of these are universal. Which way a player's timelines grow, how far a new
/// timeline advances relative to the board it branches from, and whether a forward step
/// stays on the mover's own future are all EndspielZero choices a variant may make
/// differently - so they are parameters, not constants (ADR-0007 and the M6 plan's
/// "configuration, not code" rule).
struct TemporalPolicy {
  /// Timeline coordinate of the original timeline. Branches grow away from it, so the
  /// line axis must have room on both sides for both players.
  LineId originLine{0};
  /// Which way each player's branches run. White down (negative), Black up (positive).
  int whiteSign{-1};
  int blackSign{1};
  /// A branch from a board at (t, l) lands at (t + branchAdvance, k).
  Turn branchAdvance{1};
};

/// One board in the multiverse: a full position at a point in time on a timeline.
struct BoardRef {
  Turn turn{0};
  LineId line{0};
};

/// One timeline: the boards that exist on it, who created it, and when.
struct Timeline {
  LineId id{0};
  BranchOwner owner{BranchOwner::Original};
  /// 1-based rank among its owner's branches; 0 for the original timeline. This is the
  /// number the activity rule counts.
  int order{0};
  /// The board this timeline was born from: the line the branching move arrived on, and
  /// the turn on it that it landed on. -1 for the original timeline, which came from
  /// nowhere. Kept so the shape of the multiverse can be drawn, not just computed.
  LineId parentLine{0};
  Turn parentTurn{-1};
  std::vector<Turn> boards;  // ascending; a board exists at each

  [[nodiscard]] Turn latest() const noexcept {
    return boards.empty() ? static_cast<Turn>(-1) : boards.back();
  }
  [[nodiscard]] bool hasBoard(Turn t) const;
};

/// The multiverse's history: which boards exist, on which timeline, and who created each
/// timeline. It is enough to apply the published activity and present rules, and it is
/// deliberately just data - no positions, no moves.
///
///  - A timeline is *playable* if the board on it is the latest on that timeline.
///  - The original timeline is *active*. The nth timeline a player created is active iff
///    the opponent has created at least n-1 timelines.
///  - The *present line* aligns with the active board furthest left along the time axis;
///    every board in that column (same turn) is in the present.
///
/// Implemented from the reference game's written rules, not yet validated against a
/// transcribed corpus (M6.2). The direction and advance of branches come from the policy.
class TimelineModel {
 public:
  explicit TimelineModel(TemporalPolicy policy = {});

  [[nodiscard]] const TemporalPolicy& policy() const noexcept { return policy_; }
  [[nodiscard]] LineId originLine() const noexcept { return policy_.originLine; }

  [[nodiscard]] const std::vector<Timeline>& timelines() const noexcept { return lines_; }
  [[nodiscard]] const Timeline* find(LineId line) const;

  /// Record that a board exists at (turn, line). A duplicate is refused.
  bool addBoard(LineId line, Turn turn);

  /// Create a timeline for `owner`, branched from board (parentTurn, parentLine).
  /// Returns the new line, or nullopt for the original.
  std::optional<LineId> branch(BranchOwner owner, LineId parentLine, Turn parentTurn);

  /// The line `owner`'s next branch would take, without creating it.
  [[nodiscard]] LineId nextId(BranchOwner owner) const;
  /// The sign this owner's branches run in.
  [[nodiscard]] int signOf(BranchOwner owner) const noexcept;

  /// The nth timeline a player created is active iff the opponent created at least n-1.
  [[nodiscard]] bool active(LineId line) const;

  /// The active board furthest left along the time axis sets the present turn.
  [[nodiscard]] Turn presentTurn() const;

  /// Every board in the present column, across the active timelines.
  [[nodiscard]] std::vector<BoardRef> boardsInPresent() const;

 private:
  [[nodiscard]] Timeline* findMutable(LineId line);

  TemporalPolicy policy_;
  std::vector<Timeline> lines_;
  int whiteBranches_{0};
  int blackBranches_{0};
};

}  // namespace cb::temporal
