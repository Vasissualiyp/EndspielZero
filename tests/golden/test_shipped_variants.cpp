// SPDX-License-Identifier: GPL-3.0-or-later
// Every variant in variants/ is held to the same bar, found by scanning the
// directory so that a newly added variant is covered without anyone remembering to
// register it.
//
// The "has a legal move at the start" check exists because it caught a real bug:
// the first torus and Klein-bottle variants shipped here used the ordinary chess
// opening array, and on a board whose ranks are glued that puts the two back rows in
// contact - White began in an inescapable check and the game was over before a move.
// Gluing the board changes what a sensible starting position is, and only a test can
// be relied on to notice.
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "game/game.hpp"
#include "io/fen.hpp"
#include "io/variant_toml.hpp"
#include "movegen/movegen.hpp"
#include "oracle/oracle.hpp"
#include "pieces/atom.hpp"
#include "support/geometry_helpers.hpp"
#include "support/variants.hpp"

namespace fs = std::filesystem;
using namespace cb;

namespace {

std::vector<std::string> shippedVariantNames() {
  std::vector<std::string> names;
  for (const auto& e : fs::directory_iterator(test::repoRoot() / "variants")) {
    if (e.is_regular_file() && e.path().extension() == ".toml") {
      names.push_back(e.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace

TEST_CASE("every shipped variant loads, plays, and round-trips", "[golden][variants]") {
  const auto names = shippedVariantNames();
  REQUIRE(names.size() >= 8);

  for (const std::string& name : names) {
    CAPTURE(name);
    const auto loaded = loadVariantFile(test::variantPath(name));
    REQUIRE(loaded.has_value());
    const VariantSpec& v = *loaded;
    REQUIRE(v.finalized());
    REQUIRE(v.variantId() != 0);
    // A variant is a game someone should want to play, so it says what it is. The
    // picker shows this line; an empty one is a hole in the interface.
    REQUIRE_FALSE(v.description.empty());

    Position p = Position::startPosition(v);
    std::string why;
    REQUIRE(p.validate(&why));
    INFO(why);

    // A playable opening position: the side to move has something to do, and is not
    // already checkmated or stalemated.
    const MoveGen gen(v);
    MoveList legal(v.moveUpperBound());
    gen.generateLegal(p, legal);
    REQUIRE(legal.size() > 0);

    // FEN-N round-trip, including the hash.
    const std::string fen = toFen(p);
    const auto back = fromFen(v, fen);
    REQUIRE(back.has_value());
    REQUIRE(toFen(*back) == fen);
    REQUIRE(back->hash() == p.hash());

    // And the optimised generator agrees with the naive oracle.
    Position q = Position::startPosition(v);
    REQUIRE(gen.perft(p, 2) == oracle::perft(q, 2));
  }
}

TEST_CASE("shipped variant node counts", "[golden][variants]") {
  // Regression pins. Self-generated, and validated only by the oracle agreement
  // above plus, for standard chess, the published perft suite - so their job is to
  // notice change, not to prove correctness.
  struct Expect {
    const char* name;
    std::uint64_t depth1;
    std::uint64_t depth2;
  };
  const Expect cases[] = {
      {"standard", 20, 400},       {"cylinder", 20, 392},  {"torus", 54, 2535},
      {"mobius", 24, 576},         {"klein", 48, 1977},    {"torus3d", 34, 1028},
      {"torus3d_twist", 42, 1616}, {"mirrorbox", 20, 396}, {"cube5", 56, 3095},
      {"hyper4", 39, 1380},        {"t6", 41, 2216},
  };
  for (const Expect& c : cases) {
    CAPTURE(c.name);
    const VariantSpec v = test::loadVariant(c.name);
    Position p = Position::startPosition(v);
    const MoveGen gen(v);
    REQUIRE(gen.perft(p, 1) == c.depth1);
    REQUIRE(gen.perft(p, 2) == c.depth2);
  }
}

TEST_CASE("the queen moves along every straight line, the king one or two axes",
          "[golden][variants]") {
  // `straightLineAtoms` is the single definition, and this checks the data agrees with
  // it: the queen has a slider [1]^r for every r up to the board's dimension, so on a
  // cube she also rides the triagonals and on a tesseract the quadragonals.
  for (const std::string& name : shippedVariantNames()) {
    CAPTURE(name);
    const auto loaded = loadVariantFile(test::variantPath(name));
    REQUIRE(loaded.has_value());
    for (const PieceTypeDef& p : loaded->pieces) {
      if (p.name == "queen")
        CHECK(p.atoms == straightLineAtoms(kUnlimited, loaded->dims.dims()));
      if (p.name == "king") CHECK(p.atoms == straightLineAtoms(1));
    }
  }
}

TEST_CASE("rule-carrying variants filter moves at the game level", "[golden][variants]") {
  // The distinction the table above cannot express: MoveGen produces what the pieces can
  // do, and Game produces what the *rules* allow. For a forced-capture variant those are
  // different numbers, and it matters which one a front end asks for.
  const VariantSpec must = test::loadVariant("mustcapture");
  auto withCapture = fromFen(must, "4k3/8/3p4/8/4N3/8/8/4K3 w - - 0 1");
  REQUIRE(withCapture.has_value());

  Game g(must);
  g.position() = *withCapture;
  MoveList raw(must.moveUpperBound());
  MoveGen(must).generateLegal(g.position(), raw);

  REQUIRE(raw.size() > g.legalMoves().size());
  REQUIRE(g.legalMoves().size() == 1);  // only the knight's capture survives the filter
}

TEST_CASE("the geometry of each shipped board is what its file claims",
          "[golden][variants]") {
  SECTION("cylinder wraps the files and keeps orientation") {
    const VariantSpec v = test::loadVariant("cylinder");
    REQUIRE_FALSE(v.geom.isBox());
    REQUIRE(v.geom.isOrientable());
    // A rook on a1 reaches h1 by leaving the board's left edge.
    const CellId a1 = v.dims.toCell(Coord::of({0, 0}));
    const test::Orbit west = test::walk(v.geom, a1, test::dir(v.dims, {-1, 0}));
    REQUIRE(west.cells.front() == v.dims.toCell(Coord::of({7, 0})));
    // Because the rank axis is untouched, pawns and promotion survive.
    REQUIRE(v.orientationAxis >= 0);
    REQUIRE(v.promotion[static_cast<std::size_t>(Color::White)].active());
    REQUIRE_FALSE(v.castles.empty());
  }

  SECTION("torus has no edges and therefore no promotion rank") {
    const VariantSpec v = test::loadVariant("torus");
    REQUIRE(v.geom.isOrientable());
    // Pawns survive - an orientable board still has a global "forward" - but there
    // is no last rank to reach, so no promotion region is declared (M3.4).
    REQUIRE(v.findPiece("pawn") != kNoPiece);
    REQUIRE_FALSE(v.promotion[static_cast<std::size_t>(Color::White)].active());
    REQUIRE(v.castles.empty());
    // Every cell has all eight neighbours.
    for (CellId c = 0; c < v.dims.cellCount(); ++c) {
      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          if (dx == 0 && dy == 0) continue;
          Walker w = v.geom.start(c, test::dir(v.dims, {dx, dy}));
          REQUIRE(v.geom.step(w));
        }
      }
    }
  }

  SECTION("klein is non-orientable and has no oriented pieces") {
    const VariantSpec v = test::loadVariant("klein");
    REQUIRE_FALSE(v.geom.isOrientable());
    // The consequence that forces the design: no piece may rely on "forward".
    REQUIRE(v.findPiece("pawn") == kNoPiece);
    for (std::size_t i = 1; i < v.pieces.size(); ++i) {
      for (const MoveAtom& a : v.pieces[i].atoms) {
        CAPTURE(v.pieces[i].name);
        REQUIRE_FALSE(a.oriented);
      }
    }
    // And the fingerprint of the surface: a bishop is not colour-bound, because no
    // global two-colouring of a Klein bottle exists.
    const std::vector<Direction> diagonals{
        test::dir(v.dims, {1, 1}), test::dir(v.dims, {1, -1}), test::dir(v.dims, {-1, 1}),
        test::dir(v.dims, {-1, -1})};
    const auto reach =
        test::reachable(v.geom, v.dims.toCell(Coord::of({0, 0})), diagonals);
    REQUIRE(reach.size() == v.dims.cellCount());
  }

  SECTION("mobius reverses orientation after one circuit") {
    const VariantSpec v = test::loadVariant("mobius");
    REQUIRE_FALSE(v.geom.isOrientable());
    // Its pawns are the exception to "no oriented pieces on a non-orientable board":
    // they turn round on crossing the seam (Piece::flipped, tests/unit/movegen).
    REQUIRE(v.pieces[v.findPiece("pawn")].reorients);
    // Eight steps along the glued axis land on the mirrored rank, not at home.
    Walker w = v.geom.start(v.dims.toCell(Coord::of({0, 0})), test::dir(v.dims, {1, 0}));
    for (int i = 0; i < 8; ++i) REQUIRE(v.geom.step(w));
    REQUIRE(w.coord == Coord::of({0, 7}));
    for (int i = 0; i < 8; ++i) REQUIRE(v.geom.step(w));
    REQUIRE(w.coord == Coord::of({0, 0}));
  }

  SECTION("mirrorbox reflects instead of gluing") {
    const VariantSpec v = test::loadVariant("mirrorbox");
    REQUIRE_FALSE(v.geom.isBox());
    // A reflecting wall turns rays around but glues nothing, so the surface is still
    // orientable even though the face transform reverses handedness.
    REQUIRE(v.geom.isOrientable());
    REQUIRE(v.geom.hasOrientationReversingFace());
    // Nothing is identified, so the board still has 64 distinct cells, and pawns and
    // promotion are kept.
    REQUIRE(v.dims.cellCount() == 64);
    REQUIRE(v.promotion[static_cast<std::size_t>(Color::White)].active());
    // A ray reaching the h-file comes back along the mirrored direction.
    Walker w = v.geom.start(v.dims.toCell(Coord::of({7, 3})), test::dir(v.dims, {1, 0}));
    REQUIRE(v.geom.step(w));
    REQUIRE(w.coord == Coord::of({7, 3}));
    REQUIRE(w.dir.v[0] == -1);
  }

  SECTION("torus3d glues all three axes") {
    const VariantSpec v = test::loadVariant("torus3d");
    REQUIRE(v.dims.dims() == 3);
    for (CellId c = 0; c < v.dims.cellCount(); ++c) {
      for (int a = 0; a < 3; ++a) {
        std::array<std::int16_t, kMaxDims> vec{};
        vec[static_cast<std::size_t>(a)] = 1;
        Walker w = v.geom.start(c, Direction::make(vec, 3));
        REQUIRE(v.geom.step(w));
      }
    }
  }

  SECTION("torus3d_twist's rank flip frees the bishop's colour") {
    // M17.22. The flip is declared on the *rank* identification with `flip = ["file"]`,
    // exactly as `klein` does in two dimensions. The direction transport is derived, not
    // authored per axis count, so the same statement must carry into three dimensions:
    // a bishop's diagonals preserve the parity of the coordinate sum, so on the untwisted
    // `torus3d` it is confined to one colour class (half of the 64 cells); the twist
    // flips the file sign across the rank seam, parity is no longer preserved, and the
    // bishop reaches every cell - the three-dimensional analogue of Klein's own test.
    const VariantSpec v = test::loadVariant("torus3d_twist");
    REQUIRE(v.dims.dims() == 3);
    REQUIRE_FALSE(v.geom.isOrientable());  // the flip makes the 3-torus non-orientable
    const VariantSpec straight = test::loadVariant("torus3d");
    REQUIRE(straight.geom.isOrientable());

    // The bishop's own atoms: every diagonal of every coordinate plane.
    const std::vector<Direction> diagonals{
        test::dir(v.dims, {1, 1, 0}),  test::dir(v.dims, {1, -1, 0}),
        test::dir(v.dims, {-1, 1, 0}), test::dir(v.dims, {-1, -1, 0}),
        test::dir(v.dims, {1, 0, 1}),  test::dir(v.dims, {1, 0, -1}),
        test::dir(v.dims, {-1, 0, 1}), test::dir(v.dims, {-1, 0, -1}),
        test::dir(v.dims, {0, 1, 1}),  test::dir(v.dims, {0, 1, -1}),
        test::dir(v.dims, {0, -1, 1}), test::dir(v.dims, {0, -1, -1})};
    const CellId start = v.dims.toCell(Coord::of({0, 0, 0}));
    const auto reach = test::reachable(v.geom, start, diagonals);
    const auto confined = test::reachable(
        straight.geom, straight.dims.toCell(Coord::of({0, 0, 0})), diagonals);
    CHECK(reach.size() == v.dims.cellCount());
    CHECK(confined.size() == straight.dims.cellCount() / 2);
    CHECK(confined.size() < reach.size());
  }
}
