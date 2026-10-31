// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saving and loading an individual game: variant + start position + move history (M12.7).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/game.hpp"
#include "io/fen.hpp"
#include "io/game_file.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

/// Play the moves named by their long-algebraic text into a fresh game.
Game playText(const VariantSpec& v, const std::vector<std::string>& moves) {
  Game g(v);
  for (const std::string& text : moves) {
    const Move* found = nullptr;
    for (const Move& m : g.legalMoves()) {
      if (moveText(v, m) == text) {
        found = &m;
        break;
      }
    }
    REQUIRE(found != nullptr);
    const Move chosen = *found;
    REQUIRE(g.play(chosen).has_value());
  }
  return g;
}

}  // namespace

TEST_CASE("bundled watch games match and replay on the shipped variants", "[unit][io]") {
  for (const char* name : {"opera", "torus-wrap"}) {
    const std::string path = std::string(CB_SOURCE_DIR) + "/games/" + name + ".cbgame";
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream text;
    text << in.rdbuf();
    auto file = parseGameFile(text.str());
    REQUIRE(file.has_value());
    const VariantSpec variant = test::loadVariant(file->variant);
    Game game(variant);
    INFO(name);
    REQUIRE(file->variantId == variant.variantId());
    REQUIRE(applyGameFile(*file, game).has_value());
    CHECK(game.plyCount() == file->moves.size());
  }
}

TEST_CASE("a game round-trips through a game file", "[unit][io]") {
  const VariantSpec v = test::loadVariant("standard");
  const std::vector<std::string> moves{"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"};
  Game original = playText(v, moves);
  const std::uint64_t hash = original.position().hash();

  const std::string text = toGameFileText(v, original);
  auto parsed = parseGameFile(text);
  REQUIRE(parsed.has_value());
  CHECK(parsed->variant == "standard");
  CHECK(parsed->variantId == v.variantId());
  CHECK(parsed->moves == moves);

  Game loaded(v);
  REQUIRE(applyGameFile(*parsed, loaded).has_value());
  CHECK(loaded.plyCount() == original.plyCount());
  CHECK(loaded.position().hash() == hash);
  CHECK(loaded.moveHistory().size() == original.moveHistory().size());
}

TEST_CASE("a game file replays from its own start position", "[unit][io]") {
  // A game begun from a set-up (a FEN) writes that start, so it replays from the right
  // place rather than from the variant's default.
  const VariantSpec v = test::loadVariant("standard");
  Game g(v);
  const std::string setup = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  auto start = fromFen(v, setup);
  REQUIRE(start.has_value());
  g.setStartPosition(*start);
  REQUIRE(g.position().hash() == start->hash());

  const std::string text = toGameFileText(v, g);
  auto parsed = parseGameFile(text);
  REQUIRE(parsed.has_value());
  Game loaded(v);
  REQUIRE(applyGameFile(*parsed, loaded).has_value());
  CHECK(loaded.startPosition().hash() == start->hash());
  CHECK(loaded.position().hash() == start->hash());
}

TEST_CASE("a game file refuses an illegal move and names it", "[unit][io]") {
  const VariantSpec v = test::loadVariant("standard");
  GameFile file;
  file.variant = "standard";
  file.variantId = v.variantId();
  file.startFen = toFen(Game(v).startPosition());
  file.moves = {"e2e4", "e2e4"};  // the second is not legal
  Game g(v);
  auto ok = applyGameFile(file, g);
  REQUIRE_FALSE(ok.has_value());
  CHECK(ok.error().message.find("move 2") != std::string::npos);
  CHECK(g.plyCount() == 1);  // the first move is kept
}

TEST_CASE("a game file refuses a variant whose rules changed", "[unit][io]") {
  const VariantSpec v = test::loadVariant("standard");
  GameFile file;
  file.variant = "standard";
  file.variantId = v.variantId() ^ 0x1ull;  // a different spec
  file.startFen = toFen(Game(v).startPosition());
  Game g(v);
  auto ok = applyGameFile(file, g);
  REQUIRE_FALSE(ok.has_value());
}

TEST_CASE("game-file parsing rejects a malformed header", "[unit][io]") {
  CHECK_FALSE(parseGameFile("move e2e4\n").has_value());         // no variant
  CHECK_FALSE(parseGameFile("variant standard\n").has_value());  // no start
  CHECK_FALSE(parseGameFile("variant standard\nstart x\nbogus 1\n").has_value());
}

TEST_CASE("a glued variant's game saves and loads", "[unit][io]") {
  const VariantSpec v = test::loadVariant("torus");
  Game g(v);
  // Two legal moves, whatever they are, is enough to prove the file is variant-agnostic.
  const std::size_t want = std::min<std::size_t>(2, g.legalMoves().size());
  std::vector<std::string> moves;
  for (std::size_t i = 0; i < want; ++i) {
    const Move chosen = g.legalMoves().front();
    moves.push_back(moveText(v, chosen));
    REQUIRE(g.play(chosen).has_value());
  }
  const std::uint64_t hash = g.position().hash();
  const std::string text = toGameFileText(v, g);
  auto parsed = parseGameFile(text);
  REQUIRE(parsed.has_value());
  CHECK(parsed->variant == "torus");
  Game loaded(v);
  REQUIRE(applyGameFile(*parsed, loaded).has_value());
  CHECK(loaded.position().hash() == hash);
}
