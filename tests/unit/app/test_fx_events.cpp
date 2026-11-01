// SPDX-License-Identifier: GPL-3.0-or-later
// Presentation events (M23.10 sections 4 and 5): the session counts check and end events,
// the front end's FxTimeline stamps them with presentation time.
#include <catch2/catch_test_macros.hpp>

#include "app/fx_timeline.hpp"
#include "app/session.hpp"
#include "support/variants.hpp"
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
void play(Session& s, const char* move) { REQUIRE(s.playMoveText(move).has_value()); }
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

TEST_CASE("the timeline holds a finished game, powers it up on undo, waits when ineligible",
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
  CHECK_FALSE(t.ripple(2.0 + static_cast<double>(view::kRippleSeconds) + 0.01).has_value());
  // Undo: the hold ends and the power-up runs for its half second.
  act(*s, ActionKind::Undo);
  t.observe(s.get(), 70.0, true, false);
  CHECK_FALSE(t.end(70.1).has_value());
  REQUIRE(t.powerUp(70.25).has_value());
  CHECK_FALSE(t.powerUp(70.0 + static_cast<double>(view::kPowerUpSeconds) + 0.01).has_value());
  // The trailer swallows both.
  auto s2 = open();
  FxTimeline t2;
  t2.observe(s2.get(), 0.0, true, true);
  for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) play(*s2, m);
  t2.observe(s2.get(), 5.0, true, true);
  CHECK_FALSE(t2.end(5.1).has_value());
  CHECK_FALSE(t2.ripple(5.1).has_value());
}

TEST_CASE("a boot or a reset forgets a finished game in the timeline", "[app][fx][endfx]") {
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
