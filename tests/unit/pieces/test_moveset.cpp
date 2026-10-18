// SPDX-License-Identifier: GPL-3.0-or-later
//
// The moveset language: text that denotes sets of moves.
#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

#include "pieces/moveset.hpp"

using namespace cb;

namespace {

MovesetContext board(std::uint8_t dims, int orient = 1) {
  MovesetContext c;
  c.dims = dims;
  static const char* kNames[]{"file", "rank", "level", "aeon", "e", "f", "g", "h"};
  for (std::uint8_t i = 0; i < dims; ++i) c.axisNames.push_back(kNames[i]);
  c.orientAxis = orient;
  return c;
}

std::size_t directions(const std::vector<MoveAtom>& atoms, std::uint8_t dims, int orient) {
  std::size_t n = 0;
  for (const MoveAtom& a : atoms) n += expandAtomFor(a, dims, orient, Color::White).size();
  return n;
}

}  // namespace

TEST_CASE("the classic pieces are one line each", "[unit][pieces][moveset]") {
  const MovesetContext c = board(2);
  const auto rook = evalMoveset("[1]^k", c);
  REQUIRE(rook.has_value());
  REQUIRE(rook->size() == 1);
  CHECK((*rook)[0].mags.size() == 1);
  CHECK((*rook)[0].maxK == kUnlimited);
  CHECK((*rook)[0].mode == MoveMode::Slide);

  const auto knight = evalMoveset("[1,2]^1", c);
  REQUIRE(knight.has_value());
  CHECK((*knight)[0].maxK == 1);
  CHECK((*knight)[0].mode == MoveMode::Leap);
  CHECK(directions(*knight, 2, 1) == 8);

  // Comments and the union sign.
  const auto queen = evalMoveset("[1]^k  -- the file and rank lines\n \xE2\x88\xAA [1,1]^k", c);
  REQUIRE(queen.has_value());
  CHECK(queen->size() == 2);
}

TEST_CASE("the queen is a union over the board's dimension", "[unit][pieces][moveset]") {
  // 3^D - 1 directions: every nonzero vector in {-1,0,1}^D, up to scale.
  for (const int dd : {2, 3, 4, 8}) {
    const auto d = static_cast<std::uint8_t>(dd);
    const auto queen = evalMoveset("U r in 1..D : [1 x r]^k", board(d));
    REQUIRE(queen.has_value());
    REQUIRE(queen->size() == d);
    std::size_t pow3 = 1;
    for (int i = 0; i < d; ++i) pow3 *= 3;
    CHECK(directions(*queen, d, 1) == pow3 - 1);
  }
  // Written out by hand, it is the same set.
  const auto spelled = evalMoveset("[1]^k U [1,1]^k U [1,1,1]^k U [1,1,1,1]^k", board(4));
  const auto loop = evalMoveset("U r in 1..D : [1 x r]^k", board(4));
  REQUIRE(spelled.has_value());
  REQUIRE(loop.has_value());
  CHECK(*spelled == *loop);
}

TEST_CASE("the three kinds and pinned components read as written", "[unit][pieces][moveset]") {
  const MovesetContext c = board(2);
  const auto lance = evalMoveset("expanded [rank:+1]^k positive", c);
  REQUIRE(lance.has_value());
  REQUIRE(lance->size() == 1);
  CHECK((*lance)[0].kind == MoveKind::Expanded);
  CHECK((*lance)[0].sign == StepSign::Positive);
  CHECK((*lance)[0].flip);  // names the orientation axis
  CHECK(directions(*lance, 2, 1) == 1);

  const auto five = evalMoveset("expanded [rank:+1]^5 positive", c);
  REQUIRE(five.has_value());
  CHECK((*five)[0].maxK == 5);

  const auto push = evalMoveset("exact [rank:+1] cannot", c);
  REQUIRE(push.has_value());
  CHECK((*push)[0].kind == MoveKind::Exact);
  CHECK((*push)[0].capture == CapturePolicy::Cannot);

  const auto two = evalMoveset("exact [rank:+2] slide cannot", c);
  REQUIRE(two.has_value());
  CHECK(exactSteps((*two)[0]) == 2);

  // A literal vector is positional, and signed.
  const auto left = evalMoveset("exact [-1,0]", c);
  REQUIRE(left.has_value());
  const auto dirs = expandAtomFor((*left)[0], 2, 1, Color::White);
  REQUIRE(dirs.size() == 1);
  CHECK(dirs[0].v[0] == -1);

  const auto take = evalMoveset("[rank:+1, 1]^1 must", board(4));
  REQUIRE(take.has_value());
  CHECK(directions(*take, 4, 1) == 6);  // +/-1 on any of the three other axes
}

