// SPDX-License-Identifier: GPL-3.0-or-later
// The hover aids (M20): Ctrl-peek, and what attacks / defends a hovered square.
#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "app/session.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::unique_ptr<Session> open(const char* variantName = "standard") {
  auto s = Session::create(test::loadVariant(variantName));
  REQUIRE(s.has_value());
  return std::move(*s);
}

CellId cell(const Session& s, const char* name) {
  auto c = parseCell(s.variant().dims, name);
  REQUIRE(c.has_value());
  return *c;
}

std::vector<CellId> cells(const Session& s, std::initializer_list<const char*> names) {
  std::vector<CellId> v;
  for (const char* n : names) v.push_back(cell(s, n));
  std::sort(v.begin(), v.end());
  return v;
}

std::vector<CellId> vec(std::span<const CellId> s) {
  return {s.begin(), s.end()};
}

void play(Session& s, const char* script) {
  REQUIRE(s.applyScript(script).has_value());
}

}  // namespace

TEST_CASE("Ctrl over a piece shows its moves, either side's", "[unit][app]") {
  auto s = open();
  s->setHover(cell(*s, "e2"), true);
  CHECK(vec(s->snapshot().peekMoves()) == cells(*s, {"e3", "e4"}));
  s->setHover(cell(*s, "b8"), true);  // the opponent's knight
  CHECK(vec(s->snapshot().peekMoves()) == cells(*s, {"a6", "c6"}));
  s->setHover(cell(*s, "a8"), true);  // a blocked rook
  CHECK(s->snapshot().peekMoves().empty());
  s->setHover(cell(*s, "e2"), false);  // no Ctrl
  CHECK(s->snapshot().peekMoves().empty());
}

TEST_CASE("hovering an empty square shows attackers, paths and defenders",
          "[unit][app]") {
  auto s = open();
  play(*s, "click e2\nclick e4");
  s->setHover(cell(*s, "d5"), true);  // black to move: white's e4 pawn attacks d5
  CHECK(vec(s->snapshot().hintAttackers()) == cells(*s, {"e4"}));
  CHECK(s->snapshot().hintPath().empty());
  play(*s, "click e7\nclick e5\nclick d1\nclick h5");
  // Black to move; e2 is empty and white's queen attacks it from h5 down the diagonal.
  s->setHover(cell(*s, "e2"), true);
  CHECK(vec(s->snapshot().hintAttackers()) == cells(*s, {"e1", "f1", "g1", "h5"}));
  CHECK(vec(s->snapshot().hintPath()) == cells(*s, {"f3", "g4"}));
  // Black's own pieces that could recapture there: none reach e2.
  CHECK(s->snapshot().hintDefenders().empty());
}

TEST_CASE("hovering a legal destination judges it after the move", "[unit][app]") {
  auto s = open();
  play(*s, "click g1");
  s->setHover(cell(*s, "f3"), true);
  // After Nf3 the square is defended by the pawns on e2 and g2 and attacked by nothing.
  CHECK(s->snapshot().hintAttackers().empty());
  CHECK(vec(s->snapshot().hintDefenders()) == cells(*s, {"e2", "g2"}));
  play(*s, "click f3\nclick e5\nclick f3");
  play(*s, "undo");
}

TEST_CASE("hover aids follow the options and the position", "[unit][app]") {
  auto s = open();
  s->setHintOptions({false, false});
  s->setHover(cell(*s, "e2"), true);
  CHECK(s->snapshot().peekMoves().empty());
  s->setHover(cell(*s, "e5"), true);
  CHECK(s->snapshot().hintAttackers().empty());
  s->setHintOptions({true, true});
  s->setHover(cell(*s, "e2"), true);
  CHECK_FALSE(s->snapshot().peekMoves().empty());
  // A move recomputes from the stored pointer: no stale peek.
  play(*s, "click e2\nclick e4");
  CHECK(s->snapshot().peekMoves().empty());  // e2 is empty now
  s->setHover(kInvalidCell, false);
  CHECK(s->snapshot().peekMoves().empty());
}

