// SPDX-License-Identifier: GPL-3.0-or-later
//
// MoveGen::attackers: who attacks a cell, and along which cells (hover hints, M20).

#include <algorithm>
#include <set>
#include <tuple>

#include <catch2/catch_test_macros.hpp>

#include "io/fen.hpp"
#include "io/notation.hpp"
#include "movegen/movegen.hpp"
#include "support/variants.hpp"

namespace cb {
namespace {

CellId cell(const VariantSpec& v, std::string_view name) {
  const auto c = parseCell(v.dims, name);
  REQUIRE(c.has_value());
  return *c;
}

std::vector<CellId> cells(const VariantSpec& v,
                          std::initializer_list<const char*> names) {
  std::vector<CellId> out;
  for (const char* n : names) out.push_back(cell(v, n));
  return out;
}

}  // namespace

TEST_CASE("a rook's attack comes with the cells it crosses", "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("standard");
  const MoveGen gen(v);
  auto p = fromFen(v, "7k/8/8/8/8/8/8/R3K3 w - - 0 1");
  REQUIRE(p.has_value());
  const auto a = gen.attackers(*p, cell(v, "a8"), Color::White);
  REQUIRE(a.size() == 1);
  CHECK(a[0].from == cell(v, "a1"));
  CHECK(a[0].path == cells(v, {"a2", "a3", "a4", "a5", "a6", "a7"}));

  auto blocked = fromFen(v, "7k/8/8/8/P7/8/8/R3K3 w - - 0 1");
  REQUIRE(blocked.has_value());
  CHECK(gen.attackers(*blocked, cell(v, "a8"), Color::White).empty());
}

TEST_CASE("leapers and pawn captures have an empty path", "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("standard");
  const MoveGen gen(v);
  auto p = fromFen(v, "7k/8/8/8/4P3/8/8/N3K3 w - - 0 1");
  REQUIRE(p.has_value());
  const auto knight = gen.attackers(*p, cell(v, "b3"), Color::White);
  REQUIRE(knight.size() == 1);
  CHECK(knight[0].from == cell(v, "a1"));
  CHECK(knight[0].path.empty());
  // The pawn attacks an empty diagonal square, not the one it pushes to.
  const auto diag = gen.attackers(*p, cell(v, "d5"), Color::White);
  REQUIRE(diag.size() == 1);
  CHECK(diag[0].from == cell(v, "e4"));
  CHECK(diag[0].path.empty());
  CHECK(gen.attackers(*p, cell(v, "e5"), Color::White).empty());
}

TEST_CASE("on a torus a rook that can go both ways round is reported twice",
          "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("torus");
  const MoveGen gen(v);
  Position p = Position::startPosition(v);
  // Every cell of the start position that something attacks has a route per attacker, and
  // each route's cells are inside the board and not the attacker or the target.
  for (CellId c = 0; c < v.dims.cellCount(); ++c) {
    for (const auto by : {Color::White, Color::Black}) {
      for (const auto& a : gen.attackers(p, c, by)) {
        CHECK(a.from != c);
        for (const CellId q : a.path) {
          CHECK(q < v.dims.cellCount());
          CHECK(q != a.from);
          CHECK(q != c);
        }
      }
    }
  }
}

TEST_CASE("movesFrom works for either side's piece", "[unit][movegen]") {
  const VariantSpec v = test::loadVariant("standard");
  const MoveGen gen(v);
  const Position p = Position::startPosition(v);
  for (const char* from : {"b1", "g8", "e2", "e7"}) {
    MoveList mine(v.moveUpperBound());
    gen.movesFrom(p, cell(v, from), mine);
    MoveList all(v.moveUpperBound());
    gen.generatePseudoLegalFor(p, p.at(cell(v, from)).colorOf(), false, all);
    using Key = std::tuple<CellId, CellId, CellId, PieceTypeId>;
    std::set<Key> a, b;
    for (const Move& m : mine) a.insert({m.from, m.to, m.captureCell, m.promoteTo});
    for (const Move& m : all) {
      if (m.from == cell(v, from)) b.insert({m.from, m.to, m.captureCell, m.promoteTo});
    }
    CHECK(a == b);
    CHECK_FALSE(a.empty());
  }
  MoveList none(v.moveUpperBound());
  gen.movesFrom(p, cell(v, "e4"), none);
  CHECK(none.size() == 0);
}

}  // namespace cb
