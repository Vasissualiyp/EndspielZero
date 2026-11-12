// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>
#include <imgui_internal.h>
#include "render/lab_scene.hpp"
#include "render/ui.hpp"
#include "support/variants.hpp"
#include "view/rig_move.hpp"
using namespace cb;
using namespace cb::render;
namespace {
void sameCamera(const view::OrbitCamera& a, const view::OrbitCamera& b) {
  CHECK(view::length(a.target - b.target) < 1e-4f);
  CHECK(std::abs(std::remainder(a.yaw - b.yaw, 6.283185307f)) < 1e-4f);
  CHECK(std::abs(a.distance - b.distance) < 1e-4f);
  CHECK(std::abs(a.pitch - b.pitch) < 1e-4f);
}
}  // namespace
TEST_CASE("Ui station clock camera interruption skip and projector phases",
          "[render][rig]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct Sdl {
    ~Sdl() { SDL_Quit(); }
  } sdl;
  auto* window = SDL_CreateWindow("stations", 960, 540, SDL_WINDOW_HIDDEN);
  REQUIRE(window);
  struct Window {
    SDL_Window* w;
    ~Window() { SDL_DestroyWindow(w); }
  } win{window};
  auto ctx = VulkanContext::create();
  REQUIRE(ctx);
  {
    auto ui = Ui::create(*ctx, window, view::Theme::manifold());
    REQUIRE(ui);
    auto shell = app::Shell::create({"standard", "torus"}, {});
    REQUIRE(shell);
    shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
      return test::loadVariant(name);
    });
    shell->dismissWelcome();
    auto doc =
        io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
    REQUIRE(doc);
    auto lab = LabScene::create(*doc);
    REQUIRE(lab);
    const auto menu = *lab->station("menu"), library = *lab->station("library");
    (*ui)->setStationCamera([&](app::Screen s) {
      return lab->station(s == app::Screen::NewGame    ? "library"
                          : s == app::Screen::Settings ? "settings"
                                                       : "menu");
    });
    (*ui)->setFocusFor([](app::Screen s) -> std::string_view {
      return s == app::Screen::NewGame ? "magazine" : "projector";
    });
    (*ui)->setLabBackdrop(true);
    const auto frame = [&](float dt = 0.f) {
      (*ui)->tick(dt);
      (*ui)->newFrame();
      auto request = (*ui)->build(*shell, 60);
      (*ui)->endFrame();
      return request;
    };
    frame();
    (*ui)->skipTransition();
    frame();
    CHECK((*ui)->salience("projector") == 1);
    shell->go(app::Screen::NewGame);
    frame();
    CHECK((*ui)->screenMove() == app::ScreenMove::Station);
    REQUIRE((*ui)->labCamera());
    sameCamera(*(*ui)->labCamera(), menu);
    (*ui)->injectKey(ImGuiKey_Enter, true);
    CHECK(frame().loadVariant.empty());
    (*ui)->injectKey(ImGuiKey_Enter, false);
    frame();
    const float total = view::rigMoveSeconds(menu, library, 1 / kCellsPerMetre);
    frame(total * .2f);
    sameCamera(*(*ui)->labCamera(), view::blendCamera(menu, library, view::rigEase(.2f)));
    auto h = (*ui)->hologramRequest();
    CHECK(h.kind == HologramRequest::Manifold);
    CHECK(h.scale == Catch::Approx(.5));
    CHECK(h.alpha == h.scale);
    CHECK(h.squashZ == Catch::Approx(.25));
    CHECK((*ui)->salience("projector") < 1);
    CHECK((*ui)->salience("magazine") == 0);
    frame(total * .25f);
    CHECK((*ui)->hologramRequest().kind == HologramRequest::None);
    frame(total * .30f);
    h = (*ui)->hologramRequest();
    CHECK(h.kind == HologramRequest::Overture);
    CHECK(h.scale == Catch::Approx(.5));
    CHECK(h.squashZ == h.scale);
    const auto before = *(*ui)->labCamera();
    shell->go(app::Screen::Settings);
    frame();
    sameCamera(*(*ui)->labCamera(), before);
    (*ui)->skipTransition();
    sameCamera(*(*ui)->labCamera(), *lab->station("settings"));
    // Preserve the leaving variant's scene even when the next screen refreshes its own.
    shell->go(app::Screen::NewGame);
    frame();
    (*ui)->skipTransition();
    shell->overtures().jumpTo(app::Overture::Torus);
    shell->overtures().setProgress(1);
    frame();
    const auto count = (*ui)->overtureScene()->quads.size();
    shell->go(app::Screen::MainMenu);
    frame();
    CHECK((*ui)->hologramRequest().kind == HologramRequest::Overture);
    REQUIRE((*ui)->overtureScene());
    CHECK((*ui)->overtureScene()->quads.size() == count);
    (*ui)->skipTransition();
    frame();
    shell->go(app::Screen::Welcome);
    frame(total * .1f);
    h = (*ui)->hologramRequest();
    CHECK(h.kind == HologramRequest::Manifold);
    CHECK(h.scale == 1);
    CHECK(h.alpha == 1);
    (*ui)->setPresentationIntensity(0);
    shell->go(app::Screen::NewGame);
    frame();
    sameCamera(*(*ui)->labCamera(), library);
    frame(.4f);
    CHECK((*ui)->hologramRequest().alpha == 1);
    (*ui)->setLabBackdrop(false);
    shell->go(app::Screen::MainMenu);
    frame();
    CHECK((*ui)->screenMove() == app::ScreenMove::Cut);
    (*ui)->setPresentationIntensity(1);
    (*ui)->setLabBackdrop(true);
    shell->go(app::Screen::Welcome);
    frame();
    (*ui)->skipTransition();
    frame();
    shell->go(app::Screen::MainMenu);
    frame();
    auto* tutorial = ImGui::FindWindowByName("##welcome");
    REQUIRE(tutorial);
    CHECK(tutorial->LastFrameActive == ImGui::GetFrameCount());
    (*ui)->skipTransition();
    frame();
    // A newly encountered pane must be measured while invisible: accepting input
    // later must not move it after its content is already visible.
    shell->go(app::Screen::QuitConfirm);
    frame();
    frame(.65f * view::rigMoveSeconds(menu, menu, 1 / kCellsPerMetre));
    auto* quit = ImGui::FindWindowByName("##quitconfirm");
    REQUIRE(quit);
    const float beforeInput = quit->Pos.y;
    (*ui)->skipTransition();
    frame();
    frame();
    CHECK(quit->Pos.y == Catch::Approx(beforeInput));
    shell->go(app::Screen::MainMenu);
    frame();
    (*ui)->skipTransition();
    frame();
    CHECK((*ui)->hologramRequest().kind == HologramRequest::Manifold);
    (*ui)->setCinema(true);
    frame();
    CHECK((*ui)->hologramRequest().kind == HologramRequest::None);
  }
  CHECK(ctx->takeValidationMessages().empty());
}
#endif
