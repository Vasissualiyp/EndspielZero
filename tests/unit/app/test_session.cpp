// SPDX-License-Identifier: GPL-3.0-or-later
// The whole interaction path, driven by scripts, with no window and no screenshots.
//
// Selection, illegal clicks, promotion choice, undo and view changes are all just
// actions in, snapshot out - so the flows a player actually performs are covered by
// ordinary assertions rather than by someone clicking around (M4.6).
#include "app/session.hpp"

#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "io/fen.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::unique_ptr<Session> open(const char* variantName) {
  auto s = Session::create(test::loadVariant(variantName));
  REQUIRE(s.has_value());
  return std::move(*s);
}

CellId cell(const Session& s, const char* name) {
  auto c = parseCell(s.variant().dims, name);
  REQUIRE(c.has_value());
  return *c;
}

}  // namespace

TEST_CASE("presentation epochs identify replacement rather than ordinary activity",
          "[unit][app][panel-response]") {
  auto s = open("standard");
  const std::string fen = toFen(s->game().position());
  const auto epoch = s->presentationEpoch();
  REQUIRE(
      s->applyScript("click e2\nhover e3\norbit 0.1 0.1\nclick e4\nundo").has_value());
  s->advanceAnimation(0.1f);
  CHECK(s->presentationEpoch() == epoch);
  REQUIRE_FALSE(s->loadFen("bad fen").has_value());
  CHECK(s->presentationEpoch() == epoch);
  REQUIRE(s->loadFen(fen).has_value());
  CHECK(s->presentationEpoch() != epoch);
  auto before = s->presentationEpoch();
  REQUIRE(s->loadFen(fen).has_value());
  CHECK(s->presentationEpoch() != before);
  before = s->presentationEpoch();
  REQUIRE(s->applyScript("reset").has_value());
  CHECK(s->presentationEpoch() != before);

  GameFile file;
  file.variant = "another-variant";
  file.startFen = fen;
  before = s->presentationEpoch();
  REQUIRE_FALSE(s->loadGame(file).has_value());
  CHECK(s->presentationEpoch() == before);
  file.variant = "standard";
  file.moves = {"e2e4", "e7e5"};
  REQUIRE(s->loadGame(file).has_value());
  CHECK(s->presentationEpoch() != before);
  before = s->presentationEpoch();
  file.moves = {"e2e4", "e2e4"};
  REQUIRE_FALSE(s->loadGame(file).has_value());
  CHECK(s->game().plyCount() == 1);  // import may mutate before reporting failure
  CHECK(s->presentationEpoch() != before);
}

TEST_CASE("clicking a piece selects it and shows its legal moves", "[unit][app]") {
  auto s = open("standard");
  REQUIRE(s->selected() == kInvalidCell);
  REQUIRE(s->snapshot().highlighted().empty());

  REQUIRE(s->applyScript("click e2").has_value());
  REQUIRE(s->selected() == cell(*s, "e2"));
  // A pawn on its home rank has exactly two destinations, and they are the engine's,
  // not the renderer's idea of them.
  REQUIRE(s->snapshot().highlighted().size() == 2);
  const auto lit = s->snapshot().highlighted();
  REQUIRE(std::find(lit.begin(), lit.end(), cell(*s, "e3")) != lit.end());
  REQUIRE(std::find(lit.begin(), lit.end(), cell(*s, "e4")) != lit.end());
}

TEST_CASE("clicking a highlighted destination plays the move", "[unit][app]") {
  auto s = open("standard");
  REQUIRE(s->applyScript("click e2\nclick e4").has_value());
  REQUIRE(s->game().plyCount() == 1);
  REQUIRE(s->selected() == kInvalidCell);
  REQUIRE(s->snapshot().highlighted().empty());
  REQUIRE(s->game().position().sideToMove() == Color::Black);
  REQUIRE(s->message() == "e2e4");
}

