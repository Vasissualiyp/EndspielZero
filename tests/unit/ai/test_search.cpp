// SPDX-License-Identifier: GPL-3.0-or-later
//
// M10.2-M10.3, the demo floor: a move that is always legal, takes what it can, and does
// not hang a piece to an immediate reply - on every shipped variant, with no per-variant
// code, and reproducibly.
#include <algorithm>
#include <cstdint>

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

/// A small fixed budget at the engine's own settings: enough for the mop-up term to do
/// its work, cheap enough for a Debug build.
ai::SearchOptions budget(std::uint64_t nodes) {
  ai::SearchOptions o;
  o.maxNodes = nodes;
  return o;
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
    // Every difficulty level answers legally here too, on a budget the slowest board can
    // afford.
    for (int level = ai::kMinLevel; level <= ai::kMaxLevel; ++level) {
      CAPTURE(level);
      ai::SearchOptions o = ai::optionsForLevel(level);
      o.quiescence = 0;
      o.maxNodes = 12;
      const ai::SearchResult r = ai::chooseMove(g, eval, o);
      REQUIRE(r.found);
      CHECK(g.isLegal(r.best));
    }
  }
}

TEST_CASE("it takes a free piece", "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const Game g = gameAt(v, "4k3/8/8/8/3r4/8/8/3QK3 w - - 0 1");
  const ai::SearchResult r = ai::chooseMove(g, ai::Evaluator(v), budget(4000));
  REQUIRE(r.found);
  CHECK(text(v, r.best) == "d1d4");
}

TEST_CASE("it does not hang its queen to a defended pawn", "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const Game g = gameAt(v, "4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1");
  const ai::SearchResult r = ai::chooseMove(g, ai::Evaluator(v), budget(4000));
  REQUIRE(r.found);
  CHECK(text(v, r.best) != "d1d5");
  CHECK(r.score > -200);  // nothing is lost
}

