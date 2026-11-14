// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>

#include "app/debug_console.hpp"
#include "app/shell.hpp"
#include "io/lab_doc.hpp"
#include "io/variant_toml.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {
std::unique_ptr<Shell> makeShell() {
  auto shell = Shell::create({"standard"}, {});
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  shell->dismissWelcome();
  return shell;
}
bool has(const std::string& s, const char* t) {
  return s.find(t) != std::string::npos;
}
}  // namespace

TEST_CASE("seamhover pins a glued drawn axis and off clears it", "[unit][app][seams]") {
  auto shell = makeShell();
  DebugConsole console;
  CHECK(has(console.run(*shell, "seamhover file"), "no game is running"));
  REQUIRE(shell->startGame("torus").has_value());
  auto* session = shell->session();
  CHECK_FALSE(session->seamHoverPin().has_value());
  const auto legend =
      view::seamLegend(session->seams(), session->viewConfig(), session->variant().dims);
  REQUIRE(legend.size() == 2);
  for (const auto& entry : legend) {
    CHECK(console.run(*shell, "seamhover " + entry.axis) ==
          "highlighting " + entry.axis + " edges");
    CHECK(session->seamHoverPin() == entry.screenAxis);
  }
  CHECK(console.run(*shell, "seamhover level") ==
        "no glued axis named level; this board has: file, rank");
  CHECK(session->seamHoverPin() == legend[1].screenAxis);
  CHECK(console.run(*shell, "seamhover off") == "seam highlight off");
  CHECK_FALSE(session->seamHoverPin().has_value());
  CHECK(console.run(*shell, "seamhover") == "usage: seamhover <axis>|off");
  CHECK(console.run(*shell, "seamhover file extra") == "usage: seamhover <axis>|off");
  CHECK(has(DebugConsole::help(), "seamhover <axis>|off"));
  REQUIRE(shell->startGame("mirrorbox").has_value());
  CHECK(has(console.run(*shell, "seamhover file"), "no glued axis named file"));
  CHECK_FALSE(shell->session()->seamHoverPin().has_value());
}

TEST_CASE("developer presentation is opt-in and reachable without a game",
          "[unit][app][dev-overlay]") {
  auto shell = makeShell();
  DebugConsole console;
  CHECK_FALSE(shell->settings().devOverlay);
  CHECK(console.run(*shell, "get devOverlay") == "dev_overlay = false");
  CHECK(console.run(*shell, "dev on") == "dev overlay: on");
  CHECK(shell->settings().devOverlay);
  CHECK(console.run(*shell, "get dev_overlay") == "dev_overlay = true");
  CHECK(console.run(*shell, "dev off") == "dev overlay: off");
  CHECK_FALSE(shell->settings().devOverlay);
  CHECK(console.run(*shell, "dev") == "dev overlay: on");
  CHECK(console.run(*shell, "dev") == "dev overlay: off");
  CHECK(console.run(*shell, "set devOverlay true") == "devOverlay is now true");
  CHECK(shell->settings().devOverlay);
  CHECK(console.run(*shell, "dev maybe") == "usage: dev [on|off]");
  CHECK(console.run(*shell, "dev on extra") == "usage: dev [on|off]");
  CHECK(shell->settings().devOverlay);
  CHECK(console.run(*shell, "set dev_overlay false") == "dev_overlay is now false");
  CHECK_FALSE(shell->settings().devOverlay);
  CHECK(has(DebugConsole::help(), "dev [on|off]"));
}

TEST_CASE("the debug console reaches what the interface does not", "[unit][app][debug]") {
  auto shell = makeShell();
  DebugConsole c;
  CHECK(has(c.run(*shell, "moves"), "no game is running"));
  CHECK(has(c.run(*shell, "load standard"), "started"));
  CHECK(has(c.run(*shell, "moves"), "20 legal moves"));
  CHECK(c.run(*shell, "perft 3") == "8902");
  CHECK(has(c.run(*shell, "hash"), "ok"));
  CHECK(has(c.run(*shell, "play e2e4"), "played"));
  CHECK(has(c.run(*shell, "unmoved"), "unmoved"));
  CHECK(has(c.run(*shell, "attacked e4"), "attacked"));
  CHECK(has(c.run(*shell, "bogus"), "unknown command"));
  CHECK(has(c.run(*shell, "undo"), "ok"));
  CHECK(!c.log().empty());
  c.run(*shell, "clear");
  CHECK(c.log().empty());
}