TEST_CASE("clicks that are not moves behave the way a board does", "[unit][app]") {
  auto s = open("standard");

  SECTION("clicking an enemy piece with nothing selected selects nothing") {
    REQUIRE(s->applyScript("click e7").has_value());
    REQUIRE(s->selected() == kInvalidCell);
    REQUIRE(s->game().plyCount() == 0);
  }
  SECTION("clicking an empty cell with nothing selected does nothing") {
    REQUIRE(s->applyScript("click e5").has_value());
    REQUIRE(s->selected() == kInvalidCell);
  }
  SECTION("clicking the selected piece again deselects it") {
    REQUIRE(s->applyScript("click e2\nclick e2").has_value());
    REQUIRE(s->selected() == kInvalidCell);
    REQUIRE(s->game().plyCount() == 0);
  }
  SECTION("clicking another of your own pieces reselects") {
    REQUIRE(s->applyScript("click e2\nclick d2").has_value());
    REQUIRE(s->selected() == cell(*s, "d2"));
    REQUIRE(s->game().plyCount() == 0);
  }
  SECTION("clicking an illegal destination selects nothing and plays nothing") {
    REQUIRE(s->applyScript("click e2\nclick e5").has_value());
    REQUIRE(s->game().plyCount() == 0);
    REQUIRE(s->selected() == kInvalidCell);
  }
}

TEST_CASE("undo walks the game back", "[unit][app]") {
  auto s = open("standard");
  const std::uint64_t startHash = s->game().position().hash();
  REQUIRE(s->applyScript("click e2\nclick e4\nclick e7\nclick e5").has_value());
  REQUIRE(s->game().plyCount() == 2);
  REQUIRE(s->applyScript("undo\nundo").has_value());
  REQUIRE(s->game().plyCount() == 0);
  // Exactly back where it started, hash included - undo is not an approximation.
  REQUIRE(s->game().position().hash() == startHash);
  REQUIRE(s->applyScript("undo").has_value());
  REQUIRE(s->message() == "nothing to undo");
}

TEST_CASE("promotion asks rather than guessing", "[unit][app]") {
  // A bare position with one white pawn a step from promoting.
  const char* kAboutToPromote = "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1";

  SECTION("with several choices the session stops and asks") {
    // Choosing silently would be a guess: a variant can promote to pieces nobody
    // expects, so the player is asked rather than defaulted to a queen.
    auto s = open("standard");
    REQUIRE(s->loadFen(kAboutToPromote).has_value());
    REQUIRE(s->applyScript("click b7\nclick b8").has_value());
    REQUIRE(s->game().plyCount() == 0);
    REQUIRE(s->pendingPromotion().active);
    REQUIRE(s->pendingPromotion().choices.size() == 4);

    // The board is frozen until the choice is made: the move is already committed.
    REQUIRE(s->applyScript("click a1").has_value());
    REQUIRE(s->game().plyCount() == 0);

    REQUIRE(s->choosePromotion(s->variant().findPiece("rook")).has_value());
    REQUIRE(s->game().plyCount() == 1);
    REQUIRE(s->game().position().at(cell(*s, "b8")).type ==
            s->variant().findPiece("rook"));
    REQUIRE_FALSE(s->pendingPromotion().active);
  }

  SECTION("cancelling puts the move back") {
    auto s = open("standard");
    REQUIRE(s->loadFen(kAboutToPromote).has_value());
    REQUIRE(s->applyScript("click b7\nclick b8").has_value());
    s->cancelPromotion();
    REQUIRE_FALSE(s->pendingPromotion().active);
    REQUIRE(s->game().plyCount() == 0);
    REQUIRE(s->selected() == kInvalidCell);
  }

  SECTION("a standing preference skips the prompt") {
    auto s = open("standard");
    REQUIRE(s->loadFen(kAboutToPromote).has_value());
    REQUIRE(s->applyScript("promote knight\nclick b7\nclick b8").has_value());
    REQUIRE_FALSE(s->pendingPromotion().active);
    REQUIRE(s->game().position().at(cell(*s, "b8")).type ==
            s->variant().findPiece("knight"));
  }

  SECTION("all four promotions are offered as one highlighted destination") {
    auto s = open("standard");
    REQUIRE(s->loadFen(kAboutToPromote).has_value());
    REQUIRE(s->applyScript("click b7").has_value());
    // Four legal moves land on b8, but a player sees one square to click.
    REQUIRE(s->snapshot().highlighted().size() == 1);
    int landing = 0;
    for (const Move& m : s->game().legalMoves()) {
      if (m.to == cell(*s, "b8")) ++landing;
    }
    REQUIRE(landing == 4);
  }
}

