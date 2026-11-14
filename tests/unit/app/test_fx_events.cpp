// SPDX-License-Identifier: GPL-3.0-or-later
// Presentation events (M23.10 sections 4 and 5): the session counts check and end events,
// the front end's FxTimeline stamps them with presentation time.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/fx_timeline.hpp"
#include "app/session.hpp"
#include "support/variants.hpp"
#include "view/hud_fx.hpp"
#include "view/check_ripple.hpp"
#include "view/game_end_fx.hpp"

using namespace cb;
using namespace cb::app;

namespace {
std::unique_ptr<Session> open(const char* name = "standard") {
  auto s = Session::create(test::loadVariant(name));
  REQUIRE(s.has_value());
  return std::move(*s);
}
void play(Session& s, const char* move) {
  REQUIRE(s.playMoveText(move).has_value());
}
void act(Session& s, ActionKind k) {
  Action a;
  a.kind = k;
  REQUIRE(s.apply(a).has_value());
}
CellId cellOf(const Session& s, int file, int rank) {
  Coord c(s.variant().dims.dims());
  c.c[0] = static_cast<std::int16_t>(file);
  c.c[1] = static_cast<std::int16_t>(rank);
  return s.variant().dims.toCell(c);
}
}  // namespace

TEST_CASE("a check-giving move fires the ripple once; quiet moves and undo do not",
          "[app][fx][ripple]") {
  auto s = open();
  CHECK(s->checkSerial() == 0);
  play(*s, "e2e4");
  play(*s, "f7f6");
  CHECK(s->checkSerial() == 0);
  play(*s, "d1h5");  // check along the diagonal
  CHECK(s->checkSerial() == 1);
  CHECK(*s->checkEvent() == cellOf(*s, 4, 7));  // the black king on e8
  // Selecting something while the check persists does not fire it again.
  Action sel;
  sel.kind = ActionKind::ClickCell;
  sel.cell = cellOf(*s, 4, 7);
  REQUIRE(s->apply(sel).has_value());
  CHECK(s->checkSerial() == 1);
  play(*s, "g7g6");
  CHECK(s->checkSerial() == 1);
  // Undo back into a position that is in check does not fire it either.
  act(*s, ActionKind::Undo);
  act(*s, ActionKind::Undo);
  CHECK(s->checkSerial() == 1);
}

TEST_CASE("a console ripple fires without touching the game", "[app][fx][ripple]") {
  auto s = open();
  const std::size_t ply = s->game().plyCount();
  s->requestRipple(cellOf(*s, 3, 3));
  CHECK(s->checkSerial() == 1);
  CHECK(*s->checkEvent() == cellOf(*s, 3, 3));
  CHECK(s->game().plyCount() == ply);
}

TEST_CASE("fool's mate fires a decisive end against white from the white king",
          "[app][fx][endfx]") {
  auto s = open();
  for (const char* m : {"f2f3", "e7e5", "g2g4"}) play(*s, m);
  CHECK(s->endSerial() == 0);
  play(*s, "d8h4");
  REQUIRE(s->endEvent().has_value());
  CHECK(s->endSerial() == 1);
  CHECK(s->endEvent()->kind == view::EndKind::Decisive);
  CHECK(s->endEvent()->loser == Color::White);
  CHECK(s->endEvent()->origin == cellOf(*s, 4, 0));  // e1
  CHECK(s->endEvent()->mated);
  CHECK(s->checkSerial() == 1);  // the mating move also gave check
  // Undo takes it back out: a power-up, no end event.
  act(*s, ActionKind::Undo);
  CHECK_FALSE(s->endEvent().has_value());
  CHECK(s->powerUpSerial() == 1);
  CHECK(s->endSerial() == 1);
  CHECK(s->checkSerial() == 1);
}

