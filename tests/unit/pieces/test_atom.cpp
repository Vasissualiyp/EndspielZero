// SPDX-License-Identifier: GPL-3.0-or-later
#include "pieces/atom.hpp"

#include <set>

#include <catch2/catch_test_macros.hpp>

using namespace cb;

namespace {

SmallVec<std::int16_t, kMaxDims> mags(std::initializer_list<int> v) {
  SmallVec<std::int16_t, kMaxDims> out;
  for (int x : v) out.push(static_cast<std::int16_t>(x));
  return out;
}

std::size_t countDirs(std::initializer_list<int> m, int dims) {
  return expandAtom(mags(m), static_cast<std::uint8_t>(dims)).size();
}

}  // namespace

TEST_CASE("atom expansion matches hand-computed direction counts", "[unit][pieces]") {
  // The table from docs/plan/M1-core-2d.md section M1.4. These are the numbers a
  // reader can check by hand, which is exactly why they are pinned here.
  //                      D=1  D=2  D=3  D=4
  // [1]      rook step     2    4    6    8
  // [1,1]    bishop step   0    4   12   24
  // [1,2]    knight        0    8   24   48
  // [1,1,1]  trigonal      0    0    8   32
  // [1,2,3]  exotic        0    0   48  192
  REQUIRE(countDirs({1}, 1) == 2);
  REQUIRE(countDirs({1}, 2) == 4);
  REQUIRE(countDirs({1}, 3) == 6);
  REQUIRE(countDirs({1}, 4) == 8);

  REQUIRE(countDirs({1, 1}, 1) == 0);
  REQUIRE(countDirs({1, 1}, 2) == 4);
  REQUIRE(countDirs({1, 1}, 3) == 12);
  REQUIRE(countDirs({1, 1}, 4) == 24);

  REQUIRE(countDirs({1, 2}, 2) == 8);
  REQUIRE(countDirs({1, 2}, 3) == 24);
  REQUIRE(countDirs({1, 2}, 4) == 48);

  REQUIRE(countDirs({1, 1, 1}, 2) == 0);
  REQUIRE(countDirs({1, 1, 1}, 3) == 8);
  REQUIRE(countDirs({1, 1, 1}, 4) == 32);

  REQUIRE(countDirs({1, 2, 3}, 3) == 48);
  REQUIRE(countDirs({1, 2, 3}, 4) == 192);
}

TEST_CASE("atom expansion matches the closed form for every atom and dimension",
          "[unit][pieces]") {
  // Enumeration and formula are computed independently; agreement over the whole
  // small-atom space is what catches duplicate-magnitude double counting.
  const std::vector<std::vector<int>> atoms{
      {1},       {2},          {3},          {1, 1},      {1, 2},
      {2, 2},    {1, 3},       {1, 1, 1},    {1, 1, 2},   {1, 2, 2},
      {1, 2, 3}, {1, 1, 1, 1}, {1, 1, 2, 2}, {1, 2, 3, 4}};
  for (const auto& a : atoms) {
    for (int d = 1; d <= kMaxDims; ++d) {
      const auto m = mags({});
      SmallVec<std::int16_t, kMaxDims> mm;
      for (int x : a) mm.push(static_cast<std::int16_t>(x));
      const auto dims = static_cast<std::uint8_t>(d);
      CAPTURE(a, d);
      REQUIRE(static_cast<std::uint64_t>(expandAtom(mm, dims).size()) ==
              expectedDirectionCount(mm, dims));
      (void)m;
    }
  }
}

TEST_CASE("an atom needing more axes than the board has expands to nothing",
          "[unit][pieces]") {
  // A deliberate decision, not an error: a 3-D-only piece on a 2-D board simply
  // has no moves of that kind (M1.4).
  REQUIRE(expandAtom(mags({1, 1, 1}), 2).empty());
  REQUIRE(expandAtom(mags({1, 2, 3, 4}), 3).empty());
}