TEST_CASE("the move camera leads the view only when asked, and locks picking",
          "[unit][app]") {
  // M11.5: the move camera is folded into Session::camera(), so the board, the pick ray
  // and everything drawn over it agree. Off by default; when on, a shot owns the view for
  // the move and the board is locked so a click cannot resolve against where it was.
  auto s = open("standard");
  s->setAnimationSeconds(0.2f);
  constexpr float kW = 800.0f;
  constexpr float kH = 600.0f;
  const auto projectToPixel = [&](CellId c) {
    view::Vec3 at{};
    for (const view::Placement& p : s->placements()) {
      if (p.cell == c) at = view::Vec3{p.x, p.y, p.z};
    }
    const view::Mat4 vp = s->camera().viewProj(kW / kH);
    const float x = vp[0] * at.x + vp[4] * at.y + vp[8] * at.z + vp[12];
    const float y = vp[1] * at.x + vp[5] * at.y + vp[9] * at.z + vp[13];
    const float w = vp[3] * at.x + vp[7] * at.y + vp[11] * at.z + vp[15];
    return std::pair{(x / w * 0.5f + 0.5f) * kW - 0.5f,
                     (y / w * 0.5f + 0.5f) * kH - 0.5f};
  };

  // Default: a move animates but no shot owns the view, so a pixel still resolves.
  REQUIRE(s->applyScript("click e2\nclick e4").has_value());
  REQUIRE_FALSE(s->shotInFlight());
  const CellId a1 = cell(*s, "a1");
  const auto [px, py] = projectToPixel(a1);
  REQUIRE(s->clickPixel(px, py, kW, kH) == a1);

  // Ask the camera to follow, play a move: a shot is in flight and the board locks.
  s->setCameraMode("route");
  s->setFollowStrength(1.0f);
  REQUIRE(s->applyScript("click d7\nclick d5").has_value());
  REQUIRE(s->shotInFlight());
  REQUIRE(s->advanceAnimation(0.12f));  // into the body of the move
  REQUIRE(s->shotInFlight());
  const auto [qx, qy] = projectToPixel(a1);
  REQUIRE(s->clickPixel(qx, qy, kW, kH) == kInvalidCell);

  // With the shot leading, the effective camera has moved; when it settles, it returns.
  const view::OrbitCamera during = s->camera();
  while (s->advanceAnimation(0.05f)) {
  }
  REQUIRE_FALSE(s->shotInFlight());
  const view::OrbitCamera after = s->camera();
  const float moved = std::abs(during.target.x - after.target.x) +
                      std::abs(during.target.y - after.target.y) +
                      std::abs(during.target.z - after.target.z);
  CHECK(moved > 1e-3f);
}

TEST_CASE("a click resolved from a pixel reaches the right cell", "[unit][app]") {
  // The full path a mouse takes: pixel -> pick ray -> cell -> action.
  auto s = open("standard");
  const auto& placements = s->placements();

  // Find where e2 is drawn, project it, and click that pixel.
  const CellId e2 = cell(*s, "e2");
  const view::Placement* target = nullptr;
  for (const view::Placement& p : placements) {
    if (p.cell == e2) target = &p;
  }
  REQUIRE(target != nullptr);

  constexpr float kW = 800.0f;
  constexpr float kH = 600.0f;
  const view::Mat4 vp = s->camera().viewProj(kW / kH);
  const float cx = vp[0] * target->x + vp[4] * target->y + vp[8] * target->z + vp[12];
  const float cy = vp[1] * target->x + vp[5] * target->y + vp[9] * target->z + vp[13];
  const float cw = vp[3] * target->x + vp[7] * target->y + vp[11] * target->z + vp[15];
  const float px = (cx / cw * 0.5f + 0.5f) * kW - 0.5f;
  const float py = (cy / cw * 0.5f + 0.5f) * kH - 0.5f;

  REQUIRE(s->clickPixel(px, py, kW, kH) == e2);
  REQUIRE(s->selected() == e2);

  // And a click into empty space deselects rather than erroring.
  REQUIRE(s->clickPixel(1.0f, 1.0f, kW, kH) == kInvalidCell);
  REQUIRE(s->selected() == kInvalidCell);
}

TEST_CASE("the view can be re-aimed at other axes", "[unit][app]") {
  auto s = open("cube5");
  REQUIRE(s->viewConfig().screenAxes.size() == 3);
  REQUIRE(s->applyScript("axes file,level").has_value());
  REQUIRE(s->viewConfig().screenAxes.size() == 2);
  REQUIRE(s->viewConfig().gridAxes.size() == 1);
  // Re-aiming re-lays out the board and re-frames the camera, so every cell is still
  // placed exactly once.
  REQUIRE(s->placements().size() == s->variant().dims.cellCount());

  REQUIRE_FALSE(s->applyScript("axes file,nosuchaxis").has_value());
}

