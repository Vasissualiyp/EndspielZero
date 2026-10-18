// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/camera_track.hpp"
#include "app/editor.hpp"
#include "app/overture.hpp"
#include "app/session.hpp"
#include "app/settings.hpp"
#include "view/morph.hpp"
#include "view/trailer_camera.hpp"

namespace cb::app {

/// Which screen the player is looking at.
enum class Screen : std::uint8_t {
  Welcome,  ///< the tutorial: what this is, and where to start (M14.1)
  MainMenu,
  NewGame,  ///< choosing a variant
  Watch,    ///< choosing a bundled game to watch (M12.8)
  Game,
  Paused,
  Settings,
  Editor,            ///< board and piece designers, not built yet
  GameInfo,          ///< what the loaded variant's rules actually are
  PieceMoves,        ///< how each piece in the loaded variant moves
  QuitConfirm,       ///< the "are you sure?" step before the window closes
  PauseQuitConfirm,  ///< the same question, asked from the pause menu
};

/// One entry of the curated "start here" path (M14.2): a variant and one line saying what
/// changes there.
struct CuratedVariant {
  std::string name;
  std::string what;
};

/// A bundled showcase game the "Watch a game" action (M12.8) can play. The name is the
/// file stem shown in the picker; the file is already parsed, so starting one needs no
/// filesystem access and a test can supply its own.
struct WatchGame {
  std::string name;
  GameFile file;
};

/// One geometry beat of the main-menu attract sequence (M13.9): a variant, a short legal
/// game played on it, and where the flat/shape and ghost poses rest for the beat. The
/// moves are in the engine's long-algebraic notation; the engine proves them legal when
/// the beat starts, so a bad line is a load failure rather than a silent misplay.
struct AttractStep {
  std::string variant;
  std::vector<std::string> moves;
  float formedTarget{1.0f};
  float ghostTarget{1.0f};
  /// The `geometryFormed` a beat starts from, when it is meant to reveal. Negative (the
  /// default) means "leave the pose wherever the previous beat left it", so every beat
  /// but a reveal is already formed. The cylinder beat uses this to begin part-way up the
  /// roll rather than at the rank-mirrored flat end (M13.9, the M18.6 mirror workaround).
  float formedFrom{-1.0f};
  /// Draw this beat's pieces as the flat icon pucks (`shapePieceCylinders`) rather than
  /// the ordinary sculptures. A discrete style snap, not something to ease: there is no
  /// continuous half-icon/half-sculpture state. Set on the shapes whose far side a
  /// sculptural piece cannot be read through (M13.9 revision, Part H).
  bool flatPieces{false};
  float spreadTarget{1.0f};
  /// The `geometryShellSpread` a beat starts from, same "-1 means inherit" convention as
  /// `formedFrom`. Only the 3-D torus beat sets this (to 0 - every level on one ring,
  /// matching the plain torus the Klein beat has just folded back into), so its own
  /// levels visibly spread apart into the nested shells as the beat plays (M13.9
  /// revision: "klein morphs to torus, then the torus copies itself into shells").
  float spreadFrom{-1.0f};
  /// Carry the previous beat's own end-of-game position onto this beat's board (M13.9
  /// Part F), through `embedOnto`, instead of starting this beat's `moves` from the
  /// variant's own declared start. `moves` then has to be a legal continuation of the
  /// *embedded* position, not of the variant's own opening - verified the same way every
  /// other beat is, by loading it. False (the default) starts this beat fresh, which is
  /// still the right choice whenever the previous beat's board cannot carry the pieces
  /// this one needs (Klein and the 3-D torus field no bishop or queen at their own start
  /// for exactly this reason - see `defaultAttractSequence`'s own comment).
  bool continuesPosition{false};
  /// Seconds each move is held once it has landed; negative (the default) means the
  /// player's `Settings::watchDwell`. The long opener plays briskly (M13.9 revision).
  float dwell{-1.0f};
  /// Multiplier on this beat's own shape morph (on top of the trailer's morph speed), for
  /// a transition that should take longer - the torus bending closed - than another.
  float morphSpeed{1.0f};
};
using AttractSequence = std::vector<AttractStep>;

/// The shipped trailer sequence (M13.9 revision): the standard board, then cylinder ->
/// torus -> Klein -> (Klein folds back to a torus) -> the 3-D torus, looping the shape
/// beats among themselves. Free so a capture or a test can name it. Every cut between
/// shapes is a real, continuous geometry morph, not a hard swap: cylinder's own end
/// state is bit-for-bit the torus morph's open-tube waypoint, the torus's own end state
/// is bit-for-bit the Klein morph's ring-formed waypoint, and the Klein beat ends by
/// easing back down to that same waypoint before the cut into the 3-D torus, which
/// itself opens with every level collapsed onto that one ring and spreads them apart
/// over the beat (`shellSpread`). Only the very first beat (`standard`) has nothing to
/// morph from.
[[nodiscard]] AttractSequence defaultAttractSequence();

/// How the trailer's camera behaves (M13.9 revision). `Auto` is the calm default: no
/// following, a barely perceptible sway. `Follow` is the old chase behind the moving
/// piece. `Authored` holds still except where a recorded keyframe eases it.
enum class TrailerCamera : std::uint8_t { Auto, Follow, Authored };

/// A point on the trailer timeline: a beat, a move in it, and eighths through that move.
struct TrailerCue {
  std::size_t beat{0};
  std::size_t ply{0};
  std::size_t sub{0};
};

/// The pose scalars the trailer's edit mode owns while the trailer is paused: where the
/// SHAPE / GHOST / SPREAD buttons put them, and what a keyframe then records.
struct TrailerPose {
  float formed{1.0f};
  float ghost{1.0f};
  float shellSpread{1.0f};
};

/// The curated first-run path, standard first, exotic last. A front door, not a gate -
/// the full library stays available.
[[nodiscard]] const std::vector<CuratedVariant>& curatedVariants();

/// One term a first-time player may not know, and what it means (M14.4). The definitions
/// are shipped rather than written per screen, so two screens cannot disagree about what
/// a torus is, and acceptance fact 3 - "every visible curated-set term is defined" - is
/// something a test can check rather than a hope.
struct GlossaryEntry {
  std::string_view term;
  std::string_view definition;
};

/// The glossary, longest term first so a scan can match "Möbius band" before "band".
[[nodiscard]] const std::vector<GlossaryEntry>& glossary();
/// The definition of `term`, or empty when this game does not define it.
[[nodiscard]] std::string_view glossaryFor(std::string_view term);

std::string_view screenName(Screen s) noexcept;

/// How far in from the front door a screen is - the rung the shell's ladder lights.
int screenDepth(Screen s) noexcept;

/// The application around the game: menus, settings, and the game itself.
///
/// Screen state and settings live here rather than in the interface, so the transitions
/// a player can make - and the ones they cannot, like resuming with no game loaded -
/// are testable without a window.
class Shell {
 public:
  static std::unique_ptr<Shell> create(std::vector<std::string> library,
                                       std::filesystem::path settingsPath = {});

