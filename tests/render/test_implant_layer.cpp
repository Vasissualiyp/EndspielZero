// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

#include "render/board_renderer.hpp"
#include "render/implant_layer.hpp"
#include "render/lab_scene.hpp"
#include "render/ui.hpp"
#include "view/implant_fx.hpp"

using namespace cb;
using namespace cb::render;

TEST_CASE("implant composite preserves settled UI and confines organic effects",
          "[render][gpu][implant]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct Sdl {
    ~Sdl() { SDL_Quit(); }
  } sdl;
  auto* window = SDL_CreateWindow("implant", 960, 540, SDL_WINDOW_HIDDEN);
  REQUIRE(window);
  struct Win {
    SDL_Window* w;
    ~Win() { SDL_DestroyWindow(w); }
  } win{window};
  auto ctx = VulkanContext::create();
  REQUIRE(ctx);
  auto ui = Ui::create(*ctx, window, view::implantTheme(view::Theme::manifold()));
  auto renderer = BoardRenderer::create(*ctx);
  auto target = OffscreenTarget::create(*ctx, 960, 540);
  REQUIRE(ui);
  REQUIRE(renderer);
  REQUIRE(target);
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  auto shell = app::Shell::create({"standard"}, {});
  REQUIRE(shell);
  shell->dismissWelcome();
  (*ui)->setLabBackdrop(true);
  InstanceSet instances;
  lab->setFileRanges(renderer->addLabMeshes(lab->fileMeshes()));
  lab->appendTo(instances, view::Theme::manifold());
  renderer->setTheme(view::Theme::manifold());
  const auto camera = *lab->station("menu");
  const auto frame = [&](std::optional<ImplantParams> params, bool overlay = true) {
    (*ui)->newFrame();
    (void)(*ui)->build(*shell, 60);
    (*ui)->endFrame();
    (*ui)->skipTransition();
    renderer->setImplant(params);
    std::function<void(VkCommandBuffer)> draw;
    if (overlay) draw = [&](VkCommandBuffer cmd) { (*ui)->record(cmd); };
    REQUIRE(renderer->render(*target, instances, camera, draw).has_value());
    auto pixels = target->readPixels();
    REQUIRE(pixels);
    return *pixels;
  };
  frame(std::nullopt);
  frame(std::nullopt);
  const auto direct = frame(std::nullopt);
  ImplantParams p;
  p.pixelScale = .5f;
  const auto settled = frame(p);
  for (std::size_t i = 0, reported = 0; i < direct.size() && reported < 8; ++i) {
    if (std::abs(int(direct[i]) - int(settled[i])) > 2) {
      WARN("settled mismatch x=" << (i / 4) % 960 << " y=" << i / (4 * 960)
                                 << " channel=" << i % 4 << " direct=" << int(direct[i])
                                 << " layer=" << int(settled[i]));
      ++reported;
    }
  }
  const auto maxDiff = [](const auto& a, const auto& b) {
    int diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
      diff = std::max(diff, std::abs(int(a[i]) - int(b[i])));
    return diff;
  };
  // Overlapping draws quantise in the transparent RGBA8 attachment before blending
  // with the scene: the amended spec permits two code values per channel.
  CHECK(maxDiff(direct, settled) <= 2);
  // An arriving record on the menu half: outside its expanded rectangle is identity.
  p.panelCount = 1;
  p.panels[0] = {{490, 60, 930, 510}, .3f, 0, 0, 17};
  const auto forming = frame(p);
  std::size_t changed = 0;
  int outside = 0;
  for (int y = 0; y < 540; ++y)
    for (int x = 0; x < 960; ++x)
      for (int c = 0; c < 4; ++c) {
        const auto i = static_cast<std::size_t>((y * 960 + x) * 4 + c);
        const int d = std::abs(int(settled[i]) - int(forming[i]));
        if (x >= 490 && x < 930 && y >= 60 && y < 510) changed += d > 1;
        if (x < 478 || x > 942 || y < 48 || y > 522) outside = std::max(outside, d);
      }
  CHECK(changed > 100);
  CHECK(outside <= 1);
  // Tools pass through even during a maximal dip and unresolved formation.
  p.toolCount = 1;
  p.toolRects[0] = {0, 0, 960, 540};
  p.heartbeat = 1.02f;
  p.dip = .6f;
  CHECK(maxDiff(frame(p), settled) <= 1);
  p.toolCount = 0;
  p.heartbeat = 1;
  p.dip = 0;
  const auto state = view::implantLeaving(.3f, 1);
  p.panels[0].form = state.form;
  p.panels[0].dissolve = state.dissolve;
  p.panels[0].afterimage = state.afterimage;
  p.body[0] = .7f;
  p.body[1] = .6f;
  p.body[2] = 1;
  const auto afterimage = frame(p);
  const auto empty = frame(std::nullopt, false);
  std::size_t afterimagePixels = 0;
  for (int y = 60; y < 510; ++y)
    for (int x = 490; x < 930; ++x) {
      const auto i = static_cast<std::size_t>((y * 960 + x) * 4);
      for (std::size_t c = 0; c < 3; ++c)
        afterimagePixels += afterimage[i + c] != empty[i + c];
    }
  CHECK(afterimagePixels > 0);
  CHECK_FALSE(view::implantLeaving(.6f, 1).visible());
  p.panelCount = 0;
  CHECK(maxDiff(frame(p), settled) <= 1);
  const auto messages = ctx->takeValidationMessages();
  CAPTURE(messages);
  CHECK(messages.empty());
}