TEST_CASE("expanded directions are distinct and correctly shaped", "[unit][pieces]") {
  const auto dirs = expandAtom(mags({1, 2}), 3);
  std::set<std::string> seen;
  for (const Direction& d : dirs) {
    REQUIRE(seen.insert(d.toString()).second);  // no duplicates
    REQUIRE(d.nsup == 2);                       // exactly two nonzero components
    std::vector<int> abs;
    for (std::uint8_t i = 0; i < d.n; ++i) {
      if (d.v[i] != 0) abs.push_back(std::abs(d.v[i]));
    }
    std::sort(abs.begin(), abs.end());
    REQUIRE(abs == std::vector<int>{1, 2});
  }
  REQUIRE(dirs.size() == 24);
}

TEST_CASE("expansion is deterministic and canonically ordered", "[unit][pieces]") {
  // Direction indices are baked into the variant's tables and into hashes, so the
  // order must not depend on the compiler or the run.
  const auto a = expandAtom(mags({1, 2}), 4);
  const auto b = expandAtom(mags({2, 1}), 4);  // canonicalization makes these equal
  REQUIRE(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
  for (std::size_t i = 1; i < a.size(); ++i) {
    REQUIRE(std::lexicographical_compare(a[i - 1].v.begin(), a[i - 1].v.begin() + 4,
                                         a[i].v.begin(), a[i].v.begin() + 4));
  }
}

TEST_CASE("the direction count grows as the spec's worst case predicts",
          "[unit][pieces]") {
  // [1,2,3] at 8 dimensions is P(8,3) * 2^3 = 336 * 8 = 2688 directions. The
  // load-time budget exists because of numbers like this (M2.3).
  REQUIRE(expectedDirectionCount(mags({1, 2, 3}), 8) == 2688);
  REQUIRE(expandAtom(mags({1, 2, 3}), 8).size() == 2688);
}

TEST_CASE("atom canonicalization sorts magnitudes and rejects nonsense",
          "[unit][pieces]") {
  MoveAtom a;
  a.mags = mags({3, 1, 2});
  const auto c = MoveAtom::canonicalize(a);
  REQUIRE(c.has_value());
  REQUIRE(c->mags[0] == 1);
  REQUIRE(c->mags[2] == 3);

  SECTION("no magnitudes") {
    MoveAtom bad;
    REQUIRE_FALSE(MoveAtom::canonicalize(bad).has_value());
  }
  SECTION("a zero magnitude is written by omission, not by a zero") {
    MoveAtom bad;
    bad.mags = mags({1, 0});
    const auto r = MoveAtom::canonicalize(bad);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().message.find("positive") != std::string::npos);
  }
  SECTION("maxK of zero") {
    MoveAtom bad;
    bad.mags = mags({1});
    bad.maxK = 0;
    REQUIRE_FALSE(MoveAtom::canonicalize(bad).has_value());
  }
}

TEST_CASE("atoms compare equal after canonicalization", "[unit][pieces]") {
  MoveAtom a;
  a.mags = mags({2, 1});
  MoveAtom b;
  b.mags = mags({1, 2});
  REQUIRE(MoveAtom::canonicalize(a).value() == MoveAtom::canonicalize(b).value());
}

TEST_CASE("the queen reaches every straight line, the king only one or two axes",
          "[unit][pieces]") {
  const std::vector<MoveAtom> king = straightLineAtoms(1);
  REQUIRE(king.size() == 2);
  CHECK(king[0].maxK == 1);
  CHECK(king[0].mode == MoveMode::Leap);
  for (const MoveAtom& a : king) {
    for (const Direction& d : expandAtom(a.mags, 6)) CHECK(d.nsup <= 2);
  }

  for (std::uint8_t dims = 1; dims <= 6; ++dims) {
    const std::vector<MoveAtom> queen = straightLineAtoms(kUnlimited, dims);
    REQUIRE(queen.size() == dims);
    std::size_t total = 0;
    for (std::size_t r = 0; r < queen.size(); ++r) {
      CHECK(queen[r].mags.size() == r + 1);
      CHECK(queen[r].mode == MoveMode::Slide);
      total += expandAtom(queen[r].mags, dims).size();
    }
    // Every nonzero vector in {-1,0,1}^dims, up to scale: 3^dims - 1 directions.
    std::size_t pow3 = 1;
    for (int i = 0; i < dims; ++i) pow3 *= 3;
    CHECK(total == pow3 - 1);
  }
}