  [[nodiscard]] Screen screen() const noexcept { return screen_; }
  [[nodiscard]] Settings& settings() noexcept { return settings_; }
  [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
  [[nodiscard]] const std::vector<std::string>& library() const noexcept {
    return library_;
  }
  [[nodiscard]] const std::string& currentVariant() const noexcept {
    return currentVariant_;
  }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] bool quitRequested() const noexcept { return quit_; }

  /// True before the tutorial has ever been dismissed. The shell no longer opens on it
  /// (the Tutorial row does), so this only records whether the player has seen it.
  [[nodiscard]] bool firstRun() const noexcept { return !settings_.seenWelcome; }
  /// Leave the tutorial: remember it was seen and open the main menu (M14.1).
  void dismissWelcome();
  /// A variant for "Surprise me", rotating through the library so a run of picks does not
  /// repeat. The caller captions it with `variantDescription` (M14.2).
  [[nodiscard]] std::string surpriseVariant();

  /// The running game, or nullptr on a screen that has none.
  [[nodiscard]] Session* session() noexcept { return session_.get(); }
  [[nodiscard]] const Session* session() const noexcept { return session_.get(); }
  [[nodiscard]] bool hasGame() const noexcept { return session_ != nullptr; }

  /// True while the board should be drawn behind whatever screen is showing - the pause
  /// and reference screens sit over the game rather than replacing it.
  [[nodiscard]] bool showsBoard() const noexcept;

