// SPDX-License-Identifier: GPL-3.0-or-later
//
// M10.2-M10.3, the demo floor: a move that is always legal, takes what it can, and does
// not hang a piece to an immediate reply - on every shipped variant, with no per-variant
// code, and reproducibly.
#include <catch2/catch_test_macros.hpp>

#include "ai/search.hpp"
#include "io/fen.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

Game gameAt(const VariantSpec& v, std::string_view fen) {
  Game g(v);
  auto p = fromFen(v, fen);
  REQUIRE(p.has_value());
  g.setStartPosition(*p);
  return g;
}

std::string text(const VariantSpec& v, const Move& m) {
  return moveText(v, m);
}

}  // namespace

TEST_CASE("the opponent plays a legal move on every shipped variant, the same one twice",
          "[unit][ai]") {
  for (const char* name :
       {"standard", "atomic", "atomic_torus", "charged", "mustcapture", "cylinder",
        "torus", "mobius", "klein", "mirrorbox", "cube5", "hyper4", "hyper4_8x8",
        "torus3d", "torus3d_8x8", "torus3d_twist", "t6", "5d"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    Game g(v);
    const ai::Evaluator eval(v);
    ai::SearchOptions small;
    // The sweep is about legality, not strength: one ply, no quiescence. The glued 3-D
    // boards pay a forward-scan attack test per legal move, which in a Debug build is
    // tens of milliseconds a node.
    small.depth = 1;
    small.quiescence = 0;
    small.maxNodes = 60;
    const ai::SearchResult a = ai::chooseMove(g, eval, small);
    const ai::SearchResult b = ai::chooseMove(g, eval, small);
    REQUIRE(a.found);
    CHECK(g.isLegal(a.best));
    CHECK(a.best == b.best);
    CHECK(a.nodes == b.nodes);
    // And the caller's game is untouched.
    CHECK(g.plyCount() == 0);
  }
}

TEST_CASE("it takes a free piece", "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const Game g = gameAt(v, "4k3/8/8/8/3r4/8/8/3QK3 w - - 0 1");
  const ai::SearchResult r = ai::chooseMove(g, ai::Evaluator(v));
  REQUIRE(r.found);
  CHECK(text(v, r.best) == "d1d4");
}

TEST_CASE("it does not hang its queen to a defended pawn", "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const Game g = gameAt(v, "4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1");
  const ai::SearchResult r = ai::chooseMove(g, ai::Evaluator(v));
  REQUIRE(r.found);
  CHECK(text(v, r.best) != "d1d5");
  CHECK(r.score > -200);  // nothing is lost
}

TEST_CASE("it finds a mate in one and scores it as one", "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const Game g = gameAt(v, "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
  const ai::SearchResult r = ai::chooseMove(g, ai::Evaluator(v));
  REQUIRE(r.found);
  CHECK(text(v, r.best) == "a1a8");
  CHECK(r.score > ai::kMate / 2);
}

TEST_CASE("derived values put standard chess's army in its familiar order",
          "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const ai::Evaluator e(v);
  const int pawn = e.pieceValue(v.findPiece("pawn"));
  const int knight = e.pieceValue(v.findPiece("knight"));
  const int bishop = e.pieceValue(v.findPiece("bishop"));
  const int rook = e.pieceValue(v.findPiece("rook"));
  const int queen = e.pieceValue(v.findPiece("queen"));
  CAPTURE(pawn, knight, bishop, rook, queen);
  CHECK(pawn < knight);
  CHECK(knight < rook);
  CHECK(bishop < rook);
  CHECK(rook < queen);
  CHECK(e.pieceValue(v.findPiece("king")) == 0);
  // Deterministic: derived from the variant, not from the clock.
  CHECK(ai::Evaluator(v).pieceValue(v.findPiece("queen")) == queen);
}