TEST_CASE("oriented expansion gives a pawn its direction in any dimension",
          "[unit][pieces]") {
  SECTION("2-D push") {
    const auto white = expandAtomOriented(mags({1}), 2, 1, Color::White);
    REQUIRE(white.size() == 1);
    REQUIRE(white[0].v[1] == 1);
    const auto black = expandAtomOriented(mags({1}), 2, 1, Color::Black);
    REQUIRE(black.size() == 1);
    REQUIRE(black[0].v[1] == -1);
  }
  SECTION("2-D capture") {
    const auto white = expandAtomOriented(mags({1, 1}), 2, 1, Color::White);
    REQUIRE(white.size() == 2);
    for (const Direction& d : white) REQUIRE(d.v[1] == 1);
  }
  SECTION("3-D capture fans out sideways but never backwards") {
    const auto white = expandAtomOriented(mags({1, 1}), 3, 1, Color::White);
    REQUIRE(white.size() == 4);  // (+-1, +1, 0) and (0, +1, +-1)
    for (const Direction& d : white) REQUIRE(d.v[1] == 1);
  }
  SECTION("White and Black expansions are exact negations") {
    const auto w = expandAtomOriented(mags({1, 2}), 4, 0, Color::White);
    const auto b = expandAtomOriented(mags({1, 2}), 4, 0, Color::Black);
    REQUIRE(w.size() == b.size());
    std::set<std::string> negated;
    for (const Direction& d : w) negated.insert(d.negated().toString());
    for (const Direction& d : b) REQUIRE(negated.contains(d.toString()));
  }
}

namespace {

MoveAtom exactAtom(std::initializer_list<Pin> pins, MoveMode mode = MoveMode::Leap) {
  MoveAtom a;
  a.kind = MoveKind::Exact;
  for (const Pin& p : pins) a.pins.push(p);
  a.mode = mode;
  return a;
}

MoveAtom expandedAtom(std::initializer_list<Pin> pins, StepSign sign, std::uint32_t maxK) {
  MoveAtom a;
  a.kind = MoveKind::Expanded;
  for (const Pin& p : pins) a.pins.push(p);
  a.sign = sign;
  a.maxK = maxK;
  a.mode = MoveMode::Slide;
  return a;
}

}  // namespace

TEST_CASE("an exact move is one literal direction", "[unit][pieces]") {
  // [1,0] on (file, rank): one cell along the file, and nothing else.
  const MoveAtom right = *MoveAtom::canonicalize(exactAtom({Pin{0, 1}}));
  const auto dirs = expandAtomFor(right, 2, -1, Color::White);
  REQUIRE(dirs.size() == 1);
  CHECK(dirs[0].v[0] == 1);
  CHECK(dirs[0].v[1] == 0);
  // Without flip, the other colour goes the same way.
  const auto black = expandAtomFor(right, 2, 1, Color::Black);
  REQUIRE(black.size() == 1);
  CHECK(black[0].v[0] == 1);

  // A two-cell exact slide is the unit step taken twice, so the cell between is checked.
  const MoveAtom two = *MoveAtom::canonicalize(exactAtom({Pin{1, 2}}, MoveMode::Slide));
  CHECK(exactSteps(two) == 2);
  const auto step = expandAtomFor(two, 2, -1, Color::White);
  REQUIRE(step.size() == 1);
  CHECK(step[0].v[1] == 1);
  CHECK(stepRange(two).lo == 2);
  CHECK(stepRange(two).hi == 2);
  // A leap of two is a single jump of two.
  const MoveAtom jump = *MoveAtom::canonicalize(exactAtom({Pin{1, 2}}, MoveMode::Leap));
  CHECK(expandAtomFor(jump, 2, -1, Color::White)[0].v[1] == 2);

  // An exact move has no step range.
  MoveAtom ranged = exactAtom({Pin{0, 1}});
  ranged.maxK = 3;
  CHECK_FALSE(MoveAtom::canonicalize(ranged).has_value());
}

