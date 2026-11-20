// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>
#include <cmath>
#include <random>
#include "render/lab_light.hpp"
#include "render/lab_scene.hpp"
#include "render/projector_hologram.hpp"
#include "render/ui.hpp"
#include "render/vulkan_context.hpp"
#include "support/variants.hpp"
using namespace cb;
using namespace cb::render;
namespace {
view::Vec3 centre(const Instance& i) {
  return {i.center[0], i.center[1], i.center[2]};
}
view::Vec3 rotated(view::Vec3 p, const float* q) {
  const view::Vec3 v{q[0], q[1], q[2]};
  const auto t = view::cross(v, p) * 2;
  return p + t * q[3] + view::cross(v, t);
}
void bounded(const Hologram& h, const HologramPlacement& at) {
  for (const auto* group : {&h.quads, &h.tokens, &h.lines})
    for (const auto& i : *group) {
      CHECK(view::length(centre(i) - at.centre) <= at.radius * at.scale * 1.05f);
      CHECK(i.material == 4);
      CHECK(i.fxCell == 0);
      for (float q : i.quat) CHECK(std::isfinite(q));
    }
}
}  // namespace
TEST_CASE("projector overtures preserve counts and their world sphere",
          "[unit][render][hologram]") {
  const auto theme = view::Theme::manifold();
  HologramPlacement at{{3, -2, 5}, 4, .7f, 1, 1};
  for (auto which :
       {app::Overture::Standard, app::Overture::T6, app::Overture::Torus3d}) {
    const auto scene =
        overtureScene(which, which == app::Overture::Standard ? 0.f : 1.f, false, theme);
    const auto h = overtureHologram(scene, theme, at);
    REQUIRE(h.quads.size() == scene.quads.size());
    REQUIRE(h.tokens.size() == scene.tokens.size());
    bounded(h, at);
    std::size_t count = 0;
    for (const auto b : h.tokenBatches) {
      CHECK(b.first == count);
      count += b.count;
    }
    CHECK(count == h.tokens.size());
  }
}
TEST_CASE("library projector keeps a1 on the near left corner",
          "[unit][render][hologram]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  auto lens = *doc->anchor("projector.lens");
  lens[2] += .32f;
  const HologramPlacement at{lab->toWorld(lens), 4, 0, 1, 1};
  const auto h = overtureHologram(
      overtureScene(app::Overture::Standard, 0, false, view::Theme::manifold()),
      view::Theme::manifold(), at);
  REQUIRE(h.quads.size() == 64);
  // This pins M24.3's bench-preview orientation. M24.4c's Library uses the wall
  // projector and a different station; Watch retains this original bench view.
  const auto camera = *lab->station("watch");
  const auto a1 = centre(h.quads[0]), a8 = centre(h.quads[7]), h1 = centre(h.quads[56]);
  CHECK(camera.project(a1, 16.f / 9, 1920, 1080).x <
        camera.project(h1, 16.f / 9, 1920, 1080).x);
  CHECK(view::length(a1 - camera.eye()) < view::length(a8 - camera.eye()));
}

