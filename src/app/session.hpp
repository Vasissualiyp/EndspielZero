// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ai/search.hpp"
#include "app/game_runner.hpp"
#include "app/hotseat.hpp"
#include "game/game.hpp"
#include "io/game_file.hpp"
#include "view/camera.hpp"
#include "view/game_end_fx.hpp"
#include "view/layout.hpp"
#include "view/move_anim.hpp"
#include "view/move_camera.hpp"
#include "view/seams.hpp"
#include "view/snapshot.hpp"

namespace cb::app {

enum class ActionKind : std::uint8_t {
  None,
  ClickCell,  ///< the user clicked a cell (already resolved from a pixel)
  Hover,      ///< the pointer is over `cell` (kInvalidCell: off the board); `ctrl` held
  Undo,
  Reset,
  Orbit,          ///< dx, dy in radians
  Pan,            ///< dx, dy: slide the look-at point in the view plane
  Zoom,           ///< dx as a multiplier
  SetScreenAxes,  ///< text is a comma-separated axis name list
  SetPromotion,   ///< text is a piece name
  Confirm,        ///< play the move that is waiting for confirmation
  Cancel,         ///< discard it
  Resign,         ///< the side to move concedes
  AgreeDraw,      ///< the players agree a draw
  EngineMove,     ///< the computer plays one move for the side to move, now (scripts)
};

struct Action {
  ActionKind kind{ActionKind::None};
  CellId cell{kInvalidCell};
  float dx{0};
  float dy{0};
  std::string text;
  /// For SetScreenAxes: lay the sub-board grid down the screen instead of across it.
  bool gridVertical{false};
  /// For Hover: whether Ctrl is held.
  bool ctrl{false};
};

/// Which hover aids are on (M20).
struct HintOptions {
  bool ctrlPeek{true};
  bool attackHints{true};
};

/// Everything a front end needs, with no front end in it.
///
/// Input arrives as actions and the result is a snapshot; nothing here knows about SDL,
/// Vulkan, or a window. That is what lets a scripted action log drive the whole
/// interaction path in a test - selection, illegal clicks, promotion, undo - with no
/// display and no screenshots (M4.6).
class Session {
 public:
  /// Takes the variant by value and keeps it at a stable address: Game, Position and
  /// MoveGen all hold pointers into it.
  static Result<std::unique_ptr<Session>> create(VariantSpec variant);
  /// Stops a running opponent search before the game it reads from goes away.
  ~Session();