TEST_CASE("console position edits invalidate presentation on the same session",
          "[unit][app][panel-response]") {
  auto shell = makeShell();
  REQUIRE(shell->startGame("standard").has_value());
  DebugConsole console;
  auto* session = shell->session();
  auto epoch = session->presentationEpoch();
  CHECK(console.run(*shell, "setfen " + console.run(*shell, "fen")) == "position set");
  CHECK(shell->session() == session);
  CHECK(session->presentationEpoch() != epoch);
  epoch = session->presentationEpoch();
  CHECK_FALSE(has(console.run(*shell, "remove a2"), "error"));
  CHECK(shell->session() == session);
  CHECK(session->presentationEpoch() != epoch);
}

TEST_CASE("post processing settings and aliases work before a game starts",
          "[unit][app][post]") {
  auto shell = makeShell();
  DebugConsole c;
  CHECK(shell->settings().postProcess);
  CHECK(shell->settings().bloomScale == 1);
  CHECK(c.run(*shell, "post off") == "post processing: off");
  CHECK_FALSE(shell->settings().postProcess);
  CHECK(c.run(*shell, "post") == "post processing: on");
  CHECK(c.run(*shell, "post on") == "post processing: on");
  CHECK(c.run(*shell, "get postProcess") == "post_process = true");
  CHECK(c.run(*shell, "set bloomScale 0.5") == "bloomScale is now 0.5");
  CHECK(shell->settings().bloomScale == .5f);
  CHECK(c.run(*shell, "get bloomScale") == "bloom_scale = 0.5");
  CHECK(c.run(*shell, "post yes") == "usage: post [on|off]");
  CHECK(c.run(*shell, "post off extra") == "usage: post [on|off]");
  CHECK(shell->settings().postProcess);
  CHECK(c.run(*shell, "set bloom_scale -1") == "bloom_scale is now -1");
  CHECK(shell->settings().bloomScale == 0);
  CHECK(c.run(*shell, "set bloomScale nan") == "bloomScale is now nan");
  CHECK(std::isfinite(shell->settings().bloomScale));
  CHECK(has(DebugConsole::help(), "post [on|off]"));
}

TEST_CASE("presentation intensity is a setting the console reaches and clamps",
          "[unit][app][fx]") {
  auto shell = makeShell();
  DebugConsole c;
  CHECK(shell->settings().presentationIntensity == 1.0f);
  CHECK(c.run(*shell, "set presentationIntensity 0.25") ==
        "presentationIntensity is now 0.25");
  CHECK(shell->settings().presentationIntensity == 0.25f);
  CHECK(c.run(*shell, "get presentationIntensity") == "presentation_intensity = 0.25");
  CHECK(c.run(*shell, "set presentation_intensity 7") ==
        "presentation_intensity is now 7");
  CHECK(shell->settings().presentationIntensity == 1.0f);
  CHECK(c.run(*shell, "set presentationIntensity nan") ==
        "presentationIntensity is now nan");
  CHECK(std::isfinite(shell->settings().presentationIntensity));
}

TEST_CASE("the console replays the board boot and help lists it", "[unit][app][boot]") {
  auto shell = makeShell();
  DebugConsole console;
  CHECK(has(DebugConsole::help(), "boot"));
  CHECK(console.run(*shell, "load standard") == "started standard");
  const std::uint64_t before = shell->session()->bootSerial();
  CHECK(has(console.run(*shell, "boot"), "boot"));
  CHECK(shell->session()->bootSerial() == before + 1);
  // Undo does not boot; reset does.
  console.run(*shell, "play e2e4");
  console.run(*shell, "undo");
  CHECK(shell->session()->bootSerial() == before + 1);
  console.run(*shell, "reset");
  CHECK(shell->session()->bootSerial() == before + 2);
}