TEST_CASE("names, imports, difference and definitions", "[unit][pieces][moveset]") {
  MovesetContext c = board(2);
  c.lookup = [](std::string_view name) -> std::optional<std::string> {
    if (name == "rook") return "[1]^k";
    if (name == "bishop") return "[1,1]^k";
    return std::nullopt;
  };
  const auto queen = evalMoveset("rook U bishop", c);
  REQUIRE(queen.has_value());
  CHECK(queen->size() == 2);
  // Union is idempotent and order-independent as a set.
  const auto again = evalMoveset("rook U bishop U rook", c);
  REQUIRE(again.has_value());
  CHECK(*again == *queen);
  const auto swapped = evalMoveset("bishop U rook", c);
  REQUIRE(swapped.has_value());
  CHECK(swapped->size() == 2);
  CHECK(directions(*swapped, 2, 1) == directions(*queen, 2, 1));

  const auto minusRook = evalMoveset("(rook U bishop) \\ rook", c);
  REQUIRE(minusRook.has_value());
  CHECK(*minusRook == *evalMoveset("bishop", c));

  const auto defined = evalMoveset("wazir = [1]^1\nferz = [1,1]^1\nwazir U ferz", c);
  REQUIRE(defined.has_value());
  CHECK(defined->size() == 2);
}

TEST_CASE("every mistake is reported where it is", "[unit][pieces][moveset]") {
  const MovesetContext c = board(2);
  const auto bad = [&](const std::string& text) { return evalMoveset(text, c); };
  const auto message = [&](const std::string& text) {
    const auto r = bad(text);
    REQUIRE_FALSE(r.has_value());
    return r.error();
  };
  CHECK(message("[1,2").message.find("']'") != std::string::npos);
  CHECK(message("[file:+1, nonesuch:+1]").message.find("nonesuch") != std::string::npos);
  CHECK(message("exact [1,0]^3").message.find("step range") != std::string::npos);
  CHECK(message("[1]^k slid").message.find("slid") != std::string::npos);
  CHECK(message("nothing").message.find("no moveset is called") != std::string::npos);
  CHECK(message("[1,1,1]^k U (").message.find("expected") != std::string::npos);
  CHECK(message("[0]").message.find("at least 1") != std::string::npos);
  CHECK(message("[-1]").message.find("unsigned") != std::string::npos);
  CHECK(message("U r in 1..100 : [1]^k").message.find("64") != std::string::npos);
  CHECK(message("r").message.size() > 0);
  CHECK(message("[1]^k \\ [2]^k").message.find("removes nothing") != std::string::npos);
  // The line is named.
  CHECK(message("[1]^k\n[1,2").line == 2);
  // An atom that needs more axes than the board has is *not* an error: it denotes moves
  // that do not exist here, the engine's deliberate rule.
  const auto unicorn = bad("[1,1,1]^k");
  REQUIRE(unicorn.has_value());
  CHECK(directions(*unicorn, 2, 1) == 0);
  // A name defined in terms of itself is stopped, not followed forever.
  MovesetContext loop = board(2);
  loop.lookup = [](std::string_view n) -> std::optional<std::string> {
    if (n == "a") return std::string("a");
    return std::nullopt;
  };
  CHECK_FALSE(evalMoveset("a", loop).has_value());
}

TEST_CASE("print and parse are inverse", "[unit][pieces][moveset]") {
  const MovesetContext c = board(3);
  for (const char* text :
       {"[1]^k", "[1,2]^1", "[1,1]^k U [1,2]^1", "expanded [rank:+1]^5 positive",
        "exact [rank:+2] slide cannot", "[rank:+1, 1]^1 must", "exact [file:+1, rank:+1]",
        "[1]^2..4 slide", "[1,2]^k", "[1]^1 hop", "[1,1,1]^k",
        "exact [rank:+1] noflip"}) {
    INFO(text);
    const auto atoms = evalMoveset(text, c);
    REQUIRE(atoms.has_value());
    const std::string printed = printMoveset(*atoms, c.axisNames, c.orientAxis);
    const auto back = evalMoveset(printed, c);
    INFO(printed);
    REQUIRE(back.has_value());
    CHECK(*back == *atoms);
    // And printing is a fixed point: what is printed prints the same.
    CHECK(printMoveset(*back, c.axisNames, c.orientAxis) == printed);
  }
}

TEST_CASE("malformed text never crashes or loops", "[unit][pieces][moveset]") {
  std::mt19937 rng(12345);
  const std::string alphabet = "[](),:^.\\= \nU1234xin rank file exact expanded k D inf -+_";
  const MovesetContext c = board(3);
  for (int i = 0; i < 4000; ++i) {
    std::string text;
    const std::size_t len = rng() % 40;
    for (std::size_t j = 0; j < len; ++j) text += alphabet[rng() % alphabet.size()];
    (void)evalMoveset(text, c);  // any answer is fine; no crash, no hang
  }
  SUCCEED();
}