TEST_CASE("a vertical grid can be chosen, and flat mode refuses it", "[unit][app]") {
  auto s = open("cube5");
  Action v;
  v.kind = ActionKind::SetScreenAxes;
  v.text = "file,rank";
  v.gridVertical = true;
  REQUIRE(s->apply(v).has_value());
  REQUIRE(s->viewConfig().gridVertical);
  REQUIRE(s->viewConfig().gridAxes.size() == 1);

  const auto slices = view::enumerateSlices(s->variant().dims, s->viewConfig());
  REQUIRE(slices.size() == 5);
  REQUIRE(slices[1].originX == 0.0f);
  REQUIRE(slices[1].originY > 0.0f);  // stacked down, not across

  // A top-down camera cannot see a column of depth, so flat mode drops the vertical
  // arrangement and spreads the boards instead.
  s->setFlatView(true);
  REQUIRE_FALSE(s->viewConfig().gridVertical);
}

TEST_CASE("flat mode spreads a 3-D board's levels instead of hiding them",
          "[unit][app]") {
  auto s = open("cube5");
  // The default 3-D view uses three screen axes, which is one board stacked in depth.
  REQUIRE(s->viewConfig().screenAxes.size() == 3);
  REQUIRE(view::enumerateSlices(s->variant().dims, s->viewConfig()).size() == 1);

  s->setFlatView(true);
  // The depth axis becomes a grid axis and the five levels spread across the screen, so
  // none is hidden under another.
  REQUIRE(s->viewConfig().screenAxes.size() == 2);
  REQUIRE_FALSE(s->viewConfig().gridVertical);
  REQUIRE(view::enumerateSlices(s->variant().dims, s->viewConfig()).size() == 5);
}

TEST_CASE("leaving flat mode restores the chosen 3-D view", "[unit][app]") {
  auto s = open("cube5");
  s->setFlatView(true);
  REQUIRE(s->viewConfig().screenAxes.size() == 2);
  s->setFlatView(false);
  REQUIRE(s->viewConfig().screenAxes.size() == 3);  // the cube is back
  REQUIRE(view::enumerateSlices(s->variant().dims, s->viewConfig()).size() == 1);
}

TEST_CASE("camera actions stay within sane limits", "[unit][app]") {
  auto s = open("standard");
  const float startPitch = s->camera().pitch;
  REQUIRE(s->applyScript("orbit 0.2 0.1").has_value());
  REQUIRE(s->camera().pitch > startPitch);
  // Pitch is clamped to the solid band: no amount of dragging can turn the board
  // inside out, with the camera looking up at its underside or straight past the pole.
  REQUIRE(s->applyScript("orbit 0 100\norbit 0 100").has_value());
  REQUIRE(s->camera().pitch <= 1.55f);
  REQUIRE(s->camera().pitch > 0.0f);
  REQUIRE(s->applyScript("orbit 0 -1000").has_value());
  REQUIRE(s->camera().pitch > 0.0f);
  REQUIRE(s->camera().pitch < 1.5707963f);

  const float startDistance = s->camera().distance;
  REQUIRE(s->applyScript("zoom 0.5").has_value());
  REQUIRE(s->camera().distance < startDistance);
}

TEST_CASE("every shipped variant is playable through the session", "[unit][app]") {
  // The interaction layer must not assume two dimensions, a box board, or pawns.
  for (const char* name : {"standard", "cylinder", "torus", "mobius", "klein",
                           "mirrorbox", "cube5", "hyper4", "torus3d", "5d"}) {
    CAPTURE(name);
    auto s = open(name);
    REQUIRE(s->game().result() == GameResult::InProgress);

    // Play the first legal move by clicking it, whatever the board looks like.
    const auto& moves = s->game().legalMoves();
    REQUIRE_FALSE(moves.empty());
    Action pick;
    pick.kind = ActionKind::ClickCell;
    pick.cell = moves.front().from;
    REQUIRE(s->apply(pick).has_value());
    REQUIRE(s->selected() == moves.front().from);
    REQUIRE_FALSE(s->snapshot().highlighted().empty());

    Action go;
    go.kind = ActionKind::ClickCell;
    go.cell = s->snapshot().highlighted().front();
    REQUIRE(s->apply(go).has_value());
    REQUIRE(s->game().plyCount() == 1);
    REQUIRE_FALSE(s->statusLine().empty());
  }
}