TEST_CASE("it finds a mate in one and scores it as one", "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const Game g = gameAt(v, "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
  const ai::SearchResult r = ai::chooseMove(g, ai::Evaluator(v), budget(4000));
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

namespace {

/// Both sides played by the engine from `fen`; the number of full moves until the game
/// ends (or `limit`), and how it ended.
struct Playout {
  int plies{0};
  GameResult result{GameResult::InProgress};
  EndReason reason{EndReason::None};
};

Playout playOut(const VariantSpec& v, std::string_view fen, int limitPlies,
                const ai::SearchOptions& strong, const ai::SearchOptions& weak) {
  Game g = gameAt(v, fen);
  const ai::Evaluator eval(v);
  const Color stronger = g.position().sideToMove();
  Playout out;
  while (g.result() == GameResult::InProgress && out.plies < limitPlies) {
    const ai::SearchResult r =
        ai::chooseMove(g, eval, g.position().sideToMove() == stronger ? strong : weak);
    REQUIRE(r.found);
    REQUIRE(g.play(r.best).has_value());
    ++out.plies;
  }
  out.result = g.result();
  out.reason = g.endReason();
  return out;
}

}  // namespace

namespace {

struct MateCase {
  const char* name;
  const char* fen;
  int limitPlies;
};

constexpr MateCase kMates[] = {
    {"KQK", "8/8/8/4k3/8/8/2Q5/4K3 w - - 0 1", 80},
    {"KRK", "8/8/8/4k3/8/8/2R5/4K3 w - - 0 1", 120},
    {"KRBK", "8/8/8/4k3/8/8/2RB4/4K3 w - - 0 1", 120},
};

}  // namespace

TEST_CASE("the engine mates with queen, rook, or rook and bishop against a lone king",
          "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  // Limits are in plies (about 40 moves for KQK, 60 for KRK and KRBK); the defender looks
  // one ply and a few hundred nodes, the attacker a modest budget.
  for (const MateCase& c : kMates) {
    CAPTURE(c.name);
    const Playout p = playOut(v, c.fen, c.limitPlies, budget(600), budget(300));
    CAPTURE(p.plies);
    CHECK(p.result == GameResult::WhiteWins);
    CHECK(p.reason == EndReason::Checkmate);
  }
}

TEST_CASE("the engine at level 4 converts against a level 2 defence",
          "[unit][ai][slow]") {
  const VariantSpec v = test::loadVariant("standard");
  for (const MateCase& c : kMates) {
    CAPTURE(c.name);
    const Playout p =
        playOut(v, c.fen, c.limitPlies, ai::optionsForLevel(4), ai::optionsForLevel(2));
    CAPTURE(p.plies);
    CHECK(p.result == GameResult::WhiteWins);
    CHECK(p.reason == EndReason::Checkmate);
  }
}

TEST_CASE("a winning side does not repeat the position", "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  Game g = gameAt(v, "8/8/8/4k3/8/8/2R5/4K3 w - - 0 1");
  const ai::Evaluator eval(v);
  for (int i = 0; i < 30 && g.result() == GameResult::InProgress; ++i) {
    const ai::SearchResult r = ai::chooseMove(g, eval, budget(600));
    REQUIRE(g.play(r.best).has_value());
    CHECK(g.endReason() != EndReason::Repetition);
    CHECK(g.endReason() != EndReason::DrawClock);
  }
}

TEST_CASE("a tight node budget still gives a legal, reproducible move", "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const ai::Evaluator eval(v);
  const Game g = gameAt(v, "4k3/8/8/8/3r4/8/8/3QK3 w - - 0 1");
  for (const std::uint64_t budget : {1U, 10U, 40U, 200U, 1500U}) {
    CAPTURE(budget);
    ai::SearchOptions o;
    o.maxNodes = budget;
    const ai::SearchResult a = ai::chooseMove(g, eval, o);
    const ai::SearchResult b = ai::chooseMove(g, eval, o);
    REQUIRE(a.found);
    CHECK(g.isLegal(a.best));
    CHECK(a.best == b.best);
    CHECK(a.nodes == b.nodes);
  }
  // Enough for depth 1 to finish: the free rook is taken, not a half-searched guess.
  ai::SearchOptions o;
  o.maxNodes = 400;
  CHECK(text(v, ai::chooseMove(g, eval, o).best) == "d1d4");
}

TEST_CASE("the mop-up tables put the lone king's edge and distance in the evaluator",
          "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const ai::Evaluator e(v);
  REQUIRE(e.hasMopUp());
  CHECK(e.royalMobility(0) == 3);   // a1
  CHECK(e.royalMobility(1) == 5);   // an edge
  CHECK(e.royalMobility(27) == 8);  // the middle
  // The lone king in the corner is worse for it than in the middle, same material.
  Game corner = gameAt(v, "k7/8/8/8/8/8/8/R3K3 b - - 0 1");
  Game middle = gameAt(v, "8/8/8/3k4/8/8/8/R3K3 b - - 0 1");
  CHECK(e.evaluate(corner.position()) < e.evaluate(middle.position()));
}

TEST_CASE("every level is deterministic and every option set is a legal choice",
          "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const ai::Evaluator eval(v);
  const Game g =
      gameAt(v, "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4");
  for (int level = ai::kMinLevel; level <= ai::kMaxLevel; ++level) {
    CAPTURE(level);
    ai::SearchOptions o = ai::optionsForLevel(level);
    o.maxNodes = std::min<std::uint64_t>(o.maxNodes, 1500);  // Debug is slow
    const ai::SearchResult a = ai::chooseMove(g, eval, o);
    const ai::SearchResult b = ai::chooseMove(g, eval, o);
    REQUIRE(a.found);
    CHECK(g.isLegal(a.best));
    CHECK(a.best == b.best);
    CHECK(a.score == b.score);
    CHECK(a.nodes == b.nodes);
  }
}

TEST_CASE("the levels run from a shallow, noisy beginner to an unlimited expert",
          "[unit][ai]") {
  CHECK(ai::optionsForLevel(0).level == ai::kMinLevel);  // clamped
  CHECK(ai::optionsForLevel(9).level == ai::kMaxLevel);
  for (int level = ai::kMinLevel; level < ai::kMaxLevel; ++level) {
    CAPTURE(level);
    const ai::SearchOptions lo = ai::optionsForLevel(level);
    const ai::SearchOptions hi = ai::optionsForLevel(level + 1);
    CHECK(lo.depth <= hi.depth);
    CHECK(lo.maxNodes <= hi.maxNodes);
    CHECK(lo.noise >= hi.noise);
  }
  CHECK(ai::optionsForLevel(ai::kMaxLevel).noise == 0);
}

TEST_CASE("the weakest level never misses a mate in one, whatever the noise",
          "[unit][ai]") {
  const VariantSpec v = test::loadVariant("standard");
  const ai::Evaluator eval(v);
  for (const char* fen :
       {"6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1", "7k/8/6K1/8/8/8/Q7/8 w - - 0 1",
        "k7/8/1K6/8/8/8/8/7R w - - 0 1"}) {
    CAPTURE(fen);
    const Game g = gameAt(v, fen);
    for (int level = ai::kMinLevel; level <= ai::kMaxLevel; ++level) {
      CAPTURE(level);
      ai::SearchOptions o = ai::optionsForLevel(level);
      o.maxNodes = std::min<std::uint64_t>(o.maxNodes, 1500);
      const ai::SearchResult r = ai::chooseMove(g, eval, o);
      REQUIRE(r.found);
      Game after = g;
      REQUIRE(after.play(r.best).has_value());
      CHECK(after.endReason() == EndReason::Checkmate);
    }
  }
}

TEST_CASE("the stronger level at least ties the weakest over a few games",
          "[unit][ai][slow]") {
  const VariantSpec v = test::loadVariant("standard");
  const ai::Evaluator eval(v);
  // Each game: hard (level 4) against beginner (level 1), once with each colour, 80 plies
  // at most. Score as a player: a win 2, a draw (or unfinished) 1, a loss 0.
  int hard = 0;
  int easy = 0;
  for (const Color hardSide : {Color::White, Color::Black}) {
    Game g(v);
    int plies = 0;
    while (g.result() == GameResult::InProgress && plies < 80) {
      const bool hardToMove = g.position().sideToMove() == hardSide;
      ai::SearchOptions o = ai::optionsForLevel(hardToMove ? 4 : 1);
      o.maxNodes = std::min<std::uint64_t>(o.maxNodes, hardToMove ? 3000 : 400);
      const ai::SearchResult r = ai::chooseMove(g, eval, o);
      REQUIRE(r.found);
      REQUIRE(g.play(r.best).has_value());
      ++plies;
    }
    const GameResult res = g.result();
    const bool hardWon = (res == GameResult::WhiteWins) == (hardSide == Color::White) &&
                         (res == GameResult::WhiteWins || res == GameResult::BlackWins);
    const bool easyWon =
        (res == GameResult::WhiteWins || res == GameResult::BlackWins) && !hardWon;
    hard += hardWon ? 2 : easyWon ? 0 : 1;
    easy += easyWon ? 2 : hardWon ? 0 : 1;
  }
  CAPTURE(hard, easy);
  CHECK(hard >= easy);
}
