// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "position/move.hpp"
#include "position/position.hpp"
#include "variant/variant.hpp"
#include "view/layout.hpp"
#include "view/seams.hpp"
#include "view/theme.hpp"

namespace cb::view {

/// What happened on one step of a move.
enum class StepKind : std::uint8_t {
  Interior,  ///< an ordinary step to the neighbouring cell
  Portal,    ///< the step left through a gluing and arrived somewhere else entirely
  Bounce,    ///< the step hit a reflecting wall and turned around
};

struct PathStep {
  CellId from{kInvalidCell};
  CellId to{kInvalidCell};
  StepKind kind{StepKind::Interior};
  /// The direction *after* the step. On a Klein seam this is not the direction the
  /// piece left with, which is exactly why a piece re-entering a twisted board appears
  /// to come out sideways - and why the animation has to be told rather than guess.
  Direction dir{};
  /// When the step crossed a boundary, the face it went through. The portal is oriented
  /// by this and not by the travel direction: a diagonal move can cross the file edge
  /// while its direction leans along the rank, and the portal must face the edge.
  std::uint8_t faceAxis{0};
  Side faceSide{Side::Max};
};

/// How a piece actually travelled from one cell to another.
///
/// Reconstructed from the variant's own atoms, not from the difference between the two
/// cells: on a glued board the difference means nothing, and the question "did it pass
/// through the cells in between" is the whole difference between a rook and a knight.
struct MovePath {
  CellId from{kInvalidCell};
  CellId to{kInvalidCell};
  /// True when the piece arrived without traversing what lies between - a leap or a
  /// hop. Drawn as an arc over the board rather than as a line across it.
  bool leap{false};
  /// True when nothing in the variant explains the move, which is what a castle or a
  /// rule-effect displacement looks like from here. Drawn as a plain glide.
  bool unexplained{false};
  /// The direction the piece set off in. Needed to know which wall it leaves through
  /// on the very first step.
  Direction startDir{};
  std::vector<PathStep> steps;
  /// The square a piece was lifted from, or `kInvalidCell` when the move takes nothing.
  /// This is `Move::captureCell`, not `Move::to`: they are the same square for every
  /// ordinary capture and differ for exactly one move, en passant.
  CellId captureCell{kInvalidCell};
  /// The piece standing on `captureCell` before the move, empty if nothing was taken.
  /// The position has already moved on by the time the renderer draws, so the flourish
  /// needs its own copy of what left.
  Piece captured{};
};

/// How much of a captured piece is still on the board, as a function of the move's own
/// progress: 1 while the capturing piece is still on its way, falling smoothly to 0 by
/// the time it arrives. The captured piece is lifted off the board, not deleted between
/// frames. Pure in `progress`, so a `--clip` pinned at a `t` reproduces.
[[nodiscard]] float captureFade(float progress) noexcept;

/// The bright pulse at the captured square: 0 until the pieces close, a peak of 1 as they
/// meet, back to 0 as the move lands. Same pure-in-`progress` contract as `captureFade`,
/// and the same smoothstep shape `shotEnvelope` uses for the camera.
[[nodiscard]] float captureFlash(float progress) noexcept;

/// Projector handoff (M23.9 D). A hologram is light projected by a square, so a move
/// hands the piece from panel to panel instead of gliding between them. Pure in
/// `progress`.
///
/// `copies` are the partial projections to draw this frame, one per cell on the route
/// (`from`, each step's `to`; a leap or an unexplained move is just `from` and `to`):
/// `opacity` 0..1, `scan` 0 (solid figure) to 1 (scan lines only) - only the destination
/// resolves to a solid figure. `heat` is the projector base's warm-up for cells on the
/// route, ahead of the leading copy and cooling with an afterglow behind it.
struct HandoffCopy {
  CellId cell{kInvalidCell};
  float opacity{0};
  float scan{0};
  bool destination{false};
};
struct HandoffHeat {
  CellId cell{kInvalidCell};
  float heat{0};
};
struct HandoffState {
  std::vector<HandoffCopy> copies;
  std::vector<HandoffHeat> heat;
};
/// The fraction of the move spent handing off; the rest is the destination resolving.
inline constexpr float kHandoffSpan = 0.90f;
/// A new head's copy rises to full opacity within this much progress (and under a quarter
/// of a hop).
inline constexpr float kHandoffMaxRise = 0.05f;
/// Level a copy drops to once the head has moved on, and its scan dominance there.
inline constexpr float kHandoffGhostOpacity = 0.40f;
inline constexpr float kHandoffGhostScan = 0.75f;
/// A standing piece's faint lit base, as heat. Far below any move cue.
inline constexpr float kFootprintHeat = 0.22f;
/// Total move duration cap, seconds; per-cell time shrinks on long moves.
inline constexpr float kMaxMoveSeconds = 1.5f;
/// An LED capture is a colour change on the captured cell's own emitters, not a slab
/// drawn over the board: they switch to the capture colour while the heads meet and
/// settle back. `held` = switching to the capture colour (age since contact), else
/// settling back to the cell's own colour (age since release). Pure in `progress`.
struct CapturePulse {
  bool active{false};
  bool held{false};
  float age{0};  ///< seconds the per-emitter transition has been running
};
inline constexpr float kCaptureContact = 0.55f;
inline constexpr float kCaptureRelease = 0.72f;
/// The transition clock runs faster than the move's, so it settles before the move ends.
inline constexpr float kCapturePulseSpeed = 1.6f;
[[nodiscard]] CapturePulse capturePulse(float progress, float durationSeconds) noexcept;

/// `intensity` is the shared presentation intensity: at 0 (calm) there is no trail, only
/// the plain `from -> to` crossfade.
[[nodiscard]] HandoffState handoffState(const MovePath& path, float progress,
                                        float intensity = 1.0f);

/// Trace the route a move took. `type` and `side` are the piece that moved, read from
/// the position *before* the move is applied - which is also the position `pos` must be,
/// because a sliding ray cannot pass through a piece, and the route a player watched is
/// the clear one, not whichever ray the atom table happens to list first. `side` picks
/// the forward directions, so for a flipped piece it is `Piece::orientation()`.
MovePath tracePath(const VariantSpec& v, const Position& pos, PieceTypeId type,
                   Color side, const Move& m);

/// A move being drawn: a polyline in world space, broken wherever the piece went
/// through a seam, plus the portals that open when it does.
class MoveAnimation {
 public:
  /// One straight run. A portal ends a run and the next one starts on the far side.
  struct Run {
    std::vector<float> x, y, z;
  };
  /// A portal, with how far open it is this frame. Both ends of a crossing open at
  /// once: the point of the animation is that you see where the piece is going to
  /// come out at the moment it leaves.
  struct Portal {
    float x{0}, y{0}, z{0};
    /// Which way the portal faces, in world space. The iris is drawn edge-on to this,
    /// so a portal on the file seam stands across the file rather than along it.
    float nx{0}, ny{0}, nz{0};
    Rgba color{};
    float openAt{0};     ///< time it is fully open, in seconds from the start
    float intensity{0};  ///< 0..1, filled in by openPortals()
  };

