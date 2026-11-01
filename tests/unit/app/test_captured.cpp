// SPDX-License-Identifier: GPL-3.0-or-later
// The captured trays' source (M23.10 section 7): what each side has taken, from the history.
#include "app/captured.hpp"

#include <catch2/catch_test_macros.hpp>

#include "app/session.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {
std::unique_ptr<Session> open() {
  auto s = Session::create(test::loadVariant("standard"));
  REQUIRE(s.has_value());
  return std::move(*s);
}
void play(Session& s, const char* m) { REQUIRE(s.playMoveText(m).has_value()); }
PieceTypeId typeNamed(const VariantSpec& v, const char* name) {
  for (std::size_t i = 0; i < v.pieces.size(); ++i)
    if (v.pieces[i].name == name) return static_cast<PieceTypeId>(i);
  FAIL("no piece named " << name);
  return 0;
}
}  // namespace

TEST_CASE("capturedBySide: an ordinary capture lands in the capturer's tray",
          "[unit][app][captured]") {
  auto s = open();
  CHECK(capturedBySide(s->game()).byWhite.empty());
  for (const char* m : {"e2e4", "d7d5", "e4d5", "d8d5"}) play(*s, m);
  const CapturedPieces c = capturedBySide(s->game());
  REQUIRE(c.byWhite.size() == 1);
  REQUIRE(c.byBlack.size() == 1);
  const PieceTypeId pawn = typeNamed(s->variant(), "pawn");
  CHECK(c.byWhite[0].type == pawn);
  CHECK(c.byWhite[0].colorOf() == Color::Black);
  CHECK(c.byBlack[0].type == pawn);
  CHECK(c.byBlack[0].colorOf() == Color::White);
}

TEST_CASE("capturedBySide: en passant takes the pawn that is not on the landing cell",
          "[unit][app][captured]") {
  auto s = open();
  for (const char* m : {"e2e4", "a7a6", "e4e5", "d7d5", "e5d6"}) play(*s, m);
  const CapturedPieces c = capturedBySide(s->game());
  REQUIRE(c.byWhite.size() == 1);
  CHECK(c.byWhite[0].type == typeNamed(s->variant(), "pawn"));
  CHECK(c.byWhite[0].colorOf() == Color::Black);
  CHECK(c.byBlack.empty());
}

TEST_CASE("capturedBySide: a capturing promotion counts the piece taken, not the promotion",
          "[unit][app][captured]") {
  auto s = open();
  for (const char* m : {"b2b4", "c7c5", "b4c5", "b7b6", "c5b6", "g8f6", "b6b7", "f6h5"})
    play(*s, m);
  play(*s, "b7a8q");
  const CapturedPieces c = capturedBySide(s->game());
  REQUIRE(c.byWhite.size() == 3);  // the c5 and b6 pawns, then the a8 rook
  CHECK(c.byWhite[0].type == typeNamed(s->variant(), "pawn"));
  CHECK(c.byWhite[2].type == typeNamed(s->variant(), "rook"));
  CHECK(c.byWhite[2].colorOf() == Color::Black);
  CHECK(c.byBlack.empty());
}

TEST_CASE("capturedBySide: a game set up from a FEN shows nothing", "[unit][app][captured]") {
  auto s = open();
  REQUIRE(s->loadFen("r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1").has_value());
  play(*s, "b7a8q");
  CHECK(capturedBySide(s->game()).byWhite.empty());
}

TEST_CASE("capturedBySide: undo takes the capture back", "[unit][app][captured]") {
  auto s = open();
  for (const char* m : {"e2e4", "d7d5", "e4d5"}) play(*s, m);
  CHECK(capturedBySide(s->game()).byWhite.size() == 1);
  Action undo;
  undo.kind = ActionKind::Undo;
  REQUIRE(s->apply(undo).has_value());
  CHECK(capturedBySide(s->game()).byWhite.empty());
}