TEST_CASE("stalemate and an agreed draw fire a draw end", "[app][fx][endfx]") {
  {
    auto s = open();
    REQUIRE(s->loadFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1").has_value());
    CHECK(s->endSerial() == 0);  // a loaded position is the baseline, not an event
    auto t = open();
    play(*t, "e2e4");
    act(*t, ActionKind::AgreeDraw);
    REQUIRE(t->endEvent().has_value());
    CHECK(t->endEvent()->kind == view::EndKind::Draw);
  }
  {
    auto s = open();
    REQUIRE(s->loadFen("7k/8/5QK1/8/8/8/8/8 w - - 0 1").has_value());
    play(*s, "f6f7");  // black king h8 has no move and is not in check
    REQUIRE(s->endEvent().has_value());
    CHECK(s->endEvent()->kind == view::EndKind::Draw);
  }
}

TEST_CASE("reset clears the end quietly (the board boots instead of powering up)",
          "[app][fx][endfx]") {
  auto s = open();
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) play(*s, m);
  REQUIRE(s->endEvent().has_value());
  const auto boot = s->bootSerial();
  act(*s, ActionKind::Reset);
  CHECK_FALSE(s->endEvent().has_value());
  CHECK(s->powerUpSerial() == 0);
  CHECK(s->bootSerial() == boot + 1);
}

TEST_CASE(
    "the timeline holds a finished game, powers it up on undo, waits when ineligible",
    "[app][fx][endfx]") {
  auto s = open();
  FxTimeline t;
  t.observe(s.get(), 0.0, true, false);
  t.skipBoot();
  for (const char* m : {"f2f3", "e7e5", "g2g4"}) play(*s, m);
  play(*s, "d8h4");
  // Not eligible (a menu is up): the events wait.
  t.observe(s.get(), 1.0, false, false);
  CHECK_FALSE(t.end(1.0).has_value());
  CHECK_FALSE(t.ripple(1.0).has_value());
  t.observe(s.get(), 2.0, true, false);
  REQUIRE(t.end(2.5).has_value());
  CHECK(t.end(2.5)->age == 0.5f);
  CHECK(t.end(60.0)->age == 58.0f);  // the hold has no end
  CHECK(t.end(60.0)->fx.loser == Color::White);
  REQUIRE(t.ripple(2.3).has_value());
  CHECK(t.ripple(2.3)->origin == cellOf(*s, 4, 0));
  CHECK_FALSE(
      t.ripple(2.0 + static_cast<double>(view::kRippleSeconds) + 0.01).has_value());
  // Undo: the hold ends and the power-up runs for its half second.
  act(*s, ActionKind::Undo);
  t.observe(s.get(), 70.0, true, false);
  CHECK_FALSE(t.end(70.1).has_value());
  REQUIRE(t.powerUp(70.25).has_value());
  CHECK_FALSE(
      t.powerUp(70.0 + static_cast<double>(view::kPowerUpSeconds) + 0.01).has_value());
  // The trailer swallows both.
  auto s2 = open();
  FxTimeline t2;
  t2.observe(s2.get(), 0.0, true, true);
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) play(*s2, m);
  t2.observe(s2.get(), 5.0, true, true);
  CHECK_FALSE(t2.end(5.1).has_value());
  CHECK_FALSE(t2.ripple(5.1).has_value());
}

TEST_CASE("a boot or a reset forgets a finished game in the timeline",
          "[app][fx][endfx]") {
  auto s = open();
  FxTimeline t;
  t.observe(s.get(), 0.0, true, false);
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) play(*s, m);
  t.observe(s.get(), 1.0, true, false);
  REQUIRE(t.end(1.1).has_value());
  act(*s, ActionKind::Reset);
  t.observe(s.get(), 2.0, true, false);
  CHECK_FALSE(t.end(2.1).has_value());
  CHECK_FALSE(t.powerUp(2.1).has_value());
  CHECK(t.booting(2.1));
}