  /// True while the red "in check" vignette should be drawn: the side to move is in
  /// check *and* the game is the thing on screen. A position left behind on the main
  /// menu is not a warning about the screen the player is actually looking at.
  [[nodiscard]] bool showsCheckWarning() const noexcept;

  /// How far the board should be pulled back for the screen it is on: 0 on the board
  /// itself, 1 at pause, 2 one step further in at a pause panel. Stepping away from the
  /// game recedes it; the shell reports the target and the main loop animates towards it.
  [[nodiscard]] float boardPullBack() const noexcept;

  void go(Screen s);
  void back();
  void requestQuit() { quit_ = true; }

  /// Start a game. The loader is injected so the shell stays testable without files.
  using VariantLoader = std::function<Result<VariantSpec>(const std::string&)>;
  /// Install the loader and give the library its final order: difficulty first, then
  /// name. The order can only be computed here, because a difficulty lives in the
  /// variant's own file rather than in the name list the shell is handed at startup.
  void setVariantLoader(VariantLoader loader);
  /// The loader the attract trailer uses: the variants as they ship, whatever the player has
  /// saved over them. The trailer is a fixed showcase - its scripted moves are only known to
  /// be legal on the shipped rules - so a variant edited in the designer must not break it.
  void setShippedVariantLoader(VariantLoader loader) { shippedLoader_ = std::move(loader); }
  Result<void> startGame(const std::string& variantName);

  /// Saved games live in a `games` folder beside the settings file.
  [[nodiscard]] std::filesystem::path gamesDir() const;
  /// Save the running game as `name` (a `.cbgame` file in `gamesDir`).
  Result<void> saveGame(const std::string& name);
  /// Load the saved game `name`, starting its variant first when it is not the current
  /// one.
  Result<void> loadGame(const std::string& name);
  /// The names of the saved games, sorted, for a menu to list.
  [[nodiscard]] std::vector<std::string> savedGames() const;

  /// Install the bundled showcase games (M12.8). Injected like the variant loader so the
  /// shell stays testable without a filesystem; the front end reads `games/` at startup.
  void setBundledGames(std::vector<WatchGame> games) { bundled_ = std::move(games); }
  [[nodiscard]] const std::vector<WatchGame>& bundledGames() const noexcept {
    return bundled_;
  }
  /// Watch the bundled game `name` (M12.8): start its variant, load the file and hand it
  /// to the session to play. Fails, with a message, when the name is unknown or the file
  /// will not load.
  Result<void> startWatching(const std::string& name);
  /// Leave the watching mode and return to the main menu. The stop control and the
  /// escape key both land here.
  void stopWatching();
  [[nodiscard]] bool watching() const noexcept;
  /// Drive the watched game's clock for one interactive frame. Ends the watch and returns
  /// to the main menu when the file runs out.
  void advanceWatching(float dt);