  [[nodiscard]] const VariantSpec& variant() const noexcept { return *variant_; }
  [[nodiscard]] const Game& game() const noexcept { return *game_; }
  /// Presentation discontinuity: reset or position/game replacement, including an
  /// import attempt that may partially mutate before failing. Never replay/hash data.
  [[nodiscard]] std::uint64_t presentationEpoch() const noexcept {
    return presentationEpoch_;
  }
  /// Presentation events (M23.10). The session has no clock: it counts the things that
  /// happened, and the front end's `FxTimeline` stamps them with presentation time.
  /// Distinct per session object, so a replacement session is never mistaken for the old.
  [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
  /// Bumped when the board should power on: a new session, a reset, a watch start, and the
  /// console's `boot`. Never by undo, pause or resume.
  [[nodiscard]] std::uint64_t bootSerial() const noexcept { return bootSerial_; }
  void requestBoot() noexcept { ++bootSerial_; }
  /// Normalised boot order of every cell (`view::bootCellOrder`), built on first use.
  [[nodiscard]] const std::vector<float>& bootOrder() const;
  /// Bumped by every move that leaves the side to move in check (the player's, the
  /// engine's and a replay's), never by undo, selection or a position that merely stays in
  /// check. `checkEvent()` is where the ripple starts.
  [[nodiscard]] std::uint64_t checkSerial() const noexcept { return checkSerial_; }
  [[nodiscard]] std::optional<CellId> checkEvent() const noexcept { return checkEvent_; }
  /// Console `ripple`: fire the ripple from `cell` without touching the game.
  void requestRipple(CellId cell) noexcept {
    checkEvent_ = cell;
    ++checkSerial_;
  }
  /// Bumped when the result changes from undecided to decided; `endEvent()` is then set.
  /// `powerUpSerial()` is bumped when it changes back (an undo), which clears the event.
  /// Reset clears both quietly and boots instead.
  [[nodiscard]] std::uint64_t endSerial() const noexcept { return endSerial_; }
  [[nodiscard]] std::uint64_t powerUpSerial() const noexcept { return powerUpSerial_; }
  [[nodiscard]] const std::optional<view::EndFx>& endEvent() const noexcept {
    return endEvent_;
  }
  /// Console `endfx`: preview an end (or clear it) without changing the game.
  void requestEndFx(std::optional<view::EndFx> fx) noexcept {
    endEvent_ = fx;
    ++endSerial_;
  }
  /// King-step distances from `origin` to every cell, cached for the last few origins so
  /// an event's flood is computed once rather than per frame.
  [[nodiscard]] const std::vector<int>& distancesFrom(CellId origin) const;
  [[nodiscard]] const view::PositionView& snapshot() const noexcept { return snapshot_; }
  [[nodiscard]] const view::ViewConfig& viewConfig() const noexcept { return viewCfg_; }
  /// The camera to draw and to pick with.
  ///
  /// Returned by value because it is not simply the stored one: pausing steps the view
  /// back off the board, and *everything* that places something on screen has to agree
  /// about that. Handing out the stored camera is how the board once pulled away while
  /// the pieces drawn on top of it stayed put.
  [[nodiscard]] view::OrbitCamera camera() const noexcept;
  /// Set the stored orbit camera directly, bypassing the ordinary drag/zoom actions.
  /// Used by the camera-keyframe track (M13.9 Part E) to drive the view from a recorded
  /// pose instead of the player's own input - the one place something other than `apply`
  /// is allowed to move the camera, which is why it is spelled out rather than folded
  /// into `apply` as a new `ActionKind`: a keyframe is not a player gesture.
  void setOrbitCamera(const view::OrbitCamera& c) noexcept {
    camera_ = c;
    framedOnce_ = true;  // a camera somebody set is not to be re-framed away on a resize
  }
  [[nodiscard]] float boardAspect() const noexcept { return boardAspect_; }

  /// `camera()`'s own blend, against an arbitrary placement set and its bounds instead of
  /// the session's flat layout - what the geometry view passes, built from
  /// `render::PlaySurface::seats()` (M17.16). One definition either way, so the move
  /// camera cannot say something different from what `camera()` already promises.
  [[nodiscard]] view::OrbitCamera cameraOver(
      const std::vector<view::Placement>& placements, const view::Bounds& scene,
      const view::ViewConfig& cfg) const noexcept;

  /// The player's own camera with no move-shot contribution, adjusted for pull-back.
  /// The geometry view follows on top of this so it can ease in and out at the ends of a
  /// move; `cameraOver` folds the shot in, so it cannot be used as the base for a *new*
  /// shot (M17.16).
  [[nodiscard]] view::OrbitCamera playerCamera() const noexcept;

  /// How far the view has stepped back off the board, 0 to 1. Pause is the player
  /// looking up from the position, not a panel landing on top of it.
  /// Camera pull-back, 0 at the board and growing as the shell steps away from it: 1 at
  /// pause, 2 one step further into a pause panel.
  void setPullBack(float t) noexcept { pullBack_ = std::clamp(t, 0.0f, 2.0f); }
  [[nodiscard]] float pullBack() const noexcept { return pullBack_; }
  /// Where the board is glued to itself, and in what colour. Built once per variant.
  [[nodiscard]] const view::SeamMap& seams() const noexcept { return seams_; }
  /// The multiverse's branches, in lattice terms, for the view to draw connectors. Empty
  /// for a variant with no timelines.
  [[nodiscard]] std::vector<view::TimelineLink> timelineLinks() const;
  /// The palette the seams and the move animation are coloured from. Held here rather
  /// than read from the renderer so a session can be driven, and tested, with no
  /// renderer at all.
  [[nodiscard]] const view::Theme& theme() const noexcept { return theme_; }
  void setTheme(const view::Theme& t);
  /// The move currently being drawn, if any.
  [[nodiscard]] const view::MoveAnimation& animation() const noexcept { return anim_; }

  /// How the camera follows a move: "off" (the player's own orbit), "piece" or "route".
  /// Set from `Settings::cameraMode`; anything unrecognised means "off", which is the
  /// shipped behaviour and keeps every pre-M11 pose.
  void setCameraMode(std::string_view mode) noexcept;
  /// 0..1 how strongly the move camera leads the player's orbit. Zero is a no-op.
  void setFollowStrength(float strength) noexcept {
    followStrength_ = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
  }
  [[nodiscard]] float followStrength() const noexcept { return followStrength_; }
  /// Whether the move camera follows at all: a follow mode is set and the strength is not
  /// zero. Independent of whether a move is in flight, which is what the shape-follow
  /// scheduler needs to budget a move's choreography (M12.6).
  [[nodiscard]] bool followsMove() const noexcept {
    return cameraPolicy_.follow != view::FollowMode::Off && followStrength_ > 0.0f;
  }
  /// True while a move's camera shot owns the view, which is exactly when the board is
  /// interaction-locked.
  [[nodiscard]] bool shotInFlight() const noexcept;
  /// Whether the board refuses input right now: a shot in flight, or a game being watched
  /// (M12.8). One gate for picking, so a watched board and a shot-followed board cannot
  /// disagree about whether a click counts.
  [[nodiscard]] bool boardLocked() const noexcept {
    return shotInFlight() || watching_ || engineToMove();
  }

  // ---- the computer opponent (M10.2-M10.3) ---------------------------------------
  /// Which side the computer plays, or none for a game between people. Setting it
  /// abandons a search in progress.
  void setEngineSide(std::optional<Color> side);
  [[nodiscard]] std::optional<Color> engineSide() const noexcept { return engineSide_; }
  /// How hard it looks (ai::SearchOptions); the shipped default is the demo floor.
  void setEngineOptions(const ai::SearchOptions& o) noexcept { engineOptions_ = o; }
  /// The difficulty, 1 to 5 (ai::optionsForLevel). Applies from the next engine move; a
  /// search already running finishes at its old strength.
  void setEngineLevel(int level) noexcept { engineOptions_ = ai::optionsForLevel(level); }
  [[nodiscard]] const ai::SearchOptions& engineOptions() const noexcept {
    return engineOptions_;
  }
  /// The game is in progress and it is the computer's turn.
  [[nodiscard]] bool engineToMove() const noexcept;
  /// A search is running on the worker.
  [[nodiscard]] bool engineThinking() const noexcept { return engineJob_.valid(); }
  /// Drive the opponent from the frame loop: once the board has settled and the computer
  /// is to move, start a search on a worker thread (the game is copied, so the frame is
  /// never held up); when it answers, play its move through the same path a click
  /// takes. A search whose position has since changed - an undo - is discarded. Returns
  /// true while the computer is thinking or about to, so the front end keeps drawing.
  bool advanceEngine(float dt);
  /// Block until the running search answers and play it: for captures and tests, which
  /// have no frame loop to wait across.
  void finishEngineMove();
  /// Move the animation on. Returns true while something is still moving, which is how
  /// the front end knows to keep drawing frames.
  bool advanceAnimation(float dt);
  /// Pin the running move at progress `t` in [0,1], for a capture that states its own
  /// time. Does nothing when no move is animating.
  void setMoveProgress(float t) noexcept { anim_.setProgress(t); }
  [[nodiscard]] const std::vector<view::Placement>& placements() const noexcept {
    return placements_;
  }
  [[nodiscard]] CellId selected() const noexcept { return selected_; }
  /// The last thing that happened, in words, for a status bar.
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] std::string statusLine() const;

