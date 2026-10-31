// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/debug_console.hpp"
#include "app/session.hpp"
#include "app/shell.hpp"
#include "assets/piece_model.hpp"
#include "render/deco.hpp"
#include "render/overture_scene.hpp"
#include "render/piece_icon.hpp"
#include "render/piece_mesh.hpp"
#include "render/vulkan_context.hpp"
#include "view/theme.hpp"

struct SDL_Window;
union SDL_Event;

namespace cb::render {

/// The window of the authoring plane an outline editor shows, in permille. The body's
/// profile uses the whole box; an element is drawn about its own origin, so its window
/// reaches to both sides of zero.
struct DesignBox {
  float xMin{0.0f}, xMax{1000.0f}, yMin{0.0f}, yMax{1000.0f};
};

/// What the interface is asking the application to do next.
///
/// The UI decides nothing itself - it reports intent and the caller acts - which keeps
/// the rule "the renderer never invents a move" true of the panels as well as the board.
struct UiRequest {
  bool quit{false};
  /// Non-empty when the player picked a different variant from the library.
  std::string loadVariant;
  /// The area the rails leave free, as x, y, width, height in pixels. The board is
  /// drawn and picked in this rectangle, so it is centred in what the player can
  /// actually see rather than behind a panel.
  float boardRect[4]{0, 0, 0, 0};
  /// A setting changed and should be written to disk.
  bool settingsChanged{false};
  /// The interface scale changed; fonts have to be rebuilt at the new size.
  bool applyScale{false};
  bool toggleFullscreen{false};
  /// A purely visual setting changed - the icon set, say - and has to reach the
  /// interface rather than the engine.
  bool applyLooks{false};
  /// Save or load the named game (in the shell's games directory). One frame's request.
  bool saveGame{false};
  bool loadGame{false};
  std::string gameName;
  /// Watch the named bundled game (M12.8): the engine plays it and the player watches.
  /// One frame's request, cleared once acted on.
  std::string watchGame;
  /// Start the main-menu attract trailer (M13.9 revision). One frame's request: the
  /// sequence is fixed, so there is no picker step the way a bundled "Watch" game has.
  bool startAttract{false};
  /// Render the whole trailer to a video (one frame's request; the front end runs it).
  bool renderTrailer{false};
};

/// Whether the camera-projected file/rank coordinate labels are drawn.
///
/// They are flat text projected through the camera, which reads badly under any 3-D
/// perspective - a letter skews, overlaps its neighbour, or goes nearly edge-on - so they
/// appear only in the flat 2-D view, never the solid one and never on the shape.
/// `showCoordinates` still owns them within that (M17.21 label revision).
[[nodiscard]] bool coordinateLabelsShown(const app::Settings& settings,
                                         const app::Session& session) noexcept;

/// What the board's 2D/3D button means in the current state (M17.25).
///
/// Off the shape view it is the original flat/solid board toggle, with its original
/// enabled state. On the shape view - a variant with a surface, SHAPE lit, not flat - it
/// is re-purposed and re-enabled to switch the pieces between the sculptural meshes and
/// the icon-faced cylinder pucks, keeping the same 2D/3D name the player already knows. A
/// free function so the branch is testable with no device, like `coordinateLabelsShown`.
struct ViewModeButton {
  /// "2D" or "3D".
  const char* label;
  /// Live on the shape view (where the old flat meaning is dead) and wherever the
  /// original board toggle was live; greyed out exactly where the board toggle always
  /// was.
  bool enabled;
  /// True when pressing it toggles `shapePieceCylinders`; false when it toggles
  /// `flatView`.
  bool pieceStyle;
};
[[nodiscard]] ViewModeButton viewModeButton(const app::Settings& settings, bool flatView,
                                            bool surfaceAvailable) noexcept;

/// The game's interface: rails, ledger, status, promotion, library.
///
/// Drawn with Dear ImGui, restyled until it stops looking like Dear ImGui - the
/// defaults are tool-grey with soft rounding, and this is a board game played by
/// candlelight. Colours come from view::Theme, so retheming the game, or letting a
/// variant carry its own palette, never means touching this file.
class Ui {
 public:
  static Result<std::unique_ptr<Ui>> create(
      const VulkanContext& ctx, SDL_Window* window, const view::Theme& theme,
      VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM);
  ~Ui();
  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;

