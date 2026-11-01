// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string>

#include "base/result.hpp"

namespace cb::app {

/// How long a watched game holds a settled move before the next one begins, in seconds.
/// One number for both the interactive "Watch a game" action (M12.8) and the `--dwell`
/// default of the notation runner, so the two cannot drift.
inline constexpr float kDefaultDwellSeconds = 0.6f;

/// Everything the player can change, and where it is kept between sessions.
///
/// Deliberately a plain struct of values with sane defaults: a settings file that fails
/// to parse, or comes from a newer build, must never stop the game starting. Unknown
/// keys are ignored and missing ones keep their default.
struct Settings {
  // ---- display ------------------------------------------------------------
  /// Interface scale. Fonts are rebuilt at the new size rather than stretched, so text
  /// stays crisp - which is the whole point of the option on a high-density display.
  float guiScale{1.0f};
  bool fullscreen{false};
  /// Developer-only presentation: fps and unfinished menu entries. Console access only.
  bool devOverlay{false};
  /// Frame pacing. The game redraws on input, so this only bounds the idle loop.
  bool vsync{true};
  /// Cap the redraw rate, in frames per second. 0 means "as fast as vsync allows". A cap
  /// matters when vsync is off and the screen is otherwise static: the field and the
  /// overtures animate forever, so without one the loop would spin a core.
  int frameCap{0};