TEST_CASE("Ui reports explicit implant panel ages and tool bounds", "[render][implant]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct Sdl {
    ~Sdl() { SDL_Quit(); }
  } sdl;
  auto* window = SDL_CreateWindow("implant-records", 960, 540, SDL_WINDOW_HIDDEN);
  REQUIRE(window);
  struct Win {
    SDL_Window* w;
    ~Win() { SDL_DestroyWindow(w); }
  } win{window};
  auto ctx = VulkanContext::create();
  REQUIRE(ctx);
  auto ui = Ui::create(*ctx, window, view::Theme::manifold());
  REQUIRE(ui);
  auto shell = app::Shell::create({"standard"}, {});
  REQUIRE(shell);
  shell->dismissWelcome();
  (*ui)->setLabBackdrop(true);
  (*ui)->setImplantActive(true);
  (*ui)->setStationCamera([](app::Screen) { return std::optional(view::OrbitCamera{}); });
  const auto frame = [&] {
    (*ui)->tick(0);
    (*ui)->newFrame();
    const auto req = (*ui)->build(*shell, 60);
    (*ui)->endFrame();
    return req;
  };
  frame();
  (*ui)->skipTransition();
  frame();
  CHECK((*ui)->implantRecords().empty());
  shell->go(app::Screen::QuitConfirm);
  (*ui)->pinTransitionAge(.1f);
  frame();
  REQUIRE_FALSE((*ui)->implantRecords().empty());
  CHECK(std::any_of((*ui)->implantRecords().begin(), (*ui)->implantRecords().end(),
                    [](const auto& r) { return r.leaving && r.age == .1f; }));
  CHECK(std::none_of((*ui)->implantRecords().begin(), (*ui)->implantRecords().end(),
                     [](const auto& r) { return !r.leaving && r.age < 0; }));
  for (const auto& r : (*ui)->implantRecords()) {
    CHECK(r.rect[2] > r.rect[0]);
    CHECK(r.rect[3] > r.rect[1]);
  }
  (*ui)->pinPanelAge(.6f);
  frame();
  CHECK(std::none_of((*ui)->implantRecords().begin(), (*ui)->implantRecords().end(),
                     [](const auto& r) { return r.leaving; }));
  (*ui)->pinTransitionAge(5);
  (*ui)->pinPanelAge(std::nullopt);
  frame();
  CHECK((*ui)->implantRecords().empty());
  CHECK((*ui)->saccadicDip() == 0);
  // A panel-age capture pin also works after the camera flight has settled.
  (*ui)->pinPanelAge(.1f);
  frame();
  REQUIRE_FALSE((*ui)->implantRecords().empty());
  CHECK(std::all_of((*ui)->implantRecords().begin(), (*ui)->implantRecords().end(),
                    [](const auto& r) { return !r.leaving && r.age == .1f; }));
  (*ui)->pinPanelAge(std::nullopt);
  (*ui)->injectKey(ImGuiKey_F12, true);
  frame();
  REQUIRE((*ui)->toolRects().size() == 1);
  CHECK((*ui)->toolRects()[0][2] > (*ui)->toolRects()[0][0]);
  // A CLI/console preview can depart a pane never built in this Ui. Its record
  // must use this frame's completed layout, rather than a previous cached height.
  shell->previewTransition(app::Screen::Watch, app::Screen::QuitConfirm);
  (*ui)->pinTransitionAge(.1f);
  frame();
  CHECK(std::any_of((*ui)->implantRecords().begin(), (*ui)->implantRecords().end(),
                    [](const auto& r) { return r.leaving && r.age == .1f; }));
  const auto messages = ctx->takeValidationMessages();
  CAPTURE(messages);
  CHECK(messages.empty());
}
#endif
