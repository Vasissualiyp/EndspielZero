// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "render/board_renderer.hpp"
#include "render/implant_layer.hpp"
#include "render/lab_scene.hpp"
#include "render/ui.hpp"
#include "support/variants.hpp"
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
    // Opaque stem: a glyph core must be byte-stable under the heartbeat split.
    ImGui::GetForegroundDrawList()->AddRectFilled({600, 60}, {604, 84},
                                                  IM_COL32(120, 110, 160, 255));
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
  p = {};
  p.panelCount = 1;
  p.panels[0] = {{490, 30, 940, 510}, 1, 0, 0, 17};
  p.panels[0].settled = 1;
  // The glow must breathe even when the optional gaze/lens effects are disabled.
  const auto quiet = frame(p);
  p.heartbeat = 1.2f;
  const auto beat = frame(p);
  CHECK(maxDiff(quiet, beat) >= 8);  // a visible 3-5% peripheral lift
  std::size_t haloChanged = 0;
  for (int y = 100; y < 440; ++y)
    for (int x = 540; x < 800; ++x) {
      const auto i = static_cast<std::size_t>((y * 960 + x) * 4);
      haloChanged += quiet[i + 2] != beat[i + 2];
    }
  CHECK(haloChanged > 0);
  for (int y = 62; y < 82; ++y)
    for (int x = 601; x < 603; ++x)
      for (int channel = 0; channel < 4; ++channel) {
        const auto i = static_cast<std::size_t>((y * 960 + x) * 4 + channel);
        CHECK(quiet[i] == beat[i]);
      }
  p.intensity = 0;
  const auto calmPulse = frame(p);
  p.heartbeat = 1;
  CHECK(maxDiff(frame(p), calmPulse) == 0);
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
  CHECK_FALSE((*ui)->implantRecords().empty());
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
  CHECK_FALSE((*ui)->implantRecords().empty());
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

