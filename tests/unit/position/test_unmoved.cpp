// SPDX-License-Identifier: GPL-3.0-or-later
//
// "Has this piece moved?" as a fact about the piece: stored, hashed, undone, in FEN-N.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "io/fen.hpp"
#include "io/variant_toml.hpp"
#include "movegen/movegen.hpp"
#include "position/position.hpp"

using namespace cb;

namespace {

// A 5x5 board with a king each, a "runner" (one step forward or back, or two forward from
// the start - and the two only while it has never moved) and a knight, which nothing asks
// "has it moved?" about.
const char* kText = R"(
name = "runner"
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
name = "runner"
symbol = "R"
moves = "exact [rank:+1] cannot U exact [rank:+2] slide cannot unmoved U exact [rank:-1] cannot"
[[piece]]
name = "knight"
symbol = "N"
moves = "[1,2]^1"
[start]
board = "k4/5/5/2R2/K3N"
)";

struct Rig {
  VariantSpec v;
  explicit Rig(VariantSpec spec) : v(std::move(spec)) {}
  [[nodiscard]] CellId at(int file, int rank) const {
    Coord c(2);
    c.c[0] = static_cast<std::int16_t>(file);
    c.c[1] = static_cast<std::int16_t>(rank);
    return v.dims.toCell(c);
  }
};

Move find(const MoveGen& gen, Position& p, CellId from, CellId to) {
  MoveList moves(p.variant().moveUpperBound());
  gen.generateLegal(p, moves);
  for (const Move& m : moves) {
    if (m.from == from && m.to == to) return m;
  }
  FAIL("no such move");
  return {};
}

std::size_t countFrom(const MoveGen& gen, Position& p, CellId from) {
  MoveList moves(p.variant().moveUpperBound());
  gen.generateLegal(p, moves);
  std::size_t n = 0;
  for (const Move& m : moves) n += m.from == from ? 1u : 0u;
  return n;
}

/// White's piece goes `a` -> `b` and back, black's king going out and back alongside, so the
/// same side is to move and every piece is where it began.
std::uint64_t cycle(const MoveGen& gen, Position& p, const Rig& r, CellId a, CellId b) {
  Undo u[4];
  p.make(find(gen, p, a, b), u[0]);
  p.make(find(gen, p, r.at(0, 4), r.at(0, 3)), u[1]);
  p.make(find(gen, p, b, a), u[2]);
  p.make(find(gen, p, r.at(0, 3), r.at(0, 4)), u[3]);
  return p.hash();
}

}  // namespace

TEST_CASE("a move that needs the piece unmoved is available until it has moved",
          "[unit][position]") {
  auto loaded = loadVariantToml(kText, "runner");
  REQUIRE(loaded.has_value());
  const Rig r(std::move(*loaded));
  const VariantSpec& v = r.v;
  REQUIRE(v.pieces[v.findPiece("runner")].tracksMoved);
  CHECK_FALSE(v.pieces[v.findPiece("knight")].tracksMoved);

  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  const CellId home = r.at(2, 1);
  CHECK_FALSE(p.at(home).hasMoved());
  CHECK(countFrom(gen, p, home) == 3);  // forward one, forward two, back one

  // Out and back to the very same square: it has moved, so the double step is gone.
  cycle(gen, p, r, home, r.at(2, 2));
  CHECK(p.at(home).hasMoved());
  CHECK(countFrom(gen, p, home) == 2);  // forward one and back one
  CHECK(p.hash() == p.computeHash());
  CHECK(p.validate());
}

TEST_CASE("only the pieces something asks about carry the bit into the hash", "[unit][position]") {
  auto loaded = loadVariantToml(kText, "runner");
  REQUIRE(loaded.has_value());
  const Rig r(std::move(*loaded));
  const MoveGen gen(r.v);

  // The knight goes out and back: the position is the same position, and hashes the same,
  // which is what repetition detection needs.
  Position knight = Position::startPosition(r.v);
  const std::uint64_t start = knight.hash();
  CHECK(cycle(gen, knight, r, r.at(4, 0), r.at(3, 2)) == start);

  // The runner goes out and back: it has moved, which is a different position.
  Position runner = Position::startPosition(r.v);
  CHECK(cycle(gen, runner, r, r.at(2, 1), r.at(2, 2)) != start);
}

TEST_CASE("undo restores the moved bit, the hash and the FEN", "[unit][position]") {
  auto loaded = loadVariantToml(kText, "runner");
  REQUIRE(loaded.has_value());
  const VariantSpec& v = *loaded;
  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  const std::uint64_t hash0 = p.hash();
  const std::string fen0 = toFen(p);

  MoveList moves(v.moveUpperBound());
  gen.generateLegal(p, moves);
  REQUIRE(moves.size() > 0);
  for (const Move& m : moves) {
    Undo u;
    p.make(m, u);
    CHECK(p.hash() == p.computeHash());
    CHECK(p.validate());
    p.unmake(m, u);
    CHECK(p.hash() == hash0);
    CHECK(toFen(p) == fen0);
  }
}

TEST_CASE("FEN-N names the unmoved pieces only when it is not the usual answer",
          "[unit][position]") {
  auto loaded = loadVariantToml(kText, "runner");
  REQUIRE(loaded.has_value());
  const Rig r(std::move(*loaded));
  const VariantSpec& v = r.v;
  const CellId home = r.at(2, 1);

  const Position start = Position::startPosition(v);
  const std::string fen = toFen(start);
  CHECK(std::count(fen.begin(), fen.end(), ' ') == 5);  // six fields, as ever

  // Without the field, a piece on its start square is unmoved...
  const auto usual = fromFen(v, fen);
  REQUIRE(usual.has_value());
  CHECK_FALSE(usual->at(home).hasMoved());
  CHECK(usual->hash() == start.hash());
  // ...and with "-" it says nothing is, which is a different position and is written back.
  const auto moved = fromFen(v, fen + " -");
  REQUIRE(moved.has_value());
  CHECK(moved->at(home).hasMoved());
  CHECK(moved->hash() != start.hash());
  CHECK(moved->hash() == moved->computeHash());
  CHECK(toFen(*moved) == fen + " -");

  // Naming the cell makes it unmoved again, and that is the usual answer, so it is omitted.
  const auto named = fromFen(v, fen + " c2");
  REQUIRE(named.has_value());
  CHECK_FALSE(named->at(home).hasMoved());
  CHECK(toFen(*named) == fen);
}