TEST_CASE("wall subjects fit their geometry and turn flat faces toward the room",
          "[unit][render][hologram][library-framing]") {
  const auto theme = view::Theme::manifold();
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  const auto camera = *lab->station("library");
  const auto anchor = *doc->anchor("wallprojector.lens");
  const auto lens = lab->toWorld(anchor);
  HologramPlacement at{
      lab->toWorld(wallHologramCentre(anchor, kWallHologramRadiusMetres)),
      kWallHologramRadiusMetres * kCellsPerMetre,
      0,
      1,
      1,
      1,
      lens,
      true};
  for (auto which : {app::Overture::Standard, app::Overture::Cylinder}) {
    const auto scene = overtureScene(which, 0, false, theme);
    const auto h = overtureHologram(scene, theme, at);
    REQUIRE_FALSE(h.quads.empty());
    const auto n = rotated({0, 0, 1}, h.quads.front().quat);
    // Face visibility is measured from the actual viewer, not the world's X axis.
    const auto towardEye = view::normalize(camera.eye() - at.centre);
    const float angle =
        std::asin(std::abs(view::dot(n, towardEye))) * 180 / std::acos(-1.f);
    CHECK(angle >= 25);
    CHECK(angle <= 40);
    CHECK(n.x < 0);
    auto padded = scene;
    padded.cam.reach *= 3;
    const auto same = overtureHologram(padded, theme, at);
    REQUIRE(same.quads.size() == h.quads.size());
    for (std::size_t j = 0; j < h.quads.size(); ++j) {
      CHECK(view::length(centre(same.quads[j]) - centre(h.quads[j])) < 1e-5f);
      CHECK(same.quads[j].scale[0] == h.quads[j].scale[0]);
    }
    at.wallSubject = false;
    const auto bench = overtureHologram(scene, theme, at);
    CHECK(std::abs(rotated({0, 0, 1}, bench.quads.front().quat).z) > .999f);
    at.wallSubject = true;
  }
  for (auto which : {app::Overture::Cube5, app::Overture::Hyper4}) {
    const auto scene = overtureScene(which, 1, false, theme);
    const auto wall = overtureHologram(scene, theme, at);
    at.wallSubject = false;
    const auto bench = overtureHologram(scene, theme, at);
    at.wallSubject = true;
    REQUIRE(wall.quads.size() == bench.quads.size());
    for (std::size_t j = 0; j < wall.quads.size(); ++j)
      CHECK(view::length(rotated({0, 0, 1}, wall.quads[j].quat) -
                         rotated({0, 0, 1}, bench.quads[j].quat)) < 1e-4f);
  }
}