TEST_CASE("the console previews the check ripple and the end effect without moving",
          "[unit][app][fx]") {
  auto shell = makeShell();
  DebugConsole console;
  CHECK(has(DebugConsole::help(), "ripple"));
  CHECK(has(DebugConsole::help(), "endfx"));
  CHECK(console.run(*shell, "load standard") == "started standard");
  Session& s = *shell->session();
  const std::uint64_t checks = s.checkSerial();
  CHECK(has(console.run(*shell, "ripple"), "rippling from e1"));
  CHECK(s.checkSerial() == checks + 1);
  CHECK(*s.checkEvent() == s.game().position().findRoyal(Color::White));
  CHECK(has(console.run(*shell, "ripple d4"), "d4"));
  CHECK(s.checkSerial() == checks + 2);
  CHECK(has(console.run(*shell, "ripple zz9"), "zz9"));
  CHECK(s.checkSerial() == checks + 2);
  CHECK(s.game().plyCount() == 0);
  CHECK(has(console.run(*shell, "endfx win black"), "black"));
  REQUIRE(s.endEvent().has_value());
  CHECK(s.endEvent()->loser == Color::Black);
  CHECK(s.endEvent()->origin == s.game().position().findRoyal(Color::Black));
  CHECK(has(console.run(*shell, "endfx draw"), "draw"));
  CHECK(s.endEvent()->kind == view::EndKind::Draw);
  CHECK(has(console.run(*shell, "endfx off"), "cleared"));
  CHECK_FALSE(s.endEvent().has_value());
  CHECK(has(console.run(*shell, "endfx sideways"), "usage"));
  CHECK(s.game().plyCount() == 0);
}

TEST_CASE("the console previews the live HUD feedback without moving",
          "[unit][app][fx][hud-fx]") {
  auto shell = makeShell();
  DebugConsole console;
  CHECK(has(DebugConsole::help(), "hudfx"));
  CHECK(console.run(*shell, "load standard") == "started standard");
  Session& s = *shell->session();
  const auto turns = s.turnSerial();
  const auto moves = s.moveSerial();
  CHECK(has(console.run(*shell, "hudfx turn"), "turn"));
  CHECK(s.turnSerial() == turns + 1);
  CHECK(s.moveSerial() == moves);
  CHECK(has(console.run(*shell, "hudfx move"), "move"));
  CHECK(s.moveSerial() == moves + 1);
  CHECK(has(console.run(*shell, "hudfx"), "all"));
  CHECK(s.turnSerial() == turns + 2);
  CHECK(has(console.run(*shell, "hudfx think"), "shown"));
  CHECK(s.thinkingShown());
  CHECK(has(console.run(*shell, "hudfx think"), "hidden"));
  CHECK_FALSE(s.thinkingShown());
  CHECK(has(console.run(*shell, "hudfx sideways"), "usage"));
  CHECK(s.game().plyCount() == 0);
}

TEST_CASE("automatic interface sizing is reachable by short and file names",
          "[unit][app][hud-layout]") {
  auto shell = makeShell();
  DebugConsole console;
  CHECK(shell->settings().guiScaleAuto);
  CHECK(console.run(*shell, "get guiScaleAuto") == "gui_scale_auto = true");
  CHECK(console.run(*shell, "set guiScaleAuto false") == "guiScaleAuto is now false");
  CHECK_FALSE(shell->settings().guiScaleAuto);
  CHECK(console.run(*shell, "get gui_scale_auto") == "gui_scale_auto = false");
  CHECK(console.run(*shell, "set guiScale 1.25") == "guiScale is now 1.25");
  CHECK(shell->settings().guiScale == 1.25f);
  CHECK(has(DebugConsole::help(), "guiScaleAuto"));
}

TEST_CASE("result console opens only an adjudicated game", "[unit][app][result]") {
  auto shell = makeShell();
  DebugConsole c;
  REQUIRE(shell->startGame("standard").has_value());
  CHECK(c.run(*shell, "result") == "the game is not decided");
  REQUIRE(shell->session()->applyScript("resign").has_value());
  CHECK(c.run(*shell, "result") == "result panel opened");
  CHECK(shell->session()->resultRequested());
  CHECK(has(DebugConsole::help(), "result"));
}

TEST_CASE("review console browses the history and leaves it", "[unit][app][review]") {
  auto shell = makeShell();
  DebugConsole c;
  REQUIRE(shell->startGame("standard").has_value());
  CHECK(has(c.run(*shell, "review"), "cannot be reviewed"));
  REQUIRE(shell->session()->playMoveText("e2e4").has_value());
  REQUIRE(shell->session()->playMoveText("e7e5").has_value());
  CHECK(has(c.run(*shell, "review"), "ply 2 of 2"));
  CHECK(has(c.run(*shell, "review 1"), "ply 1 of 2"));
  CHECK(shell->session()->reviewPly() == std::optional<std::size_t>(1));
  CHECK(has(c.run(*shell, "review 9"), "past the end"));
  CHECK(has(c.run(*shell, "review x"), "not a ply"));
  CHECK(c.run(*shell, "review off") == "review off");
  CHECK_FALSE(shell->session()->reviewPly());
  CHECK(c.run(*shell, "review 1 2") == "usage: review [<ply>|off]");
  CHECK(has(DebugConsole::help(), "review [<ply>|off]"));
}

