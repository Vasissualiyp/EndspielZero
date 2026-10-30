// SPDX-License-Identifier: GPL-3.0-or-later
//
// A directional piece crossing an orientation-reversing seam turns round
// (Piece::flipped, MoveFlag::Reorient). Möbius is the board that makes it happen: the
// file seam reverses the rank, which is the pawns' orientation axis.

#include <algorithm>
#include <fstream>
#include <sstream>

#include <catch2/catch_test_macros.hpp>

#include "io/fen.hpp"
#include "io/notation.hpp"
#include "movegen/movegen.hpp"
#include "oracle/oracle.hpp"
#include "support/variants.hpp"

namespace cb {
namespace {

CellId cell(const VariantSpec& v, std::string_view name) {
  const auto c = parseCell(v.dims, name);
  REQUIRE(c.has_value());
  return *c;
}

std::vector<Move> movesFrom(const VariantSpec& v, const Position& p, CellId from) {
  const MoveGen gen(v);
  MoveList all(v.moveUpperBound());
  gen.generatePseudoLegal(p, all);
  std::vector<Move> out;
  for (const Move& m : all) {
    if (m.from == from) out.push_back(m);
  }
  return out;
}

const Move* find(const std::vector<Move>& ms, CellId to) {
  const auto it =
      std::find_if(ms.begin(), ms.end(), [&](const Move& m) { return m.to == to; });
  return it == ms.end() ? nullptr : &*it;
}

Position fen(const VariantSpec& v, std::string_view text) {
  auto p = fromFen(v, text);
  if (!p.has_value()) FAIL(p.error().format());
  return std::move(*p);
}

}  // namespace

TEST_CASE("a pawn capturing across the Möbius seam comes back flipped",
          "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("mobius");
  const PieceTypeId pawn = v.findPiece("pawn");
  REQUIRE(v.pieces[pawn].reorients);
  REQUIRE_FALSE(v.pieces[v.findPiece("rook")].reorients);  // no forward, nothing to flip

  // h2 x (8,2): the file seam folds it to file 0 with the rank mirrored, a6.
  Position p = fen(v, "4k3/8/n7/8/8/8/7P/4K3 w - - 0 1");
  const auto moves = movesFrom(v, p, cell(v, "h2"));
  const Move* cross = find(moves, cell(v, "a6"));
  REQUIRE(cross != nullptr);
  CHECK(has(cross->flags, MoveFlag::Reorient));
  CHECK(cross->isCapture());
  // The ordinary pushes stay on the board and do not flip.
  const Move* push = find(moves, cell(v, "h3"));
  REQUIRE(push != nullptr);
  CHECK_FALSE(has(push->flags, MoveFlag::Reorient));

  const std::uint64_t before = p.hash();
  Undo u;
  p.make(*cross, u);
  CHECK(p.at(cell(v, "a6")).flipped());
  CHECK(p.at(cell(v, "a6")).orientation() == Color::Black);
  CHECK(p.hash() == p.computeHash());
  CHECK(toFen(p).starts_with("4k3/8/P'7/"));

  // Flipped, it walks towards White's home: a5, not a7.
  p.setSideToMove(Color::White);
  const auto after = movesFrom(v, p, cell(v, "a6"));
  CHECK(find(after, cell(v, "a5")) != nullptr);
  CHECK(find(after, cell(v, "a7")) == nullptr);

  p.setSideToMove(Color::Black);
  p.unmake(*cross, u);
  CHECK(p.hash() == before);
  CHECK_FALSE(p.at(cell(v, "h2")).flipped());
  CHECK(p.at(cell(v, "a6")).colorOf() == Color::Black);
}

TEST_CASE("crossing back unflips the pawn", "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("mobius");
  // A flipped White pawn on a6 capturing down-left crosses the seam to h4.
  Position p = fen(v, "4k3/8/P'7/8/7n/8/8/4K3 w - - 0 1");
  const auto moves = movesFrom(v, p, cell(v, "a6"));
  const Move* back = find(moves, cell(v, "h4"));
  REQUIRE(back != nullptr);
  CHECK(has(back->flags, MoveFlag::Reorient));
  Undo u;
  p.make(*back, u);
  CHECK_FALSE(p.at(cell(v, "h4")).flipped());
  CHECK(p.hash() == p.computeHash());
}

TEST_CASE("a flipped pawn promotes on its own first rank", "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("mobius");
  Position p = fen(v, "4k3/8/8/8/8/8/2P'5/4K3 w - - 0 1");
  const auto moves = movesFrom(v, p, cell(v, "c2"));
  const auto promos = std::count_if(moves.begin(), moves.end(), [&](const Move& m) {
    return m.to == cell(v, "c1") && has(m.flags, MoveFlag::Promotion);
  });
  CHECK(promos == 4);
  // And the queen it becomes carries no flip: a queen has no forward.
  const auto q = std::find_if(moves.begin(), moves.end(), [&](const Move& m) {
    return m.to == cell(v, "c1") && m.promoteTo == v.findPiece("queen");
  });
  REQUIRE(q != moves.end());
  Undo u;
  p.make(*q, u);
  CHECK_FALSE(p.at(cell(v, "c1")).flipped());
  p.unmake(*q, u);
  CHECK(p.at(cell(v, "c2")).flipped());
  CHECK(p.hash() == p.computeHash());
}

TEST_CASE("a pawn crossing the seam with a promotion flips into its new last rank",
          "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("mobius");
  // h7 x (8,7) folds to a1: rank 0, which is the flipped pawn's promotion rank.
  Position p = fen(v, "4k3/7P/8/8/8/8/8/n3K3 w - - 0 1");
  const auto moves = movesFrom(v, p, cell(v, "h7"));
  const auto promos = std::count_if(moves.begin(), moves.end(), [&](const Move& m) {
    return m.to == cell(v, "a1") && has(m.flags, MoveFlag::Promotion) &&
           has(m.flags, MoveFlag::Reorient);
  });
  CHECK(promos == 4);
}

TEST_CASE(
    "the flipped marker round-trips through FEN and is refused where it means nothing",
    "[unit][io]") {
  const VariantSpec mob = test::loadVariant("mobius");
  const std::string text = "4k3/8/P'7/8/8/8/8/4K3 w - - 0 1";
  const Position p = fen(mob, text);
  CHECK(toFen(p) == text);
  CHECK(p.hash() != fen(mob, "4k3/8/P7/8/8/8/8/4K3 w - - 0 1").hash());

  const VariantSpec standard = test::loadVariant("standard");
  CHECK_FALSE(fromFen(standard, "4k3/8/P'7/8/8/8/8/4K3 w - - 0 1").has_value());
  CHECK_FALSE(fromFen(mob, "4k3/8/R'7/8/8/8/8/4K3 w - - 0 1").has_value());
}

TEST_CASE("a mirror wall across the files does not turn a pawn round",
          "[unit][movegen]") {
  // mirrorbox reverses handedness at the file walls, so its pawns track their frame -
  // but the wall negates the file, never the rank, and nothing flips.
  const VariantSpec v = test::loadVariant("mirrorbox");
  REQUIRE(v.pieces[v.findPiece("pawn")].reorients);
  Position p = fen(v, "4k3/8/8/8/8/6n1/7P/4K3 w - - 0 1");
  for (const Move& m : movesFrom(v, p, cell(v, "h2"))) {
    CHECK_FALSE(has(m.flags, MoveFlag::Reorient));
  }
}

TEST_CASE("flips_on_mirror = false keeps a pawn's forward fixed", "[unit][movegen]") {
  std::ifstream in(test::variantPath("mobius"));
  std::stringstream ss;
  ss << in.rdbuf();
  std::string text = ss.str();
  const std::string anchor = "resets_draw_clock = true\n";
  text.replace(text.find(anchor), anchor.size(), anchor + "flips_on_mirror = false\n");
  auto loaded = loadVariantToml(text, "mobius-fixed");
  REQUIRE(loaded.has_value());
  const VariantSpec& v = *loaded;
  REQUIRE_FALSE(v.pieces[v.findPiece("pawn")].reorients);
  CHECK(v.variantId() != test::loadVariant("mobius").variantId());

  Position p = fen(v, "4k3/8/n7/8/8/8/7P/4K3 w - - 0 1");
  const Move* cross = nullptr;
  const auto moves = movesFrom(v, p, cell(v, "h2"));
  cross = find(moves, cell(v, "a6"));
  REQUIRE(cross != nullptr);
  CHECK_FALSE(has(cross->flags, MoveFlag::Reorient));
}

TEST_CASE("Möbius with flipped pawns agrees with the oracle", "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("mobius");
  const MoveGen gen(v);
  for (const char* text : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1",
                           "4k3/p'6p/8/2P'5/5p'2/8/P6P'/4K3 w - - 0 1",
                           "4k3/p'6p/8/2P'5/5p'2/8/P6P'/4K3 b - - 0 1",
                           "r3k2r/1p'4P'1/8/8/8/8/1P4p1/R3K2R w - - 0 1"}) {
    CAPTURE(text);
    Position p = fen(v, text);
    Position q = fen(v, text);
    CHECK(gen.perft(p, 3) == oracle::perft(q, 3));
  }
}

}  // namespace cb