TEST_CASE("the session caches distances for an event's origin", "[app][fx]") {
  auto s = open();
  const CellId e1 = cellOf(*s, 4, 0);
  const auto& d = s->distancesFrom(e1);
  CHECK(d[e1] == 0);
  CHECK(d[cellOf(*s, 4, 7)] == 7);
  CHECK(&s->distancesFrom(e1) == &d);  // the second ask is the cached flood
}

TEST_CASE("an undo or reset inside the end delay cancels with no power-up",
          "[app][fx][endfx]") {
  auto s = open();
  FxTimeline t;
  t.observe(s.get(), 0.0, true, false);
  t.skipBoot();
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) play(*s, m);
  t.observe(s.get(), 10.0, true, false);
  REQUIRE(t.end(10.5).has_value());
  act(*s, ActionKind::Undo);
  t.observe(s.get(), 11.0, true, false);  // 1 s after the decision, inside the 3 s delay
  CHECK_FALSE(t.end(11.0).has_value());
  CHECK_FALSE(t.powerUp(11.1).has_value());
  // Past the delay the board did go down, so the same undo powers it up.
  auto s2 = open();
  FxTimeline t2;
  t2.observe(s2.get(), 0.0, true, false);
  t2.skipBoot();
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) play(*s2, m);
  t2.observe(s2.get(), 10.0, true, false);
  act(*s2, ActionKind::Undo);
  t2.observe(s2.get(), 10.0 + static_cast<double>(view::kEndDelaySeconds) + 0.5, true,
             false);
  CHECK(t2.powerUp(10.0 + static_cast<double>(view::kEndDelaySeconds) + 0.6).has_value());
}

TEST_CASE("capture events use their own visual timestamp and cancel on undo",
          "[app][fx][tray-scan]") {
  auto s = open();
  FxTimeline clock;
  clock.observe(s.get(), 0, true, true);
  CHECK(s->captureSerial() == 0);
  for (const char* m : {"e2e4", "d7d5"}) play(*s, m);
  clock.observe(s.get(), 1, true, false);
  CHECK_FALSE(clock.captureAge(1));
  play(*s, "e4d5");
  CHECK(s->captureSerial() == 1);
  clock.observe(s.get(), 2, true, false);
  REQUIRE(clock.captureAge(2.175));
  CHECK(*clock.captureAge(2.175) > .17f);
  CHECK_FALSE(clock.captureAge(2.36));
  act(*s, ActionKind::Undo);
  clock.observe(s.get(), 2.2, true, false);
  CHECK_FALSE(clock.captureAge(2.2));
  play(*s, "e4d5");
  CHECK(s->captureSerial() == 2);
  clock.observe(s.get(), 3, true, true);
  CHECK_FALSE(clock.captureAge(3));
}

TEST_CASE("framing keeps bounds inside the centred HUD clearance", "[app][hud-layout]") {
  auto s = open();
  const auto b = view::boundsOf(s->placements());
  s->setBoardAspect(16.0f / 9.0f);
  s->frameOn(b);
  const float before = s->camera().distance;
  CHECK(s->setFramingArea(.5f, .45f));
  s->frameOn(b);
  CHECK(s->camera().distance > before);
  const auto at = s->camera().project({b.minX, b.minY, b.minZ}, 16.0f / 9.0f, 1920, 1080);
  CHECK(at.x >= 480);
  CHECK(at.x <= 1440);
  CHECK(at.y >= 297);
  CHECK(at.y <= 783);
  const float safe = s->camera().distance;
  s->easeFrameOn(b, 1.4f, 1);
  CHECK(s->camera().distance == safe);
  CHECK_FALSE(s->setFramingArea(.5f, .45f));
}