  /// Begin the attract sequence (M13.9): a looping run of short games, each on a
  /// different shape, played on the ordinary game screen like a watched game. Does
  /// nothing when the sequence is empty or its first variant will not load.
  void startAttract(AttractSequence sequence);
  /// Leave attract mode: stop the running game, restore the opaque board so a game
  /// started afterwards does not inherit the trailer's ghosted pose, and return to the
  /// main menu.
  void stopAttract();
  [[nodiscard]] bool attracting() const noexcept { return !attract_.empty(); }
  /// Which beat of the sequence is playing.
  [[nodiscard]] std::size_t attractIndex() const noexcept { return attractIndex_; }
  /// Which beat of the sequence is playing, for a status line or a test.
  [[nodiscard]] std::size_t attractStep() const noexcept { return attractIndex_; }
  [[nodiscard]] std::size_t attractCount() const noexcept { return attract_.size(); }
  /// Drive one interactive frame of the attract sequence. Returns true when this tick
  /// started a new beat - the first step, or a cut to the next variant - so the front end
  /// can push renderer options and re-frame the camera for the shape now loaded.
  bool advanceAttract(float dt);

  // ---- the trailer's own camera (M13.9 revision) -----------------------------------
  /// The camera mode. Applied to each beat's session; never written to `Settings`.
  [[nodiscard]] TrailerCamera trailerCamera() const noexcept { return trailerCamera_; }
  void setTrailerCamera(TrailerCamera mode);
  /// Edit mode: the trailer's clock stops, the camera becomes `Authored`, and the
  /// stepping / pose controls apply. Leaving it resumes play where it stood.
  [[nodiscard]] bool trailerEditing() const noexcept { return trailerEditing_; }
  void setTrailerEditing(bool on);
  /// Pause: the trailer's clock, the game and the cut stand still, with none of the edit
  /// controls. Edit mode pauses it too. Seeking on the timeline still works while paused.
  [[nodiscard]] bool trailerPaused() const noexcept { return trailerPaused_; }
  void setTrailerPaused(bool paused) noexcept { trailerPaused_ = paused; }
  /// The pose the edit-mode buttons are driving.
  /// Viewer overrides of a beat's own look, for the buttons shown while the trailer plays
  /// (INVERT is `Settings::geometryInvert` and needs none). They hold across every beat
  /// until the trailer stops. A keyframe's pose still wins in the authored camera.
  void setTrailerGhost(float ghost) noexcept {
    trailerGhost_ = ghost;
    editPose_.ghost = ghost;
  }
  void setTrailerFlatPieces(bool flat) noexcept {
    trailerFlatPieces_ = flat;
    settings_.shapePieceCylinders = flat;
  }
  [[nodiscard]] TrailerPose& editPose() noexcept { return editPose_; }
  [[nodiscard]] const TrailerPose& editPose() const noexcept { return editPose_; }
  /// The easing speed a newly recorded keyframe gets.
  [[nodiscard]] float trailerKeySpeed() const noexcept { return trailerKeySpeed_; }
  void setTrailerKeySpeed(float s) noexcept;
  /// `base` with the trailer's resting sway applied (a pure function of the trailer
  /// clock); `base` unchanged outside the trailer or when the sway has faded out.
  [[nodiscard]] view::OrbitCamera trailerView(const view::OrbitCamera& base) const;
  /// Edit mode only: move one ply within the beat (animated forwards, a jump back).
  /// False when not editing or already at that end.
  bool trailerStep(int delta);
  /// Edit mode only: replay the chain from the first beat to `beat`, `ply` moves in.
  void trailerGoto(std::size_t beat, std::size_t ply);
  /// Edit mode only: jump `delta` beats (wrapping), landing at the start of that beat.
  void trailerJumpBeat(int delta);
  /// The same jump, deferred to the start of the next `advanceAttract`. A jump replaces
  /// the session, and the interface keeps a reference to it for the rest of its frame -
  /// so the interface must ask, never jump.
  void requestTrailerJumpBeat(int delta);
  /// Start the trailer over from its first beat (deferred a frame, like a jump). Keeps
  /// the camera mode and edit mode, drops the viewer's ghost/piece overrides.
  void requestTrailerRestart();
  /// Seek to the next (`dir` > 0) or previous keyframe on the timeline, deferred like any
  /// seek. False when there is none that way.
  bool requestTrailerSeekKey(int dir);
  /// Space: pause or resume. In edit mode it leaves edit mode and plays.
  void toggleTrailerPause();
  /// The author reached for the camera (orbit, zoom, slide). While the trailer would take
  /// the camera back - keyframes, or its own framing - that means they want to work on
  /// it, so the trailer pauses into edit mode instead of fighting the hand. The chase
  /// camera and anything outside the trailer are left alone.
  void trailerUserTookCamera();
  /// How finely the edit-mode step buttons divide a move: 1 (whole moves), 2, 4 or 8.
  /// Anything else is refused.
  [[nodiscard]] int trailerGrid() const noexcept { return trailerGrid_; }
  void setTrailerGrid(int grid) noexcept;
  /// Where in the current move edit mode sits, in eighths (0..7).
  [[nodiscard]] std::size_t trailerEditSub() const noexcept { return editSub_; }
  /// Edit mode only: step one grid unit forward or back through the trailer, crossing
  /// into the next or previous move when it runs off the end of this one. A keyframe
  /// recorded there lands on that fractional point, and the camera previews the spline's
  /// value.
  bool trailerStepFraction(int dir);
  /// The trailer's position in moves: whole beats laid end to end, plus the move, plus
  /// how far through it. The clock keyframes and the camera spline share.
  [[nodiscard]] float trailerTime() const;
  /// The trailer's whole length in the same units (every beat's moves + 1, end to end).
  [[nodiscard]] float trailerLength() const;
  /// Where beat `beat` starts on the timeline.
  [[nodiscard]] float beatOffset(std::size_t beat) const;
  /// The beat, move and fraction at timeline position `g`, snapped to the current grid
  /// and clamped to the trailer. `grid` 0 means the grid in use; otherwise 1, 2, 4 or 8.
  [[nodiscard]] TrailerCue trailerCueAt(float g, int grid = 0) const;
  /// The timeline positions of the trailer's keyframes, in order - what a timeline draws.
  [[nodiscard]] std::vector<float> trailerKeyTimes() const;
  /// The variant and move count of beat `beat`, for labelling the timeline.
  [[nodiscard]] const AttractStep& attractBeat(std::size_t beat) const {
    return attract_[beat];
  }
  /// Edit mode only: go to a point on the timeline. Deferred to the next `advanceAttract`
  /// (it may replace the session the interface is holding); a request outside edit mode
  /// is dropped. Within the current beat only the move and fraction change.
  void requestTrailerSeek(std::size_t beat, std::size_t ply, std::size_t sub);
  /// Re-apply the keyframe spline's value at the edit position - after a cut re-framed
  /// the camera - so what edit mode shows is what the keyframes say. A no-op without
  /// keyframes.
  void refreshTrailerView();
  /// True while the trailer's camera belongs to the author: in edit mode, or in the
  /// authored mode once there are keyframes for it to follow. Nothing else may move it
  /// then.
  [[nodiscard]] bool trailerOwnsCamera() const noexcept;
  /// True while the front end should keep the shape framed as it changes: the trailer is
  /// running, the camera is not the author's and not a chase. It eases the view's target
  /// and distance toward the shape's bounds every frame, so nothing jumps at a cut.
  [[nodiscard]] bool trailerAutoFrames() const noexcept {
    return attracting() && !trailerOwnsCamera() &&
           trailerCamera_ != TrailerCamera::Follow;
  }
  /// Whether any keyframe belongs to a beat of the running trailer.
  [[nodiscard]] bool trailerHasKeys() const noexcept;