TEST_CASE("an expanded move runs both ways unless told otherwise", "[unit][pieces]") {
  const MoveAtom both =
      *MoveAtom::canonicalize(expandedAtom({Pin{0, 1}}, StepSign::Both, kUnlimited));
  CHECK(expandAtomFor(both, 2, -1, Color::White).size() == 2);  // right or left
  const MoveAtom lance =
      *MoveAtom::canonicalize(expandedAtom({Pin{1, 1}}, StepSign::Positive, kUnlimited));
  const auto dirs = expandAtomFor(lance, 2, -1, Color::White);
  REQUIRE(dirs.size() == 1);
  CHECK(dirs[0].v[1] == 1);
  // Five cells forward at most: the range is the step count, not the vector.
  const MoveAtom five = *MoveAtom::canonicalize(expandedAtom({Pin{1, 1}}, StepSign::Positive, 5));
  CHECK(five.maxK == 5);
}

TEST_CASE("flip turns the other side round on the orientation axis only", "[unit][pieces]") {
  MoveAtom push = exactAtom({Pin{1, 1}});
  push.flip = true;
  const MoveAtom p = *MoveAtom::canonicalize(push);
  CHECK(expandAtomFor(p, 2, 1, Color::White)[0].v[1] == 1);
  CHECK(expandAtomFor(p, 2, 1, Color::Black)[0].v[1] == -1);
  // A component on another axis is the same for both: sideways stays sideways.
  MoveAtom diag = exactAtom({Pin{0, 1}, Pin{1, 1}});
  diag.flip = true;
  const MoveAtom d = *MoveAtom::canonicalize(diag);
  const auto black = expandAtomFor(d, 2, 1, Color::Black);
  CHECK(black[0].v[0] == 1);
  CHECK(black[0].v[1] == -1);
}

TEST_CASE("a pinned component with a free one generalizes the pawn's capture", "[unit][pieces]") {
  // Pin forward +1, one free magnitude 1: +/-1 on any other axis with +1 forward. That is
  // two directions on a plane, four in three dimensions, six in four - 2(D-1).
  MoveAtom capture;
  capture.pins.push(Pin{1, 1});
  capture.mags = mags({1});
  capture.flip = true;
  const MoveAtom c = *MoveAtom::canonicalize(capture);
  CHECK(expandAtomFor(c, 2, 1, Color::White).size() == 2);
  CHECK(expandAtomFor(c, 3, 1, Color::White).size() == 4);
  CHECK(expandAtomFor(c, 4, 1, Color::White).size() == 6);
  for (const Direction& d : expandAtomFor(c, 4, 1, Color::Black)) CHECK(d.v[1] == -1);
  // More free magnitudes than free axes: nothing, the engine's deliberate rule.
  capture.mags = mags({1, 1});
  CHECK(expandAtomFor(*MoveAtom::canonicalize(capture), 2, 1, Color::White).empty());
}

TEST_CASE("pins are checked", "[unit][pieces]") {
  CHECK_FALSE(MoveAtom::canonicalize(exactAtom({Pin{0, 0}})).has_value());
  CHECK_FALSE(MoveAtom::canonicalize(exactAtom({Pin{0, 1}, Pin{0, 2}})).has_value());
  MoveAtom none;
  none.kind = MoveKind::Exact;
  CHECK_FALSE(MoveAtom::canonicalize(none).has_value());
  MoveAtom mixed = exactAtom({Pin{1, 1}});
  mixed.oriented = true;
  CHECK_FALSE(MoveAtom::canonicalize(mixed).has_value());
}