TEST_CASE("projector collapse squashes world Z about its lens", "[unit][render][rig]") {
  const auto theme = view::Theme::manifold();
  HologramPlacement at{{1, 2, 12}, 4, .4f, 1, 1};
  at.lens = {1, 2, 5};
  const auto original = tesseractHologram(0, theme, at);
  at.squashZ = .25f;
  const auto squashed = tesseractHologram(0, theme, at);
  REQUIRE(original.lines.size() == squashed.lines.size());
  for (std::size_t i = 0; i < original.lines.size(); ++i) {
    CHECK(squashed.lines[i].center[0] == original.lines[i].center[0]);
    CHECK(squashed.lines[i].center[1] == original.lines[i].center[1]);
    CHECK(squashed.lines[i].center[2] ==
          Catch::Approx(5 + (original.lines[i].center[2] - 5) * .25f));
  }
}
TEST_CASE("projector basis quaternions rotate local axes without reflection",
          "[unit][render][hologram]") {
  CHECK(quatFromBasis({1, 0, 0}, {0, 1, 0}, {0, 0, 1}) ==
        std::array<float, 4>{0, 0, 0, 1});
  std::mt19937 rng(2413);
  std::uniform_real_distribution<float> dist(-1, 1);
  for (int j = 0; j < 100; ++j) {
    const auto t = view::normalize(view::Vec3{dist(rng), dist(rng), dist(rng)});
    const auto n = view::normalize(view::cross(t, {dist(rng), dist(rng), dist(rng)}));
    const auto b = view::cross(n, t);
    const auto q = quatFromBasis(t, b, n);
    CHECK(view::length(rotated({1, 0, 0}, q.data()) - t) < 1e-5f);
    CHECK(view::length(rotated({0, 0, 1}, q.data()) - n) < 1e-5f);
  }
}
TEST_CASE("projector tiles normals tones fades and dashed trails use the scene",
          "[unit][render][hologram]") {
  const auto theme = view::Theme::manifold();
  OvertureScene s;
  s.cam.reach = 2;
  s.quads.push_back({{{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}}, OvTone::Lit, .4f});
  s.tokens.push_back({{0, 0, 0}, {0, 1, 0}, 'K', false, 1, .6f});
  s.trails.push_back(
      {{{-1, 0, 0}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}, theme.ember, 2, .3f, true});
  HologramPlacement at{{2, 3, 4}, 4, 0, .5f, 1};
  const auto h = overtureHologram(s, theme, at);
  REQUIRE(h.quads.size() == 1);
  CHECK(h.quads[0].center[2] == 4);
  CHECK(h.quads[0].scale[0] == 4);
  CHECK(h.quads[0].scale[1] == 4);
  CHECK(h.quads[0].color[0] == theme.ember.r);
  CHECK(h.quads[0].color[3] == Catch::Approx(.2f));
  REQUIRE(h.tokens.size() == 1);
  CHECK(view::length(rotated({0, 0, 1}, h.tokens[0].quat) - view::Vec3{0, 0, 1}) < 1e-5f);
  CHECK(h.tokens[0].color[0] == theme.blackPiece.r);
  CHECK(h.tokens[0].color[3] == Catch::Approx(.3f));
  CHECK(h.tokenBatches[static_cast<std::size_t>(Archetype::Monolith)].count == 1);
  CHECK(h.lines.size() == 2);
}
TEST_CASE("projector manifold and tesseract fit and fade uniformly",
          "[unit][render][hologram]") {
  const auto theme = view::Theme::manifold();
  for (float alpha : {0.f, 1.f}) {
    HologramPlacement at{{1, 2, 3}, 4, .6f, alpha, .5f};
    const auto manifold = manifoldHologram(3, theme, at);
    REQUIRE(manifold.quads.size() == 900);
    const auto cube = tesseractHologram(3, theme, at);
    REQUIRE(cube.lines.size() == 32);
    const auto ov = overtureHologram(
        overtureScene(app::Overture::Torus3d, 1, false, theme), theme, at);
    for (const auto* h : {&manifold, &cube, &ov}) {
      CHECK(h->any());
      bounded(*h, at);
      if (alpha == 0)
        for (const auto* group : {&h->quads, &h->tokens, &h->lines})
          for (const auto& i : *group) CHECK(i.color[3] == 0);
    }
    const auto beam = projectorBeam({1, 2, -3}, at, theme);
    CHECK(beam.center[2] - beam.scale[2] / 2 == Catch::Approx(-3));
    CHECK(beam.center[2] + beam.scale[2] / 2 == Catch::Approx(at.centre.z - at.radius));
    CHECK(beam.scale[0] == 2);
    CHECK(beam.color[3] == Catch::Approx(.12f * alpha));
  }
}
TEST_CASE("projector idle light only halves the named machine light",
          "[unit][render][hologram]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  const auto wall = labLightBlock(*doc, *lab, 1, kLabKey, "projector", .5f,
                                  view::Theme::manifold(), .75f);
  for (std::size_t i = 0; i < doc->lights.size(); ++i)
    if (doc->lights[i].name == "wallprojector")
      CHECK(wall.lights[i].colorIntensity[3] ==
            Catch::Approx(doc->lights[i].intensity * .75f));
  const auto on = labLightBlock(*doc, *lab, 1, kLabKey, "projector", 1);
  const auto off = labLightBlock(*doc, *lab, 1, kLabKey, "projector", .5f);
  for (std::size_t i = 0; i < doc->lights.size(); ++i)
    CHECK(off.lights[i].colorIntensity[3] ==
          on.lights[i].colorIntensity[3] *
              (doc->lights[i].name == "projector" ? .5f : 1.f));
}
TEST_CASE("GPU projector is local and zero alpha leaves no raster fragments",
          "[render][hologram]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  auto ctx = VulkanContext::create();
  REQUIRE(ctx);
  {
    auto renderer = BoardRenderer::create(*ctx);
    REQUIRE(renderer);
    auto target = OffscreenTarget::create(*ctx, 640, 360);
    REQUIRE(target);
    renderer->setTheme(view::Theme::manifold());
    BoardOptions options;
    options.labMode = true;
    renderer->setOptions(options);
    renderer->setLights(labLightBlock(*doc, *lab, 1, kLabKey));
    lab->setFileRanges(renderer->addLabMeshes(lab->fileMeshes()));
    InstanceSet set;
    lab->appendTo(set, renderer->theme());
    const auto camera = *lab->station("menu");
    auto lens = *doc->anchor("projector.lens");
    const auto source = lab->toWorld(lens);
    lens[2] += .32f;
    HologramPlacement at{lab->toWorld(lens), 4, 0, 1, 1};
    const auto render = [&] {
      REQUIRE(renderer->render(*target, set, camera));
      auto p = target->readPixels();
      REQUIRE(p);
      return *p;
    };
    const auto empty = render();
    auto h = manifoldHologram(0, renderer->theme(), at);
    set.holo = h.quads;
    set.holoTokens = h.tokens;
    set.holoTokenBatches = h.tokenBatches;
    set.holoBeam.push_back(projectorBeam(source, at, renderer->theme()));
    const auto lit = render();
    CHECK(lit != empty);
    float x0 = 640, y0 = 360, x1 = 0, y1 = 0;
    for (float x : {-5.f, 5.f})
      for (float y : {-5.f, 5.f})
        for (float z : {-5.f, 5.f}) {
          const auto p =
              camera.project(at.centre + view::Vec3{x, y, z}, 640.f / 360, 640, 360);
          x0 = std::min(x0, p.x);
          x1 = std::max(x1, p.x);
          y0 = std::min(y0, p.y);
          y1 = std::max(y1, p.y);
        }
    const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = x1 - x0, ry = y1 - y0;
    std::size_t changed = 0, outside = 0;
    for (int y = 0; y < 360; ++y)
      for (int x = 0; x < 640; ++x) {
        const auto i = static_cast<std::size_t>((y * 640 + x) * 4);
        if (lit[i] == empty[i] && lit[i + 1] == empty[i + 1] &&
            lit[i + 2] == empty[i + 2])
          continue;
        if (std::abs(static_cast<float>(x) - cx) < rx &&
            std::abs(static_cast<float>(y) - cy) < ry)
          ++changed;
        else
          ++outside;
      }
    CHECK(changed > 50);
    CHECK(outside == 0);
    for (auto* group : {&set.holo, &set.holoTokens, &set.holoBeam})
      for (auto& i : *group) i.color[3] = 0;
    CHECK(render() == empty);
    // A box's cap has constant mesh height: opacity must still affect its RGB
    // coverage, rather than only its alpha channel or the thin side walls.
    set.holoBeam.clear();
    set.holoTokens.clear();
    auto cap = h.quads.front();
    cap.center[0] = at.centre.x;
    cap.center[1] = at.centre.y;
    cap.center[2] = at.centre.z;
    cap.scale[0] = 4;
    cap.scale[1] = 4;
    cap.scale[2] = 0;
    cap.quat[0] = 0;
    cap.quat[1] = 0;
    cap.quat[2] = 0;
    cap.quat[3] = 1;
    cap.color[0] = 1;
    cap.color[1] = 1;
    cap.color[2] = 1;
    cap.color[3] = 1;
    set.holo = {cap};
    const auto solid = render();
    set.holo[0].color[3] = .5f;
    const auto half = render();
    const auto coverage = [&](const auto& p) {
      std::size_t n = 0;
      for (std::size_t i = 0; i < p.size(); i += 4)
        if (p[i] != empty[i] || p[i + 1] != empty[i + 1] || p[i + 2] != empty[i + 2]) ++n;
      return n;
    };
    CHECK(coverage(half) < coverage(solid) * 3 / 4);
    CHECK(coverage(half) > coverage(solid) / 4);
  }
  CHECK(ctx->takeValidationMessages().empty());
}
#endif
#ifdef CB_HAVE_IMGUI
TEST_CASE("Ui requests projector subjects and refreshes overtures without flat drawing",
          "[render][hologram]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct SdlGuard {
    ~SdlGuard() { SDL_Quit(); }
  } sdl;
  auto* window = SDL_CreateWindow("hologram", 960, 540, SDL_WINDOW_HIDDEN);
  REQUIRE(window);
  struct WindowGuard {
    SDL_Window* w;
    ~WindowGuard() { SDL_DestroyWindow(w); }
  } guard{window};
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
    (*ui)->setLabBackdrop(true);
    const auto frame = [&] {
      (*ui)->tick(.1f);
      (*ui)->newFrame();
      (void)(*ui)->build(*shell, 60);
      (*ui)->endFrame();
      return (*ui)->hologramRequest();
    };
    CHECK(frame().kind == HologramRequest::Manifold);
    CHECK((*ui)->hologramRequest().radiusMetres == .26f);
    shell->go(app::Screen::Welcome);
    CHECK(frame().kind == HologramRequest::Manifold);
    CHECK((*ui)->hologramRequest().radiusMetres == .26f);
    shell->go(app::Screen::Settings);
    CHECK(frame().kind == HologramRequest::Tesseract);
    CHECK((*ui)->hologramRequest().radiusMetres == .20f);
    shell->go(app::Screen::NewGame);
    shell->overtures().jumpTo(app::Overture::Torus);
    shell->overtures().setProgress(0);
    CHECK(frame().kind == HologramRequest::Overture);
    CHECK((*ui)->hologramRequest().projector == HologramRequest::Projector::Wall);
    CHECK((*ui)->hologramRequest().radiusMetres == kWallHologramRadiusMetres);
    REQUIRE((*ui)->overtureScene());
    const auto flat = (*ui)->overtureScene()->quads.front().p[0];
    shell->overtures().setProgress(1);
    (void)frame();
    const auto formed = (*ui)->overtureScene()->quads.front().p[0];
    CHECK(std::hypot(flat.x - formed.x, flat.y - formed.y, flat.z - formed.z) > .1f);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(180, 230);
    (void)frame();
    (void)frame();
    io.AddMouseButtonEvent(0, true);
    (void)frame();
    const auto before = (*ui)->hologramRequest();
    io.AddMousePosEvent(220, 250);
    const auto after = frame();
    CHECK(after.yaw > before.yaw);
    CHECK(after.alpha == 1);
    CHECK(after.scale == 1);
    io.AddMouseButtonEvent(0, false);
    (void)frame();
    shell->go(app::Screen::Editor);
    CHECK(frame().kind == HologramRequest::None);
    shell->go(app::Screen::QuitConfirm);
    CHECK(frame().kind == HologramRequest::None);
    (*ui)->setCinema(true);
    CHECK(frame().kind == HologramRequest::None);
  }
  CHECK(ctx->takeValidationMessages().empty());
}

