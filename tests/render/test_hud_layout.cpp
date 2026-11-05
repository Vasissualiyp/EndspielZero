// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI
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
