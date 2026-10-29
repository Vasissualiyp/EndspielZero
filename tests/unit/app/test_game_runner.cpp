// SPDX-License-Identifier: GPL-3.0-or-later
//
// The M12.6 runner: stepping a saved game one move at a time so each can be animated,
// and the pure global-time timeline the capture maps frames through. Both are headless -
// they drive a Session and some floats, never a window - which is what makes the clip
// exporter reproducible.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "app/game_runner.hpp"
#include "app/session.hpp"
#include "io/fen.hpp"
#include "io/game_file.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

constexpr const char* kStandardGame =
    "# EndspielZero game v1\n"
    "variant standard\n"
    "start rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\n"
    "move e2e4\n"
    "move e7e5\n"
    "move g1f3\n";

GameFile parseOrThrow(const std::string& text) {
  auto file = parseGameFile(text);
  if (!file.has_value()) throw std::runtime_error(file.error().format());
  return std::move(*file);
}

std::unique_ptr<Session> standardSession() {
  auto session = Session::create(test::loadVariant("standard"));
  if (!session.has_value()) throw std::runtime_error(session.error().format());
  return std::move(*session);
}

}  // namespace

TEST_CASE("the runner plays a game one move at a time to the same position",
          "[unit][app][game-runner]") {
  const GameFile file = parseOrThrow(kStandardGame);
  auto session = standardSession();
  GameRunner runner(file, *session);
  REQUIRE(runner.reset().has_value());
  REQUIRE(runner.count() == 3);

  // One move at a time, exactly as the capture front end drives it.
  while (!runner.done()) {
    REQUIRE(runner.step().has_value());
  }
  CHECK(runner.index() == 3);
  CHECK(session->game().moveHistory().size() == 3);

  // The same file replayed in bulk must land on the same position.
  const VariantSpec spec = test::loadVariant("standard");
  Game reference(spec);
  REQUIRE(applyGameFile(file, reference).has_value());
  CHECK(toFen(session->game().position()) == toFen(reference.position()));
}

TEST_CASE("an illegal move stops the runner with the loader's own error",
          "[unit][app][game-runner]") {
  const GameFile file = parseOrThrow(
      "# EndspielZero game v1\n"
      "variant standard\n"
      "start rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\n"
      "move e2e4\n"
      "move e2e4\n");  // the second is not legal
  auto session = standardSession();
  GameRunner runner(file, *session);
  REQUIRE(runner.reset().has_value());
  REQUIRE(runner.step().has_value());
  CHECK(runner.index() == 1);
  CHECK(session->game().moveHistory().size() == 1);

  const auto stopped = runner.step();
  REQUIRE_FALSE(stopped.has_value());
  // The message matches `applyGameFile`'s: names the 1-based ply and the move.
  CHECK(stopped.error().format().find("move 2 ('e2e4')") != std::string::npos);
  // Position unchanged past the last legal move.
  CHECK(runner.index() == 1);
  CHECK(session->game().moveHistory().size() == 1);
}

TEST_CASE("a game file for another variant of the same name is refused",
          "[unit][app][game-runner]") {
  GameFile file = parseOrThrow(kStandardGame);
  file.variantId = 12345;  // a rules change since the save
  auto session = standardSession();
  GameRunner runner(file, *session);
  const auto reset = runner.reset();
  REQUIRE_FALSE(reset.has_value());
  CHECK(reset.error().format().find("different variant") != std::string::npos);
}

TEST_CASE("a game can begin from a stated set-up position", "[unit][app][game-runner]") {
  // Acceptance: the start position can be a FEN-N, not just the variant's opening array.
  GameFile file;
  file.variant = "standard";
  file.variantId = 0;
  file.startFen = "4k3/8/8/8/8/8/8/R3K3 w - - 0 1";
  file.moves = {"a1a7"};

  auto session = standardSession();
  GameRunner runner(file, *session);
  REQUIRE(runner.reset().has_value());
  REQUIRE(runner.step().has_value());
  CHECK(toFen(session->game().position()) == "4k3/R7/8/8/8/8/8/4K3 b - - 1 1");
}

TEST_CASE("a 4-D variant plays through the same game-file format",
          "[unit][app][game-runner]") {
  // The file format is the engine's own notation, so a board above two dimensions needs
  // no second parser - which the acceptance asks of `hyper4`.
  const VariantSpec spec = test::loadVariant("hyper4");
  Game probe(spec);
  REQUIRE_FALSE(probe.legalMoves().empty());
  const Move first = probe.legalMoves().front();

  GameFile file;
  file.variant = "hyper4";
  file.variantId = 0;
  file.startFen = toFen(Position::startPosition(spec));
  file.moves = {moveText(spec, first)};

  auto session = Session::create(test::loadVariant("hyper4"));
  REQUIRE(session.has_value());
  Session& s = **session;
  GameRunner runner(file, s);
  REQUIRE(runner.reset().has_value());
  REQUIRE(runner.step().has_value());
  CHECK(s.game().moveHistory().size() == 1);

  REQUIRE(probe.play(first).has_value());
  CHECK(toFen(s.game().position()) == toFen(probe.position()));
}

TEST_CASE("the playback timeline maps a global time to a move and its elapsed seconds",
          "[unit][app][game-runner]") {
  const std::vector<float> bodies{2.0f, 3.0f};
  const float dwell = 0.5f;
  CHECK(playbackTotalSeconds(bodies, dwell) == 5.5f);

  CHECK(playbackCursor(bodies, dwell, 0.0f).move == 0);
  CHECK(playbackCursor(bodies, dwell, 0.5f).elapsed == 0.5f);

  // Inside the first move's dwell the picture is the landed pose.
  const PlaybackCursor held = playbackCursor(bodies, dwell, 2.25f);
  CHECK(held.move == 0);
  CHECK(held.elapsed == 2.0f);

  // The second move begins after the dwell.
  const PlaybackCursor second = playbackCursor(bodies, dwell, 2.5f);
  CHECK(second.move == 1);
  CHECK(second.elapsed == 0.0f);

  // Past the end, the final landed pose is held.
  const PlaybackCursor end = playbackCursor(bodies, dwell, 99.0f);
  CHECK(end.move == 1);
  CHECK(end.elapsed == 3.0f);
}