TEST_CASE("a malformed action script is rejected with its line number", "[unit][app]") {
  auto s = open("standard");
  const auto r = s->applyScript("click e2\nwaggle\n");
  REQUIRE_FALSE(r.has_value());
  REQUIRE(r.error().line == 2);
  REQUIRE(r.error().message.find("waggle") != std::string::npos);
}

TEST_CASE("confirm before moving holds the move until it is confirmed", "[unit][app]") {
  auto s = open("standard");
  s->setConfirmMoves(true);
  REQUIRE(s->applyScript("click e2\nclick e4").has_value());
  CHECK(s->game().plyCount() == 0);  // not played yet
  REQUIRE(s->pendingMove().active);
  CHECK(s->pendingMove().from != kInvalidCell);

  REQUIRE(s->confirmMove().has_value());
  CHECK_FALSE(s->pendingMove().active);
  CHECK(s->game().plyCount() == 1);
}

TEST_CASE("a move waiting for confirmation can be cancelled", "[unit][app]") {
  auto s = open("standard");
  s->setConfirmMoves(true);
  REQUIRE(s->applyScript("click e2\nclick e4").has_value());
  REQUIRE(s->pendingMove().active);
  s->cancelMove();
  CHECK_FALSE(s->pendingMove().active);
  CHECK(s->game().plyCount() == 0);
}

TEST_CASE("against the computer, it answers and the board is locked on its turn",
          "[unit][app]") {
  auto s = open("standard");
  s->setEngineLevel(1);  // the sweep is about turns, not strength; Debug is slow
  s->setEngineSide(Color::Black);
  CHECK_FALSE(s->engineToMove());
  REQUIRE(s->applyScript("click e2\nclick e4").has_value());
  REQUIRE(s->game().plyCount() == 1);
  CHECK(s->engineToMove());
  CHECK(s->boardLocked());
  // A click on Black's pieces does nothing while it is the computer's turn.
  REQUIRE(s->applyScript("click e7\nclick e5").has_value());
  CHECK(s->game().plyCount() == 1);

  s->finishEngineMove();
  REQUIRE(s->game().plyCount() == 2);
  CHECK(s->game().position().sideToMove() == Color::White);
  CHECK_FALSE(s->engineToMove());

  // Undo takes back to the player's own turn, not just the computer's reply.
  REQUIRE(s->applyScript("undo").has_value());
  CHECK(s->game().plyCount() == 0);
}

TEST_CASE("the frame loop drives the computer on a worker without blocking",
          "[unit][app]") {
  auto s = open("standard");
  s->setEngineLevel(1);
  s->setEngineSide(Color::White);
  // White is the computer and moves first; give the clock long enough to start, then
  // wait for the worker by ticking (each tick returns at once).
  for (int i = 0; i < 2000 && s->game().plyCount() == 0; ++i) {
    (void)s->advanceEngine(0.05f);
    (void)s->advanceAnimation(1.0f);
    if (s->engineThinking()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(s->game().plyCount() == 1);
  CHECK(s->game().position().sideToMove() == Color::Black);
}

TEST_CASE("a script can ask the computer for a move", "[unit][app]") {
  auto s = open("standard");
  s->setEngineLevel(1);
  REQUIRE(s->applyScript("engine\nengine").has_value());
  CHECK(s->game().plyCount() == 2);
}

TEST_CASE("the computer's level sets how hard it looks, from the next move on",
          "[unit][app]") {
  auto s = open("standard");
  CHECK(s->engineOptions().level == ai::kDefaultLevel);
  for (int level = ai::kMinLevel; level <= ai::kMaxLevel; ++level) {
    s->setEngineLevel(level);
    const ai::SearchOptions want = ai::optionsForLevel(level);
    CHECK(s->engineOptions().level == level);
    CHECK(s->engineOptions().depth == want.depth);
    CHECK(s->engineOptions().maxNodes == want.maxNodes);
    CHECK(s->engineOptions().noise == want.noise);
  }
  // Out of range is clamped rather than refused.
  s->setEngineLevel(99);
  CHECK(s->engineOptions().level == ai::kMaxLevel);
  s->setEngineLevel(-3);
  CHECK(s->engineOptions().level == ai::kMinLevel);
}