  /// A variant's advertised difficulty, for the library's order and its colours.
  /// Resolved through the loader once and remembered; a name that will not load is
  /// `Other` rather than an error, because the picker must still be able to draw it.
  [[nodiscard]] Difficulty difficultyOf(const std::string& name) const;

  /// The one-line description a variant's own file gives it, or empty when it cannot be
  /// loaded. The picker has only names to work from, so this is how it shows an author's
  /// pitch before the game itself is started.
  [[nodiscard]] std::string variantDescription(const std::string& name) const;

  /// The spec of a variant the player is *looking at* but has not started.
  ///
  /// The library screen draws the lattice of whichever variant is selected, which means
  /// it needs a loaded spec for a game that does not exist yet. One is cached, because
  /// the alternative is parsing a file every frame; a name that will not load gives
  /// nullptr and the screen falls back to a plain board.
  [[nodiscard]] const VariantSpec* preview(const std::string& name) const;

  /// The variant's *source text*, for the editor. Separate from the resolved-spec loader
  /// above because editing needs the author's spelling, which the spec has thrown away;
  /// injected like the loader so the shell stays testable without files.
  using VariantSourceLoader = std::function<Result<std::string>(const std::string&)>;
  void setVariantSourceLoader(VariantSourceLoader loader) {
    sourceLoader_ = std::move(loader);
  }