TEST_CASE("hover is a script verb and pickPixel is pure", "[unit][app]") {
  auto s = open();
  auto a = parseAction(s->variant(), "hover e4 ctrl");
  REQUIRE(a.has_value());
  CHECK(a->kind == ActionKind::Hover);
  CHECK(a->ctrl);
  auto off = parseAction(s->variant(), "hover off");
  REQUIRE(off.has_value());
  CHECK(off->cell == kInvalidCell);
  play(*s, "click e2");
  const auto before = vec(s->snapshot().highlighted());
  (void)s->pickPixel(100.0f, 100.0f, 800.0f, 600.0f);
  CHECK(s->selected() == cell(*s, "e2"));
  CHECK(vec(s->snapshot().highlighted()) == before);
}

TEST_CASE("hints need Ctrl; the hovered square is always marked", "[unit][app]") {
  auto s = open();
  play(*s, "click e2\nclick e4");
  s->setHover(cell(*s, "d5"), false);
  CHECK(s->snapshot().hover() == cell(*s, "d5"));
  CHECK(s->snapshot().hintAttackers().empty());
  CHECK(s->snapshot().peekMoves().empty());
  s->setHover(cell(*s, "d5"), true);
  CHECK_FALSE(s->snapshot().hintAttackers().empty());
  s->setHover(kInvalidCell, false);
  CHECK(s->snapshot().hover() == kInvalidCell);
  // Even with every aid off the square is marked.
  s->setHintOptions({false, false});
  s->setHover(cell(*s, "e5"), true);
  CHECK(s->snapshot().hover() == cell(*s, "e5"));
}

TEST_CASE("Ctrl over a capturable target shows its attackers, not a peek", "[unit][app]") {
  auto s = open();
  play(*s, "click e2\nclick e4\nclick d7\nclick d5\nclick e4");
  s->setHover(cell(*s, "d5"), true);  // a capture target of the selected pawn
  CHECK(s->snapshot().peekMoves().empty());
  CHECK_FALSE(s->snapshot().hintAttackers().empty());  // the queen would retake on d5
}

TEST_CASE("cancelSelection drops the selected piece", "[unit][app]") {
  auto s = open();
  CHECK_FALSE(s->cancelSelection());
  play(*s, "click e2");
  REQUIRE(s->selected() != kInvalidCell);
  CHECK(s->cancelSelection());
  CHECK(s->selected() == kInvalidCell);
  CHECK(s->snapshot().highlighted().empty());
}

TEST_CASE("a piece that lands under the pointer is not tinted until the pointer leaves",
          "[unit][app]") {
  auto s = open();
  play(*s, "click e2");
  s->setHover(cell(*s, "e4"), false);
  CHECK(s->snapshot().hover() == cell(*s, "e4"));
  play(*s, "click e4");  // the pawn lands on the hovered square
  CHECK(s->snapshot().hover() == kInvalidCell);
  s->setHover(cell(*s, "e4"), true);  // same square, Ctrl pressed: still quiet
  CHECK(s->snapshot().hover() == kInvalidCell);
  s->setHover(cell(*s, "e5"), false);  // out ...
  CHECK(s->snapshot().hover() == cell(*s, "e5"));
  s->setHover(cell(*s, "e4"), false);  // ... and back
  CHECK(s->snapshot().hover() == cell(*s, "e4"));
}

TEST_CASE("the divider between two squares picks one of them", "[unit][app]") {
  auto s = open();
  constexpr float w = 800.0f, h = 600.0f;
  const auto& pl = s->placements();
  const CellId a1 = cell(*s, "a1"), a2 = cell(*s, "a2");
  const view::Placement *pa = nullptr, *pb = nullptr;
  for (const auto& p : pl) {
    if (p.cell == a1) pa = &p;
    if (p.cell == a2) pb = &p;
  }
  REQUIRE((pa != nullptr && pb != nullptr));
  // Walk along the line between the two centres: every point resolves to a1 or a2, with
  // the switch at the middle and no gap where nothing is picked.
  for (int i = 0; i <= 20; ++i) {
    const float t = static_cast<float>(i) / 20.0f;
    const auto sp = s->camera().project(
        view::Vec3{pa->x + (pb->x - pa->x) * t, pa->y + (pb->y - pa->y) * t, pa->z}, w / h,
        w, h);
    const CellId c = s->pickPixel(sp.x, sp.y, w, h);
    INFO("t = " << t);
    CHECK((c == a1 || c == a2));
    if (i <= 8) CHECK(c == a1);
    if (i >= 12) CHECK(c == a2);
  }
}
