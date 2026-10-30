// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compound moves (M19): castling as data. The mover makes an exact step, the partner is found
// along it, and the cells named must not be attacked - no coordinate anywhere.
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "io/fen.hpp"
#include "io/notation.hpp"
#include "io/variant_toml.hpp"
#include "movegen/movegen.hpp"
#include "position/position.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

/// The shipped standard variant with its two compound moves replaced by the four fixed-cell
/// [[castle]] templates the engine used to have - the legacy path, kept working, and the thing
/// the compound moves are judged against.
std::string standardWithLegacyCastles() {
  std::ifstream in(test::variantPath("standard"));
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string text = ss.str();
  while (true) {
    const auto at = text.find("[[compound]]");
    if (at == std::string::npos) break;
    const auto next = text.find("\n[", at + 1);
    text.erase(at, next == std::string::npos ? std::string::npos : next - at + 1);
  }
  const auto start = text.find("[start]");
  const std::string castles = R"toml(
[[castle]]
name = "K"
color = "white"
king_from = "e1"
king_to = "g1"
rook_from = "h1"
rook_to = "f1"
empty = ["f1", "g1"]
safe = ["e1", "f1", "g1"]
rights_bit = 0

[[castle]]
name = "Q"
color = "white"
king_from = "e1"
king_to = "c1"
rook_from = "a1"
rook_to = "d1"
empty = ["b1", "c1", "d1"]
safe = ["e1", "d1", "c1"]
rights_bit = 1

[[castle]]
name = "k"
color = "black"
king_from = "e8"
king_to = "g8"
rook_from = "h8"
rook_to = "f8"
empty = ["f8", "g8"]
safe = ["e8", "f8", "g8"]
rights_bit = 2

[[castle]]
name = "q"
color = "black"
king_from = "e8"
king_to = "c8"
rook_from = "a8"
rook_to = "d8"
empty = ["b8", "c8", "d8"]
safe = ["e8", "d8", "c8"]
rights_bit = 3

)toml";
  return text.substr(0, start) + castles + text.substr(start);
}

}  // namespace

TEST_CASE("castling as compound moves is the shipped castling", "[unit][position][compound]") {
  const auto legacy = loadVariantToml(standardWithLegacyCastles(), "standard-legacy");
  if (!legacy.has_value()) WARN(legacy.error().format());
  REQUIRE(legacy.has_value());
  REQUIRE(legacy->castles.size() == 4);
  const VariantSpec compoundVariant = test::loadVariant("standard");
  REQUIRE(compoundVariant.castles.empty());
  REQUIRE(compoundVariant.compounds.size() == 2);
  // (the names below read: the legacy variant is "shipped", the compound one is judged)
  const VariantSpec& shipped = *legacy;
  const auto compound = &compoundVariant;

  // The standard positions where castling matters: both wings open, one side blocked, through
  // an attacked cell, out of check, after a rook has gone.
  for (const char* fen :
       {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R b KQkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w Kkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w - - 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R b Qq - 0 1",
        "r3k2r/8/8/8/4r3/8/8/R3K2R w KQkq - 0 1",
        "r3k2r/8/8/8/8/5r2/8/R3K2R w KQkq - 0 1",
        "r3k2r/8/8/8/8/8/8/R2QK2R w KQkq - 0 1"}) {
    INFO(fen);
    auto a = fromFen(shipped, fen);
    auto b = fromFen(*compound, fen);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    // The position reads back as the same FEN.
    CHECK(toFen(*b) == toFen(*a));
    const MoveGen ga(shipped);
    const MoveGen gb(*compound);
    for (int depth = 1; depth <= 3; ++depth) {
      CHECK(ga.perft(*a, depth) == gb.perft(*b, depth));
    }
  }
}

TEST_CASE("the shipped standard castles through compound moves", "[unit][position][compound]") {
  const VariantSpec v = test::loadVariant("standard");
  // Kiwipete, the classic: 48 moves, including both White castles.
  auto p = fromFen(v, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  REQUIRE(p.has_value());
  const MoveGen gen(v);
  CHECK(gen.perft(*p, 1) == 48);
  CHECK(gen.perft(*p, 2) == 2039);
  CHECK(gen.perft(*p, 3) == 97862);
  MoveList moves(v.moveUpperBound());
  gen.generateLegal(*p, moves);
  int castles = 0;
  for (const Move& m : moves) {
    if (m.isCastle()) {
      ++castles;
      const std::string san = sanText(v, *p, m, moves);
      CHECK((san == "O-O" || san == "O-O-O"));
    }
  }
  CHECK(castles == 2);
}

TEST_CASE("castling is lost by moving, and undo gives it back", "[unit][position][compound]") {
  const VariantSpec v = test::loadVariant("standard");
  auto p = fromFen(v, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  REQUIRE(p.has_value());
  const MoveGen gen(v);
  const std::string before = toFen(*p);
  const std::uint64_t hash0 = p->hash();

  MoveList moves(v.moveUpperBound());
  gen.generateLegal(*p, moves);
  for (const Move& m : moves) {
    Undo u;
    p->make(m, u);
    CHECK(p->hash() == p->computeHash());
    CHECK(p->validate());
    // Rook or king moved: that wing's right is gone from the FEN as well.
    const std::string after = toFen(*p);
    if (m.isCastle()) CHECK(after.find(" KQ") == std::string::npos);
    p->unmake(m, u);
    CHECK(toFen(*p) == before);
    CHECK(p->hash() == hash0);
  }
}

TEST_CASE("a compound move works along any axis and any board", "[unit][position][compound]") {
  // A 3-D board where the "king" steps two along the *level* axis and the partner is found
  // above it: no file, no rank, no coordinate anywhere in the definition.
  const char* text = R"toml(
name = "stack"
side_to_move = "white"
[[axis]]
name = "file"
extent = 3
[[axis]]
name = "rank"
extent = 3
[[axis]]
name = "level"
extent = 5
[[piece]]
name = "king"
symbol = "K"
royal = true
moves = "king"
[[piece]]
name = "tower"
symbol = "T"
moves = "[1]^k"
[[compound]]
name = "vault"
piece = "king"
vector = [0, 0, 2]
partner = ["tower"]
safe = ["from", "path", "to"]
fen = "V"
[start]
board = "k2/3/3|3/3/3|3/3/3|3/3/3|KT1/3/3"
)toml";
  const auto loaded = loadVariantToml(text, "stack");
  if (!loaded.has_value()) WARN(loaded.error().format());
  REQUIRE(loaded.has_value());
  const VariantSpec& v = *loaded;
  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  MoveList moves(v.moveUpperBound());
  gen.generateLegal(p, moves);
  int vaults = 0;
  for (const Move& m : moves) vaults += m.isCastle() ? 1 : 0;
  CHECK(vaults == 0);  // the tower is beside the king, not above it: nothing to find
}