  /// Sampled state for one frame.
  struct Sample {
    float x{0}, y{0}, z{0};
    /// Extra height, so a leap arcs over what it is leaping and a portal exit rises
    /// out of the floor rather than sliding out of a wall.
    float lift{0};
    bool moving{false};
  };

  /// Begin drawing `m`, whose route is `path`. `placements` supplies world positions
  /// and `seams` the colour of any portal on the way, so the portal that opens is the
  /// same colour as the edge the piece left through.
  void start(const ViewConfig& cfg, const std::vector<Placement>& placements,
             const SeamMap& seams, const Theme& theme, const MovePath& path,
             float secondsPerCell);
  void clear();

  void advance(float dt);
  /// Put the animation at an exact point and hold it there, for a capture. A screenshot
  /// or a clip frame has to state its `t` rather than integrate towards one, or it is a
  /// different picture on a faster machine. Does nothing while inactive.
  void setProgress(float t) noexcept;
  [[nodiscard]] bool active() const noexcept { return active_; }
  /// How far through the move, 0 to 1. The clock both the animation and the move camera
  /// read, so they cannot disagree about where the piece is.
  [[nodiscard]] float progress() const noexcept {
    return duration_ > 0.0f && elapsed_ < duration_ ? elapsed_ / duration_ : 1.0f;
  }
  /// The full length of the running move in seconds, 0 when nothing is animating. The
  /// shape-follow choreography (M17.19) needs it to budget its Travel stage before the
  /// move has finished.
  [[nodiscard]] float duration() const noexcept { return active_ ? duration_ : 0.0f; }
  [[nodiscard]] CellId travellingTo() const noexcept { return to_; }
  /// True when the running move took something and the flourish has a piece to draw.
  [[nodiscard]] bool captures() const noexcept {
    return captureCell_ != kInvalidCell && !captured_.empty();
  }
  /// The square the captured piece stood on (`captureCell`, not `to`).
  [[nodiscard]] CellId captureCell() const noexcept { return captureCell_; }
  /// The piece the move took, as it stood before the move.
  [[nodiscard]] Piece capturedPiece() const noexcept { return captured_; }
  /// The route this animation was last started with. Needed by anything that samples a
  /// *different* placement of the same move - the geometry view's surface sampler, which
  /// cannot reconstruct the route itself (M17.15).
  [[nodiscard]] const MovePath& path() const noexcept { return path_; }
  [[nodiscard]] Sample sample() const;
  /// Portals with any intensity left this frame, for the renderer to draw.
  [[nodiscard]] std::vector<Portal> openPortals() const;

 private:
  bool active_{false};
  bool leap_{false};
  float elapsed_{0};
  float duration_{0.0001f};
  CellId to_{kInvalidCell};
  /// The capture the running move carries, kept so the renderer can draw its flourish
  /// after the position has already moved on (M18.1).
  CellId captureCell_{kInvalidCell};
  Piece captured_{};
  MovePath path_;
  std::vector<Run> runs_;
  std::vector<float> runStart_;  ///< normalised time each run begins at
  std::vector<float> runEnd_;
  std::vector<Portal> portals_;
};

}  // namespace cb::view