  /// Where a variant's file is written when the editor saves. Injected for the same
  /// reason as the loaders: the shell stays testable without a filesystem.
  using VariantPathResolver = std::function<std::filesystem::path(const std::string&)>;
  void setVariantPathResolver(VariantPathResolver resolver) {
    pathResolver_ = std::move(resolver);
  }

  /// Open a variant's source in the editor. Fails when no source loader is installed or
  /// the variant will not load. Replaces any document already open.
  Result<void> openEditor(const std::string& variantName);

  /// Write the open document back to its file. Fails when nothing is open or no path
  /// resolver is installed.
  Result<void> saveEditor();
  /// Leaving the editor with unsaved changes asks first: `back()` stays put and raises this
  /// request, which the editor screen answers with its own dialog (save, leave without saving,
  /// or stay). Nothing is lost to an Esc.
  [[nodiscard]] bool editorLeaveRequested() const noexcept { return editorLeaveRequested_; }
  void clearEditorLeaveRequest() noexcept { editorLeaveRequested_ = false; }
  /// Throw away the editor's unsaved changes by reopening the file it was opened from.
  void discardEditorChanges();
  /// Leave the editor now, whatever its state: the dialog's answer.
  void leaveEditor() {
    editorLeaveRequested_ = false;
    forceLeaveEditor_ = true;
    back();
    forceLeaveEditor_ = false;
  }

  /// The document being edited, or nullptr when the editor has nothing open.
  [[nodiscard]] Editor* editor() noexcept { return editor_.get(); }
  [[nodiscard]] const Editor* editor() const noexcept { return editor_.get(); }
  [[nodiscard]] const std::string& editorVariant() const noexcept {
    return editorVariant_;
  }

  /// The library screen's overture: which variant's animation is playing, where it is in
  /// its cycle, and what is queued behind it. Held here because it is a transition like
  /// the screens are, and is tested the same way - with no window.
  [[nodiscard]] OverturePlayer& overtures() noexcept { return overtures_; }
  [[nodiscard]] const OverturePlayer& overtures() const noexcept { return overtures_; }

  // ---- the camera-keyframe track (M13.9 Part E) ----------------------------------
  //
  // A scaffold, not a finished authoring tool: the morph is coded once, here, and the
  // sequence of keyframes - where to put one, what the camera and shape pose should look
  // like, how fast to ease into it - is left to whoever plays the game and presses
  // record. There is no in-game editor for the list itself yet; the saved file is plain
  // text (`camera_track.hpp`) and is meant to be hand-edited too.

  /// The running track: a sparse set of saved camera + shape poses, each pinned to a
  /// point in the game, that the live view eases toward as play reaches it
  /// (`advanceCameraTrack`). Empty until a track is loaded or a keyframe is recorded.
  [[nodiscard]] CameraTrack& cameraTrack() noexcept { return cameraTrack_; }
  [[nodiscard]] const CameraTrack& cameraTrack() const noexcept { return cameraTrack_; }