TEST_CASE("transition console previews a screen move from another screen",
          "[unit][app][transition]") {
  auto shell = makeShell();
  DebugConsole c;
  CHECK(c.run(*shell, "transition game menu") == "no game loaded");
  CHECK(has(c.run(*shell, "transition menu"), "usage"));
  CHECK(has(c.run(*shell, "transition menu nowhere"), "usage"));
  REQUIRE(shell->startGame("standard").has_value());
  const auto serial = shell->transitionSerial();
  CHECK(c.run(*shell, "transition paused menu") == "transition paused -> menu");
  CHECK(shell->screen() == Screen::MainMenu);
  CHECK(shell->transitionFrom() == Screen::Paused);
  CHECK(shell->transitionSerial() == serial + 1);
  CHECK(has(DebugConsole::help(), "transition <from> <to>"));
}

TEST_CASE("lab setting and command work without a game", "[unit][app][lab]") {
  auto shell = makeShell();
  DebugConsole console;
  CHECK_FALSE(shell->settings().labScene);
  CHECK(console.run(*shell, "lab") == "lab on");
  CHECK(console.run(*shell, "get labScene") == "lab_scene = true");
  CHECK(console.run(*shell, "lab off") == "lab off");
  CHECK(console.run(*shell, "lab on") == "lab on");
  CHECK(console.run(*shell, "lab bad") == "usage: lab [on|off]");
  CHECK(console.run(*shell, "set labScene false") == "labScene is now false");
  CHECK_FALSE(shell->settings().labScene);
  const auto path = std::filesystem::path(CB_BINARY_DIR) / "lab-settings.conf";
  shell->settings().labScene = true;
  REQUIRE(shell->settings().save(path).has_value());
  CHECK(app::Settings::load(path).labScene);
  std::filesystem::remove(path);
  CHECK(DebugConsole::help().find("lab [on|off]") != std::string::npos);
}

TEST_CASE("room light setting clamps and survives saving without a game",
          "[unit][app][light]") {
  auto shell = makeShell();
  DebugConsole c;
  CHECK(shell->settings().roomLight == 1);
  CHECK(c.run(*shell, "get roomLight") == "room_light = 1");
  CHECK(c.run(*shell, "set roomLight 0.35") == "roomLight is now 0.35");
  CHECK(shell->settings().roomLight == .35f);
  const auto path = std::filesystem::path(CB_BINARY_DIR) / "room-light-settings.conf";
  REQUIRE(shell->settings().save(path));
  CHECK(Settings::load(path).roomLight == .35f);
  std::filesystem::remove(path);
  (void)c.run(*shell, "set room_light 3");
  CHECK(shell->settings().roomLight == 2);
  (void)c.run(*shell, "set roomLight -1");
  CHECK(shell->settings().roomLight == 0);
  (void)c.run(*shell, "set roomLight nan");
  CHECK(shell->settings().roomLight == 1);
}
TEST_CASE("lights console dumps the loaded lab independently of a running game",
          "[unit][app][light]") {
  auto shell = makeShell();
  DebugConsole c;
  CHECK(c.run(*shell, "lights") == "lab is off");
  const auto doc =
      io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  c.setLabDoc(&*doc);
  const auto text = c.run(*shell, "lights");
  CHECK(has(text, "lamp spot (0.70, 2.60, 1.45) r 2.0 m  i 1.20  room"));
  CHECK(has(text, "projector point"));
  CHECK(has(text, "machine"));
  CHECK(has(DebugConsole::help(), "lights"));
  c.setLabDoc(nullptr);
  CHECK(c.run(*shell, "lights") == "lab is off");
}

TEST_CASE("projector can be disabled without a game and persists",
          "[unit][app][hologram]") {
  auto shell = makeShell();
  DebugConsole c;
  CHECK(shell->settings().projector);
  CHECK(c.run(*shell, "projector off") == "projector off");
  CHECK_FALSE(shell->settings().projector);
  CHECK(c.run(*shell, "projector bad") == "usage: projector [on|off]");
  CHECK_FALSE(shell->settings().projector);
  const auto path = std::filesystem::temp_directory_path() / "cb-projector-settings.conf";
  REQUIRE(shell->settings().save(path));
  CHECK_FALSE(Settings::load(path).projector);
  std::filesystem::remove(path);
  CHECK(c.run(*shell, "projector on") == "projector on");
  CHECK(has(DebugConsole::help(), "projector [on|off]"));
}