TEST_CASE("wall projector beam and collapse follow minus X without rotating the subject",
          "[unit][render][hologram]") {
  HologramPlacement at;
  at.lens = {30, 20, 25};
  at.centre = {19.6f, 20, 25};
  at.radius = 8.4f;
  const auto beam = projectorBeam(at.lens, at, view::Theme::manifold());
  CHECK(beam.center[0] == Catch::Approx(29));
  CHECK(beam.center[1] == 20);
  CHECK(beam.center[2] == 25);
  const auto axis = rotated({0, 0, 1}, beam.quat);
  CHECK(axis.x == Catch::Approx(-1));
  const auto normal = tesseractHologram(.3f, view::Theme::manifold(), at);
  at.squashZ = .25f;
  const auto collapsed = tesseractHologram(.3f, view::Theme::manifold(), at);
  REQUIRE(normal.lines.size() == collapsed.lines.size());
  for (std::size_t n = 0; n < normal.lines.size(); ++n) {
    CHECK(collapsed.lines[n].center[0] ==
          Catch::Approx(30 + (normal.lines[n].center[0] - 30) * .25f));
    CHECK(collapsed.lines[n].center[1] == Catch::Approx(normal.lines[n].center[1]));
    CHECK(collapsed.lines[n].center[2] == Catch::Approx(normal.lines[n].center[2]));
  }
}