TEST_CASE("every neural screen records its windows and editor preview (M25.1)",
          "[render][implant][neural]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct Sdl {
    ~Sdl() { SDL_Quit(); }
  } sdl;
  auto* window = SDL_CreateWindow("neural-inventory", 1280, 720, SDL_WINDOW_HIDDEN);
  REQUIRE(window);
  struct Win {
    SDL_Window* w;
    ~Win() { SDL_DestroyWindow(w); }
  } win{window};
  auto ctx = VulkanContext::create();
  REQUIRE(ctx);
  auto ui = Ui::create(*ctx, window, view::implantTheme(view::Theme::manifold()));
  REQUIRE(ui);
  auto shell = app::Shell::create({"standard"}, {});
  REQUIRE(shell);
  shell->dismissWelcome();
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    return test::loadVariant(name);
  });
  REQUIRE(shell->startGame("standard"));
  (*ui)->setImplantActive(true);
  (*ui)->setLabBackdrop(true);
  const auto frame = [&] {
    (*ui)->newFrame();
    (void)(*ui)->build(*shell, 60);
    (*ui)->endFrame();
    (*ui)->skipTransition();
  };
  for (const auto screen :
       {app::Screen::MainMenu, app::Screen::Welcome, app::Screen::NewGame,
        app::Screen::Watch, app::Screen::Settings, app::Screen::QuitConfirm,
        app::Screen::Editor, app::Screen::Game, app::Screen::Paused,
        app::Screen::PauseQuitConfirm, app::Screen::GameInfo, app::Screen::PieceMoves}) {
    CAPTURE(static_cast<int>(screen));
    shell->go(screen);
    frame();
    frame();
    frame();
    CHECK_FALSE((*ui)->implantRecords().empty());
    for (const auto& r : (*ui)->implantRecords()) {
      CHECK(r.rect[2] > r.rect[0]);
      CHECK(r.rect[3] > r.rect[1]);
    }
    CHECK((*ui)->toolRects().empty());
  }
  shell->setVariantSourceLoader([](const std::string& name) -> Result<std::string> {
    std::ifstream in(test::variantPath(name));
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
  });
  shell->setVariantPathResolver(
      [](const std::string& name) { return test::variantPath(name); });
  shell->go(app::Screen::Game);
  frame();
  frame();
  shell->pause();
  (*ui)->pinTransitionAge(.1f);
  frame();
  CHECK(std::any_of(
      (*ui)->implantRecords().begin(), (*ui)->implantRecords().end(),
      [](const auto& r) { return r.leaving && r.seed == ImHashStr("##status"); }));
  (*ui)->pinTransitionAge(std::nullopt);
  REQUIRE(shell->openEditor("standard"));
  shell->go(app::Screen::Editor);
  for (int page : {0, 1, 2, 3}) {
    for (int tab = 0; tab < (page == 1 ? 8 : 1); ++tab) {
      CAPTURE(page, tab);
      (*ui)->openEditorPage(page, tab);
      frame();
      frame();
      frame();
      CHECK_FALSE((*ui)->implantRecords().empty());
      if (page == 3) {
        const auto* pane = ImGui::FindWindowByName("##editor");
        REQUIRE(pane);
        const auto colour = view::Theme::manifold().moss;
        const auto green = ImGui::ColorConvertFloat4ToU32(
            {colour.r, colour.g, colour.b, colour.a * .85f});
        float largestColourFace = 0;
        const auto& dl = *pane->DrawList;
        for (int i = 0; i + 2 < dl.IdxBuffer.Size; i += 3) {
          const auto& a = dl.VtxBuffer[dl.IdxBuffer[i]];
          const auto& b = dl.VtxBuffer[dl.IdxBuffer[i + 1]];
          const auto& c = dl.VtxBuffer[dl.IdxBuffer[i + 2]];
          if (a.col == green && b.col == green && c.col == green)
            largestColourFace =
                std::max(largestColourFace,
                         .5f * std::abs((b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) -
                                        (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x)));
        }
        CHECK(largestColourFace < 40);  // semantic ticks, no green button plates
      }
      if (page == 1) {
        CHECK((*ui)->hologramRequest().kind == HologramRequest::Editor);
        REQUIRE_FALSE((*ui)->editorPreviewScene().tokens.empty());
        CHECK((*ui)->editorPreviewScene().tokens.front().normal.z < -.25f);
      }
    }
  }
  REQUIRE(shell->openEditor("cube5"));
  (*ui)->openEditorPage(1, 0);
  (*ui)->openEditorPiece("unicorn");
  frame();
  frame();
  frame();
  REQUIRE_FALSE((*ui)->editorPreviewScene().tokens.empty());
  CHECK((*ui)->editorPreviewScene().tokens.front().glyph == 'U');
  shell->go(app::Screen::Game);
  auto& session = *shell->session();
  session.setConfirmMoves(false);
  REQUIRE(session.loadFen("4k3/P7/8/8/8/8/8/4K3 w - - 0 1"));
  REQUIRE(session.applyScript("click a7\nclick a8"));
  REQUIRE(session.pendingPromotion().active);
  frame();
  (*ui)->tick(.6f);
  frame();
  const auto covered = [&](const char* name) {
    const auto* pane = ImGui::FindWindowByName(name);
    REQUIRE(pane);
    CHECK(std::any_of((*ui)->implantRecords().begin(), (*ui)->implantRecords().end(),
                      [&](const auto& r) { return r.seed == pane->ID; }));
  };
  covered("promotion");
  CHECK(ImGui::GetStyleColorVec4(ImGuiCol_PopupBg).w == 0);
  session.cancelPromotion();
  REQUIRE(session.loadFen("4k3/8/8/8/8/8/P7/4K3 w - - 0 1"));
  session.setConfirmMoves(true);
  REQUIRE(session.applyScript("click a2\nclick a3"));
  REQUIRE(session.pendingMove().active);
  frame();
  (*ui)->tick(.6f);
  frame();
  covered("confirmmove");
  session.cancelMove();
  session.setConfirmMoves(false);
  app::Action finish;
  finish.kind = app::ActionKind::AgreeDraw;
  REQUIRE(session.apply(finish));
  REQUIRE(session.showResult());
  frame();
  (*ui)->tick(.6f);
  frame();
  covered("##result");
  (*ui)->injectKey(ImGuiKey_F12, true);
  frame();
  REQUIRE((*ui)->toolRects().size() == 1);
  const auto messages = ctx->takeValidationMessages();
  CAPTURE(messages);
  CHECK(messages.empty());
}
#endif
