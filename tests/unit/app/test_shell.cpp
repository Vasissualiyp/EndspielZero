// SPDX-License-Identifier: GPL-3.0-or-later
// The application around the game: which screens a player can reach, and what happens
// to a game while they are elsewhere. Tested without a window, because these are the
// transitions that break quietly - resuming with nothing loaded, losing a position by
// opening the settings, a menu offering something that does not exist.
#include "app/shell.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

#include "io/game_file.hpp"
#include "space/coord.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::unique_ptr<Shell> makeShell(const std::filesystem::path& settings = {}) {
  auto shell = Shell::create({"standard", "klein", "cube5"}, settings);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  // These tests are about what happens after a game is started, not the first-run
  // sequence (M14.1), which has its own test below. Dismiss it so the shell opens on the
  // main menu as it did before.
  shell->dismissWelcome();
  return shell;
}

std::filesystem::path tempSettings(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "endspielzero-tests";
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::filesystem::remove(path);
  return path;
}

}  // namespace

TEST_CASE("the shipped variants advertise their difficulty", "[unit][app]") {
  // The level lives in each variant's own file, so changing it is a data edit. These
  // are the five levels the library orders and colours by.
  REQUIRE(test::loadVariant("standard").difficulty == Difficulty::Easy);
  REQUIRE(test::loadVariant("atomic").difficulty == Difficulty::Easy);
  REQUIRE(test::loadVariant("mirrorbox").difficulty == Difficulty::Easy);
  REQUIRE(test::loadVariant("mustcapture").difficulty == Difficulty::Easy);
  REQUIRE(test::loadVariant("cylinder").difficulty == Difficulty::Easy);

  REQUIRE(test::loadVariant("torus").difficulty == Difficulty::Medium);
  REQUIRE(test::loadVariant("mobius").difficulty == Difficulty::Medium);
  REQUIRE(test::loadVariant("klein").difficulty == Difficulty::Medium);
  REQUIRE(test::loadVariant("cube5").difficulty == Difficulty::Medium);
  REQUIRE(test::loadVariant("atomic_torus").difficulty == Difficulty::Medium);

  REQUIRE(test::loadVariant("5d").difficulty == Difficulty::Hard);
  REQUIRE(test::loadVariant("torus3d").difficulty == Difficulty::Hard);
  REQUIRE(test::loadVariant("hyper4").difficulty == Difficulty::Hard);

  REQUIRE(test::loadVariant("t6").difficulty == Difficulty::Impossible);
  REQUIRE(test::loadVariant("charged").difficulty == Difficulty::Other);
}

TEST_CASE("the library is ordered by difficulty, then alphabetically", "[unit][app]") {
  auto shell =
      Shell::create({"t6", "standard", "klein", "charged", "hyper4", "atomic", "torus"},
                    tempSettings("order.conf"));
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  const std::vector<std::string> expected{"atomic", "standard", "klein",  "torus",
                                          "hyper4", "t6",       "charged"};
  REQUIRE(shell->library() == expected);
}

TEST_CASE("a variant this build cannot load is library-ordered as other", "[unit][app]") {
  // A Workshop variant, later, or simply a broken file: it must still draw in the
  // library, at the end with the unknowns, rather than vanish or crash the picker.
  auto shell = Shell::create({"hyper4", "mystery"}, tempSettings("unknown.conf"));
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  REQUIRE(shell->difficultyOf("mystery") == Difficulty::Other);
  REQUIRE(shell->library() == std::vector<std::string>{"hyper4", "mystery"});
}

TEST_CASE("the game opens on the main menu with nothing loaded", "[unit][app]") {
  auto shell = makeShell(tempSettings("open.conf"));
  REQUIRE(shell->screen() == Screen::MainMenu);
  REQUIRE_FALSE(shell->hasGame());
  REQUIRE_FALSE(shell->showsBoard());
  REQUIRE_FALSE(shell->quitRequested());
}

