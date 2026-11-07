// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI
#include <cmath>

#include "render/hud_layout.hpp"

using namespace cb;
using namespace cb::render;
using Catch::Approx;

TEST_CASE("automatic UI scaling rounds to quarter steps and clamps",
          "[render][hud-layout]") {
  const int heights[] = {600, 720, 900, 1080, 1440, 2160, 4320, 809, 810};
  const float factors[] = {1, 1, 1.25f, 1.5f, 2, 3, 3, 1, 1.25f};
  for (std::size_t i = 0; i < std::size(heights); ++i) {
    CAPTURE(heights[i]);
    CHECK(autoUiFactor(heights[i]) == factors[i]);
  }
  CHECK(effectiveUiScale(1.25f, true, 1080, true, false) == 1.25f);
  CHECK(effectiveUiScale(1.25f, true, 1080, true, true) == 1.875f);
  CHECK(effectiveUiScale(1.25f, true, 1080, false, false) == 1.875f);
  CHECK(effectiveUiScale(1.25f, false, 1080, false, false) == 1.25f);
  CHECK(effectiveUiScale(1.25f, true, 1080, true, true, 2) == 2);
}

TEST_CASE("the stage subtracts panel bands with a gutter and viewport origin",
          "[render][hud-layout]") {
  const HudRect vp{100, 50, 1280, 720};
  const StagePanel panels[] = {{{116, 66, 180, 70}, StageEdge::Top},
                               {{116, 152, 100, 120}, StageEdge::Left},
                               {{1184, 152, 180, 450}, StageEdge::Right},
                               {{400, 640, 680, 114}, StageEdge::Bottom}};
  const auto r = stageRect(vp, panels, 16);
  CHECK(r.x == 232);
  CHECK(r.y == 152);
  CHECK(r.width == 936);
  CHECK(r.height == 472);
  const auto full = stageRect(vp, {}, 16);
  CHECK(full.x == vp.x);
  CHECK(full.width == vp.width);
}

TEST_CASE("dock layout wraps without clipping and keeps menu last",
          "[render][hud-layout]") {
  const float widths[] = {78, 84, 64, 84, 100, 92, 84, 78};
  for (float scale : {1.0f, 2.0f, 3.0f}) {
    std::vector<float> sized;
    for (float w : widths) sized.push_back(w * scale);
    const auto l = dockLayout(sized, 2, 1100, 34 * scale, 8 * scale, 12 * scale);
    REQUIRE(l.buttons.size() == std::size(widths));
    for (const auto& b : l.buttons) {
      CHECK(b.x >= 0);
      CHECK(b.x + b.width <= l.width + 0.01f);
      CHECK(b.y + b.height <= l.height + 0.01f);
    }
    const auto& menu = l.buttons.back();
    const auto& view = l.buttons[l.buttons.size() - 2];
    if (menu.y == view.y)
      CHECK(menu.x - view.x - view.width == Approx(12 * scale + 1));
    else
      CHECK(menu.x == 0);
    CHECK(l.rules.size() == 2);
  }
}

TEST_CASE("tray groups show repeated icons through four then xN",
          "[render][hud-layout]") {
  const Piece p{1, static_cast<std::uint8_t>(Color::Black)};
  std::vector<Piece> pieces(5, p);
  auto g = groupCaptured(pieces);
  REQUIRE(g.size() == 1);
  CHECK(g[0].count == 5);
  CHECK(g[0].icons() == 1);
  CHECK(g[0].label() == "x5");
  pieces.resize(4);
  g = groupCaptured(pieces);
  CHECK(g[0].icons() == 4);
  CHECK(g[0].label().empty());
}

TEST_CASE("a tray capture scans up over 350ms and calm settles instantly",
          "[render][hud-layout]") {
  CHECK(captureReveal(0, 1) == 0);
  CHECK(captureReveal(.175f, 1) == Approx(.5f));
  CHECK(captureReveal(.35f, 1) == 1);
  CHECK(captureReveal(0, 0) == 1);
}
#endif

#ifdef CB_HAVE_IMGUI
TEST_CASE("the dock fits its button groups without an empty middle",
          "[render][hud-layout]") {
  const float widths[] = {78, 84, 64, 78};
  const auto l = dockLayout(widths, 2, 1800, 42, 8, 12);
  REQUIRE(l.buttons.size() == 4);
  CHECK(l.width == 338);
  CHECK(l.height == 42);
  CHECK(l.buttons[3].x - l.buttons[2].x - l.buttons[2].width == 13);
  CHECK(l.buttons.back().x + l.buttons.back().width == l.width);
}
#endif

