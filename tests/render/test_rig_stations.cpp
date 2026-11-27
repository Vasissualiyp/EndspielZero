// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>
#include <imgui_internal.h>
#include "render/lab_scene.hpp"
#include "render/ui.hpp"
#include "support/variants.hpp"
#include "view/neural_vr.hpp"
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
    CHECK(h.projector == HologramRequest::Projector::Bench);
    CHECK(h.radiusMetres == Catch::Approx(std::lerp(kMenuHologramRadiusMetres,
                                                    kWallHologramRadiusMetres, .104f)));
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
    CHECK(h.projector == HologramRequest::Projector::Wall);
    CHECK((*ui)->rackSlides()[0] == Catch::Approx(.06f));
    CHECK((*ui)->rackSlides()[1] == 0);
    CHECK(h.scale == Catch::Approx(.5));
    CHECK(h.squashZ == h.scale);
    const auto before = *(*ui)->labCamera();
    shell->go(app::Screen::Settings);
    frame();
    sameCamera(*(*ui)->labCamera(), before);
    CHECK((*ui)->hologramRequest().projector == HologramRequest::Projector::Wall);
    CHECK((*ui)->rackSlides()[0] == Catch::Approx(.06f));
    frame(.125f);
    CHECK((*ui)->rackSlides()[0] == Catch::Approx(.03f));
    frame(.125f);
    CHECK((*ui)->rackSlides()[0] == 0);
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
    // Console/CLI previews reconstruct the source even when it was never drawn.
    (*ui)->pinTransitionAge(0.f);
    shell->previewTransition(app::Screen::MainMenu, app::Screen::NewGame);
    frame();
    h = (*ui)->hologramRequest();
    CHECK(h.projector == HologramRequest::Projector::Bench);
    CHECK(h.radiusMetres == .26f);
    CHECK((*ui)->rackSlides()[0] == 0);
    (*ui)->skipTransition();
    shell->previewTransition(app::Screen::NewGame, app::Screen::MainMenu);
    frame();
    h = (*ui)->hologramRequest();
    CHECK(h.kind == HologramRequest::Overture);
    CHECK(h.projector == HologramRequest::Projector::Wall);
    CHECK(h.radiusMetres == kWallHologramRadiusMetres);
    CHECK((*ui)->rackSlides()[0] == .06f);
    (*ui)->pinTransitionAge(.125f);
    frame();
    CHECK((*ui)->rackSlides()[0] == Catch::Approx(.03f));
    (*ui)->pinTransitionAge(.25f);
    frame();
    CHECK((*ui)->rackSlides()[0] == 0);
    (*ui)->pinTransitionAge(std::nullopt);
    (*ui)->setCinema(true);
    frame();
    CHECK((*ui)->hologramRequest().kind == HologramRequest::None);
  }
  CHECK(ctx->takeValidationMessages().empty());
}
TEST_CASE("Ui calm VR handoff keeps both halves of its clock and the leaving board",
          "[render][rig][vr][vrcalm]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct Sdl {
    ~Sdl() { SDL_Quit(); }
  } sdl;
  auto* window = SDL_CreateWindow("VR clock", 960, 540, SDL_WINDOW_HIDDEN);
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
    auto shell = app::Shell::create({"standard"}, {});
    REQUIRE(shell);
    shell->dismissWelcome();
    shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
      return test::loadVariant(name);
    });
    REQUIRE(shell->startGame("standard"));
    const auto frame = [&] {
      (*ui)->tick(0);
      (*ui)->newFrame();
      (void)(*ui)->build(*shell, 60);
      (*ui)->endFrame();
    };
    frame();
    for (const bool calm : {true, false}) {
      (*ui)->setPresentationIntensity(calm ? 0.f : 1.f);
      for (const bool vr : {true, false}) {
        for (const bool entering : {true, false}) {
          CAPTURE(calm, vr, entering);
          (*ui)->setRigMoves(
              true, view::kVrEnterSeconds, view::kVrLeaveSeconds,
              view::stowTravelSeconds(1),
              vr ? std::optional{Ui::CalmRigTiming{view::kVrEnterCalmSeconds,
                                                   view::kVrLeaveCalmSeconds}}
                 : std::nullopt);
          const auto from = entering ? app::Screen::MainMenu : app::Screen::Game;
          const auto to = entering ? app::Screen::Game : app::Screen::MainMenu;
          (*ui)->pinTransitionAge(0.f);
          shell->previewTransition(from, to);
          frame();
          // Configuration is frame-local; a running move must retain its own clock.
          (*ui)->setRigMoves(true, 20, 30, 40);
          const float full = entering
                                 ? view::kVrEnterSeconds + 1.f
                                 : view::kVrLeaveSeconds + view::stowTravelSeconds(1) +
                                       app::kStowMenuSeconds;
          const float total = calm ? (vr ? .6f : .3f) : full;
          REQUIRE((*ui)->rigMove());
          CHECK((*ui)->rigMove()->total == Catch::Approx(total));
          for (const float age : {.3f, .45f, .6f}) {
            (*ui)->pinTransitionAge(age);
            frame();
            const auto move = *(*ui)->rigMove();
            CHECK(move.age == Catch::Approx(std::min(age, total)));
            CHECK(move.done == (age >= total));
            if (calm && vr) {
              const auto state =
                  entering ? view::vrEnter(move.age, 0) : view::vrLeave(move.age, 0);
              CHECK(state.dark == Catch::Approx(age == .3f    ? 1
                                                : age == .45f ? .5f
                                                              : 0)
                                      .margin(1e-5f));
              CHECK((*ui)->holdsBoard() == (!entering && age < total));
            }
          }
          (*ui)->pinTransitionAge(.1f);
          frame();
          (*ui)->skipTransition();
          CHECK((*ui)->rigMove()->done);
          CHECK((*ui)->rigMove()->age == Catch::Approx(total));
          CHECK_FALSE((*ui)->holdsBoard());
        }
      }
    }
  }
  CHECK(ctx->takeValidationMessages().empty());
}
TEST_CASE("the neural HUD advertises rig skip from the first frame (M25.3b)",
          "[render][rig][gantryhint]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct Sdl {
    ~Sdl() { SDL_Quit(); }
  } sdl;
  auto* window = SDL_CreateWindow("gantry hint", 960, 540, SDL_WINDOW_HIDDEN);
  REQUIRE(window);
  struct Window {
    SDL_Window* w;
    ~Window() { SDL_DestroyWindow(w); }
  } win{window};
  auto ctx = VulkanContext::create();
  REQUIRE(ctx);
  {
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
    (*ui)->setHudFx(0, 0, 0);
    (*ui)->setRigMoves(true, view::deployLastLock(1), 1, view::stowTravelSeconds(1));
    const auto frame = [&] {
      (*ui)->tick(0);
      (*ui)->newFrame();
      (void)(*ui)->build(*shell, 60);
      (*ui)->endFrame();
    };
    frame();
    for (bool entering : {true, false}) {
      shell->previewTransition(entering ? app::Screen::MainMenu : app::Screen::Game,
                               entering ? app::Screen::Game : app::Screen::MainMenu);
      for (float age : {0.f, .05f, .25f, 2.f}) {
        CAPTURE(entering, age);
        (*ui)->pinTransitionAge(age);
        frame();
        auto* hint = ImGui::FindWindowByName("##rig-hint");
        REQUIRE(hint);
        CHECK(hint->LastFrameActive == ImGui::GetFrameCount());
        CHECK_FALSE(hint->DrawList->VtxBuffer.empty());
        CHECK(
            std::any_of((*ui)->implantRecords().begin(), (*ui)->implantRecords().end(),
                        [&](const auto& r) { return r.seed == hint->ID && r.settled; }));
        // The implant chooses the strongest transition record at each glyph. An
        // invisible booting HUD overlapping this hint must not erase its settled text.
        for (const auto& r : (*ui)->implantRecords()) {
          if (r.seed == hint->ID) continue;
          CAPTURE(r.seed, r.rect, r.age, r.leaving);
          CHECK_FALSE((hint->Pos.x + hint->Size.x >= r.rect[0] - 24 &&
                       hint->Pos.x <= r.rect[2] + 24 &&
                       hint->Pos.y + hint->Size.y >= r.rect[1] - 24 &&
                       hint->Pos.y <= r.rect[3] + 24));
        }
      }
      (*ui)->pinTransitionAge(std::nullopt);
      (*ui)->skipTransition();
      frame();
      CHECK((*ui)->rigMove()->done);
      auto* hint = ImGui::FindWindowByName("##rig-hint");
      REQUIRE(hint);
      CHECK(hint->LastFrameActive != ImGui::GetFrameCount());
    }
  }
  CHECK(ctx->takeValidationMessages().empty());
}
#endif