  Result<void> apply(const Action& a);

  /// Tell the session how wide the board's drawing area is relative to its height, so
  /// the default camera frames the board in the space the interface leaves it.
  void setBoardAspect(float aspect);

  /// Frame the camera on a box of world space, keeping the angle the player is looking
  /// from. The geometry view needs this: the shape is a different size from the flat
  /// board and sits somewhere else entirely, so framing on the layout's own bounds
  /// leaves the board off in a corner of the window.
  ///
  /// `headroom` is how far above the box the camera should look - a piece's crown must
  /// not be clipped. A caller whose bounds already include room for what stands on them
  /// (the play surface pads for its pieces) passes 0, so the look-at stays on the box's
  /// centre instead of being lifted off it (M17.11).
  void frameOn(const view::Bounds& b, float headroom = 1.4f);

  /// Ease the camera's target and distance toward the framing of `b` by `k` in [0,1],
  /// keeping the player's own view angle. Unlike `frameOn`, which commits a new frame
  /// outright, this follows a box that is still moving - the flat/shape morph's bounds
  /// change every frame while it rolls, so a single snapshot lets the board drift
  /// off-centre (M17.24).
  void easeFrameOn(const view::Bounds& b, float headroom, float k);

  /// Look straight down with no perspective, for a machine that would rather not draw
  /// the scene in three dimensions - and for a player who simply prefers a diagram.
  void setFlatView(bool flat);
  [[nodiscard]] bool flatView() const noexcept { return flat_; }