  // ---- board --------------------------------------------------------------
  bool showLegalMoves{true};
  /// Holding Ctrl over a piece shows its moves (either side; the opponent's ignore
  /// check).
  bool peekMovesOnCtrl{true};
  /// Holding Ctrl over an empty square, or a highlighted destination, shows what attacks
  /// and defends it.
  bool showAttackHints{true};
  bool showLastMove{true};
  bool showCheck{true};
  /// Draw cyan edges where the board is glued to itself. Off only for a player who
  /// already knows the geometry by heart.
  bool showSeams{true};
  /// Nothing closer to the camera than this many cells is drawn: stops polygons
  /// flickering across the lens when the camera is inside a shape.
  float nearClip{0.5f};
  /// The near clip while the trailer plays (the larger of this and `nearClip`). The 3-D
  /// torus beat puts the camera inside its nested shells, a few cells from the target,
  /// where half a cell clears nothing; 2 keeps the geometry hugging the lens out of
  /// frame.
  float trailerNearClip{2.0f};
  /// Draw the corner legend that names what the cyan seam colours mean (M14.4).
  bool showSeamLegend{true};
  bool showCoordinates{true};
  /// On a board with more than two spatial axes, flip the square colours on every odd
  /// layer (the 2nd, 4th, ...), so a layer's colours alternate with its neighbours' like
  /// a 3-D chequerboard instead of repeating.
  bool swapLayerColors{false};
  /// How much taller a valuable piece stands. 0 makes every piece the same height.
  float pieceHeightScale{1.0f};
  /// Draw the board flat and straight down, with pieces as lettered tokens. Cheaper to
  /// draw than the models, and on a slow machine that is the difference between a game
  /// and a slideshow.
  bool flatView{false};
  /// Draw the play board as the shape its geometry describes - a cylinder, a torus, a
  /// Moebius band - instead of a square diagram (M17). A presentation choice, so it never
  /// enters `VariantId`; offered only for a glued 2-D variant, which is the only geometry
  /// with a surface to become.
  bool geometryView{false};
  /// Where the board sits on its own surface, in cells. Sliding one along the files puts
  /// a1 where b1 was; keep going and the board comes all the way round, which on a
  /// Moebius band takes two laps and arrives mirrored. It is the one way to feel a gluing
  /// rather than be told about it.
  float geometrySlideU{0.0f};
  float geometrySlideV{0.0f};
  /// The `hyper4` tesseract's own fourth-dimension pose: two accumulated rotation angles
  /// in radians, fed to `SurfacePose::hyperSpinU`/`hyperSpinV`, driven by the middle-drag
  /// (U by the horizontal motion, V by the vertical) where a glued board slides. U starts
  /// at the library's fixed file/aeon spin `0.9` and V at `0`, so the opening, undragged
  /// picture is exactly the pre-M17.12.B one. Persisted with the slide, both being the
  /// player's chosen pose; ignored by every variant but `hyper4`.
  float geometryHyperSpinU{0.9f};
  float geometryHyperSpinV{0.0f};
  /// How far the geometry view has been turned through itself, 0 to 1: a torus pulled
  /// inside out through its own hole, a cylinder rolled back over itself like a sock.
  ///
  /// All three are a pose of the same board - the cells, the moves and the position are
  /// untouched - and each is a pure function of its number, so a capture reproduces.
  float geometryEvert{0.0f};
  /// The INVERT button's *target*: true asks the front end to ease `geometryEvert` to 1,
  /// false back to 0. The ramp lives in the front end, not in the pose, so a pose stays a
  /// pure function of its number and a still stays reproducible (M17.7).
  bool geometryInvert{false};
  /// The live, eased amount of the shape, 0 (flat board) to 1 (fully formed), fed to
  /// `SurfacePose::formed` (M18.6). The SHAPE toggle is the target: the front end eases
  /// this toward 0 or 1 with the same ramp `geometryEvert` uses, so the pose stays a pure
  /// function of the number and `--shot --formed` reproduces. Transient like the align
  /// offset: not written to disk, reset from `geometryView` when the shell is built.
  float geometryFormed{1.0f};
  /// How fast `geometryFormed` eases toward its target: the SHAPE toggle's flat<->shape
  /// roll divides by this, so 1 is the designed half-second pace, smaller is slower and
  /// more legible, larger a quicker cut. The same kind of control as `shapeMorphSpeed`,
  /// for the board's own morph rather than the shape-follow choreography's. Clamped to
  /// 0.25..4 so a zero cannot freeze the board mid-roll (M18.6).
  float geometryFormSpeed{1.0f};
  /// The trailer's own multipliers on the shape morph and on every piece's move animation
  /// (1 = as the other settings say; the morph defaults to 0.5, calmer), so tuning the
  /// trailer leaves the player's own game alone. Clamped to [0.25, 4].
  float trailerMorphSpeed{0.5f};
  float trailerMoveSpeed{1.0f};
  /// The live, eased spread of a D >= 3 nested-shell shape's own levels, 0 (every level
  /// on one ring) to 1 (the shipped, fully nested shells), fed to
  /// `SurfacePose::shellSpread` (M13.9). Eases toward its own target the same way
  /// `geometryFormed` does, at the same `geometryFormSpeed` pace. Transient like
  /// `geometryFormed`: not written to disk.
  float geometryShellSpread{1.0f};
  /// How opaque the geometry view's board mesh is, 1 fully opaque down to `kGhostMin` as
  /// a ghost, so the far side of the shape and the pieces on it show through. The pieces
  /// stay opaque; the point is to see them (M17.10).
  float geometryGhost{1.0f};
  /// Draw the shape view's pieces as icon-faced pucks instead of the sculptural meshes
  /// (M17.25). The tangent-field walk must put one unavoidable flip on a non-orientable
  /// shape, and a sculptural piece across that line faces the wrong way; a flat icon
  /// reads as the piece it is from either side. The 2D/3D button switches this while
  /// SHAPE is on. Only meaningful in the geometry view, which is the only place the
  /// button offers it.
  bool shapePieceCylinders{false};
  /// How many half-turns the Klein bottle's ring twists over one lap: 1 is the shipped
  /// surface and reverses the file coordinate (a Klein bottle); an even count does not
  /// reverse it (an orientable double twist). The figure-eight cross-section only closes
  /// on whole half-turns, so other twist counts would leave the join open.
  float kleinTwist{1.0f};
  /// The ring or loop radius of a glued surface as a multiple of the shipped one, its
  /// cross-section left unchanged: a larger value opens a tight torus or Klein bottle, or
  /// lengthens the Moebius loop, without thickening the material. 1 is the shipped shape.
  float geometryWidth{1.0f};
  /// The cross-section size of a glued surface as a multiple of the shipped one - the
  /// tube's thickness, its outer-to-inner radius - leaving the ring radius alone. A
  /// legibility cheat like `geometryWidth`: the cells stretch around a fatter tube.
  float geometryThickness{1.0f};
  /// For the Klein bottle: how many board squares the cylinder is rotated about its own
  /// axis before it is collapsed into the figure-eight (one square = 360/nx degrees).
  /// This moves which file lands where on the figure-eight, to line the squares up at the
  /// seam. Signed, so a counter-clockwise square can be dialled in directly rather than
  /// winding the long way round (e.g. -1 instead of +7 on an 8-wide board) - a debug
  /// tuning knob, not a shipped control.
  float kleinShift{0.0f};
  /// Turn the ring so the piece a followed move travels on presents its outer face to the
  /// camera, rather than the camera clipping through the tube wall to see it (M17.17).
  /// **On by default**: the clipping is a defect, not a look (M17.16 revision).
  bool geometryAlign{true};
  /// When following a move, hold the piece upright - its surface normal up the screen -
  /// rather than letting it tilt and flip with the shape. The tilt is the other camera
  /// mode and has its own charm, so this is a stated choice (M17.16 revision).
  bool followUpright{true};
  /// How far the chase camera looks down on the followed piece, in degrees above the
  /// surface's tangent plane: 0 is directly behind, 90 straight overhead.
  float followElevationDeg{30.0f};
  /// How fast the shape-follow choreography moves (M17.19): the align, approach and
  /// return stages and the per-cell morph all divide by this. 1 is the designed pace;
  /// smaller is slower and more legible, larger is a quicker cut.
  float shapeMorphSpeed{1.0f};
  /// How a move is followed on a shape: "chase" is the third-person camera that sits
  /// behind the piece along its route - the shipped follow. "turntable" instead keeps the
  /// player's camera angle and turns the **board** (the slide a middle-drag gives). A
  /// name rather than an enum because this layer is below the renderer where the modes
  /// live.
  std::string shapeFollow{"chase"};
  /// The align feature's own eased slide offsets, in cells, added on top of the player's
  /// manual `geometrySlideU`/`geometrySlideV`. Transient: not written to disk, so a
  /// reload starts aligned to nothing.
  float geometryAlignOffset{0.0f};
  float geometryAlignOffsetV{0.0f};
  /// Which flat-piece icon set to draw: "faceted" or "primitive". A name rather than an
  /// enum because this layer is below the renderer, where the sets are defined, and a
  /// settings file should stay readable anyway.
  std::string pieceIcons{"faceted"};
  /// Which palette to draw with: "manifold" (the shipped look) or "console". A name for
  /// the same reason as `pieceIcons` - this layer is below the renderer, and a settings
  /// file should stay readable.
  std::string theme{"console"};
  /// Slide pieces along the route they actually took, through portals and off walls.
  bool animateMoves{true};
  /// Multiplier on how fast that happens. Higher is faster.
  float animationSpeed{1.0f};
  /// Multiplier on how fast the library screen's overtures play: their forward and
  /// reverse sweeps and their dwells. Higher is faster. Independent of `animationSpeed`,
  /// which is about moves during a game, not the New Game screen.
  float overtureSpeed{1.0f};