  /// Feed a platform event. Returns true when the interface consumed it, in which case
  /// the click was on a panel and must not also fall through to the board.
  bool processEvent(const SDL_Event& e);

  /// True while the pointer or the keyboard belongs to the interface.
  [[nodiscard]] bool capturesMouse() const;
  [[nodiscard]] bool capturesKeyboard() const;

  /// Advance the shell's own clock. The menus animate continuously - the manifold
  /// turns, the field drifts - so the interface needs time, which only the main loop
  /// knows about.
  void tick(float dt);

  void newFrame();

  /// Build whichever screen the shell says the player is on.
  UiRequest build(app::Shell& shell, float fps);

  /// Rebuild the fonts at a new interface scale.
  ///
  /// The atlas is regenerated rather than the existing glyphs stretched: a scale option
  /// that produces blurry text is not worth having. Must not be called inside a frame.
  Result<void> setScale(float scale);
  void setIconStyle(IconStyle s) noexcept { iconStyle_ = s; }
  /// Cinema mode: the board is the whole frame and the interface's furniture is dropped.
  /// The one thing still drawn is the frame's own look - the grade and the vignette.
  /// What the trailer panel says about a render in progress, and whether one is running
  /// (so the button is not offered twice). Set each frame by the front end that runs it.
  void setRenderStatus(std::string text, bool running) {
    renderStatus_ = std::move(text);
    renderRunning_ = running;
  }
  void setCinema(bool on) noexcept { cinema_ = on; }
  /// Open the editor on a page, and the piece designer on one of its tabs. For `--shot`,
  /// so every designer is reviewable the same way every other screen is.
  void openEditorPage(int page, int tab = 0) noexcept {
    editorPage_ = page;
    designTab_ = tab;
  }
  /// How many dimensions the move preview shows. For `--shot`, so a 4-D preview is
  /// reviewable without a mouse.
  void setPreviewDims(int dims) noexcept { previewDims_ = dims; }
  /// The piece the designer opens on, by name. For `--shot --piece`.
  /// Drive the pointer without a display, for `--shot --ui-click`: where it is and
  /// whether the left button is down on the next frame.
  void injectMouse(float x, float y, bool down, int button = 0) noexcept {
    inject_ = true;
    injectPos_ = ImVec2(x, y);
    injectDown_ = down;
    injectButton_ = button;
  }
  /// A key event for the next frame (ImGuiKey), for `--ui-click "key:delete"`.
  void injectKey(int key, bool down) noexcept {
    injectKey_ = key;
    injectKeyDown_ = down;
  }
  void openEditorPiece(std::string name) { pendingPiece_ = std::move(name); }

  /// Point the library at a variant, for a capture that names one - the same forcing the
  /// overture player gets from `jumpTo`. No effect once the player has picked.
  void pickVariant(const std::string& name) { pickedVariant_ = name; }
  /// Swap the palette. The theme is read every frame, so a change is immediate - and a
  /// retheme never touches the renderer.
  void setTheme(const view::Theme& t) noexcept;
  /// Presentation seconds for live controls, independent of transitions/background.
  void setVisualTime(double seconds) noexcept;
  /// Finish the frame and record its draw commands into the board's render pass.
  void endFrame();
  void record(VkCommandBuffer cmd);
  /// Record the board screen's ambient field into the board's own render pass, before the
  /// board's draws, so it is painted behind the board rather than over it (M18
  /// correction). A no-op when the current screen has no field - every screen but the
  /// board.
  void recordBackground(VkCommandBuffer cmd);

