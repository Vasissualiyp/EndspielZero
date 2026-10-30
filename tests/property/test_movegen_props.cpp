// SPDX-License-Identifier: GPL-3.0-or-later
// The core of the project's correctness story (ADR-0009): invariants asserted over
// randomly generated variants, and the optimised generator proven equal to the
// naive oracle. Every bug class these catch is one that hand-written cases in two
// dimensions structurally cannot.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "io/notation.hpp"
#include "movegen/movegen.hpp"
#include "oracle/oracle.hpp"
#include "support/alloc_tripwire.hpp"
#include "support/random_variant.hpp"
#include "variant/standard.hpp"

using namespace cb;

namespace {

std::string describe(const VariantSpec& v) {
  std::string s = v.name + "  axes:";
  for (std::uint8_t a = 0; a < v.dims.dims(); ++a)
    s += " " + std::to_string(v.dims.extent(a));
  s += v.geom.isBox() ? "  box" : "  identified";
  if (!v.geom.isOrientable()) s += " NON-ORIENTABLE";
  s += "  pieces:";
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    for (const MoveAtom& atom : v.pieces[i].atoms) s += " " + atom.toString();
  }
  return s;
}

std::vector<Move> fastPseudoLegal(const MoveGen& gen, const Position& p) {
  MoveList out(p.variant().moveUpperBound());
  gen.generatePseudoLegal(p, out);
  return std::vector<Move>(out.begin(), out.end());
}

/// Play a random legal game, running the requested checks at every position.
template <class Check>
void randomPlayout(Position& p, Rng& rng, int maxPlies, Check check) {
  for (int ply = 0; ply < maxPlies; ++ply) {
    check(p);
    std::vector<Move> moves = oracle::legal(p);
    if (moves.empty()) return;
    const Move m = moves[rng.below(static_cast<std::uint32_t>(moves.size()))];
    Undo u;
    p.make(m, u);
  }
}

}  // namespace

TEST_CASE("the optimised generator agrees with the naive oracle", "[property][movegen]") {
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 40)));
  const VariantSpec v = test::randomVariant(seed);
  INFO("variant: " << describe(v));
  const MoveGen gen(v);
  Position p = Position::startPosition(v);
  Rng rng(seed * 7919 + 13);

  randomPlayout(p, rng, 8, [&](Position& pos) {
    std::vector<Move> fast = fastPseudoLegal(gen, pos);
    std::vector<Move> slow = oracle::pseudoLegal(pos);
    oracle::sortMoves(fast);
    oracle::sortMoves(slow);
    INFO("position: " << pos.hash() << " side " << static_cast<int>(pos.sideToMove()));
    REQUIRE(fast.size() == slow.size());
    for (std::size_t i = 0; i < fast.size(); ++i) {
      INFO("move " << i << " fast " << moveText(v, fast[i]) << " slow "
                   << moveText(v, slow[i]));
      REQUIRE(fast[i] == slow[i]);
    }
  });
}

TEST_CASE("the reverse-walk attack query agrees with a forward scan",
          "[property][movegen]") {
  // isAttacked is the hottest predicate in the engine and the easiest to get
  // subtly wrong on a wrapped board, because it walks directions backwards.
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 25)));
  const VariantSpec v = test::randomVariant(seed);
  INFO("variant: " << describe(v));
  const MoveGen gen(v);
  Position p = Position::startPosition(v);
  Rng rng(seed * 104729 + 7);

  randomPlayout(p, rng, 4, [&](Position& pos) {
    for (CellId c = 0; c < v.dims.cellCount(); ++c) {
      for (int ci = 0; ci < kNumColors; ++ci) {
        const auto by = static_cast<Color>(ci);
        INFO("cell " << c << " by " << ci);
        REQUIRE(gen.isAttacked(pos, c, by) == oracle::isAttacked(pos, c, by));
      }
    }
  });
}

TEST_CASE("attackers agrees with isAttacked", "[property][movegen]") {
  // The path-reporting query and the boolean one must never drift apart.
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 25)));
  const VariantSpec v = test::randomVariant(seed);
  INFO("variant: " << describe(v));
  const MoveGen gen(v);
  Position p = Position::startPosition(v);
  Rng rng(seed * 104729 + 11);
  randomPlayout(p, rng, 4, [&](Position& pos) {
    for (CellId c = 0; c < v.dims.cellCount(); ++c) {
      for (int ci = 0; ci < kNumColors; ++ci) {
        const auto by = static_cast<Color>(ci);
        INFO("cell " << c << " by " << ci);
        REQUIRE(gen.attackers(pos, c, by).empty() == !gen.isAttacked(pos, c, by));
      }
    }
  });
}