  /// Two players, one keyboard. When on, the front end routes key presses through
  /// feedHotSeat so each half enters its own moves.
  void setHotSeat(bool on) noexcept { hotSeatEnabled_ = on; }
  [[nodiscard]] bool hotSeat() const noexcept { return hotSeatEnabled_; }
  /// Feed one key, as the unshifted character it produces. True when it belonged to a
  /// player and was consumed - so the front end can skip its own shortcuts. A completed
  /// square is applied as a click.
  bool feedHotSeat(char key);
  [[nodiscard]] const HotSeat& hotSeatKeys() const noexcept { return hotSeatKeys_; }

  /// Whether this cell's board exists yet (always true for a non-temporal variant). The
  /// temporal lattice is mostly unfilled space, so the view draws only live boards.
  [[nodiscard]] bool boardVisible(CellId c) const;

  /// How long a move takes to draw, per cell travelled. Zero turns animation off, and
  /// a move then simply appears where it landed.
  void setAnimationSeconds(float secondsPerCell) { animSeconds_ = secondsPerCell; }

  /// A move the player has committed to except for what the pawn becomes.
  ///
  /// The session stops and asks rather than choosing silently: a variant may promote to
  /// pieces nobody expects, so "queen by default" is a guess, not a rule. While a
  /// promotion is pending the board is frozen - clicks do nothing until it is resolved.
  struct PendingPromotion {
    bool active{false};
    CellId from{kInvalidCell};
    CellId to{kInvalidCell};
    std::vector<PieceTypeId> choices;
  };
  [[nodiscard]] const PendingPromotion& pendingPromotion() const noexcept {
    return pending_;
  }
  Result<void> choosePromotion(PieceTypeId piece);
  void cancelPromotion();

  /// A move the player has chosen but not yet played, when "confirm before moving" is on.
  /// The board is frozen while it waits, exactly as it is for a promotion: the player has
  /// committed to the move and owes only a yes or a no.
  struct PendingMove {
    bool active{false};
    CellId from{kInvalidCell};
    CellId to{kInvalidCell};
  };
  [[nodiscard]] const PendingMove& pendingMove() const noexcept { return pendingMove_; }
  Result<void> confirmMove();
  void cancelMove();
  void setConfirmMoves(bool on) noexcept { confirmMoves_ = on; }

  // ---- hover aids (M20) ----------------------------------------------------------
  /// Right-click: drop the selected piece, or the promotion / confirmation waiting on it.
  /// True when something was cancelled.
  bool cancelSelection();
  void setHintOptions(const HintOptions& o);
  /// The pointer is over `cell` (kInvalidCell: off the board). Cheap when unchanged.
  void setHover(CellId cell, bool ctrl);
  [[nodiscard]] CellId hoverCell() const noexcept { return hoverCell_; }
  /// The cell under a pixel of the board rectangle, or kInvalidCell. Pure: no selection
  /// change, no snapshot rebuild. Refuses (kInvalidCell) when boardLocked().
  [[nodiscard]] CellId pickPixel(float px, float py, float width, float height) const;
  [[nodiscard]] bool confirmMoves() const noexcept { return confirmMoves_; }