 private:
  Ui() = default;
  void applyStyle();
  Result<void> loadFonts();
  /// The interface's own multisample-matched pipeline for a standalone draw list. Dear
  /// ImGui's backend builds its one pipeline at the sample count it was initialised with
  /// (single-sample, for the overlay pass), and a pipeline's sample count must match the
  /// attachment it draws into, so drawing the field inside the board's 4-sample pass
  /// needs a second pipeline built from the same shaders at the board's sample count.
  Result<void> buildBoardFieldPipeline(VkFormat colorFormat);
  /// Clear the field's dedicated draw list and give it the frame's clip rect and font
  /// texture, ready for `buildGameHud` to fill. Called once per frame, before any screen.
  void resetBoardFieldList();
  /// Wrap a standalone draw list in a minimal ImDrawData and submit it with the field's
  /// multisample-matched pipeline.
  void recordDrawList(VkCommandBuffer cmd, ImDrawList* list);

  /// The debug window (F12 or backtick): scrollback and a command line, nothing pretty.
  void drawDebugConsole(app::Shell& shell);
  app::DebugConsole console_;
  bool consoleOpen_{false};
  bool consoleFocus_{false};
  std::array<char, 512> consoleInput_{};
  int consoleHistoryPos_{-1};

  UiRequest buildGameHud(app::Shell& shell, float fps);
  [[nodiscard]] ImFont* actionFont() const noexcept;
  /// The two-pane frame every menu screen sits in: the drifting field, the screen's
  /// own object, and the depth ladder. Returns the rectangle the menu itself gets.
  void drawShellFrame(app::Shell& shell, ImVec2& menuMin, ImVec2& menuMax);
  /// The pause section's frame: the board keeps the left of the screen behind a scrim
  /// (the main loop draws it there, pulled back and blurred), and the menu pane takes
  /// the right. Returns where the pane goes and leaves the board rectangle on `request`.
  void pauseFrame(UiRequest& request, ImVec2& menuMin, ImVec2& menuMax);
  /// The piece designer's left half: a board with the selected piece at its centre and
  /// the cells it can reach marked quiet or capture. Reads the editor's atoms and walks
  /// them, so it shows the real movement rather than a picture of it.
  void drawEditorPreview(app::Shell& shell, const ImVec2& min, const ImVec2& max);
  /// The library's object: the selected variant's overture, in the half the
  /// decoration would otherwise occupy.
  void drawOvertureObject(app::Shell& shell, const ImVec2& min, const ImVec2& max,
                          float zoom, float alpha);
  /// Let the player turn whatever object the screen is showing, by dragging it.
  ///
  /// The turn is an *offset* on the object's own animation rather than a replacement, so
  /// a manifold someone has taken hold of keeps turning under their hand instead of
  /// freezing. Kept across screens, because it reads as one camera on one object.
  void updateObjectDrag(app::Shell& shell, const ImVec2& min, const ImVec2& max);
  /// A red glow at the screen's edges while the side to move is in check and the board
  /// is on screen: the position is in trouble, but only while it is the thing being
  /// looked at. Returns with the game, not with the screen.
  void drawCheckEdges(app::Shell& shell);
  /// The cinema frame's dressing (M18.3): a low-alpha grade and a neutral vignette, the
  /// only thing the interface draws when `cinema_` is set. A 2-D draw-list overlay, like
  /// the check warning, rather than a render-target post pass.
  void drawCinemaLook();