TEST_CASE("moves count turn and new-move events; loads, captures of nothing and undo behave",
          "[app][fx][hud-fx]") {
  auto s = open();
  CHECK(s->turnSerial() == 0);
  CHECK(s->moveSerial() == 0);
  play(*s, "e2e4");
  CHECK(s->turnSerial() == 1);
  CHECK(s->moveSerial() == 1);
  play(*s, "e7e5");
  CHECK(s->turnSerial() == 2);
  CHECK(s->moveSerial() == 2);
  // Undo flips the turn back but is not a new move.
  act(*s, ActionKind::Undo);
  CHECK(s->turnSerial() == 3);
  CHECK(s->moveSerial() == 2);
  // Selecting changes neither.
  Action sel;
  sel.kind = ActionKind::ClickCell;
  sel.cell = cellOf(*s, 4, 6);
  REQUIRE(s->apply(sel).has_value());
  CHECK(s->turnSerial() == 3);
  s->requestTurnFlip();
  s->requestMoveFx();
  CHECK(s->turnSerial() == 4);
  CHECK(s->moveSerial() == 3);
}

TEST_CASE("the timeline stamps turn, move and thinking events and swallows them",
          "[app][fx][hud-fx]") {
  auto s = open();
  FxTimeline t;
  t.observe(s.get(), 0.0, true, false);
  t.skipBoot();
  CHECK_FALSE(t.turnAge(0.1));
  CHECK_FALSE(t.moveAge(0.1));
  play(*s, "e2e4");
  t.observe(s.get(), 1.0, true, false);
  REQUIRE(t.turnAge(1.1));
  CHECK(*t.turnAge(1.1) == 0.1f);
  CHECK_FALSE(t.turnAge(1.0 + static_cast<double>(view::kTurnFlipSeconds) + .01));
  REQUIRE(t.moveAge(1.1));
  CHECK_FALSE(t.moveAge(1.3));
  CHECK(t.animating(1.1));
  // Thinking runs for as long as the session shows it, and stops at once.
  CHECK_FALSE(t.thinkingAge(2.0));
  s->setThinkingPreview(true);
  t.observe(s.get(), 2.0, true, false);
  REQUIRE(t.thinkingAge(2.5));
  CHECK(*t.thinkingAge(2.5) == 0.5f);
  CHECK(t.animating(9.0));
  s->setThinkingPreview(false);
  t.observe(s.get(), 3.0, true, false);
  CHECK_FALSE(t.thinkingAge(3.0));
  // An ineligible frame waits; the trailer swallows.
  play(*s, "e7e5");
  t.observe(s.get(), 4.0, false, false);
  CHECK_FALSE(t.turnAge(4.05));
  t.observe(s.get(), 5.0, true, false);
  CHECK(t.turnAge(5.05));
  auto s2 = open();
  FxTimeline t2;
  t2.observe(s2.get(), 0.0, true, true);
  play(*s2, "e2e4");
  t2.observe(s2.get(), 1.0, true, true);
  CHECK_FALSE(t2.turnAge(1.05));
  CHECK_FALSE(t2.moveAge(1.05));
  // A reset boots and forgets pending hud events.
  t.observe(s.get(), 6.0, true, false);
  act(*s, ActionKind::Reset);
  t.observe(s.get(), 6.05, true, false);
  CHECK_FALSE(t.turnAge(6.06));
}

TEST_CASE("a deploy holds the boot until the last slab locks (M24.5)", "[unit][app][rig]") {
  auto s = open();
  FxTimeline t;
  t.holdBootUntil(11.62);
  t.observe(s.get(), 10.0, true, false);
  // Before the gate the board is dark and powered off: the boot has not begun.
  REQUIRE(t.bootAge(10.5).has_value());
  CHECK(*t.bootAge(10.5) == 0.0f);
  CHECK(*t.bootAge(11.62) == 0.0f);
  CHECK(*t.bootAge(12.12) == Catch::Approx(0.5f));
  // A skip opens the gate now; a later boot (rematch) is not held.
  t.holdBootUntil(0);
  CHECK(*t.bootAge(10.5) == Catch::Approx(0.5f));
  FxTimeline plain;
  plain.observe(s.get(), 10.0, true, false);
  CHECK(*plain.bootAge(10.5) == Catch::Approx(0.5f));
}
