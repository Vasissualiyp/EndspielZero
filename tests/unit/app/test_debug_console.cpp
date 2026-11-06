// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>

#include "app/debug_console.hpp"
#include "app/shell.hpp"
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
