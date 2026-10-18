// SPDX-License-Identifier: GPL-3.0-or-later
//
// Marks (M19): named tags on cells for a number of plies or forever, left by moves; blocking
// tags; and captures that take a piece beside the landing cell - en passant, a checkers jump.
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "io/fen.hpp"
#include "io/variant_toml.hpp"
#include "movegen/movegen.hpp"
#include "position/position.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

CellId cellAt(const VariantSpec& v, int file, int rank) {
  Coord c(2);
  c.c[0] = static_cast<std::int16_t>(file);
  c.c[1] = static_cast<std::int16_t>(rank);
  return v.dims.toCell(c);
}

Move find(const MoveGen& gen, Position& p, CellId from, CellId to) {
  MoveList moves(p.variant().moveUpperBound());
  gen.generateLegal(p, moves);
  for (const Move& m : moves) {
    if (m.from == from && m.to == to) return m;
  }
  std::string have;
  for (const Move& m : moves) have += std::to_string(m.from) + ">" + std::to_string(m.to) + " ";
  FAIL("no such move " << from << ">" << to << "; have " << have);
  return {};
}

bool has(const MoveGen& gen, Position& p, CellId from, CellId to) {
  MoveList moves(p.variant().moveUpperBound());
  gen.generateLegal(p, moves);
  for (const Move& m : moves) {
    if (m.from == from && m.to == to) return true;
  }
  return false;
}

// A bomber lands and wrecks the cells round it: blocking "ruin" marks for three plies, and
// a "scar" tag for good that blocks nothing.
const char* kBomber = R"toml(
name = "bomber"
orientation_axis = "rank"
side_to_move = "white"
[[axis]]
name = "file"
extent = 7
[[axis]]
name = "rank"
extent = 7
[[mark]]
name = "ruin"
blocks = true
[[mark]]
name = "scar"
[[piece]]
name = "king"
symbol = "K"
royal = true
moves = "king"
[[piece]]
name = "bomber"
symbol = "B"
moves = "[1,2]^1 marks ruin(around 1, 3) marks scar(landed, forever)"
[[piece]]
name = "rook"
symbol = "R"
moves = "[1]^k"
[start]
board = "k6/7/7/7/7/B6/K1R4"
)toml";

}  // namespace

TEST_CASE("a move leaves marks that age, block, and come back on undo", "[unit][position][marks]") {
  const auto loaded = loadVariantToml(kBomber, "bomber");
  REQUIRE(loaded.has_value());
  const VariantSpec& v = *loaded;
  REQUIRE(v.hasBlockingMarks);
  CHECK(v.markTags.size() == 3);  // ep, ruin, scar

  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  const std::uint64_t hash0 = p.hash();
  CHECK(p.marks().empty());

  // The bomber (a1's neighbour: file 0, rank 1) jumps to (1,3): [1,2] from (0,1).
  const CellId from = cellAt(v, 0, 1);
  const CellId land = cellAt(v, 1, 3);
  Undo u1;
  p.make(find(gen, p, from, land), u1);
  // 8 neighbours with ruin, plus the landing cell's scar.
  std::size_t ruin = 0, scar = 0;
  for (const Mark& m : p.marks()) {
    if (m.tag == 1) ++ruin;
    if (m.tag == 2) ++scar;
  }
  CHECK(ruin == 8);
  CHECK(scar == 1);
  CHECK(p.blockedAt(cellAt(v, 2, 3)));
  CHECK_FALSE(p.blockedAt(land));  // the scar blocks nothing
  CHECK(p.hash() == p.computeHash());

  // Black's king moves; the ruins have one ply fewer left, but still block.
  Undo u2;
  p.make(find(gen, p, cellAt(v, 0, 6), cellAt(v, 1, 6)), u2);
  CHECK(p.hash() == p.computeHash());
  // White's rook on rank 0 cannot run up a ruined file... the rook is at (2,0) and the ruins
  // are on files 0-2, ranks 2-4: it stops at (2,1).
  CHECK(has(gen, p, cellAt(v, 2, 0), cellAt(v, 2, 1)));
  CHECK_FALSE(has(gen, p, cellAt(v, 2, 0), cellAt(v, 2, 2)));
  CHECK_FALSE(has(gen, p, cellAt(v, 2, 0), cellAt(v, 2, 3)));

  // Three plies after the bomb, the ruins have gone and the scar has not.
  Undo u3, u4;
  p.make(find(gen, p, cellAt(v, 2, 0), cellAt(v, 3, 0)), u3);
  p.make(find(gen, p, cellAt(v, 1, 6), cellAt(v, 0, 6)), u4);
  ruin = 0;
  scar = 0;
  for (const Mark& m : p.marks()) {
    if (m.tag == 1) ++ruin;
    if (m.tag == 2) ++scar;
  }
  CHECK(ruin == 0);
  CHECK(scar == 1);
  CHECK(p.hash() == p.computeHash());

  (void)hash0;
}