  /// Replace the current position from FEN-N, discarding history. Used for setting up a
  /// study or a test position, and by the front end's position entry.
  Result<void> loadFen(std::string_view fen);

  /// Write this game (variant, start position, move history) to a game file (M12.7).
  Result<void> saveGame(const std::filesystem::path& path) const;
  /// Replay a parsed game file into this session. Fails if the file names a different
  /// variant; a front end starts that variant first.
  Result<void> loadGame(const GameFile& file);

  /// Play the legal move named in the engine's notation, starting its animation. The
  /// M12.6 runner steps a saved game this way, one move at a time, so each can be
  /// animated - unlike `loadGame`, which replays them all at once. Fails, naming the
  /// move, when it is not legal in the current position.
  Result<void> playMoveText(std::string_view text);

  /// Begin watching `file`: the engine plays it move by move and the board refuses input
  /// (M12.8). The first move is started immediately; `advanceWatching` then holds each
  /// settled move for `dwellSeconds` before playing the next. Not deterministic - the
  /// caller drives it with the real interactive clock - so this is the menu's mode, not
  /// the clip exporter's.
  ///
  /// `previewSeconds` > 0 shows each move before it is played: once the dwell has passed,
  /// the piece about to move is selected - so the board lights its legal squares - and
  /// held for that long, then the move plays. The first move of a watch starts at once.
  ///
  /// With `holdFirstMove` the first move waits for `releaseWatch()` instead of starting at
  /// once - the trailer's first board powers on before anything moves (M23.10).
  Result<void> startWatching(GameFile file, float dwellSeconds, float previewSeconds = 0.0f,
                             bool holdFirstMove = false);
  /// Play the held first move of a watch. A no-op when nothing is held.
  Result<void> releaseWatch();
  [[nodiscard]] bool watching() const noexcept { return watching_; }
  /// Leave the watching mode. The game is left where it stopped; the caller decides
  /// which screen to show next.
  void stopWatching() noexcept;
  /// Advance the watch's clock by `dt`: while the current move is still drawing it does
  /// nothing; once settled it holds for the dwell and then plays the next move. Ends the
  /// watch when the file has no move left. Returns true while still watching, so the
  /// front end knows to keep drawing.
  bool advanceWatching(float dt);
  /// How many moves have been played (1 once the first move has started) and how many
  /// there are, for a status line.
  [[nodiscard]] std::size_t watchIndex() const noexcept;
  [[nodiscard]] std::size_t watchCount() const noexcept;
  /// Play the watched game's next move now, without waiting out the dwell. False when no
  /// watch is running or it has no move left. The trailer's edit mode steps with it.
  bool watchStep();
  /// Put the watched game exactly `index` moves in (0 = its start position), settled: no
  /// animation is left running. Replays from the start, so it can go backwards. Fails
  /// when no watch is running or `index` is past the last move.
  Result<void> watchSeek(std::size_t index);

  /// Resolve a pixel to a cell and act on it. Returns kInvalidCell if the click missed
  /// the board, which is not an error - it deselects, like clicking off a board does.
  CellId clickPixel(float px, float py, float width, float height);

  /// Run a newline-separated action script. Written for tests, and equally the basis of
  /// a replay format.
  Result<void> applyScript(std::string_view script);

 private:
  Session() = default;
  void refreshSnapshot();
  void refreshView();
  /// Re-lay the temporal view after a move changed which boards exist, keeping the
  /// player's camera angle and zoom.
  void refreshTemporalView();
  void applyViewMode();
  /// The view actually laid out: the player's choice, adjusted for the flat mode, which
  /// cannot show a depth axis because a top-down camera would stack the boards unseen.
  [[nodiscard]] view::ViewConfig effectiveViewConfig() const;
  [[nodiscard]] const Move* findMove(CellId from, CellId to) const;
  void refreshHints();
  [[nodiscard]] std::vector<PieceTypeId> promotionChoicesFor(CellId from,
                                                             CellId to) const;
  Result<void> playChecked(const Move& m);