TEST_CASE("the game opens on the main menu, and the tutorial is on demand",
          "[unit][app]") {
  // M14.1, revised: the curated first-run sequence no longer opens by itself. The game
  // opens on its menu, and the Tutorial row summons the sequence.
  const auto path = tempSettings("welcome.conf");
  auto shell = Shell::create({"standard", "torus"}, path);
  REQUIRE(shell->screen() == Screen::MainMenu);
  REQUIRE_FALSE(shell->hasGame());
  REQUIRE_FALSE(shell->showsBoard());

  shell->go(Screen::Welcome);
  REQUIRE(shell->screen() == Screen::Welcome);
  shell->back();
  REQUIRE(shell->screen() == Screen::MainMenu);

  // A later launch opens on the menu too, however many times the tutorial has been seen.
  auto again = Shell::create({"standard", "torus"}, path);
  REQUIRE(again->screen() == Screen::MainMenu);
}

TEST_CASE("the curated path starts at standard and explains each step", "[unit][app]") {
  // M14.2: a front door from the board a newcomer knows to the one they cannot picture.
  const std::vector<CuratedVariant>& path = curatedVariants();
  REQUIRE_FALSE(path.empty());
  REQUIRE(path.front().name == "standard");
  for (const CuratedVariant& c : path) {
    CHECK_FALSE(c.name.empty());
    CHECK_FALSE(c.what.empty());
  }
}

TEST_CASE("surprise me picks a strange board, not the classic", "[unit][app]") {
  auto shell = makeShell(tempSettings("surprise.conf"));
  const std::string pick = shell->surpriseVariant();
  CHECK(pick != "standard");
  const auto& lib = shell->library();
  CHECK(std::find(lib.begin(), lib.end(), pick) != lib.end());
}