  /// Where a pane sits in the shell's transition, as the scale and fade the pane is
  /// drawn with. The arriving pane and the departing ghost ask for their own.
  struct PaneMove {
    float scale{1.0f};
    float alpha{1.0f};
  };
  [[nodiscard]] PaneMove paneMove() const noexcept;
  /// The two halves of the transition clock. The departing screen runs first and is
  /// finished before the arriving one begins, so the two moves are a sequence rather
  /// than a cross-fade; `outEase` reaches 1 while `inEase` is still 0.
  [[nodiscard]] float outEase() const noexcept;
  [[nodiscard]] float inEase() const noexcept;
  /// Draw the screen being left, once more, scaled and faded - the departing menu has to
  /// be seen to shrink or swell, not simply switch off. Its input is disabled and its
  /// request discarded, so re-running its build cannot start or change anything.
  void drawGhost(app::Shell& shell);
  /// Pixels at the current interface scale. Every hardcoded size goes through this, or
  /// the scale option moves the text and leaves the panels behind.
  [[nodiscard]] float px(float v) const noexcept { return v * scale_; }
  UiRequest buildMainMenu(app::Shell& shell);
  /// The bundled-game picker for "Watch a game" (M12.8): the list of showcase games and
  /// the one the START button will play.
  UiRequest buildWatch(app::Shell& shell);
  /// The tutorial screen: what this is, and the curated "start here" path, reached from
  /// the main menu's Tutorial row rather than opened on first launch (M14.1/M14.2).
  UiRequest buildWelcome(app::Shell& shell);
  UiRequest buildNewGame(app::Shell& shell);
  UiRequest buildPause(app::Shell& shell);
  UiRequest buildSettings(app::Shell& shell);
  UiRequest buildEditor(app::Shell& shell);
  UiRequest buildQuitConfirm(app::Shell& shell);
  UiRequest buildPauseQuitConfirm(app::Shell& shell);
  UiRequest buildGameInfo(app::Shell& shell);
  /// The trailer's camera-authoring rows (M13.9 revision): stepping, keyframes, and the
  /// shape-mode buttons, shown above the trailer's main row while it is paused for edit.
  void buildTrailerEditRows(app::Shell& shell, app::Session& session, UiRequest& request,
                            ImFont* small, bool editing);
  /// The trailer's timeline strip (M13.9): beats as segments, keyframes as diamonds, the
  /// playhead, and - in edit mode - click or drag to scrub. Returns the height it takes
  /// so the controls sit above it.
  float buildTrailerTimeline(app::Shell& shell);
  UiRequest buildPieceMoves(app::Shell& shell);
  std::string renderStatus_;
  bool renderRunning_{false};
  /// The piece designer's three tabs. They share one pane, because they are three jobs
  /// on one piece and moving between them must not feel like leaving the piece behind.
  void drawMovesTab(app::Shell& shell, const std::string& piece);
  /// The body: a side profile, a revolve and a symmetry (M7.3.2, M7.3.3).
  void drawBodyTab(app::Shell& shell);
  /// The flat outline the board draws when it is drawn flat (M7.3.5).
  void drawIconTab(app::Shell& shell);
  void drawIdentityTab(app::Shell& shell, const std::string& piece);
  void drawFieldsTab(app::Shell& shell);
  void drawRulesTab(app::Shell& shell);
  void drawMarksTab(app::Shell& shell);
  void drawCompoundTab(app::Shell& shell);
  /// Write the author's finished edit to the document. Held back while the pointer is
  /// down, so a drag is one undo entry, and refused while the shape is invalid.
  void commitDesign(app::Shell& shell, const std::string& piece);
  /// Load the working model and icon for whichever piece is selected, once per
  /// selection. Seeded from the shipped archetype, so "start from the queen" always
  /// gives something editable rather than an empty canvas.
  void syncDesignAssets(app::Shell& shell);
  /// One editable closed outline on a grid. Returns true when the author changed it.
  ///
  /// Shared by both designers because they are the same job on different axes - which
  /// is also why the spec asks for one drawing surface rather than two.
  bool outlineEditor(const char* id, ImVec2 min, ImVec2 max, assets::Outline& outline,
                     int& selected, bool yUp, const char* xLabel, const char* yLabel,
                     const DesignBox& box = DesignBox{});
  /// The body as the game draws it: the real assembled mesh, shaded, turning.
  void drawRevolvePreview(ImDrawList* dl, ImVec2 min, ImVec2 max,
                          const assets::PieceModel& model, float spin);