TEST_CASE("the lens flare of power-on peaks at 1.6 lens radii and fades out",
          "[unit][render][hologram][wake]") {
  const view::Vec3 lens{30, 20, 25}, toward{19.6f, 20, 25};
  const auto theme = view::Theme::manifold();
  CHECK_FALSE(projectorFlare(lens, toward, 4.f, 0.f, theme));
  CHECK_FALSE(projectorFlare(lens, toward, 4.f, .04f, theme));
  CHECK_FALSE(projectorFlare(lens, toward, 4.f, .26f, theme));
  const auto start = projectorFlare(lens, toward, 4.f, .05f, theme);
  REQUIRE(start);
  CHECK(start->scale[0] == Catch::Approx(0).margin(1e-4));
  CHECK(start->color[3] == Catch::Approx(.5f * theme.ember.a));
  const auto peak = projectorFlare(lens, toward, 4.f, .15f, theme);
  REQUIRE(peak);
  CHECK(peak->scale[0] == Catch::Approx(2 * 1.6f * 4.f));
  CHECK(peak->color[3] < start->color[3]);
  const auto end = projectorFlare(lens, toward, 4.f, .2499f, theme);
  REQUIRE(end);
  CHECK(end->scale[0] == Catch::Approx(2 * 1.f * 4.f).margin(.05));
  CHECK(end->color[3] < .01f);
  const auto axis = rotated({0, 0, 1}, peak->quat);
  CHECK(axis.x == Catch::Approx(-1));  // the disc faces along the beam
}
#endif