TEST_CASE("implant and heartbeat toggles persist and have debug aliases",
          "[unit][app][implant]") {
  auto shell = app::Shell::create({"standard"}, {});
  REQUIRE(shell);
  app::DebugConsole c;
  CHECK(shell->settings().implantFx);
  CHECK(c.run(*shell, "implant off") == "implant off");
  CHECK(c.run(*shell, "get implantFx") == "implant_fx = false");
  CHECK(c.run(*shell, "set implantFx true") == "implantFx is now true");
  CHECK(shell->settings().implantFx);
  CHECK(c.run(*shell, "heartbeat off") == "heartbeat off");
  CHECK_FALSE(shell->settings().heartbeat);
  const auto path = std::filesystem::temp_directory_path() / "cb-implant-settings.conf";
  REQUIRE(shell->settings().save(path));
  CHECK(app::Settings::load(path).implantFx);
  CHECK_FALSE(app::Settings::load(path).heartbeat);
  std::filesystem::remove(path);
  CHECK(c.run(*shell, "implant bad") == "usage: implant [on|off]");
  CHECK(has(app::DebugConsole::help(), "implant [on|off]"));
  CHECK(has(app::DebugConsole::help(), "heartbeat [on|off]"));
}

TEST_CASE("living gaze controls are inspectable and living preference persists", "[unit][app][living]") {
  auto shell=app::Shell::create({"standard"},{}); REQUIRE(shell);
  app::DebugConsole c;
  CHECK(shell->settings().livingVision);
  CHECK(c.run(*shell,"living off")=="living off");
  CHECK(c.run(*shell,"get livingVision")=="living_vision = false");
  CHECK(c.run(*shell,"tremor off")=="tremor off");
  CHECK(c.run(*shell,"wander off")=="wander off");
  CHECK(c.run(*shell,"gaze .3 .8")=="gaze pinned");
  REQUIRE(shell->settings().gazePin); CHECK((*shell->settings().gazePin)[0]==.3f);
  CHECK(c.run(*shell,"gaze ui")=="gaze ui");
  CHECK(shell->settings().gazeUi==1);
  CHECK(c.run(*shell,"gaze off")=="gaze off");
  CHECK_FALSE(shell->settings().gazePin);
  CHECK(c.run(*shell,"warp .02")=="warp set");
  CHECK(shell->settings().warpOverride==.02f);
  CHECK(c.run(*shell,"warp default")=="warp default");
  CHECK_FALSE(shell->settings().warpOverride);
  CHECK(has(c.run(*shell,"gaze"),"focus"));
  CHECK(has(c.run(*shell,"gaze nan .1"),"usage"));
  CHECK(has(c.run(*shell,"warp inf"),"usage"));
  const auto path=std::filesystem::temp_directory_path()/"cb-living-settings.conf";
  REQUIRE(shell->settings().save(path));
  CHECK_FALSE(app::Settings::load(path).livingVision);
  std::filesystem::remove(path);
}

TEST_CASE("wake and handshake replay requests and the launch preference persist",
          "[unit][app][wake]") {
  auto shell=app::Shell::create({"standard"},{}); REQUIRE(shell);
  app::DebugConsole c;
  CHECK(shell->settings().wakeOnLaunch);
  CHECK(shell->settings().wakeReplay==0);
  CHECK(c.run(*shell,"wake")=="wake replaying");
  CHECK(shell->settings().wakeReplay==1);
  CHECK(c.run(*shell,"handshake")=="handshake replaying");
  CHECK(shell->settings().handshakeReplay==1);
  CHECK(shell->settings().wakeReplay==1);
  CHECK(c.run(*shell,"set wakeOnLaunch false")=="wakeOnLaunch is now false");
  CHECK_FALSE(shell->settings().wakeOnLaunch);
  CHECK(has(app::DebugConsole::help(),"wake"));
  CHECK(has(app::DebugConsole::help(),"handshake"));
  const auto path=std::filesystem::temp_directory_path()/"cb-wake-settings.conf";
  REQUIRE(shell->settings().save(path));
  CHECK_FALSE(app::Settings::load(path).wakeOnLaunch);
  std::filesystem::remove(path);
}