  const VulkanContext* ctx_{nullptr};
  SDL_Window* window_{nullptr};
  view::Theme theme_{view::Theme::manifold()};
  VkDescriptorPool pool_{VK_NULL_HANDLE};
  void* fontDisplay_{nullptr};  ///< ImFont*, kept opaque so the header stays light
  void* fontBody_{nullptr};
  void* fontSmall_{nullptr};
  void* fontMono_{nullptr};
  void* fontAction_{nullptr};
  void* fontLabel_{nullptr};
  /// Which flat-piece table to draw from. Pushed in from the player's settings; the
  /// interface holds no opinion of its own about it.
  IconStyle iconStyle_{IconStyle::Faceted};
  float scale_{1.0f};
  /// True while the interface is dressing a cinema capture rather than building a screen
  /// (M18.3). Off for ordinary play, so the frame is byte-for-byte what it always was.
  bool cinema_{false};
  /// The name in the pause menu's save/load field, kept between frames.
  char gameName_[64]{"quick"};
  /// The shell's clock, and where it is in a screen change. `enter_` runs 0 to 1 as a
  /// screen arrives; the menu is scaled and faded along it, which is as close to a
  /// camera push as a 2-D draw list gets.
  float clock_{0.0f};
  double visualTime_{0};
  /// The last frame's dt, so `build` can move the overture on. `tick` is the only place
  /// that knows about time, and the overture is advanced where the shell is to hand.
  float lastDt_{0.0f};
  float enter_{1.0f};
  int lastScreen_{-1};
  /// Which piece the piece designer has selected. Editor state, not shell state: the
  /// document is what matters and it lives in the shell; which row is highlighted is a
  /// view concern.
  int editorPiece_{0};
  /// The editor's own page: its home (pick a designer), the piece designer, or its help.
  /// Kept here rather than as a Screen so stepping in does not change the shell's depth.
  int editorPage_{0};

  /// The piece body and flat icon being designed, and which piece they were loaded for.
  /// Editing state, like `editorPiece_`: the document the shell owns is the variant, and
  /// these are not part of it until they are saved.
  assets::PieceModel designModel_{};
  assets::IconModel designIcon_{};
  std::string designFor_;
  int designProfilePoint_{-1};
  int designIconPoint_{-1};
  std::size_t designIconPoly_{0};
  /// Draw the right half and mirror it: an authoring aid, not a model operation. What
  /// is stored is always the finished outline, so an icon stays as dumb as the shipped
  /// tables.
  bool designIconMirror_{false};
  /// The revision of the document the design state below was last read from, and the
  /// last values that match it. The document is the store: a change the author makes is
  /// written to it (one undo entry) when it settles, and an undo or a switch of piece
  /// reloads from it.
  std::uint64_t designSyncedRevision_{~0ull};
  assets::PieceModel loadedModel_{};
  assets::IconModel loadedIcon_{};
  int designElement_{-1};
  int designElementPoint_{-1};
  /// The 3-D preview's mesh and the model it was built from, rebuilt only on a change.
  ModelMesh previewMesh_{};
  assets::PieceModel previewMeshModel_{};
  bool previewMeshValid_{false};
  /// A note from the last move edit that did not take, shown in red under the list.
  /// The rest of the piece, drawn faintly behind the outline being edited (the flat
  /// icon's other fills and its cut-outs), set by the caller around `outlineEditor`.
  std::vector<const assets::Outline*> outlineGhostFills_;
  std::vector<const assets::Outline*> outlineGhostCuts_;
  /// Where the "unsaved changes" dialog would leave to: 0 no dialog, 1 the editor's home,
  /// 2 out of the editor altogether.
  int leaveTarget_{0};
  /// The piece's move rows are invalid (set while drawing them): they are shown red, and
  /// the header stays quiet rather than repeating the loader's sentence.
  bool movesInvalid_{false};
  std::string moveNote_;
  /// The move row whose detail (steps, home rank, en passant, reach) is open.
  int selectedAtom_{-1};
  /// The piece designer's identity form: text buffers and which piece they were filled
  /// for.
  std::string identityFor_;
  std::array<char, 64> nameBuf_{};
  std::array<char, 4> symbolBuf_{};
  /// The loader's verdict on the document, cached per revision.
  std::uint64_t validatedRevision_{~0ull};
  std::string validateNote_;
  std::string pendingPiece_;
  bool inject_{false};
  bool injectDown_{false};
  int injectButton_{0};
  int injectKey_{0};
  bool injectKeyDown_{false};
  ImVec2 injectPos_{0, 0};
  /// Which of the piece designer's three tabs is showing.
  int designTab_{0};
  /// How many dimensions the move preview board has, and how wide it is. A piece is
  /// declared once and played on whatever board a variant declares, so "what does this
  /// do on a 4-D board" is a question the designer has to be able to answer.
  int previewDims_{2};