TEST_CASE("undo puts the marks, the blocked set and the hash back", "[unit][position][marks]") {
  const auto loaded = loadVariantToml(kBomber, "bomber");
  REQUIRE(loaded.has_value());
  const VariantSpec& v = *loaded;
  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  const std::uint64_t hash0 = p.hash();
  MoveList moves(v.moveUpperBound());
  gen.generateLegal(p, moves);
  REQUIRE(moves.size() > 0);
  for (const Move& first : moves) {
    Undo u1;
    p.make(first, u1);
    const std::uint64_t afterFirst = p.hash();
    const auto marksFirst = p.marks();
    MoveList replies(v.moveUpperBound());
    gen.generateLegal(p, replies);
    for (const Move& second : replies) {
      Undo u2;
      p.make(second, u2);
      CHECK(p.hash() == p.computeHash());
      CHECK(p.validate());
      p.unmake(second, u2);
      CHECK(p.hash() == afterFirst);
      CHECK(p.marks() == marksFirst);
    }
    p.unmake(first, u1);
    CHECK(p.hash() == hash0);
    CHECK(p.marks().empty());
  }
}

TEST_CASE("a capture at an offset takes the piece beside the landing cell", "[unit][position][marks]") {
  // A checkers jump: leap two forward onto an empty cell, taking the piece jumped over.
  const char* text = R"toml(
name = "jumper"
orientation_axis = "rank"
side_to_move = "white"
[[axis]]
name = "file"
extent = 5
[[axis]]
name = "rank"
extent = 5
[[piece]]
name = "king"
symbol = "K"
royal = true
moves = "king"
[[piece]]
name = "jumper"
symbol = "J"
moves = "exact [rank:+2] leap must captures at [rank:-1]"
[[piece]]
name = "man"
symbol = "M"
moves = "exact [rank:+1] cannot"
[start]
board = "k4/5/2M2/5/KJ3"
)toml";
  const auto loaded = loadVariantToml(text, "jumper");
  REQUIRE(loaded.has_value());
  const VariantSpec& v = *loaded;
  CHECK(v.hasOffsetCaptures);
  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  // The jumper is at (1,0); the man at (2,2): not in line, nothing to jump.
  CHECK_FALSE(has(gen, p, cellAt(v, 1, 0), cellAt(v, 1, 2)));

  // Stand a black man in front of it and the jump is there, and takes him.
  auto next = fromFen(v, "k4/5/5/1m3/KJ3 w - - 0 1");
  REQUIRE(next.has_value());
  Position q = *next;
  const Move jump = find(gen, q, cellAt(v, 1, 0), cellAt(v, 1, 2));
  CHECK(jump.captureCell == cellAt(v, 1, 1));
  Undo u;
  q.make(jump, u);
  CHECK(q.at(cellAt(v, 1, 1)).empty());
  CHECK(q.at(cellAt(v, 1, 2)).type == v.findPiece("jumper"));
  CHECK(q.hash() == q.computeHash());
  q.unmake(jump, u);
  CHECK(q.at(cellAt(v, 1, 1)).type == v.findPiece("man"));
}

TEST_CASE("en passant written as marks and a capture offset is the shipped en passant",
          "[unit][position][marks]") {
  // The shipped pawn, but spelled with the general mechanism and none of the legacy flags:
  // the double step leaves an "ep" mark on the cell it passed for one ply, and the capture
  // lands on a marked cell and takes the piece behind it.
  std::ifstream in(test::variantPath("standard"));
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string text = ss.str();
  const auto a = text.find("name = \"pawn\"");
  const auto b = text.find("[[piece]]", a);
  const std::string pawn = R"toml(name = "pawn"
symbol = "P"
resets_draw_clock = true
promotes_to = ["knight", "bishop", "rook", "queen"]
moves = "exact [rank:+1] cannot U exact [rank:+2] slide cannot unmoved marks ep(passed, 1) U [rank:+1, 1]^1 must captures at [rank:-1] needs ep U [rank:+1, 1]^1 must"

)toml";
  text = text.substr(0, a) + pawn + text.substr(b);
  const auto general = loadVariantToml(text, "standard-marks");
  REQUIRE(general.has_value());
  const VariantSpec shipped = test::loadVariant("standard");

  for (const char* fen : {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
                          // white may take on f6 en passant
                          "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
                          "rnbqkbnr/pppp1ppp/8/4p3/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 2"}) {
    INFO(fen);
    auto a1 = fromFen(shipped, fen);
    auto b1 = fromFen(*general, fen);
    REQUIRE(a1.has_value());
    REQUIRE(b1.has_value());
    const MoveGen ga(shipped);
    const MoveGen gb(*general);
    for (int depth = 1; depth <= 3; ++depth) {
      CHECK(ga.perft(*a1, depth) == gb.perft(*b1, depth));
    }
  }
}

TEST_CASE("marks are written in FEN-N and read back", "[unit][position][marks]") {
  const auto loaded = loadVariantToml(kBomber, "bomber");
  REQUIRE(loaded.has_value());
  const VariantSpec& v = *loaded;
  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  Undo u;
  p.make(find(gen, p, cellAt(v, 0, 1), cellAt(v, 1, 3)), u);
  const std::string fen = toFen(p);
  CHECK(fen.find("scar:b4:0:w") != std::string::npos);
  const auto back = fromFen(v, fen);
  REQUIRE(back.has_value());
  CHECK(back->marks().size() == p.marks().size());
  CHECK(back->hash() == p.hash());
  CHECK(toFen(*back) == fen);
}
