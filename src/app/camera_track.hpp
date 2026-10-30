// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "base/result.hpp"
#include "view/camera.hpp"

namespace cb::app {

/// Which point in the game a camera keyframe is pinned to: a variant (one beat of an
/// attract sequence, or a watched game's own variant) and a ply count into it. Two cues
/// order by variant then ply, the same way an `AttractSequence`'s own beats already do -
/// a cue does not need to know where its variant sits in a sequence, only that cues
/// within the same variant order by ply.
struct CameraCue {
  std::string variant;
  std::size_t ply{0};
  /// Which beat of the attract trailer this is, counted from 1; 0 for an ordinary game.
  /// The trailer plays `klein` twice and replays `standard` every lap, so (variant, ply)
  /// alone is ambiguous inside it - and a keyframe authored for the trailer must not
  /// fire in a game the player started themselves (M13.9 revision).
  std::size_t beat{0};
  /// How far through the move after `ply` the keyframe sits, in eighths of a move (0..7).
  /// A whole-move cue is 0. Quarter-move keyframes use 0, 2, 4, 6; eighths use them all -
  /// a camera that has to circle something needs several keyframes within one move.
  std::size_t sub{0};
};

[[nodiscard]] inline bool operator==(const CameraCue& a, const CameraCue& b) noexcept {
  return a.ply == b.ply && a.beat == b.beat && a.sub == b.sub && a.variant == b.variant;
}

/// One saved camera pose and shape pose, and how fast the view eases into it (M13.9 Part
/// E: the camera-keyframe authoring scaffold). The morph itself is fixed machinery - an
/// orbit-camera slerp for the camera (`view::slerpCamera`), the same exponential ease
/// `geometryFormed` already uses for the pose scalars - a keyframe only says *when*
/// (`cue`), *where* (`camera` and the pose fields) and *how fast* (`morphSpeed`). The
/// scaffold's whole point: the morph is coded once, here; where to put a keyframe and
/// what it looks like is the author's call, made by orbiting the camera and sliding the
/// shape by hand and then recording what the view already shows.
struct CameraKeyframe {
  CameraCue cue;
  view::OrbitCamera camera;
  /// `Settings::geometryFormed` - 0 the flat board, 1 fully formed.
  float formed{1.0f};
  /// `Settings::geometryGhost` - 1 opaque, down to the ghost minimum.
  float ghost{1.0f};
  /// `Settings::geometryShellSpread` - 1 the shipped nested-shell spacing, 0 collapsed.
  float shellSpread{1.0f};
  /// `Settings::geometrySlideU`/`geometrySlideV` - the "middle mouse button morph" that
  /// slides the board round the shape.
  float slideU{0.0f};
  float slideV{0.0f};
  /// `Settings::shapePieceCylinders` - flat icon pucks instead of sculptural pieces.
  bool flatPieces{false};
  /// `Settings::geometryInvert` - the shape turned inside out (the INVERT button).
  bool invert{false};
  /// How fast the view eases toward this keyframe once play reaches its cue, in the same
  /// units as `Settings::geometryFormSpeed` (`view::advanceFormMorph`'s own rate - bigger
  /// is quicker). Adjustable per keyframe, per the M13.9 ask: the morph's speed is one of
  /// the things an author gets to tune, not just its endpoints.
  float morphSpeed{2.0f};
};

/// An ordered set of keyframes, at most one per cue. Not a timeline sampled at an
/// arbitrary point in between - a keyframe is a *waypoint* the live view eases toward
/// once play reaches its own cue (see `Shell::advanceCameraTrack`). That is what lets the
/// player keep orbiting freely everywhere else without a keyframe forcing an answer for
/// every frame between two waypoints: a track can be as sparse as one keyframe per beat,
/// or as dense as the author wants.
class CameraTrack {
 public:
  /// Insert, or replace in place, the keyframe at `kf.cue`. Kept sorted by cue (variant,
  /// then ply) so `keyframes()` and the text form are stable and diffable.
  void set(CameraKeyframe kf);
  /// Remove the keyframe at `cue`, if any. Returns whether one was removed.
  bool erase(const CameraCue& cue);
  [[nodiscard]] const CameraKeyframe* find(const CameraCue& cue) const noexcept;
  [[nodiscard]] const std::vector<CameraKeyframe>& keyframes() const noexcept {
    return keyframes_;
  }
  [[nodiscard]] bool empty() const noexcept { return keyframes_.empty(); }
  void clear() noexcept { keyframes_.clear(); }

 private:
  std::vector<CameraKeyframe> keyframes_;
};

/// Unwrap a keyframe slide channel for interpolation: each value becomes its equivalent
/// (view::nearestLap with that key's own lap) nearest the previous *unwrapped* value, so
/// consecutive keys never sweep a spurious lap. A key whose lap is 0 (its variant does not
/// slide on that axis) holds the previous unwrapped value; the first key keeps its own.
[[nodiscard]] std::vector<float> unwrapSlides(const std::vector<float>& values,
                                              const std::vector<float>& laps);

/// Serialize a track as text, one keyframe per line - the same line-oriented, hand-
/// editable spirit as a `GameFile` (`io/game_file.hpp`).
[[nodiscard]] std::string cameraTrackText(const CameraTrack& track);
/// Parse a track written by `cameraTrackText`.
[[nodiscard]] Result<CameraTrack> parseCameraTrack(std::string_view text);

}  // namespace cb::app