  /// Record the current camera and shape pose as a keyframe at the current cue - the
  /// running session's variant and its ply count right now. Does nothing with no session
  /// running. `speed` is the new keyframe's own easing rate
  /// (`Settings::geometryFormSpeed` units); re-recording at the same cue replaces it,
  /// speed included. Persists the track immediately, the same way a setting change
  /// persists (`applySettings`).
  void recordCameraKeyframe(float speed = 2.0f);
  /// Remove the keyframe at the current cue, if any, and persist. Returns whether one was
  /// removed.
  bool eraseCameraKeyframe();
  /// Where the track is read from and written to: a file beside the settings file, the
  /// same convention `gamesDir()` uses.
  [[nodiscard]] std::filesystem::path cameraTrackPath() const;
  /// Where a rendered trailer goes: a `trailer_render` folder beside the settings file.
  [[nodiscard]] std::filesystem::path trailerRenderDir() const {
    return settingsPath_.parent_path() / "trailer_render";
  }
  /// Advance the track's own easing for one frame. When play has reached a cue with a
  /// keyframe different from the one last eased toward, starts a new blend from the
  /// view's *current* camera and shape pose toward it (`view::slerpCamera` for the
  /// camera, a plain lerp for the pose scalars, both paced by the keyframe's own speed);
  /// every later frame at the same cue advances that blend. A cue with no keyframe, or a
  /// finished blend, leaves the camera and pose exactly where they are - free for the
  /// player's own orbit, or wherever the last keyframe left them. A no-op with no session
  /// or an empty track.
  void advanceCameraTrack(float dt);
  /// Where the shape on screen is centred right now (the renderer's bounds centre; the
  /// flat board's centre where there is no shape). A keyframe's target is stored relative
  /// to the standard board's centre and applied relative to this, so a camera authored
  /// on one shape stays on the middle of the next one instead of on the edge of it.
  [[nodiscard]] float flatBoardYaw() const;
  void setTrailerShapeCentre(const view::Vec3& centre) noexcept { shapeCentre_ = centre; }

  void pause();
  void resume();

  /// Persist settings, and apply anything that has to reach the session.
  void applySettings();

 private:
  Shell() = default;

  /// Start beat `index`: switch to its variant and load its short game into the watch
  /// runner, landing on the game screen. Fails when the variant will not load.
  Result<void> startAttractStep(std::size_t index);
  /// Put the session's follow policy where `trailerCamera_` says (session only).
  void applyTrailerCamera();
  /// The trailer's keyframes in time order, as (trailer time, keyframe); keyframes naming
  /// a beat that does not exist or that has a different variant are skipped.
  [[nodiscard]] std::vector<std::pair<float, const CameraKeyframe*>> trailerKeys() const;
  /// Track which move the trailer is in and how far through it. `dt` 0 just refreshes.
  void updateMovePhase(float dt, const AttractStep& step);
  /// Ease the three pose scalars one frame toward `want`.
  void easePose(const TrailerPose& want, float dt, float formSpeed);
  /// Put the camera and pose where the keyframe spline says they are at trailer time `g`.
  /// `toEditPose` aims the morph scalars at the edit pose (edit mode eases to it) rather
  /// than writing the settings directly (playback).
  void applySplineAt(float g, bool toEditPose);
  void seekNow(const TrailerCue& to);
  /// Whether the keyframes own the pose right now (see `poseKeysFrom`).
  [[nodiscard]] bool splinePoseActive() const;
  [[nodiscard]] float poseKeysFrom(std::size_t beat) const;

