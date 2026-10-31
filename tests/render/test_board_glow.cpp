// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN
#include <catch2/catch_approx.hpp>

#include <vector>

#include "render/board_glow.hpp"

using namespace cb::render;
using Catch::Approx;

TEST_CASE("the backdrop glow is centred on the board's projected box", "[render][glow]") {
  // A board square about the origin seen from straight above: projects symmetric.
  cb::view::OrbitCamera cam;
  cam.target = {0, 0, 0};
  cam.distance = 14.0f;
  cam.yaw = 0.0f;
  cam.pitch = 1.5f;
  const std::vector<std::array<float, 3>> pts{
      {-4, -4, 0}, {4, -4, 0}, {4, 4, 0}, {-4, 4, 0}};
  const auto g = projectBoardGlow(pts, cam.viewProj(16.0f / 9.0f), {}, 1920, 1080);
  REQUIRE(g.valid);
  CHECK(g.cx == Approx(0.5f).margin(0.02));
  CHECK(g.cy == Approx(0.5f).margin(0.05));
  CHECK(g.hx > 0.05f);
  CHECK(g.hy > 0.05f);
  CHECK(g.hx < 0.5f);
}

TEST_CASE("the glow follows a board sub-rectangle and ignores points behind the camera",
          "[render][glow]") {
  cb::view::OrbitCamera cam;
  cam.target = {0, 0, 0};
  cam.distance = 14.0f;
  cam.pitch = 1.5f;
  const std::vector<std::array<float, 3>> pts{{-1, -1, 0}, {1, 1, 0}};
  const float aspect = 960.0f / 1080.0f;
  const auto g =
      projectBoardGlow(pts, cam.viewProj(aspect), {960, 0, 960, 1080}, 1920, 1080);
  REQUIRE(g.valid);
  CHECK(g.cx == Approx(0.75f).margin(0.02));  // the right half's middle
  // Everything behind the eye: invalid.
  const std::vector<std::array<float, 3>> behind{{0, 0, 1000}};
  CHECK_FALSE(projectBoardGlow(behind, cam.viewProj(aspect), {}, 1920, 1080).valid);
  CHECK_FALSE(projectBoardGlow({}, cam.viewProj(aspect), {}, 1920, 1080).valid);
}
#endif