  /// What the player has placed on the piece preview's board, per dimension count, by
  /// cell (0 empty, 1 black, 2 white). The selected piece is white and stands at the
  /// centre; the rest is a scratch board for trying captures and blocks.
  std::array<std::vector<std::uint8_t>, 5> previewOcc_{};
  /// What a left click puts down: 0 clears, 1 an enemy (capturable), 2 a friend (blocks).
  int previewTool_{1};
  /// The cell under the pointer on the preview board, for the hover outline.
  std::int64_t previewHover_{-1};
  /// The screen being left, kept as a number so its own build can be re-run as a ghost,
  /// and the screen being built now, so `paneMove` knows whether this is a menu move.
  int leavingScreen_{-1};
  int currentScreen_{-1};
  /// True only while drawGhost is building the departing pane, so drawShellFrame skips
  /// the background it already drew and beginPane uses the departing move.
  bool ghosting_{false};
  /// The half of the frame the object owns, remembered by drawShellFrame so a screen can
  /// put its own thing there: the piece reference draws its move diagrams, not a deco.
  ImVec2 decoMin_{0, 0};
  ImVec2 decoMax_{0, 0};
  /// Which piece the piece-moves reference shows on its left. View state, like the
  /// editor's selected piece.
  int pieceMovesPick_{0};
  /// How far the player has turned the menu's object, and whether they are turning it
  /// now. View state: which way an ornament faces is not something the shell decides.
  float objectYaw_{0.0f};
  float objectElev_{0.0f};
  bool objectDrag_{false};
  /// The last overture scene built, and the inputs it was built from. A scene is a pure
  /// function of `(which, t, intro, theme)`, so caching it is exact: it skips rebuilding
  /// identical geometry, above all across the 700 ms held dwell. `setTheme` clears it.
  app::Overture overtureCacheWhich_{app::Overture::None};
  float overtureCacheT_{0.0f};
  bool overtureCacheIntro_{false};
  bool overtureCacheValid_{false};
  OvertureScene overtureCache_{};
  /// The variant the cache was built for, and whether it used the derived scene. Needed
  /// because two derived variants both map to the same fallback `Overture` value, so the
  /// enum alone cannot tell them apart.
  std::string overtureCacheVariant_;
  bool overtureCacheDerived_{false};
  /// The decoration of the screen being left, and which way the camera is going. One
  /// clock (`enter_`) drives the arriving object, the leaving ghost, the menu pane and
  /// the field, so none of them can disagree about where the camera is.
  Deco leaving_{Deco::None};
  bool deeper_{true};
  DepthField field_;
  /// The much quieter field the board screen draws behind the game: wireframes only,
  /// fewer bodies, further out and fainter, so it never competes with the pieces being
  /// played.
  DepthField boardField_{true};
  /// The field's own draw list, separate from ImGui's frame-wide background list so it
  /// can be submitted before the board - the coordinate labels and every menu share that
  /// list and must stay on the late overlay path. Null until the first frame.
  std::unique_ptr<ImDrawList> boardFieldList_;
  VkShaderModule boardFieldVert_{VK_NULL_HANDLE};
  VkShaderModule boardFieldFrag_{VK_NULL_HANDLE};
  VkDescriptorSetLayout boardFieldSetLayout_{VK_NULL_HANDLE};
  VkPipelineLayout boardFieldLayout_{VK_NULL_HANDLE};
  VkPipeline boardFieldPipeline_{VK_NULL_HANDLE};
  /// Which variant the new-game screen is showing details for.
  std::string pickedVariant_;
  /// That variant's own one-line description, and the name it was read for, so the file
  /// is read once per selection rather than once per frame.
  std::string pickedDescription_;
  std::string describedFor_;
  /// Which bundled game the "Watch a game" picker has selected (M12.8).
  std::string pickedWatch_;
  /// The selected bundled game's variant blurb, cached so the variant file is read once
  /// per selection rather than every frame.
  std::string watchDescription_;
  std::string watchDescribedFor_;
  /// Which pair of axes the "axes" control will show next.
  int axisRotation_{0};
  bool initialised_{false};
};

}  // namespace cb::render