#ifdef CB_HAVE_IMGUI
TEST_CASE("result placement stays in the stage and avoids the king", "[render][result]") {
  const HudRect stage{300, 100, 1100, 700};
  for (const auto king : {HudPoint{850, 450}, HudPoint{600, 250}, HudPoint{1100, 650}}) {
    const auto p = resultPanelPos(stage, king, {420, 200});
    CHECK(p.x >= stage.x);
    CHECK(p.y >= stage.y);
    CHECK(p.x + 420 <= stage.x + stage.width);
    CHECK(p.y + 200 <= stage.y + stage.height);
    CHECK_FALSE((king.x >= p.x - 24 && king.x <= p.x + 444 && king.y >= p.y - 24 &&
                 king.y <= p.y + 224));
  }
  const auto p = resultPanelPos(stage, std::nullopt, {420, 200});
  CHECK(p.x == Approx(640));
  CHECK(p.y == Approx(350));
}
TEST_CASE("result panel sits beside the board, not over it", "[render][result]") {
  const HudRect stage{300, 100, 1100, 700};
  const HudPoint size{240, 380};
  const auto overlaps = [](HudRect a, HudPoint p, HudPoint s) {
    return p.x < a.x + a.width && p.x + s.x > a.x && p.y < a.y + a.height &&
           p.y + s.y > a.y;
  };
  // Room on the right only: it goes there, vertically centred.
  const HudRect board{520, 180, 500, 540};
  const auto p = resultPanelPos(stage, board, HudPoint{760, 400}, size, 16);
  CHECK_FALSE(overlaps(board, p, size));
  CHECK(p.x >= stage.x);
  CHECK(p.x + size.x <= stage.x + stage.width);
  CHECK(p.y == Approx(stage.y + (stage.height - size.y) * .5f));
  CHECK(std::min(std::abs(p.x - (board.x + board.width)),
                 std::abs(p.x + size.x - board.x)) == Approx(16));
  // Only the left has room.
  const HudRect right{560, 180, 800, 540};
  const auto q = resultPanelPos(stage, right, HudPoint{900, 400}, size, 16);
  CHECK_FALSE(overlaps(right, q, size));
  CHECK(q.x + size.x == Approx(right.x - 16));
  // A board filling the stage leaves no room: the king-clear placement is used.
  const HudRect full{300, 100, 1100, 700};
  const HudPoint king{850, 450};
  const auto f = resultPanelPos(stage, full, king, size, 16);
  const auto old = resultPanelPos(stage, king, size);
  CHECK(f.x == Approx(old.x));
  CHECK(f.y == Approx(old.y));
  // No board known: identical to the plain overload.
  const auto n = resultPanelPos(stage, std::nullopt, king, size, 16);
  CHECK(n.x == Approx(old.x));
}
TEST_CASE("result panel traces after the end hold and calm only fades",
          "[render][result]") {
  const float hold = view::kEndDelaySeconds + 7 / view::kPowerDownCellsPerSecond +
                     view::kPowerDownFallSeconds;
  CHECK(resultPanelFx(hold - .01f, view::EndKind::Decisive, 7, 1).draw == 0);
  CHECK(resultPanelFx(hold + .2f, view::EndKind::Decisive, 7, 1).draw == Approx(.5f));
  CHECK(resultPanelFx(hold + .2f, view::EndKind::Decisive, 7, 1).content == 0);
  CHECK(resultPanelFx(hold + .8f, view::EndKind::Decisive, 7, 1).content == 1);
  const auto calm = resultPanelFx(view::kEndDelaySeconds + view::kCalmFadeSeconds + .2f,
                                  view::EndKind::Decisive, 7, 0);
  CHECK(calm.draw == 1);
  CHECK(calm.content == Approx(.5f));
  CHECK(calm.level == Approx(.5f));
}

TEST_CASE("vector arrows point where they say, inside their square", "[render][arrow]") {
  const HudPoint c{100, 50};
  const float size = 10;
  for (const auto dir : {ArrowDir::Right, ArrowDir::Left, ArrowDir::Up}) {
    const auto strokes = arrowStrokes(c, size, dir);
    for (const auto& s : strokes)
      for (const auto& p : {s.a, s.b}) {
        CHECK(std::fabs(p.x - c.x) <= size * 0.5f + 1e-4f);
        CHECK(std::fabs(p.y - c.y) <= size * 0.5f + 1e-4f);
      }
    // The shaft runs through the centre, and both head strokes end at its tip.
    const HudPoint tip = strokes[0].b;
    CHECK(strokes[1].a.x == Approx(tip.x));
    CHECK(strokes[2].a.y == Approx(tip.y));
    CHECK(strokes[0].a.x + strokes[0].b.x == Approx(2 * c.x));
    CHECK(strokes[0].a.y + strokes[0].b.y == Approx(2 * c.y));
    // The head strokes lie behind the tip, along the shaft.
    const float dx = tip.x - strokes[0].a.x, dy = tip.y - strokes[0].a.y;
    for (int k = 1; k < 3; ++k) {
      const auto& h = strokes[static_cast<std::size_t>(k)];
      CHECK((h.b.x - tip.x) * dx + (h.b.y - tip.y) * dy < 0);
    }
  }
  CHECK(arrowStrokes(c, size, ArrowDir::Right)[0].b.x > c.x);
  CHECK(arrowStrokes(c, size, ArrowDir::Left)[0].b.x < c.x);
  CHECK(arrowStrokes(c, size, ArrowDir::Up)[0].b.y < c.y);
}
#endif