  std::unique_ptr<VariantSpec> variant_;
  std::unique_ptr<Game> game_;
  std::uint64_t presentationEpoch_{0};
  std::uint64_t id_{0};
  std::uint64_t bootSerial_{1};
  mutable std::vector<float> bootOrder_;
  /// Presentation event bookkeeping (see `trackEvents`).
  void trackEvents();
  bool eventsPrimed_{false};
  std::size_t lastPly_{0};
  bool endDecided_{false};
  bool endEventDirty_{false};
  std::uint64_t checkSerial_{0};
  std::optional<CellId> checkEvent_;
  std::uint64_t endSerial_{0};
  std::uint64_t powerUpSerial_{0};
  std::optional<view::EndFx> endEvent_;
  mutable std::vector<std::pair<CellId, std::vector<int>>> distanceCache_;
  view::PositionView snapshot_;
  /// What the player asked for; `viewCfg_` is this with the flat-mode adjustment applied.
  view::ViewConfig chosenCfg_;
  view::ViewConfig viewCfg_;
  view::OrbitCamera camera_;
  std::vector<view::Placement> placements_;
  view::SeamMap seams_;
  view::Theme theme_{view::Theme::console()};
  view::MoveAnimation anim_;
  /// The route the running animation is following, kept so `camera()` can plan a shot
  /// from the same trace the animation draws. Valid only while `anim_` is active.
  view::MovePath lastPath_;
  bool pathValid_{false};
  /// Off until a setting says otherwise: `CameraPolicy`'s own default follows the route,
  /// but the shipped look is the player's own camera, so the session starts there and
  /// `Shell::applySettings` moves it. Aggregate init keeps the rest of the record's
  /// defaults.
  view::CameraPolicy cameraPolicy_{view::FollowMode::Off};
  float followStrength_{0.6f};
  float animSeconds_{0.085f};
  bool flat_{false};
  bool hotSeatEnabled_{false};
  HotSeat hotSeatKeys_;
  CellId selected_{kInvalidCell};
  float boardAspect_{1.3f};
  float pullBack_{0.0f};
  bool framedOnce_{false};
  PendingPromotion pending_;
  PendingMove pendingMove_;
  bool confirmMoves_{false};
  HintOptions hints_{};
  CellId hoverCell_{kInvalidCell};
  bool hoverCtrl_{false};
  /// A square a piece just arrived on under the pointer: not hover-marked until the
  /// pointer has left it.
  CellId suppressedHover_{kInvalidCell};
  std::string promotionPreference_;
  /// The watching mode (M12.8): the file, the runner over it, and the dwell clock. The
  /// file is heap-held so the runner's reference to it cannot be invalidated by a move.
  std::unique_ptr<GameFile> watchFile_;
  std::unique_ptr<GameRunner> watchRunner_;
  bool watching_{false};
  float watchDwell_{0.0f};
  float watchClock_{0.0f};
  float watchPreview_{0.0f};
  float previewClock_{0.0f};
  bool previewing_{false};
  std::string message_;
  bool watchHeld_{false};

  /// The opponent: its side, its options, the derived evaluator (built on first use,
  /// per variant), the search in flight and the position it was asked about.
  std::optional<Color> engineSide_;
  ai::SearchOptions engineOptions_{ai::optionsForLevel(ai::kDefaultLevel)};
  std::unique_ptr<ai::Evaluator> engineEval_;
  std::future<ai::SearchResult> engineJob_;
  std::shared_ptr<std::atomic<bool>> engineStop_;
  std::uint64_t engineAskedHash_{0};
  std::size_t engineAskedPly_{0};
  float engineClock_{0.0f};
  void stopEngine();
  void startEngine();
  void takeEngineAnswer(const ai::SearchResult& r);
};

/// Parse one line of an action script: "click e4", "undo", "reset", "orbit 0.1 0",
/// "zoom 1.2", "axes file,rank,level", "promote queen".
Result<Action> parseAction(const VariantSpec& v, std::string_view line);

}  // namespace cb::app