TEST_CASE("starting the same variant again begins a fresh game", "[unit][app]") {
  // Leaving a game and picking the same variant from the library is "start over", not a
  // no-op: the main loop used to skip the request when the name matched the running game.
  auto shell = makeShell(tempSettings("restart.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  REQUIRE(shell->session()->applyScript("click e2\nclick e4").has_value());
  REQUIRE(shell->session()->game().plyCount() == 1);

  shell->go(Screen::MainMenu);
  REQUIRE(shell->startGame("standard").has_value());
  REQUIRE(shell->screen() == Screen::Game);
  REQUIRE(shell->session()->game().plyCount() == 0);  // a new game, not the old one
}

TEST_CASE("starting a game puts you in it", "[unit][app]") {
  auto shell = makeShell(tempSettings("start.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  REQUIRE(shell->screen() == Screen::Game);
  REQUIRE(shell->hasGame());
  REQUIRE(shell->showsBoard());
  REQUIRE(shell->currentVariant() == "standard");
  REQUIRE(shell->session()->game().legalMoves().size() == 20);
}

TEST_CASE("a variant that will not load leaves the shell usable", "[unit][app]") {
  auto shell = makeShell(tempSettings("bad.conf"));
  REQUIRE_FALSE(shell->startGame("no-such-variant").has_value());
  REQUIRE(shell->screen() == Screen::MainMenu);
  REQUIRE_FALSE(shell->hasGame());
  REQUIRE_FALSE(shell->message().empty());  // and it says why
}

TEST_CASE("the picker reads a variant's own description", "[unit][app]") {
  // The picker knows only names, so the shell is what turns one into the line the
  // variant file carries. A name that will not load yields nothing rather than an
  // invented sentence.
  auto shell = makeShell(tempSettings("describe.conf"));
  REQUIRE_FALSE(shell->variantDescription("klein").empty());
  REQUIRE(shell->variantDescription("no-such-variant").empty());
}

TEST_CASE("quitting asks first, and backing out is free", "[unit][app]") {
  // Quit is a screen of its own, one step in from the menu, not an immediate exit: the
  // shell only reports quit when the confirmation is accepted.
  auto shell = makeShell(tempSettings("quit.conf"));
  REQUIRE(screenDepth(Screen::QuitConfirm) == 1);
  shell->go(Screen::QuitConfirm);
  REQUIRE(shell->screen() == Screen::QuitConfirm);
  REQUIRE_FALSE(shell->quitRequested());
  shell->back();
  REQUIRE(shell->screen() == Screen::MainMenu);
  REQUIRE_FALSE(shell->quitRequested());

  shell->go(Screen::QuitConfirm);
  shell->requestQuit();
  REQUIRE(shell->quitRequested());
}

TEST_CASE("pausing keeps the game and the board", "[unit][app]") {
  // The pause screen sits over the position: losing sight of it while paused would make
  // "game mode" and "piece moves" much less useful than they should be.
  auto shell = makeShell(tempSettings("pause.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  REQUIRE(shell->session()->applyScript("click e2\nclick e4").has_value());
  REQUIRE(shell->showsBoard());

  shell->pause();
  REQUIRE(shell->screen() == Screen::Paused);
  REQUIRE(shell->showsBoard());
  REQUIRE(shell->session()->game().plyCount() == 1);

  shell->go(Screen::PieceMoves);
  REQUIRE(shell->showsBoard());
  shell->back();
  REQUIRE(shell->screen() == Screen::Paused);

  shell->resume();
  REQUIRE(shell->screen() == Screen::Game);
  REQUIRE(shell->showsBoard());
  REQUIRE(shell->session()->game().plyCount() == 1);  // the position survived
}

TEST_CASE("the check warning follows the board, and leaves with it", "[unit][app]") {
  // The red vignette warns about the position in front of you. A game abandoned to the
  // main menu - still checked, its session kept for a later resume - must not keep the
  // screen red: the warning has to follow the board, not the loaded game.
  auto shell = makeShell(tempSettings("checkwarn.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  REQUIRE_FALSE(shell->showsCheckWarning());  // the board is up, the king is safe

  // Fool's mate: the side to move is left in check.
  REQUIRE(shell->session()
              ->applyScript("click f2\nclick f3\nclick e7\nclick e5\n"
                            "click g2\nclick g4\nclick d8\nclick h4")
              .has_value());
  REQUIRE(shell->session()->game().inCheck());
  REQUIRE(shell->showsCheckWarning());

  // The pause screen sits over the board, so the warning is still its business.
  shell->pause();
  REQUIRE(shell->showsCheckWarning());

  // Leaving for the menu abandons the game; the board, and its warning, stay behind.
  shell->go(Screen::MainMenu);
  REQUIRE(shell->hasGame());  // the session is kept, for a later Continue
  REQUIRE_FALSE(shell->showsCheckWarning());
}

TEST_CASE("the board recedes a step at each pause level", "[unit][app]") {
  // The main loop animates the camera toward this: 0 on the board, 1 at pause, 2 one
  // step further in at a pause panel.
  auto shell = makeShell(tempSettings("pullback.conf"));
  REQUIRE(shell->boardPullBack() == 0.0f);  // no game loaded
  REQUIRE(shell->startGame("standard").has_value());
  REQUIRE(shell->boardPullBack() == 0.0f);  // on the board

  shell->pause();
  REQUIRE(shell->boardPullBack() == 1.0f);
  shell->go(Screen::GameInfo);
  REQUIRE(shell->boardPullBack() == 2.0f);
  shell->back();
  REQUIRE(shell->screen() == Screen::Paused);
  REQUIRE(shell->boardPullBack() == 1.0f);  // back to pause
  shell->go(Screen::PieceMoves);
  REQUIRE(shell->boardPullBack() == 2.0f);
  shell->back();
  REQUIRE(shell->boardPullBack() == 1.0f);
  shell->resume();
  REQUIRE(shell->boardPullBack() == 0.0f);  // back on the board
}

TEST_CASE("quitting from the pause menu uses its own prompt", "[unit][app]") {
  auto shell = makeShell(tempSettings("pausequit.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  shell->pause();
  shell->go(Screen::PauseQuitConfirm);
  REQUIRE(shell->screen() == Screen::PauseQuitConfirm);
  REQUIRE_FALSE(shell->quitRequested());
  shell->back();
  REQUIRE(shell->screen() == Screen::Paused);
  REQUIRE(shell->hasGame());
}

TEST_CASE("pausing with no game loaded does nothing", "[unit][app]") {
  auto shell = makeShell(tempSettings("nopause.conf"));
  shell->pause();
  REQUIRE(shell->screen() == Screen::MainMenu);
  shell->resume();
  REQUIRE(shell->screen() == Screen::MainMenu);
}

TEST_CASE("settings return you where you came from", "[unit][app]") {
  auto shell = makeShell(tempSettings("return.conf"));

  SECTION("from the main menu") {
    shell->go(Screen::Settings);
    REQUIRE_FALSE(shell->showsBoard());
    shell->back();
    REQUIRE(shell->screen() == Screen::MainMenu);
  }

  SECTION("from a paused game") {
    REQUIRE(shell->startGame("standard").has_value());
    shell->pause();
    shell->go(Screen::Settings);
    // Opened over a game, the board stays visible behind the panel.
    REQUIRE(shell->showsBoard());
    shell->back();
    REQUIRE(shell->screen() == Screen::Paused);
    REQUIRE(shell->hasGame());
  }
}

TEST_CASE("settings survive a restart", "[unit][app]") {
  const auto path = tempSettings("roundtrip.conf");
  {
    auto shell = makeShell(path);
    shell->settings().guiScale = 1.6f;
    shell->settings().devOverlay = true;
    shell->settings().postProcess = false;
    shell->settings().bloomScale = .5f;
    shell->settings().showSeams = false;
    shell->settings().invertOrbitY = true;
    shell->settings().autoPromoteTo = "knight";
    shell->settings().computerLevel = 5;
    shell->settings().volumeMusic = 0.25f;
    shell->settings().overtureSpeed = 1.75f;
    shell->settings().cameraMode = "route";
    shell->settings().followStrength = 0.4f;
    shell->settings().shapeFollow = "chase";
    shell->settings().followElevationDeg = 45.0f;
    shell->settings().kleinTwist = 3.0f;
    shell->settings().geometryWidth = 1.5f;
    shell->settings().geometryThickness = 1.25f;
    shell->settings().kleinShift = 3.0f;
    shell->settings().geometryFormSpeed = 2.5f;
    shell->settings().shapePieceCylinders = true;
    shell->settings().frameCap = 60;
    shell->settings().nearClip = 1.25f;
    shell->settings().trailerNearClip = 3.5f;
    shell->settings().peekMovesOnCtrl = false;
    shell->settings().showAttackHints = false;
    REQUIRE(shell->startGame("klein").has_value());  // also records the last variant
    shell->applySettings();
    // The session takes the level from the setting, for its next move.
    REQUIRE(shell->session()->engineOptions().level == 5);
  }
  {
    auto shell = makeShell(path);
    REQUIRE(shell->settings().guiScale == 1.6f);
    REQUIRE(shell->settings().devOverlay);
    REQUIRE_FALSE(shell->settings().postProcess);
    REQUIRE(shell->settings().bloomScale == .5f);
    REQUIRE_FALSE(shell->settings().showSeams);
    REQUIRE(shell->settings().invertOrbitY);
    REQUIRE(shell->settings().autoPromoteTo == "knight");
    REQUIRE(shell->settings().computerLevel == 5);
    REQUIRE(shell->settings().volumeMusic == 0.25f);
    REQUIRE(shell->currentVariant() == "klein");
    // The library overture's speed is a setting, and it reaches its player on apply.
    REQUIRE(shell->settings().overtureSpeed == 1.75f);
    REQUIRE(shell->overtures().speed() == 1.75f);
    REQUIRE(shell->settings().cameraMode == "route");
    REQUIRE(shell->settings().followStrength == 0.4f);
    REQUIRE(shell->settings().shapeFollow == "chase");
    REQUIRE(shell->settings().followElevationDeg == 45.0f);
    REQUIRE(shell->settings().kleinTwist == 3.0f);
    REQUIRE(shell->settings().geometryWidth == 1.5f);
    REQUIRE(shell->settings().geometryThickness == 1.25f);
    REQUIRE(shell->settings().kleinShift == 3.0f);
    REQUIRE(shell->settings().geometryFormSpeed == 2.5f);
    // The shape view's piece style round-trips with the other geometry settings (M17.25).
    REQUIRE(shell->settings().shapePieceCylinders);
    REQUIRE(shell->settings().frameCap == 60);
    REQUIRE(shell->settings().nearClip == 1.25f);
    REQUIRE(shell->settings().trailerNearClip == 3.5f);
    REQUIRE_FALSE(shell->settings().peekMovesOnCtrl);
    REQUIRE_FALSE(shell->settings().showAttackHints);
  }
}

TEST_CASE("a broken settings file does not stop the game starting", "[unit][app]") {
  // The file is hand-editable and may come from a newer build, so anything unparseable
  // has to degrade to a default rather than take the game down with it.
  const auto path = tempSettings("broken.conf");
  {
    std::ofstream out(path);
    out << "this line has no equals sign\n";
    out << "gui_scale = not-a-number\n";
    out << "gui_scale = 1.25\n";
    out << "unknown_future_key = 7\n";
    out << "# a comment\n";
    out << "show_seams = false   # trailing comment\n";
  }
  const Settings s = Settings::load(path);
  REQUIRE(s.guiScale == 1.25f);
  REQUIRE_FALSE(s.showSeams);
  REQUIRE(s.showLegalMoves);  // untouched keys keep their defaults
}

TEST_CASE("settings are clamped to usable values", "[unit][app]") {
  Settings s;
  s.guiScale = 99.0f;
  s.orbitSensitivity = -4.0f;
  s.volumeMaster = 5.0f;
  s.shapeFollow = "sideways";
  s.followElevationDeg = 200.0f;
  s.kleinTwist = 99.0f;
  s.geometryWidth = 99.0f;
  s.geometryThickness = 99.0f;
  s.kleinShift = 99.0f;
  s.geometryFormSpeed = 0.0f;
  s.animationSpeed = 0.001f;
  s.lastVariant.clear();
  s.computerLevel = 42;
  s.sanitize();
  REQUIRE(s.computerLevel == 5);
  s.computerLevel = -1;
  s.sanitize();
  REQUIRE(s.computerLevel == 1);
  REQUIRE(s.guiScale <= 3.0f);
  REQUIRE(s.orbitSensitivity >= 0.1f);
  REQUIRE(s.volumeMaster <= 1.0f);
  REQUIRE(s.followElevationDeg <= 80.0f);
  REQUIRE(s.kleinTwist <= 6.0f);
  REQUIRE(s.geometryWidth <= 4.0f);
  REQUIRE(s.geometryThickness <= 3.0f);
  REQUIRE(s.kleinShift <= 7.0f);
  // A zero would freeze the morph mid-roll with no way back, so it clamps up (M18.6).
  REQUIRE(s.geometryFormSpeed >= 0.25f);
  REQUIRE(s.animationSpeed >= 0.05f);
  REQUIRE(s.shapeFollow == "chase");
  REQUIRE_FALSE(s.lastVariant.empty());
}

TEST_CASE("the Klein square shift can be negative, within its own range", "[unit][app]") {
  // Signed, so a counter-clockwise square is set directly (e.g. -1) rather than winding
  // the long way round through the positive values (+7 on an 8-wide board) to land on
  // the same square - a debug tuning knob's own ergonomics, not a shipped control.
  Settings over;
  over.kleinShift = -99.0f;
  over.sanitize();
  REQUIRE(over.kleinShift >= -7.0f);

  Settings fine;
  fine.kleinShift = -3.0f;
  fine.sanitize();
  REQUIRE(fine.kleinShift == -3.0f);
}

TEST_CASE("a missing settings file means defaults, not an error", "[unit][app]") {
  const Settings s = Settings::load("/nonexistent/path/settings.conf");
  REQUIRE(s.guiScale == 1.0f);
  REQUIRE(s.lastVariant == "standard");
  // The shape view keeps its sculptural pieces until the player asks for the pucks
  // (M17.25): the default must move nothing.
  REQUIRE_FALSE(s.shapePieceCylinders);
}

TEST_CASE("the editor opens the current variant and saves it back", "[unit][app]") {
  auto shell = makeShell(tempSettings("editor.conf"));
  shell->setVariantSourceLoader([](const std::string& name) -> Result<std::string> {
    std::ifstream in(test::variantPath(name));
    if (!in) return fail(ErrorCode::ParseError, "cannot open '" + name + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  });
  const auto out =
      std::filesystem::temp_directory_path() / "endspielzero-shell-editor.toml";
  shell->setVariantPathResolver([&](const std::string&) { return out; });

  // Opening the editor screen with nothing loaded edits the current variant (standard,
  // since nothing was started), and opens its source.
  shell->go(Screen::Editor);
  REQUIRE(shell->editor() != nullptr);
  CHECK(shell->editorVariant() == "standard");
  CHECK_FALSE(shell->editor()->dirty());

  auto atoms = shell->editor()->pieceAtoms("rook");
  REQUIRE(atoms.has_value());
  auto edited = *atoms;
  MoveAtom leap;
  leap.mags.push(1);
  leap.mags.push(2);
  edited.push_back(*MoveAtom::canonicalize(leap));
  REQUIRE(shell->editor()->setPieceAtoms("rook", edited).has_value());
  CHECK(shell->editor()->dirty());

  REQUIRE(shell->saveEditor().has_value());
  CHECK_FALSE(shell->editor()->dirty());

  // What was saved is a real variant, and it is no longer the shipped standard.
  auto reloaded = loadVariantFile(out);
  REQUIRE(reloaded.has_value());
  auto original = loadVariantFile(test::variantPath("standard"));
  REQUIRE(original.has_value());
  CHECK(reloaded->variantId() != original->variantId());

  std::error_code ec;
  std::filesystem::remove(out, ec);
}

TEST_CASE("the editor will not save a document the loader would refuse", "[unit][app]") {
  auto shell = makeShell(tempSettings("editor-invalid.conf"));
  shell->setVariantSourceLoader([](const std::string& name) -> Result<std::string> {
    std::ifstream in(test::variantPath(name));
    if (!in) return fail(ErrorCode::ParseError, "cannot open '" + name + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  });
  const auto out = std::filesystem::temp_directory_path() / "chessbox-shell-invalid.toml";
  std::error_code ec;
  std::filesystem::remove(out, ec);
  shell->setVariantPathResolver([&](const std::string&) { return out; });
  shell->go(Screen::Editor);
  REQUIRE(shell->editor() != nullptr);

  // Removing the knight leaves the starting position naming a piece that is gone: the
  // loader's complaint, which is also the save's - there is no "save anyway".
  REQUIRE(shell->editor()->removePiece("knight").has_value());
  CHECK_FALSE(shell->editor()->validate().has_value());
  CHECK_FALSE(shell->saveEditor().has_value());
  CHECK_FALSE(std::filesystem::exists(out));
  CHECK(shell->editor()->dirty());

  shell->editor()->undo();
  CHECK(shell->saveEditor().has_value());
  CHECK(std::filesystem::exists(out));
  std::filesystem::remove(out, ec);
}

TEST_CASE("every exotic term the curated path shows is defined", "[unit][app]") {
  const auto& g = glossary();
  REQUIRE_FALSE(g.empty());

  std::vector<std::string_view> seen;
  for (const GlossaryEntry& e : g) {
    CHECK_FALSE(e.term.empty());
    CHECK_FALSE(e.definition.empty());
    CHECK(std::find(seen.begin(), seen.end(), e.term) == seen.end());
    seen.push_back(e.term);
    CHECK(glossaryFor(e.term) == e.definition);
  }
  // A term this game does not define is not accidentally defined.
  CHECK(glossaryFor("quintic").empty());
  CHECK(glossaryFor("").empty());

  // Acceptance fact 3: the exotic vocabulary a first-run player meets on the curated
  // path. Adding a caption that uses a new such term without defining it fails here.
  for (const char* term : {"torus", "cylinder", "Mobius band", "Klein bottle", "glued",
                           "seam", "dimension", "slice", "timeline", "time travel"}) {
    INFO(term);
    CHECK_FALSE(glossaryFor(term).empty());
  }
}

TEST_CASE("the curated captions show no undefined term", "[unit][app]") {
  // Acceptance fact 3's rot guard: a caption is walked word by word, and a word that is
  // neither plain-English scaffolding nor a glossary term fails here. A caption that
  // reaches for a new exotic word must define it first.
  const std::vector<std::string_view> kPlain{
      "a",       "and",      "axis",  "become",  "board", "boards", "can",   "close",
      "cube5",   "edge",     "edges", "file",    "five",  "from",   "grows", "h",
      "know",    "land",     "move",  "no",      "on",    "past",   "rank",  "same",
      "stacked", "standard", "the",   "then",    "third", "time",   "too",   "turn",
      "with",    "you",      "5d",    "already",
  };
  const auto plain = [&](std::string_view w) {
    return std::find(kPlain.begin(), kPlain.end(), w) != kPlain.end();
  };
  for (const CuratedVariant& c : curatedVariants()) {
    const std::string text = c.name + " " + c.what;
    std::string word;
    const auto check = [&] {
      if (word.empty()) return;
      INFO(text);
      INFO(word);
      CHECK((plain(word) || !glossaryFor(word).empty()));
      word.clear();
    };
    for (const char ch : text) {
      if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
        word.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      } else {
        check();
      }
    }
    check();
  }
}

TEST_CASE("a game is saved and reopened through the shell", "[unit][app]") {
  const auto settings = tempSettings("save-load.conf");
  auto shell = makeShell(settings);
  REQUIRE(shell->startGame("standard").has_value());
  app::Session& s = *shell->session();

  // Two legal clicks, whatever they are, so there is a position worth saving.
  for (int i = 0; i < 2; ++i) {
    REQUIRE_FALSE(s.game().legalMoves().empty());
    const Move m = s.game().legalMoves().front();
    app::Action a;
    a.kind = app::ActionKind::ClickCell;
    a.cell = m.from;
    REQUIRE(s.apply(a).has_value());
    a.cell = m.to;
    REQUIRE(s.apply(a).has_value());
  }
  const std::uint64_t hash = s.game().position().hash();
  const std::size_t plies = s.game().plyCount();

  REQUIRE(shell->saveGame("mygame").has_value());
  REQUIRE(shell->savedGames() == std::vector<std::string>{"mygame"});

  // Start something else, then reopen the saved game: the shell switches back to the
  // saved variant and reproduces the position.
  REQUIRE(shell->startGame("klein").has_value());
  REQUIRE(shell->loadGame("mygame").has_value());
  CHECK(shell->currentVariant() == "standard");
  CHECK(shell->screen() == Screen::Game);
  CHECK(shell->session()->game().position().hash() == hash);
  CHECK(shell->session()->game().plyCount() == plies);
}

TEST_CASE("save/load rejects a bad name and a missing game", "[unit][app]") {
  const auto settings = tempSettings("save-load-bad.conf");
  auto shell = makeShell(settings);
  REQUIRE(shell->startGame("standard").has_value());
  CHECK_FALSE(shell->saveGame("").has_value());
  CHECK_FALSE(shell->saveGame("../escape").has_value());
  CHECK_FALSE(shell->loadGame("does-not-exist").has_value());
}

namespace {

WatchGame watchGame(const char* name, const char* variant, const char* fen,
                    std::vector<std::string> moves) {
  WatchGame g;
  g.name = name;
  g.file.variant = variant;
  g.file.variantId = 0;
  g.file.startFen = fen;
  g.file.moves = std::move(moves);
  return g;
}

constexpr const char* kStandardFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr const char* kTorusFen = "8/8/8/rnbqkbnr/pppppppp/8/PPPPPPPP/RNBQKBNR w - - 0 1";

}  // namespace

TEST_CASE("the bundled games are listed and one can be watched", "[unit][app][watch]") {
  auto shell = makeShell(tempSettings("watch.conf"));
  shell->setBundledGames({
      watchGame("flat-demo", "standard", kStandardFen, {"e2e4", "e7e5"}),
      watchGame("glued-demo", "torus", kTorusFen, {"d1d5"}),
  });
  REQUIRE(shell->bundledGames().size() == 2);
  CHECK(shell->bundledGames().front().name == "flat-demo");
  CHECK(shell->bundledGames().back().file.variant == "torus");

  // Picking the glued game starts the torus and its first move, on the game screen.
  REQUIRE(shell->startWatching("glued-demo").has_value());
  REQUIRE(shell->watching());
  CHECK(shell->screen() == Screen::Game);
  CHECK(shell->currentVariant() == "torus");
  CHECK(shell->session()->game().moveHistory().size() == 1);

  // The stop control returns to the main menu and leaves the mode.
  shell->stopWatching();
  CHECK_FALSE(shell->watching());
  CHECK(shell->screen() == Screen::MainMenu);
}

TEST_CASE("the watch flips to the file's own variant", "[unit][app][watch]") {
  // A flat game after a glued one: the shell resolves the variant from the file, so the
  // starting position and the watched moves are the file's, not whatever was loaded.
  auto shell = makeShell(tempSettings("watch-switch.conf"));
  shell->setBundledGames({
      watchGame("flat-demo", "standard", kStandardFen, {"e2e4"}),
      watchGame("glued-demo", "torus", kTorusFen, {"d1d5"}),
  });
  REQUIRE(shell->startWatching("glued-demo").has_value());
  REQUIRE(shell->watching());
  REQUIRE(shell->startWatching("flat-demo").has_value());
  CHECK(shell->currentVariant() == "standard");
  CHECK(shell->session()->game().moveHistory().size() == 1);
  CHECK(shell->session()->game().moveHistory().front().from ==
        shell->session()->variant().dims.toCell(Coord::of({4, 1})));
}

TEST_CASE("a watch ends on its own and returns to the menu", "[unit][app][watch]") {
  auto shell = makeShell(tempSettings("watch-end.conf"));
  shell->setBundledGames(
      {watchGame("flat-demo", "standard", kStandardFen, {"e2e4", "e7e5"})});
  REQUIRE(shell->startWatching("flat-demo").has_value());
  // Drive the same clock the interactive loop does: the move body, then the dwell.
  for (int i = 0; i < 20000 && shell->watching(); ++i) {
    shell->session()->advanceAnimation(0.05f);
    shell->advanceWatching(0.05f);
  }
  CHECK_FALSE(shell->watching());
  CHECK(shell->screen() == Screen::MainMenu);
  CHECK(shell->session()->game().moveHistory().size() == 2);
}

TEST_CASE("watching an unknown bundled game is refused", "[unit][app][watch]") {
  auto shell = makeShell(tempSettings("watch-bad.conf"));
  shell->setBundledGames({watchGame("flat-demo", "standard", kStandardFen, {"e2e4"})});
  CHECK_FALSE(shell->startWatching("no-such-game").has_value());
  CHECK_FALSE(shell->watching());
}