  std::vector<std::string> library_;
  std::filesystem::path settingsPath_;
  Settings settings_;
  std::unique_ptr<Session> session_;
  std::string currentVariant_;
  std::string message_;
  Screen screen_{Screen::MainMenu};
  Screen previous_{Screen::MainMenu};
  bool quit_{false};
  VariantLoader loader_;
  VariantLoader shippedLoader_;
  VariantSourceLoader sourceLoader_;
  VariantPathResolver pathResolver_;
  OverturePlayer overtures_;
  /// Where "Surprise me" is in the library, so repeated presses move on (M14.2).
  std::size_t surpriseCursor_{0};
  std::unique_ptr<Editor> editor_;
  bool editorLeaveRequested_{false};
  bool forceLeaveEditor_{false};
  std::string editorVariant_;
  std::filesystem::path editorPath_;
  mutable std::unique_ptr<VariantSpec> preview_;
  mutable std::string previewName_;
  /// Difficulty per variant name, filled on first ask so the picker does not re-read a
  /// file for every row on every frame.
  mutable std::map<std::string, Difficulty> difficulty_;
  /// The bundled showcase games for "Watch a game" (M12.8).
  std::vector<WatchGame> bundled_;
  /// The main-menu attract sequence, empty when none is running (M13.9).
  AttractSequence attract_;
  view::Vec3 shapeCentre_{3.5f, 3.5f, 0.7f};
  std::size_t attractIndex_{0};
  TrailerCamera trailerCamera_{TrailerCamera::Authored};
  bool trailerEditing_{false};
  /// A beat was entered outside `advanceAttract` (a jump); the next call reports it.
  bool trailerCutPending_{false};
  float trailerClock_{0.0f};
  /// Added to the trailer timeline position so it stays continuous across a cut (a beat's
  /// first move starts at once, which would otherwise skip the clock ahead); it decays to 0.
  float trailerShift_{0.0f};
  float swayWeight_{1.0f};
  float trailerKeySpeed_{2.0f};
  TrailerPose editPose_;
  /// The eased morphs behind the pose scalars (see `view::advanceEasedMorph`).
  view::EasedMorph formedEase_;
  view::EasedMorph ghostEase_;
  view::EasedMorph spreadEase_;
  std::optional<TrailerCue> pendingSeek_;
  bool pendingRestart_{false};
  bool trailerPaused_{false};
  /// A seek landed part-way through a move; the next phase update starts there.
  float seekPhase_{-1.0f};
  int trailerGrid_{1};
  std::size_t editSub_{0};
  /// The move the trailer last saw, and how long ago it landed, for the position within
  /// it.
  std::size_t lastPly_{static_cast<std::size_t>(-1)};
  float moveElapsed_{0.0f};
  float moveDuration_{1.0f};
  std::optional<float> trailerGhost_;
  std::optional<bool> trailerFlatPieces_;

  /// Where the current point in the running game is, for the camera track: the session's
  /// variant and its ply count right now.
  [[nodiscard]] CameraCue currentCue() const;
  /// Write the track to `cameraTrackPath()`, silently - a camera track is a convenience,
  /// not save data a failed write should ever interrupt play over.
  void persistCameraTrack() const;

  CameraTrack cameraTrack_;
  /// The cue the view last started easing toward, so a repeated frame at the same cue
  /// does not restart the blend, and a cue with no keyframe is not mistaken for "nothing
  /// changed" right after one that did have one.
  std::optional<CameraCue> cameraTrackCue_;
  /// The camera and pose the running blend eases *from* - captured once, when a new
  /// target cue is first reached, so the blend's start does not drift as the player's own
  /// orbit (or an earlier blend) keeps moving before `advanceCameraTrack` catches up.
  view::OrbitCamera cameraTrackFrom_;
  float cameraTrackFromFormed_{1.0f};
  float cameraTrackFromGhost_{1.0f};
  float cameraTrackFromShellSpread_{1.0f};
  float cameraTrackFromSlideU_{0.0f};
  float cameraTrackFromSlideV_{0.0f};
  /// 0 at the start of a blend, 1 once it has fully eased in (and the idle state: a cue
  /// with no keyframe is set to 1 immediately, meaning "nothing to do").
  float cameraTrackT_{1.0f};
};

}  // namespace cb::app