  // ---- camera -------------------------------------------------------------
  float orbitSensitivity{1.0f};
  float zoomSensitivity{1.0f};
  bool invertOrbitY{false};
  /// How the camera follows a move: "off", "piece" or "route". Off is the default and the
  /// pre-M11 behaviour. A name rather than an enum because this layer is below the view.
  std::string cameraMode{"off"};
  /// How strongly the move camera leads the player's own orbit, 0 to 1.
  float followStrength{0.6f};

  // ---- gameplay -----------------------------------------------------------
  /// Ask before playing a move rather than playing it on the second click.
  bool confirmMoves{false};
  /// Two players share one keyboard, one half each: player one types squares with
  /// qwertasdfgzxcvb, player two with the other half.
  bool hotSeat{false};
  /// Play against the computer (M10.2-M10.3): it takes Black, or White when
  /// `computerPlaysWhite` is set. Off is a game between people at one screen.
  bool vsComputer{false};
  bool computerPlaysWhite{false};
  /// How hard it plays, 1 (Easy) to 5 (Expert): ai::optionsForLevel. 3 is Normal.
  int computerLevel{3};
  /// Piece name to promote to without asking; empty means always ask.
  std::string autoPromoteTo;
  std::string lastVariant{"standard"};
  /// Seconds a watched bundled game (M12.8) holds each settled move before the next
  /// begins. The same concept `--dwell` names for a clip; `kDefaultDwellSeconds` is the
  /// shared default.
  float watchDwell{kDefaultDwellSeconds};
  /// Seconds the trailer shows a move's piece and legal squares before playing it; 0
  /// turns the preview off. Clamped to [0, 0.5].
  float trailerMovePreview{0.3f};
  /// The first-run welcome screen has been seen (M14.1). Persisted, so the sequence plays
  /// once.
  bool seenWelcome{false};

  // ---- audio --------------------------------------------------------------
  // Sound effects are generated in code at startup and played through SDL3 (M18.5);
  // `volumeMaster * volumeEffects` scales every one. There is no music in this build yet,
  // so `volumeMusic` is stored and read by the settings screen but multiplies nothing -
  // wiring a loop in later means feeding it to the mixer as one more factor.
  float volumeMaster{0.8f};
  float volumeMusic{0.6f};
  float volumeEffects{0.9f};

  /// Where settings live: /endspielzero/settings.conf, or the platform's
  /// equivalent.
  static std::filesystem::path defaultPath();

  /// Read a settings file. A missing file is not an error - it means "defaults".
  static Settings load(const std::filesystem::path& path);
  Result<void> save(const std::filesystem::path& path) const;

  /// Clamp anything out of range. Called after loading, so a hand-edited file cannot
  /// produce an unusable interface.
  void sanitize();
};

}  // namespace cb::app
