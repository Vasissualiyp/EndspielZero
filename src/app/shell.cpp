// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/shell.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>

#include "app/embed.hpp"
#include "io/fen.hpp"
#include "io/game_file.hpp"
#include "io/variant_toml.hpp"
#include "view/camera.hpp"
#include "view/morph.hpp"

namespace cb::app {

std::string_view screenName(Screen s) noexcept {
  switch (s) {
    case Screen::Welcome:
      return "welcome";
    case Screen::MainMenu:
      return "main menu";
    case Screen::NewGame:
      return "new game";
    case Screen::Watch:
      return "watch a game";
    case Screen::Game:
      return "game";
    case Screen::Paused:
      return "paused";
    case Screen::Settings:
      return "settings";
    case Screen::Editor:
      return "editor";
    case Screen::GameInfo:
      return "game mode";
    case Screen::PieceMoves:
      return "piece moves";
    case Screen::QuitConfirm:
    case Screen::PauseQuitConfirm:
      return "quit";
  }
  return "?";
}

int screenDepth(Screen s) noexcept {
  switch (s) {
    case Screen::Welcome:
    case Screen::MainMenu:
      return 0;
    case Screen::NewGame:
    case Screen::Watch:
    case Screen::Settings:
    case Screen::Editor:
    case Screen::QuitConfirm:
      return 1;
    case Screen::Game:
      return 2;
    // Pause and the reference screens are one rung off the game, still in its context
    // rather than out at the shell's own menus.
    case Screen::Paused:
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return 2;
  }
  return 0;
}

std::unique_ptr<Shell> Shell::create(std::vector<std::string> library,
                                     std::filesystem::path settingsPath) {
  auto shell = std::unique_ptr<Shell>(new Shell());
  shell->library_ = std::move(library);
  shell->settingsPath_ =
      settingsPath.empty() ? Settings::defaultPath() : std::move(settingsPath);
  shell->settings_ = Settings::load(shell->settingsPath_);
  // The shape morph's live value starts settled at its target (M18.6): a loaded
  // `geometry_view` means the fully-formed shape, its absence means the flat board. The
  // transient value is never written to disk, so it cannot come in mid-morph; deriving it
  // here is what stops a glued variant opening with a spurious morph on the first frame.
  shell->settings_.geometryFormed = shell->settings_.geometryView ? 1.0f : 0.0f;
  shell->currentVariant_ = shell->settings_.lastVariant;
  // The shell always opens on the main menu. The curated first-run sequence is now the
  // on-demand Tutorial row rather than something the game opens onto by itself.
  shell->screen_ = Screen::MainMenu;
  // A loaded setting should take effect immediately; the library overtures have their own
  // clock and are not waiting for `applySettings` (which a fresh shell has no reason to
  // call). `applySettings` keeps it in step once the player moves a control.
  shell->overtures_.setSpeed(shell->settings_.overtureSpeed);
  // The camera track (M13.9 Part E), if one has been recorded before. Its absence is the
  // ordinary case - every shell starts with no track - so a missing or unreadable file is
  // not an error, just an empty track.
  if (std::ifstream in{shell->cameraTrackPath()}; in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    if (auto track = parseCameraTrack(ss.str()); track.has_value()) {
      shell->cameraTrack_ = std::move(*track);
    }
  }
  return shell;
}

const std::vector<CuratedVariant>& curatedVariants() {
  // The first-run path: the board a newcomer knows, then one legible step at a time to
  // the board they cannot picture. A front door, not a gate - the full library stays
  // available.
  static const std::vector<CuratedVariant> kPath{
      {"standard", "the board you already know"},
      {"cylinder", "the a-file and the h-file become the same edge"},
      {"torus", "and then the rank edges close too: a board with no edges"},
      {"cube5", "the board grows a third axis - five boards stacked"},
      {"5d", "and then time: a move can land on a board from a past turn"},
  };
  return kPath;
}

const std::vector<GlossaryEntry>& glossary() {
  // Longest term first: a scan that replaces terms in a sentence has to match "Mobius
  // band" before it matches "band".
  static const std::vector<GlossaryEntry> kGlossary{
      {"time travel",
       "a variant where a piece may move onto a board from an earlier or later turn"},
      {"Klein bottle",
       "a board whose ends are joined with a twist that reverses one axis; it has only "
       "one "
       "face"},
      {"multiverse",
       "the axis along which the many boards of a time-travel game are laid out, one per "
       "branch of play"},
      {"Mobius band",
       "a board whose ends are joined with a half-twist, so riding it twice comes home "
       "mirrored"},
      {"dimension", "one axis of a board - files, ranks, and any axes above them"},
      {"cylinder",
       "a board whose left and right edges are joined, so the files form a loop"},
      {"timeline",
       "the axis along which the past and future turns of a time-travel game "
       "are laid out"},
      {"vector", "how far a piece moves along each axis, written as one number per axis"},
      {"pitch", "how many cells one unit of a piece's move covers along an axis"},
      {"glued",
       "two edges of a board are joined, so a piece crossing one arrives through the "
       "other"},
      {"slice", "one board taken out of an N-dimensional one and shown on its own"},
      {"seam", "a joined edge of a glued board; its colour says where crossing it lands"},
      {"torus",
       "a board whose edges are all joined into loops - it has no edge to fall off"},
  };
  return kGlossary;
}

std::string_view glossaryFor(std::string_view term) {
  for (const GlossaryEntry& e : glossary()) {
    if (e.term == term) return e.definition;
  }
  return {};
}

void Shell::dismissWelcome() {
  settings_.seenWelcome = true;
  applySettings();  // persist, so the sequence plays once
  if (screen_ == Screen::Welcome) screen_ = Screen::MainMenu;
}

std::string Shell::surpriseVariant() {
  if (library_.empty()) return "standard";
  // Rotate, skipping standard so "surprise" is always one of the strange ones.
  for (std::size_t i = 0; i < library_.size(); ++i) {
    const std::string& name = library_[(surpriseCursor_ + i) % library_.size()];
    if (name != "standard") {
      surpriseCursor_ = (surpriseCursor_ + i + 1) % library_.size();
      return name;
    }
  }
  return library_.front();
}

bool Shell::showsBoard() const noexcept {
  // The pause and reference screens sit over the position: the board stays behind them,
  // pulled back and blurred, which is the whole look of stepping away from a game.
  switch (screen_) {
    case Screen::Game:
    case Screen::Paused:
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return session_ != nullptr;
    case Screen::Settings:
      return session_ != nullptr && previous_ != Screen::MainMenu;
    default:
      return false;
  }
}

bool Shell::showsCheckWarning() const noexcept {
  // The warning belongs to the board. Every screen that keeps the board behind it gets
  // it; a position left behind on the main menu - a game abandoned but kept for a later
  // resume - does not, because it is not a warning about the screen the player is looking
  // at. The trailer now runs on the game screen like any watched game, so a checked beat
  // would show it too (M13.9 revision, Part G).
  if (screen_ == Screen::MainMenu) return false;
  return showsBoard() && session_ != nullptr && session_->game().inCheck();
}

float Shell::boardPullBack() const noexcept {
  if (session_ == nullptr) return 0.0f;
  switch (screen_) {
    case Screen::Game:
      return 0.0f;
    case Screen::Paused:
      return 1.0f;
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return 2.0f;
    case Screen::Settings:
      return previous_ != Screen::MainMenu ? 2.0f : 0.0f;
    default:
      return 0.0f;
  }
}

void Shell::go(Screen s) {
  if (s == screen_) return;
  // Opening the editor with nothing loaded edits the current variant, or standard as a
  // base. A failure leaves the screen with no document, which the screen reports rather
  // than trapping the player on a menu.
  if (s == Screen::Editor && editor_ == nullptr) {
    (void)openEditor(currentVariant_.empty() ? "standard" : currentVariant_);
  }
  // Only remember a screen worth returning to: bouncing between two overlays should
  // still take you back to the game.
  if (screen_ != Screen::Settings && screen_ != Screen::GameInfo &&
      screen_ != Screen::PieceMoves && screen_ != Screen::Editor) {
    previous_ = screen_;
  }
  screen_ = s;
}

void Shell::discardEditorChanges() {
  if (editor_ == nullptr) return;
  const std::string name = editorVariant_;
  (void)openEditor(name);
}

void Shell::back() {
  if (screen_ == Screen::Editor && editor_ != nullptr && editor_->dirty() &&
      !forceLeaveEditor_) {
    editorLeaveRequested_ = true;
    return;
  }
  switch (screen_) {
    case Screen::Settings:
    case Screen::Editor:
    case Screen::NewGame:
    case Screen::Watch:
    case Screen::QuitConfirm:
    case Screen::PauseQuitConfirm:
      screen_ = previous_;
      break;
    case Screen::GameInfo:
    case Screen::PieceMoves:
      screen_ = session_ != nullptr ? Screen::Paused : Screen::MainMenu;
      break;
    case Screen::Paused:
      screen_ = Screen::Game;
      break;
    case Screen::Game:
      screen_ = Screen::Paused;
      break;
    case Screen::MainMenu:
      break;
    case Screen::Welcome:
      // The tutorial is a screen off the menu now, so backing out returns there.
      screen_ = Screen::MainMenu;
      break;
  }
}

void Shell::setVariantLoader(VariantLoader loader) {
  loader_ = std::move(loader);
  difficulty_.clear();
  // Difficulty first, then name. Ties are alphabetical, so the order is stable and
  // predictable rather than whatever the filesystem handed over.
  std::stable_sort(library_.begin(), library_.end(),
                   [this](const std::string& a, const std::string& b) {
                     const Difficulty da = difficultyOf(a);
                     const Difficulty db = difficultyOf(b);
                     if (da != db) return static_cast<int>(da) < static_cast<int>(db);
                     return a < b;
                   });
}

Difficulty Shell::difficultyOf(const std::string& name) const {
  // A running game already holds the spec, and it is the same one.
  if (session_ != nullptr && currentVariant_ == name)
    return session_->variant().difficulty;
  if (const auto it = difficulty_.find(name); it != difficulty_.end()) return it->second;
  Difficulty d = Difficulty::Other;
  if (loader_) {
    auto v = loader_(name);
    if (v.has_value()) d = v->difficulty;
  }
  difficulty_.emplace(name, d);
  return d;
}

Result<void> Shell::openEditor(const std::string& variantName) {
  if (!sourceLoader_) {
    return fail(ErrorCode::Internal, "no variant source loader is installed");
  }
  auto text = sourceLoader_(variantName);
  if (!text.has_value()) return fail(text.error().code, text.error().message);
  auto editor = Editor::open(*text, variantName);
  if (!editor.has_value()) {
    return fail(editor.error().code, editor.error().message, editor.error().line);
  }
  editor_ = std::make_unique<Editor>(std::move(*editor));
  editorVariant_ = variantName;
  editorPath_ = pathResolver_ ? pathResolver_(variantName) : std::filesystem::path{};
  return {};
}

Result<void> Shell::saveEditor() {
  if (editor_ == nullptr) {
    return fail(ErrorCode::Internal, "nothing is open in the editor");
  }
  if (editorPath_.empty()) {
    return fail(ErrorCode::Internal, "no save path is installed for the editor");
  }
  // One validator: the file is written only if the loader would accept it.
  if (auto ok = editor_->validate(); !ok.has_value()) {
    message_ = ok.error().format();
    return ok;
  }
  if (auto ok = editor_->saveFile(editorPath_); !ok.has_value()) return ok;
  // The file on disk changed under the cached preview; the next look loads it afresh.
  if (previewName_ == editorVariant_) {
    previewName_.clear();
    preview_.reset();
  }
  return {};
}

Result<void> Shell::startGame(const std::string& variantName) {
  if (!loader_) return fail(ErrorCode::Internal, "no variant loader is installed");
  auto variant = loader_(variantName);
  if (!variant.has_value()) {
    message_ = variant.error().format();
    return fail(variant.error().code, variant.error().message);
  }
  auto session = Session::create(std::move(*variant));
  if (!session.has_value()) {
    message_ = session.error().format();
    return fail(session.error().code, session.error().message);
  }
  session_ = std::move(*session);
  currentVariant_ = variantName;
  settings_.lastVariant = variantName;
  message_.clear();
  applySettings();
  screen_ = Screen::Game;
  previous_ = Screen::MainMenu;
  return {};
}

std::filesystem::path Shell::gamesDir() const {
  return settingsPath_.parent_path() / "games";
}

namespace {
/// A name safe to turn into a file name: no separators, no traversal, nothing exotic.
bool validGameName(const std::string& name) {
  if (name.empty() || name.size() > 64) return false;
  for (const char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}
}  // namespace

Result<void> Shell::saveGame(const std::string& name) {
  if (session_ == nullptr)
    return fail(ErrorCode::ValidationError, "there is no game to save");
  if (!validGameName(name)) {
    message_ = "a game name can use letters, digits, spaces, '-' and '_'";
    return fail(ErrorCode::ValidationError, message_);
  }
  std::error_code ec;
  std::filesystem::create_directories(gamesDir(), ec);
  const auto path = gamesDir() / (name + ".cbgame");
  if (auto ok = session_->saveGame(path); !ok.has_value()) {
    message_ = ok.error().message;
    return ok;
  }
  message_ = "saved '" + name + "'";
  return {};
}

Result<void> Shell::loadGame(const std::string& name) {
  if (!validGameName(name)) {
    message_ = "a game name can use letters, digits, spaces, '-' and '_'";
    return fail(ErrorCode::ValidationError, message_);
  }
  std::ifstream in(gamesDir() / (name + ".cbgame"));
  if (!in) {
    message_ = "no saved game called '" + name + "'";
    return fail(ErrorCode::ValidationError, message_);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  auto file = parseGameFile(ss.str());
  if (!file.has_value()) {
    message_ = file.error().format();
    return fail(file.error().code, file.error().message);
  }
  // The file names its variant; start it first if it is not the one already loaded.
  if (session_ == nullptr || file->variant != currentVariant_) {
    if (auto ok = startGame(file->variant); !ok.has_value()) return ok;
  }
  if (auto ok = session_->loadGame(*file); !ok.has_value()) {
    message_ = ok.error().message;
    return ok;
  }
  message_ = "loaded '" + name + "'";
  screen_ = Screen::Game;
  previous_ = Screen::Paused;
  return {};
}

std::vector<std::string> Shell::savedGames() const {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(gamesDir(), ec)) {
    if (e.is_regular_file() && e.path().extension() == ".cbgame") {
      names.push_back(e.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

Result<void> Shell::startWatching(const std::string& name) {
  const WatchGame* found = nullptr;
  for (const WatchGame& g : bundled_) {
    if (g.name == name) {
      found = &g;
      break;
    }
  }
  if (found == nullptr) {
    message_ = "no bundled game called '" + name + "'";
    return fail(ErrorCode::ValidationError, message_);
  }
  if (auto ok = startGame(found->file.variant); !ok.has_value()) return ok;
  if (auto ok = session_->startWatching(found->file, settings_.watchDwell);
      !ok.has_value()) {
    message_ = ok.error().format();
    return ok;
  }
  screen_ = Screen::Game;
  previous_ = Screen::MainMenu;
  return {};
}

void Shell::stopWatching() {
  if (session_ != nullptr) session_->stopWatching();
  go(Screen::MainMenu);
}

bool Shell::watching() const noexcept {
  return session_ != nullptr && session_->watching();
}

void Shell::advanceWatching(float dt) {
  if (session_ == nullptr || !session_->watching()) return;
  if (!session_->advanceWatching(dt)) go(Screen::MainMenu);
}

bool Shell::advanceEngine(float dt) {
  // Only on the board itself: a paused game, or one behind a menu, does not move on.
  if (session_ == nullptr || screen_ != Screen::Game) return false;
  return session_->advanceEngine(dt);
}

namespace {
/// The standard board's centre: what a recorded keyframe's target is measured from.
const view::Vec3 kBoardCentre{3.5f, 3.5f, 0.7f};
}  // namespace

AttractSequence defaultAttractSequence() {
  // Standard -> cylinder -> torus -> Klein bottle -> (Klein eases back to a torus) -> the
  // 8x8x4 3-D torus, then round again. Every line is real, legal play, verified through
  // the engine's own legality: `startWatching` replays it move by move and a bad move
  // fails the load.
  //
  // It is one continuous game (M13.9 Part F): every beat but the first continues the
  // position right before it through `embedOnto`, and every cut between shapes is a real
  // geometry morph (cylinder's end state is the torus morph's `kTorusRollEnd` waypoint,
  // the torus's end state is Klein's `kKleinRingEnd`, Klein eases back to that waypoint
  // before the 3-D torus, which opens with its levels collapsed onto one ring and spreads
  // them apart over the beat).
  //
  // `standard` is 18 plies (9 moves) - short on purpose, because a viewer gone by move 20
  // never sees a shape (M13.9 revision, "the standard game is too long"). Klein fields no
  // bishop or queen at all (a bishop or queen gives check from almost anywhere once a
  // surface has no two-colouring), so the opener spends its 18 plies trading exactly
  // those: both bishop pairs and both queens come off, and nothing else of value does.
  // Bxh6 gxh6 and Bxa6 bxa6 take a bishop each for a pawn; the black bishop on c8 trades
  // itself and the queens off on g4/d7; the last black bishop goes on e5 to dxe5. Both
  // sides keep every rook and knight to the 3-D torus; only pawns drop at the Klein cut,
  // by `embedOnto`'s ordinary rule for a piece type the target does not declare.
  //
  // The shape beats' ten-ply lines were found by a search over the engine's own legal
  // moves (material-safe at two plies, no shuffling, at least one wrapped step across a
  // seam, at most one capture), so each is quiet, varied and shows the seam. The loop
  // wraps back to `standard`, because every later beat continues the one before it.
  //
  AttractSequence seq;
  seq.push_back(
      {.variant = "standard",
       .moves = {"d2d4", "d7d6", "c1h6", "g7h6", "e2e4", "c8g4", "d1g4", "d8d7", "g4d7",
                 "e8d7", "f1a6", "b7a6", "g1f3", "f8g7", "e1f1", "g7e5", "d4e5", "c7c6"},
       .formedFrom = 1.0f,
       .dwell = 0.22f});
  seq.push_back({.variant = "cylinder",
                 .moves = {"f3g1", "d7c7", "b1c3", "b8d7", "a1d1", "a8f8", "h1a1", "f8a8",
                           "g1h3", "c7b8"},
                 .formedFrom = 0.0f,
                 .continuesPosition = true,
                 .morphSpeed = 0.7f});
  seq.push_back({.variant = "torus",
                 .moves = {"d1d8", "b8b7", "a1b1", "d7b8", "f1f8", "g8a1", "f8g1", "a1g8",
                           "h3b4", "b7c7"},
                 .formedFrom = view::kTorusRollEnd,
                 .continuesPosition = true,
                 .morphSpeed = 0.25f});
  // Klein hides its far side, so it ghosts and draws flat icon pucks throughout both of
  // its beats (M13.9 revision, Part H), not just the fold-in.
  seq.push_back({.variant = "klein",
                 .moves = {"d8d3", "g8a7", "g1f1", "b8h2", "f1e2", "h8c8", "b1g7", "c8f7",
                           "c3b5", "c7c8"},
                 .ghostTarget = 0.35f,
                 .formedFrom = view::kKleinRingEnd,
                 .flatPieces = true,
                 .continuesPosition = true,
                 .morphSpeed = 0.5f});
  seq.push_back({.variant = "klein",
                 .moves = {},
                 .formedTarget = view::kKleinRingEnd,
                 .ghostTarget = 0.35f,
                 .flatPieces = true,
                 .continuesPosition = true,
                 .morphSpeed = 0.5f});
  seq.push_back({.variant = "torus3d_8x8",
                 .moves = {"(1,4,0)-(3,5,0)", "(2,7,0)-(1,0,0)", "(6,6,0)-(5,6,0)",
                           "(0,6,0)-(6,6,1)", "(1,3,0)-(7,2,0)", "(7,1,0)-(7,7,1)",
                           "(5,6,0)-(5,1,0)", "(7,7,1)-(1,0,1)", "(7,2,0)-(1,2,3)",
                           "(1,0,0)-(1,7,1)"},
                 .ghostTarget = 0.35f,
                 .formedFrom = 1.0f,
                 .flatPieces = true,
                 .spreadFrom = 0.0f,
                 .continuesPosition = true,
                 .morphSpeed = 0.5f});
  return seq;
}

Result<void> Shell::startAttractStep(std::size_t index) {
  if (index >= attract_.size()) {
    return fail(ErrorCode::OutOfRange, "attract step out of range");
  }
  const AttractStep step = attract_[index];
  // The trailer plays the variants as they ship: swapped in for this call, put back on
  // every way out.
  struct LoaderSwap {
    VariantLoader& slot;
    VariantLoader saved;
    LoaderSwap(VariantLoader& s, const VariantLoader& shipped) : slot(s), saved(s) {
      if (shipped) slot = shipped;
    }
    ~LoaderSwap() { slot = std::move(saved); }
  } loaderSwap(loader_, shippedLoader_);
  // Continuity (M13.9 Part F): carry the previous beat's own end-of-game position onto
  // this beat's board. Computed - FEN and all - entirely *before* `startGame` below
  // replaces `session_`, and against a freshly loaded `VariantSpec` of our own rather
  // than the new session's eventual one: `Position` holds a raw pointer to its variant
  // (so does the `Position` `embedOnto` returns), and `startGame` loads the target
  // variant a second time into its own session regardless, so there is no way to hand
  // this call a `toVariant` that both exists yet and will still be `session_->variant()`
  // afterwards. Finishing the whole computation - through `toFen`, down to a plain string
  // with no pointers left in it - while the old session and this local variant are both
  // still alive is what the CLI's own `embed` command had to learn the hard way (see its
  // own comment, `src/cli/main.cpp`): building the embedding against a `VariantSpec` that
  // is about to move or be destroyed corrupts it the instant that happens, not safely
  // before.
  std::optional<std::string> embeddedFen;
  if (step.continuesPosition && session_ != nullptr) {
    auto targetVariant = loader_ ? loader_(step.variant) : fail(ErrorCode::Internal, "");
    if (targetVariant.has_value()) {
      embeddedFen = toFen(
          embedOnto(session_->game().position(), session_->variant(), *targetVariant));
    }
  }
  // The camera is one continuous thing across a cut: a new beat's session would otherwise
  // open on its own default view, turning the whole picture in an instant. Carried over
  // from the beat before (never for the opener, which sets its own).
  std::optional<view::OrbitCamera> carried;
  float carriedAspect = 0.0f;
  bool fromFlatBoard = false;
  if (index > 0 && session_ != nullptr) {
    carried = session_->playerCamera();
    carriedAspect = session_->boardAspect();
    fromFlatBoard = attract_[attractIndex_].variant == "standard";
  }
  if (auto ok = startGame(step.variant); !ok.has_value()) return ok;
  if (carried.has_value()) {
    if (carriedAspect > 0.0f) session_->setBoardAspect(carriedAspect);
    view::OrbitCamera c = *carried;
    // The shape renderer's flat board sits a half turn about the vertical from the
    // ordinary board; turning the view with it keeps the picture identical across the
    // cut.
    if (fromFlatBoard && step.formedFrom == 0.0f) c.yaw += 3.14159265f;
    session_->setOrbitCamera(c);
  }
  // Build the game file from the session's own started position, so its FEN always
  // matches the variant and no author has to keep one in sync (M13.9).
  GameFile file = toGameFile(session_->variant(), session_->game());
  if (embeddedFen.has_value()) file.startFen = *embeddedFen;
  file.moves = step.moves;
  if (auto ok = session_->startWatching(
          std::move(file), step.dwell >= 0.0f ? step.dwell : settings_.watchDwell,
          settings_.trailerMovePreview);
      !ok.has_value()) {
    message_ = ok.error().format();
    return fail(ok.error().code, ok.error().message);
  }
  // A revealing beat starts part-way or from flat; any other beat keeps the pose the last
  // one left, so the shape cuts rather than re-rolling every time.
  if (step.formedFrom >= 0.0f) settings_.geometryFormed = step.formedFrom;
  if (step.spreadFrom >= 0.0f) settings_.geometryShellSpread = step.spreadFrom;
  // The flat board has no slide to carry (it is the ordinary board); every other cut
  // keeps the slide, so the shape does not turn under the camera.
  if (step.formedFrom == 0.0f) {
    settings_.geometrySlideU = 0.0f;
    settings_.geometrySlideV = 0.0f;
  }
  // The piece style is a discrete per-beat choice, so it snaps at the cut rather than
  // easing like the pose (M13.9 revision, Part H).
  settings_.shapePieceCylinders = trailerFlatPieces_.value_or(step.flatPieces);
  // `startGame` landed us on the ordinary game screen; a trailer beat is a real watch
  // run, so it stays there rather than compositing behind the menu (M13.9 revision, Part
  // G).
  attractIndex_ = index;
  // Per-beat camera state: a fresh session, so its follow policy is the trailer's, not
  // the player's; a keyframe pose from the previous beat does not carry over; and the
  // track's "last cue" is forgotten so the new beat's first keyframe is reached.
  cameraTrackCue_.reset();
  lastPly_ = static_cast<std::size_t>(-1);
  moveElapsed_ = 0.0f;
  trailerShift_ = 0.0f;
  editSub_ = 0;
  applyTrailerCamera();
  return {};
}

void Shell::applyTrailerCamera() {
  if (session_ == nullptr) return;
  // Per session, never `Settings`: the player's own camera mode is theirs and survives.
  const bool chase = trailerCamera_ == TrailerCamera::Follow;
  session_->setCameraMode(chase ? "route" : "off");
  session_->setFollowStrength(chase ? 1.0f : 0.0f);
}

void Shell::setTrailerCamera(TrailerCamera mode) {
  trailerCamera_ = mode;
  if (attracting()) applyTrailerCamera();
}

void Shell::setTrailerKeySpeed(float s) noexcept {
  trailerKeySpeed_ = std::clamp(s, 0.25f, 16.0f);
}

void Shell::setTrailerEditing(bool on) {
  if (!attracting() || on == trailerEditing_) return;
  trailerEditing_ = on;
  if (on) trailerShift_ = 0.0f;
  if (on) {
    trailerCamera_ = TrailerCamera::Authored;
    applyTrailerCamera();
    editPose_ = {settings_.geometryFormed, settings_.geometryGhost,
                 settings_.geometryShellSpread};
  }
}

view::OrbitCamera Shell::trailerView(const view::OrbitCamera& base) const {
  if (!attracting()) return base;
  return view::steadySway(base, trailerClock_, swayWeight_);
}

bool Shell::trailerStep(int delta) {
  if (!attracting() || !trailerEditing_ || session_ == nullptr || delta == 0 ||
      !session_->watching()) {
    return false;
  }
  const long target = static_cast<long>(session_->watchIndex()) + delta;
  if (target < 0 || target > static_cast<long>(session_->watchCount())) return false;
  editSub_ = 0;
  if (delta == 1) return session_->watchStep();
  return session_->watchSeek(static_cast<std::size_t>(target)).has_value();
}

void Shell::trailerGoto(std::size_t beat, std::size_t ply) {
  if (!attracting() || beat >= attract_.size()) return;
  for (std::size_t i = 0; i <= beat; ++i) {
    if (!startAttractStep(i).has_value()) {
      attract_.clear();
      return;
    }
    // Every beat before the target is played to its end, so the next one embeds the real
    // finished position it would have in the trailer.
    if (i < beat && session_->watching()) {
      (void)session_->watchSeek(session_->watchCount());
    }
  }
  if (session_->watching()) {
    (void)session_->watchSeek(std::min(ply, session_->watchCount()));
  }
  const AttractStep& step = attract_[beat];
  if (trailerEditing_) {
    settings_.geometryFormed = step.formedTarget;
    settings_.geometryGhost = step.ghostTarget;
    settings_.geometryShellSpread = step.spreadTarget;
    editPose_ = {step.formedTarget, step.ghostTarget, step.spreadTarget};
  }
  editSub_ = 0;
  trailerCutPending_ = true;
}

void Shell::requestTrailerJumpBeat(int delta) {
  if (!attracting() || !trailerEditing_) return;
  const long n = static_cast<long>(attract_.size());
  const long next = ((static_cast<long>(attractIndex_) + delta) % n + n) % n;
  pendingSeek_ = TrailerCue{static_cast<std::size_t>(next), 0, 0};
}

void Shell::requestTrailerSeek(std::size_t beat, std::size_t ply, std::size_t sub) {
  if (!attracting() || beat >= attract_.size()) return;
  pendingSeek_ = TrailerCue{beat, ply, std::min<std::size_t>(sub, 7)};
}

void Shell::seekNow(const TrailerCue& to) {
  if (!attracting() || to.beat >= attract_.size()) return;
  if (to.beat != attractIndex_) {
    trailerGoto(to.beat, to.ply);
  } else if (session_->watching()) {
    (void)session_->watchSeek(std::min(to.ply, session_->watchCount()));
  }
  if (trailerEditing_) {
    editSub_ = to.sub;
    applySplineAt(trailerTime(), true);
  } else {
    // Playing (or just paused): land part-way through the move; the spline is applied
    // every frame by `advanceCameraTrack`, so there is nothing to preview here.
    seekPhase_ = static_cast<float>(to.sub) / 8.0f;
    lastPly_ = static_cast<std::size_t>(-1);
    if (session_ != nullptr) updateMovePhase(0.0f, attract_[attractIndex_]);
  }
}

void Shell::refreshTrailerView() {
  if (attracting() && trailerEditing_) applySplineAt(trailerTime(), true);
}

void Shell::trailerJumpBeat(int delta) {
  if (!attracting() || !trailerEditing_) return;
  const long n = static_cast<long>(attract_.size());
  const long next = ((static_cast<long>(attractIndex_) + delta) % n + n) % n;
  trailerGoto(static_cast<std::size_t>(next), 0);
}

void Shell::startAttract(AttractSequence sequence) {
  stopAttract();
  if (sequence.empty()) return;
  attract_ = std::move(sequence);
  attractIndex_ = 0;
  // The beats own their starting pose (`formedFrom`); the first one is the flat-to-shape
  // reveal. Clear the ghost so a beat before the first ghosting one starts opaque.
  settings_.geometryGhost = 1.0f;
  if (auto ok = startAttractStep(0); !ok.has_value()) attract_.clear();
}

void Shell::stopAttract() {
  if (attract_.empty()) return;
  attract_.clear();
  attractIndex_ = 0;
  trailerEditing_ = false;
  trailerPaused_ = false;
  seekPhase_ = -1.0f;
  pendingSeek_.reset();
  pendingRestart_ = false;
  trailerClock_ = 0.0f;
  swayWeight_ = 1.0f;
  trailerGhost_.reset();
  trailerFlatPieces_.reset();
  if (session_ != nullptr) session_->stopWatching();
  // The Klein and 3-D beats ghost the board; restore the opaque default so whatever the
  // player does next does not inherit the trailer's pose. The flat icon pucks snap on for
  // those same two beats, so they are cleared here too (M13.9 revision, Part H). The 3-D
  // beat's own level spread is cleared the same way, so a game started afterwards opens
  // on the shipped, fully-spread shape rather than the trailer's collapsed one.
  settings_.geometryGhost = 1.0f;
  settings_.shapePieceCylinders = false;
  settings_.geometryShellSpread = 1.0f;
  // The trailer now runs on its own screen, so leaving it returns to the ordinary menu,
  // exactly as `stopWatching()` does (M13.9 revision, Part G).
  go(Screen::MainMenu);
}

bool Shell::advanceAttract(float dt) {
  if (attract_.empty() || session_ == nullptr) {
    pendingSeek_.reset();
    return false;
  }
  if (pendingRestart_) {
    pendingRestart_ = false;
    const AttractSequence seq = attract_;
    const bool editing = trailerEditing_;
    startAttract(seq);  // replaces the session; nothing below holds the old one
    if (attract_.empty() || session_ == nullptr) return false;
    if (editing) setTrailerEditing(true);
    trailerCutPending_ = true;
  }
  if (pendingSeek_.has_value()) {
    const TrailerCue to = *pendingSeek_;
    pendingSeek_.reset();
    seekNow(to);  // may replace the session; nothing below holds the old one
    if (attract_.empty() || session_ == nullptr) return false;
  }
  const AttractStep& step = attract_[attractIndex_];
  const float formSpeed =
      settings_.geometryFormSpeed * settings_.trailerMorphSpeed * step.morphSpeed;
  // The resting sway fades in and out rather than switching, so changing mode or entering
  // edit mode never jumps the view.
  const float swayTarget =
      (trailerCamera_ == TrailerCamera::Auto && !trailerEditing_) ? 1.0f : 0.0f;
  swayWeight_ += std::clamp(swayTarget - swayWeight_, -dt * 1.5f, dt * 1.5f);
  if (trailerCutPending_) {
    trailerCutPending_ = false;
    return true;
  }
  if (trailerEditing_) {
    // Paused: the clock, the watch and the cut all stand still, and the pose follows the
    // edit buttons rather than the beat's targets.
    easePose(editPose_, dt, formSpeed);
    return false;
  }
  if (trailerPaused_) {
    updateMovePhase(0.0f, step);
    return false;
  }
  trailerClock_ += dt;
  trailerShift_ -= std::clamp(trailerShift_, -dt * 0.8f, dt * 0.8f);
  if (session_->watching()) session_->advanceWatching(dt);
  updateMovePhase(dt, step);
  // With keyframes in charge the spline owns the morph, so the beat's own easing stands
  // down - and must not hold the beat open waiting for a pose the spline will never
  // reach.
  bool settled = true;
  if (!splinePoseActive()) {
    const TrailerPose want{step.formedTarget, trailerGhost_.value_or(step.ghostTarget),
                           step.spreadTarget};
    easePose(want, dt, formSpeed);
    constexpr float kSettle = 1e-3f;
    settled = std::abs(settings_.geometryFormed - want.formed) <= kSettle &&
              std::abs(settings_.geometryGhost - want.ghost) <= kSettle &&
              std::abs(settings_.geometryShellSpread - want.shellSpread) <= kSettle;
  }
  // A beat ends only when its game has run out *and* the pose has stopped moving, so a
  // cut never lands mid-roll.
  if (session_->watching() || !settled) return false;
  // The sequence loops back to its own start - the literal `standard` opener - rather
  // than skipping straight to the shape beats (M13.9 Part F, the "ALL game should be
  // continuous" revision): every later beat continues the position of whatever came
  // right before it, all the way back to `standard`'s own canonical start, and that
  // chain only stays correct if every lap actually replays it. The shape beats'
  // trade-down tactics are also only verified sound on the plain, non-wrapping standard
  // board (see `defaultAttractSequence`'s own comment) - replaying them fresh from a
  // *shape* beat's own start, to skip the opener on a loop, is not a safe substitute.
  const std::size_t next = attractIndex_ + 1 < attract_.size() ? attractIndex_ + 1 : 0;
  const float before = trailerTime();
  if (auto ok = startAttractStep(next); !ok.has_value()) {
    // A beat that cannot start must not turn the trailer into an ordinary game: begin the
    // sequence again from its opener, and give up only if that fails too.
    if (auto again = startAttractStep(0); !again.has_value()) {
      attract_.clear();
      return false;
    }
    return true;
  }
  // Keep the timeline position continuous through the cut; the loop's wrap is a hard cut.
  if (next != 0) trailerShift_ = before - trailerTime();
  return true;
}

void Shell::easePose(const TrailerPose& want, float dt, float formSpeed) {
  settings_.geometryFormed = view::advanceEasedMorph(
      formedEase_, settings_.geometryFormed, want.formed, dt, formSpeed);
  settings_.geometryGhost = view::advanceEasedMorph(ghostEase_, settings_.geometryGhost,
                                                    want.ghost, dt, formSpeed);
  settings_.geometryShellSpread = view::advanceEasedMorph(
      spreadEase_, settings_.geometryShellSpread, want.shellSpread, dt, formSpeed);
}

void Shell::updateMovePhase(float dt, const AttractStep& step) {
  // Where in the current move the trailer is, for the keyframe spline: a move starts when
  // the one before it lands, and takes its animation, the preview and the dwell.
  const std::size_t ply = session_->game().moveHistory().size();
  if (ply != lastPly_) {
    lastPly_ = ply;
    moveElapsed_ = 0.0f;
    const float dwell = step.dwell >= 0.0f ? step.dwell : settings_.watchDwell;
    moveDuration_ = std::max(
        0.25f, session_->animation().duration() + dwell + settings_.trailerMovePreview);
    if (seekPhase_ >= 0.0f) {  // a seek into the middle of a move
      moveElapsed_ = seekPhase_ * moveDuration_;
      seekPhase_ = -1.0f;
    }
  } else {
    moveElapsed_ += dt;
  }
}

CameraCue Shell::currentCue() const {
  if (session_ == nullptr) return {};
  // Inside the trailer a cue also names its beat (`klein` plays twice), and is never the
  // same cue as a game the player started themselves.
  if (attracting()) {
    // A point on the trailer timeline, to the nearest eighth of a move: exactly where
    // edit mode sits, and where playback is right now.
    const TrailerCue at = trailerCueAt(trailerTime(), 8);
    return {currentVariant_, session_->game().moveHistory().size(), attractIndex_ + 1,
            trailerEditing_ ? editSub_ : at.sub};
  }
  return {currentVariant_, session_->game().moveHistory().size(), 0, 0};
}

std::filesystem::path Shell::cameraTrackPath() const {
  return settingsPath_.parent_path() / "camera_track.txt";
}

void Shell::persistCameraTrack() const {
  std::ofstream out(cameraTrackPath(), std::ios::trunc);
  if (!out) return;  // a camera track is a convenience, not something play should halt on
  out << cameraTrackText(cameraTrack_);
}

void Shell::recordCameraKeyframe(float speed) {
  if (session_ == nullptr) return;
  CameraKeyframe kf;
  kf.cue = currentCue();
  kf.camera = session_->camera();
  kf.camera.target = kf.camera.target - shapeCentre_ + kBoardCentre;
  if (attracting()) kf.camera.yaw -= flatBoardYaw();
  // In edit mode the pose eases toward the edit buttons' targets; the keyframe records
  // the targets, not wherever the ease happens to be this frame.
  const bool fromEdit = attracting() && trailerEditing_;
  kf.formed = fromEdit ? editPose_.formed : settings_.geometryFormed;
  kf.ghost = fromEdit ? editPose_.ghost : settings_.geometryGhost;
  kf.shellSpread = fromEdit ? editPose_.shellSpread : settings_.geometryShellSpread;
  kf.slideU = settings_.geometrySlideU;
  kf.slideV = settings_.geometrySlideV;
  kf.flatPieces = settings_.shapePieceCylinders;
  kf.invert = settings_.geometryInvert;
  kf.morphSpeed = speed;
  if (attracting()) {
    // A keyframe only plays in the authored mode, so recording one means choosing it.
    trailerCamera_ = TrailerCamera::Authored;
    applyTrailerCamera();
  }
  cameraTrack_.set(std::move(kf));
  persistCameraTrack();
}

bool Shell::eraseCameraKeyframe() {
  if (session_ == nullptr) return false;
  bool removed = cameraTrack_.erase(currentCue());
  if (!removed && attracting()) {
    // While playing the playhead is rarely exactly on a keyframe: take the nearest one
    // within a sixteenth of a move.
    const float g = trailerTime();
    const CameraKeyframe* nearest = nullptr;
    float best = 1.0f / 16.0f + 1e-4f;
    for (const auto& [time, kf] : trailerKeys()) {
      if (std::abs(time - g) <= best) {
        best = std::abs(time - g);
        nearest = kf;
      }
    }
    if (nearest != nullptr) removed = cameraTrack_.erase(nearest->cue);
  }
  if (removed) persistCameraTrack();
  return removed;
}

void Shell::advanceCameraTrack(float dt) {
  if (session_ == nullptr || cameraTrack_.empty()) return;
  // In the trailer only the authored camera plays keyframes; Auto and Follow own the
  // view.
  if (attracting() && trailerCamera_ != TrailerCamera::Authored) return;
  if (attracting()) {
    // The trailer's keyframes are one smooth spline over the whole timeline, not
    // waypoints to ease toward; in edit mode the camera is the author's to move.
    if (!trailerEditing_) applySplineAt(trailerTime(), false);
    return;
  }
  const CameraCue cue = currentCue();
  if (!cameraTrackCue_.has_value() || !(*cameraTrackCue_ == cue)) {
    cameraTrackCue_ = cue;
    if (cameraTrack_.find(cue) != nullptr) {
      // A new cue with a keyframe: capture the view exactly as it stands right now as the
      // blend's own start, so the ease is from wherever play actually is, not from the
      // *previous* keyframe's own endpoint (the player may have kept orbiting since).
      cameraTrackFrom_ = session_->camera();
      cameraTrackFromFormed_ = settings_.geometryFormed;
      cameraTrackFromGhost_ = settings_.geometryGhost;
      cameraTrackFromShellSpread_ = settings_.geometryShellSpread;
      cameraTrackFromSlideU_ = settings_.geometrySlideU;
      cameraTrackFromSlideV_ = settings_.geometrySlideV;
      cameraTrackT_ = 0.0f;
      const CameraKeyframe* reached = cameraTrack_.find(cue);
      settings_.geometryInvert = reached->invert;
    } else {
      cameraTrackT_ = 1.0f;  // nothing to ease toward at this cue - leave the view alone
    }
  }
  if (cameraTrackT_ >= 1.0f) return;
  const CameraKeyframe* kf = cameraTrack_.find(cue);
  if (kf == nullptr) {  // the keyframe was erased mid-blend
    cameraTrackT_ = 1.0f;
    return;
  }
  cameraTrackT_ = std::min(1.0f, cameraTrackT_ + dt * std::max(0.01f, kf->morphSpeed));
  const float t = cameraTrackT_;
  session_->setOrbitCamera(view::slerpCamera(cameraTrackFrom_, kf->camera, t));
  settings_.geometryFormed =
      cameraTrackFromFormed_ + (kf->formed - cameraTrackFromFormed_) * t;
  settings_.geometryGhost =
      cameraTrackFromGhost_ + (kf->ghost - cameraTrackFromGhost_) * t;
  settings_.geometryShellSpread =
      cameraTrackFromShellSpread_ + (kf->shellSpread - cameraTrackFromShellSpread_) * t;
  settings_.geometrySlideU =
      cameraTrackFromSlideU_ + (kf->slideU - cameraTrackFromSlideU_) * t;
  settings_.geometrySlideV =
      cameraTrackFromSlideV_ + (kf->slideV - cameraTrackFromSlideV_) * t;
  settings_.shapePieceCylinders = kf->flatPieces;
}

float Shell::beatOffset(std::size_t beat) const {
  // A beat spans plies 0..count, so count + 1 units of trailer time.
  float offset = 0.0f;
  for (std::size_t i = 0; i < beat && i < attract_.size(); ++i) {
    offset += static_cast<float>(attract_[i].moves.size() + 1);
  }
  return offset;
}

bool Shell::trailerHasKeys() const noexcept {
  for (const CameraKeyframe& kf : cameraTrack_.keyframes()) {
    const std::size_t beat = kf.cue.beat;
    if (beat >= 1 && beat <= attract_.size() &&
        attract_[beat - 1].variant == kf.cue.variant) {
      return true;
    }
  }
  return false;
}

bool Shell::trailerOwnsCamera() const noexcept {
  if (!attracting()) return false;
  return trailerEditing_ ||
         (trailerCamera_ == TrailerCamera::Authored && trailerHasKeys());
}

float Shell::trailerLength() const {
  return beatOffset(attract_.size());
}

TrailerCue Shell::trailerCueAt(float g, int grid) const {
  if (attract_.empty()) return {};
  g = std::clamp(g, 0.0f, std::max(0.0f, trailerLength() - 1e-3f));
  std::size_t beat = 0;
  while (beat + 1 < attract_.size() && beatOffset(beat + 1) <= g) ++beat;
  const float local = g - beatOffset(beat);
  const std::size_t count = attract_[beat].moves.size();
  std::size_t ply = std::min(static_cast<std::size_t>(local), count);
  const float frac = local - static_cast<float>(ply);
  const int useGrid =
      (grid == 1 || grid == 2 || grid == 4 || grid == 8) ? grid : trailerGrid_;
  const float unit = 8.0f / static_cast<float>(useGrid);
  std::size_t sub = static_cast<std::size_t>(
      static_cast<float>(std::lround(frac * 8.0f / unit)) * unit);
  if (sub >= 8) {
    sub = 0;
    if (ply < count) ++ply;
  }
  return {beat, ply, sub};
}

std::vector<float> Shell::trailerKeyTimes() const {
  std::vector<float> times;
  for (const auto& [time, kf] : trailerKeys()) times.push_back(time);
  return times;
}

float Shell::trailerTime() const {
  if (!attracting() || session_ == nullptr) return 0.0f;
  const float frac = trailerEditing_ ? static_cast<float>(editSub_) / 8.0f
                                     : std::min(0.999f, moveElapsed_ / moveDuration_);
  return beatOffset(attractIndex_) +
         static_cast<float>(session_->game().moveHistory().size()) + frac + trailerShift_;
}

std::vector<std::pair<float, const CameraKeyframe*>> Shell::trailerKeys() const {
  std::vector<std::pair<float, const CameraKeyframe*>> keys;
  for (const CameraKeyframe& kf : cameraTrack_.keyframes()) {
    const std::size_t beat = kf.cue.beat;
    if (beat < 1 || beat > attract_.size() ||
        attract_[beat - 1].variant != kf.cue.variant) {
      continue;
    }
    keys.emplace_back(beatOffset(beat - 1) + static_cast<float>(kf.cue.ply) +
                          static_cast<float>(kf.cue.sub) / 8.0f,
                      &kf);
  }
  std::sort(keys.begin(), keys.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return keys;
}

bool Shell::splinePoseActive() const {
  if (!attracting() || trailerCamera_ != TrailerCamera::Authored) return false;
  return poseKeysFrom(attractIndex_) <= trailerTime() - trailerShift_;
}

float Shell::poseKeysFrom(std::size_t beat) const {
  // The time of the first keyframe of `beat`, or infinity when it has none. Before it the
  // beat's own morph plays (the cylinder rolling up, the torus bending ...); from it on
  // the keyframes own the pose. Without this a keyframe's morph value, held backwards,
  // would erase every beat's reveal.
  float first = std::numeric_limits<float>::infinity();
  for (const auto& [time, kf] : trailerKeys()) {
    if (kf->cue.beat == beat + 1) first = std::min(first, time);
  }
  return first;
}

void Shell::applySplineAt(float g, bool toEditPose) {
  const auto keys = trailerKeys();
  if (keys.empty() || session_ == nullptr) return;
  std::vector<view::CameraWaypoint> path;
  std::vector<std::pair<float, float>> formed, ghost, spread, slideU, slideV;
  const CameraKeyframe* discrete = keys.front().second;
  for (const auto& [time, kf] : keys) {
    path.push_back({time, kf->camera});
    formed.emplace_back(time, kf->formed);
    ghost.emplace_back(time, kf->ghost);
    spread.emplace_back(time, kf->shellSpread);
    slideU.emplace_back(time, kf->slideU);
    slideV.emplace_back(time, kf->slideV);
    if (time <= g) discrete = kf;  // flat pieces and invert step at their keyframe
  }
  view::OrbitCamera cam = view::splineCamera(path, g);
  cam.target = cam.target - kBoardCentre + shapeCentre_;
  cam.yaw += flatBoardYaw();
  session_->setOrbitCamera(cam);
  // The slide follows the spline all the way, never stepping in at the first keyframe; on
  // a beat that rises from the flat board it comes in with the roll (the flat board has
  // no slide to carry).
  float slideRamp = 1.0f;
  const float firstKey = poseKeysFrom(attractIndex_);
  if (attract_[attractIndex_].formedFrom == 0.0f && std::isfinite(firstKey)) {
    // A beat's first move starts at once, so its clock begins one unit in.
    const float start = beatOffset(attractIndex_) + 1.0f;
    const float t = std::clamp(
        (g - trailerShift_ - start) / std::max(0.25f, firstKey - start), 0.0f, 1.0f);
    slideRamp = t * t * (3.0f - 2.0f * t);  // smoothstep, arriving exactly at the key
  }
  // A slide only matters modulo one lap round the tube, so only the remainder is eased
  // in: easing the whole of a slide of several laps would spin the board round and round.
  const float lap = static_cast<float>(session_->variant().dims.extent(0));
  const float rawU = view::splineScalar(slideU, g);
  // The laps are taken from the beat's first keyframe, once, so the remainder never flips
  // sign as the spline passes a half-lap.
  float anchorU = rawU;
  for (const auto& [time, kf] : keys) {
    if (kf->cue.beat == attractIndex_ + 1) {
      anchorU = kf->slideU;
      break;
    }
  }
  const float wholeLaps = lap > 0.0f ? std::round(anchorU / lap) * lap : 0.0f;
  // Whole laps are the same picture only once the tube has closed; on the rolling sheet
  // they would be a translation.
  const bool closed =
      attract_[attractIndex_].formedFrom != 0.0f || settings_.geometryFormed >= 0.9999f;
  settings_.geometrySlideU = (rawU - wholeLaps) * slideRamp + (closed ? wholeLaps : 0.0f);
  settings_.geometrySlideV = view::splineScalar(slideV, g) * slideRamp;
  // The rest of the pose (morph, ghost, piece style, invert) is only the keyframes' once
  // this beat's first keyframe is reached; before that the beat's own morph plays.
  if (poseKeysFrom(attractIndex_) > g - trailerShift_) return;
  const float f = std::clamp(view::splineScalar(formed, g), 0.0f, 1.0f);
  const float gh = std::clamp(view::splineScalar(ghost, g), 0.35f, 1.0f);
  const float sp = std::clamp(view::splineScalar(spread, g), 0.0f, 1.0f);
  if (toEditPose) {
    editPose_ = {f, gh, sp};
  } else {
    settings_.geometryFormed = f;
    settings_.geometryGhost = gh;
    settings_.geometryShellSpread = sp;
  }
  settings_.shapePieceCylinders = discrete->flatPieces;
  settings_.geometryInvert = discrete->invert;
}

float Shell::flatBoardYaw() const {
  // The shape renderer's flat board is the ordinary board turned a half turn about the
  // vertical, so a keyframe's yaw means the same picture on both only with this turn
  // added on the ordinary board's beat - which is what makes the cut into the cylinder
  // seamless.
  return attracting() && attractIndex_ < attract_.size() &&
                 attract_[attractIndex_].variant == "standard"
             ? 3.14159265f
             : 0.0f;
}

void Shell::setTrailerGrid(int grid) noexcept {
  if (grid == 1 || grid == 2 || grid == 4 || grid == 8) trailerGrid_ = grid;
}

bool Shell::trailerStepFraction(int dir) {
  if (!attracting() || !trailerEditing_ || session_ == nullptr || dir == 0) return false;
  const long units = 8 / trailerGrid_;
  const long ply = static_cast<long>(session_->game().moveHistory().size());
  const long total = ply * 8 + static_cast<long>(editSub_) + (dir > 0 ? units : -units);
  if (total < 0) return false;
  const long newPly = total / 8;
  if (newPly != ply && !trailerStep(static_cast<int>(newPly - ply))) return false;
  editSub_ = static_cast<std::size_t>(total % 8);
  applySplineAt(trailerTime(),
                true);  // preview what the spline says here, if there is one
  return true;
}

bool Shell::requestTrailerSeekKey(int dir) {
  if (!attracting() || dir == 0) return false;
  const float here = trailerTime();
  constexpr float kEps = 1.0f / 32.0f;  // closer than a sixteenth is "where we are"
  const float* found = nullptr;
  const std::vector<float> times = trailerKeyTimes();
  if (dir > 0) {
    for (const float& t : times) {
      if (t > here + kEps) {
        found = &t;
        break;
      }
    }
  } else {
    for (auto it = times.rbegin(); it != times.rend(); ++it) {
      if (*it < here - kEps) {
        found = &*it;
        break;
      }
    }
  }
  if (found == nullptr) return false;
  const TrailerCue cue = trailerCueAt(*found, 8);
  pendingSeek_ = cue;
  return true;
}

void Shell::toggleTrailerPause() {
  if (!attracting()) return;
  if (trailerEditing_) {
    setTrailerEditing(false);
    trailerPaused_ = false;
  } else {
    trailerPaused_ = !trailerPaused_;
  }
}

void Shell::trailerUserTookCamera() {
  if (!attracting() || trailerEditing_ || trailerCamera_ == TrailerCamera::Follow) return;
  setTrailerEditing(true);
}

void Shell::requestTrailerRestart() {
  if (attracting()) pendingRestart_ = true;
}

void Shell::pause() {
  if (session_ != nullptr) go(Screen::Paused);
}

std::string Shell::variantDescription(const std::string& name) const {
  // Asking the running session first saves reloading a file the game already holds.
  if (session_ != nullptr && currentVariant_ == name) {
    return session_->variant().description;
  }
  if (!loader_) return {};
  auto v = loader_(name);
  return v.has_value() ? v->description : std::string{};
}

const VariantSpec* Shell::preview(const std::string& name) const {
  if (name.empty()) return nullptr;
  // A running game already holds the spec, and it is the same one.
  if (session_ != nullptr && currentVariant_ == name) return &session_->variant();
  if (previewName_ == name) return preview_.get();
  if (!loader_) return nullptr;
  auto v = loader_(name);
  previewName_ = name;
  preview_ = v.has_value() ? std::make_unique<VariantSpec>(std::move(*v)) : nullptr;
  return preview_.get();
}

void Shell::resume() {
  if (session_ != nullptr) go(Screen::Game);
}

void Shell::applySettings() {
  settings_.sanitize();
  // The library overtures have their own clock, so their speed is applied to the player
  // rather than to the session - and it applies whether or not a game is loaded.
  overtures_.setSpeed(settings_.overtureSpeed);
  if (session_ != nullptr) {
    session_->setFlatView(settings_.flatView);
    session_->setHotSeat(settings_.hotSeat);
    session_->setEngineSide(settings_.vsComputer
                                ? std::optional<Color>(settings_.computerPlaysWhite
                                                           ? Color::White
                                                           : Color::Black)
                                : std::nullopt);
    session_->setEngineLevel(settings_.computerLevel);
    session_->setConfirmMoves(settings_.confirmMoves);
    session_->setCameraMode(settings_.cameraMode);
    session_->setFollowStrength(settings_.followStrength);
    session_->setTheme(view::themeFromName(settings_.theme));
    // Seconds per cell travelled. Nought-point-nought-eight-five at speed 1 is about
    // as fast as a move can be read; switching animation off means zero, not "instant",
    // so the session has one rule for "do not animate" rather than two.
    // The trailer has its own multiplier on top, so tuning it leaves the player's game
    // be.
    const float moveSpeed =
        settings_.animationSpeed * (attracting() ? settings_.trailerMoveSpeed : 1.0f);
    session_->setAnimationSeconds(settings_.animateMoves ? 0.085f / moveSpeed : 0.0f);
    Action a;
    a.kind = ActionKind::SetPromotion;
    a.text = settings_.autoPromoteTo;
    // An empty preference means "always ask", which the session expresses by having no
    // preference at all rather than by a sentinel piece.
    if (!settings_.autoPromoteTo.empty()) (void)session_->apply(a);
    // The lines above hand the session the player's camera mode; during the trailer the
    // trailer's own camera policy wins, or any settings change (a button in the trailer
    // panel is one) would turn the chase back on.
    if (attracting()) applyTrailerCamera();
  }
  if (auto ok = settings_.save(settingsPath_); !ok.has_value()) {
    message_ = ok.error().message;
  }
}

}  // namespace cb::app