TEST_CASE("the optimised generator agrees with the oracle - wide sweep",
          "[property][movegen][slow]") {
  // The same invariant over far more of the variant space. Kept out of the fast
  // loop by its tag, run by CI.
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(100, 400)));
  const VariantSpec v = test::randomVariant(seed);
  INFO("variant: " << describe(v));
  const MoveGen gen(v);
  Position p = Position::startPosition(v);
  Rng rng(seed * 7919 + 13);
  randomPlayout(p, rng, 16, [&](Position& pos) {
    std::vector<Move> fast = fastPseudoLegal(gen, pos);
    std::vector<Move> slow = oracle::pseudoLegal(pos);
    oracle::sortMoves(fast);
    oracle::sortMoves(slow);
    REQUIRE(fast.size() == slow.size());
    for (std::size_t i = 0; i < fast.size(); ++i) REQUIRE(fast[i] == slow[i]);
  });
}

TEST_CASE("make and unmake are exactly reversible", "[property][position]") {
  // Board, occupancy, clocks, rights, en-passant state and the incremental hash
  // must all return to their prior values - the hash by restoration, not by
  // recomputation, so a discrepancy is detectable rather than papered over.
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 50)));
  const VariantSpec v = test::randomVariant(seed);
  INFO("variant: " << describe(v));
  Position p = Position::startPosition(v);
  Rng rng(seed * 31337 + 1);

  randomPlayout(p, rng, 8, [&](Position& pos) {
    const std::uint64_t hashBefore = pos.hash();
    const std::string fenLikeBefore = std::to_string(pos.halfmoveClock()) + "/" +
                                      std::to_string(pos.castleRights()) + "/" +
                                      std::to_string(pos.epTarget());
    for (const Move& m : oracle::pseudoLegal(pos)) {
      Undo u;
      pos.make(m, u);
      REQUIRE(pos.hash() == pos.computeHash());  // incremental == from scratch
      pos.unmake(m, u);
      std::string why;
      INFO("move " << moveText(v, m));
      REQUIRE(pos.validate(&why));
      INFO(why);
      REQUIRE(pos.hash() == hashBefore);
      REQUIRE(std::to_string(pos.halfmoveClock()) + "/" +
                  std::to_string(pos.castleRights()) + "/" +
                  std::to_string(pos.epTarget()) ==
              fenLikeBefore);
    }
  });
}

TEST_CASE("perft agrees with the oracle on random variants", "[property][movegen]") {
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 25)));
  const VariantSpec v = test::randomVariant(seed);
  INFO("variant: " << describe(v));
  const MoveGen gen(v);
  Position p = Position::startPosition(v);
  Position q = Position::startPosition(v);
  REQUIRE(gen.perft(p, 2) == oracle::perft(q, 2));
}

TEST_CASE("legal moves are exactly the pseudo-legal ones that keep the royal safe",
          "[property][movegen]") {
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 30)));
  const VariantSpec v = test::randomVariant(seed);
  const MoveGen gen(v);
  Position p = Position::startPosition(v);
  Rng rng(seed + 991);
  randomPlayout(p, rng, 6, [&](Position& pos) {
    MoveList legal(v.moveUpperBound());
    gen.generateLegal(pos, legal);
    std::vector<Move> fast(legal.begin(), legal.end());
    std::vector<Move> slow = oracle::legal(pos);
    oracle::sortMoves(fast);
    oracle::sortMoves(slow);
    REQUIRE(fast.size() == slow.size());
    for (std::size_t i = 0; i < fast.size(); ++i) REQUIRE(fast[i] == slow[i]);
  });
}

TEST_CASE("move generation does not allocate", "[property][movegen]") {
  // The buffer is reserved outside the scope; generation must only bump a size.
  const auto v = makeStandardChess();
  const MoveGen gen(*v);
  Position p = Position::startPosition(*v);
  MoveList out(v->moveUpperBound());
  {
    cb::test::NoAllocScope guard;
    gen.generatePseudoLegal(p, out);
    REQUIRE(out.size() == 20);
    REQUIRE(guard.clean());
  }
  // And through a make/unmake cycle, which is the actual hot loop.
  {
    MoveList moves(v->moveUpperBound());
    gen.generatePseudoLegal(p, moves);
    cb::test::NoAllocScope guard;
    for (const Move& m : moves) {
      Undo u;
      p.make(m, u);
      (void)gen.inCheck(p, Color::White);
      p.unmake(m, u);
    }
    REQUIRE(guard.clean());
  }
}

TEST_CASE("a random variant is reproducible from its seed", "[property][support]") {
  const VariantSpec a = test::randomVariant(4242);
  const VariantSpec b = test::randomVariant(4242);
  REQUIRE(a.variantId() == b.variantId());
  REQUIRE(a.variantId() != test::randomVariant(4243).variantId());
}
