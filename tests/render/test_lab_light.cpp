// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_VULKAN
#include <cmath>
#include "render/lab_light.hpp"
#include "render/lab_scene.hpp"
#include "render/vulkan_context.hpp"
#include "support/variants.hpp"
using namespace cb;
using namespace cb::render;
namespace {
constexpr float kFocusLevelForTest = .15f;
}
TEST_CASE("local light falloff has a finite peak and an exact radius",
          "[unit][render][light]") {
  constexpr float r = 2;
  CHECK(lightFalloff(0, r) == 4);
  CHECK(lightFalloff(r, r) == 0);
  CHECK(lightFalloff(1.5f * r, r) == 0);
  float previous = lightFalloff(0, r);
  for (int i = 1; i <= 100; ++i) {
    const float next = lightFalloff(r * static_cast<float>(i) / 100, r);
    CHECK(next <= previous);
    previous = next;
  }
  CHECK(spotFactor(1, .8f, .6f) == 1);
  CHECK(spotFactor(.5f, .8f, .6f) == 0);
  CHECK(spotFactor(.7f, .8f, .6f) == Catch::Approx(.5));
  CHECK(spotFactor(.8f, .8f, .8f) == 1);
  CHECK(spotFactor(.7f, .8f, .8f) == 0);
  previous = 0;
  for (int i = 0; i <= 100; ++i) {
    const float next = spotFactor(.6f + .2f * static_cast<float>(i) / 100, .8f, .6f);
    CHECK(next >= previous);
    previous = next;
  }
}
TEST_CASE("lab light block preserves metres and separates room and machine gain",
          "[unit][render][light]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  const auto scene = LabScene::create(*doc);
  REQUIRE(scene);
  const auto b = labLightBlock(*doc, *scene, .3f, kLabKey);
  CHECK(b.header[0] == static_cast<float>(doc->lights.size()));
  CHECK(b.header[1] == kLabKey);
  CHECK(b.header[2] == 1 / kCellsPerMetre);
  for (std::size_t i = 0; i < doc->lights.size(); ++i) {
    const auto& l = doc->lights[i];
    const auto pos = scene->toWorld(l.pos);
    CHECK(b.lights[i].posRadius[0] == pos.x);
    CHECK(b.lights[i].posRadius[1] == pos.y);
    CHECK(b.lights[i].posRadius[2] == pos.z);
    CHECK(b.lights[i].posRadius[3] == l.radius);
    CHECK(b.lights[i].colorIntensity[3] == l.intensity * (l.group == "room" ? .3f : 1));
    if (l.kind == io::LabLight::Kind::Spot) {
      const auto& d = b.lights[i].dirCone;
      CHECK(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] == Catch::Approx(1));
      CHECK(b.softCos[i] <= d[3]);
    } else
      CHECK(b.lights[i].dirCone[3] == -2);
  }
  const auto neutral = neutralLightBlock();
  CHECK(neutral.header[0] == 0);
  CHECK(neutral.header[1] == 1.0f);
  CHECK(shaderMaterial(view::Material::Sky, Cut::Plank, 0) == 8);
}
TEST_CASE("GPU neutral lights are byte identical to an untouched standard renderer",
          "[render][light]") {
  auto ctx = VulkanContext::create();
  REQUIRE(ctx);
  {
    auto renderer = BoardRenderer::create(*ctx);
    REQUIRE(renderer);
    auto target = OffscreenTarget::create(*ctx, 320, 180);
    REQUIRE(target);
    const auto variant = test::loadVariant("standard");
    const auto position = Position::startPosition(variant);
    const auto snapshot = view::PositionView::capture(position);
    const auto cfg = view::ViewConfig::forBoard(variant.dims);
    const auto set = renderer->buildInstances(snapshot, cfg);
    view::OrbitCamera camera;
    camera.distance = 16;
    REQUIRE(renderer->render(*target, set, camera));
    const auto before = target->readPixels();
    REQUIRE(before);
    renderer->setLights(neutralLightBlock());
    REQUIRE(renderer->render(*target, set, camera));
    const auto after = target->readPixels();
    REQUIRE(after);
    CHECK(*before == *after);
  }
  CHECK(ctx->takeValidationMessages().empty());
}
TEST_CASE("GPU practical lights brighten the lab and vanish beyond their radius",
          "[render][light]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto scene = LabScene::create(*doc);
  REQUIRE(scene);
  auto ctx = VulkanContext::create();
  REQUIRE(ctx);
  {
    auto renderer = BoardRenderer::create(*ctx);
    REQUIRE(renderer);
    auto target = OffscreenTarget::create(*ctx, 320, 180);
    REQUIRE(target);
    renderer->setTheme(view::Theme::manifold());
    BoardOptions options;
    options.labMode = true;
    renderer->setOptions(options);
    scene->setFileRanges(renderer->addLabMeshes(scene->fileMeshes()));
    InstanceSet set;
    scene->appendTo(set, renderer->theme());
    const auto camera = *scene->station("menu");
    const auto lit = labLightBlock(*doc, *scene, 1, kLabKey);
    auto dark = labLightBlock(*doc, *scene, 0, kLabKey);
    for (auto& l : dark.lights) l.colorIntensity[3] = 0;
    const auto render = [&](const GpuLightBlock& b) {
      renderer->setLights(b);
      REQUIRE(renderer->render(*target, set, camera));
      auto p = target->readPixels();
      REQUIRE(p);
      return *p;
    };
    const auto a = render(lit), b = render(dark);
    CHECK(a != b);
    const auto luminance = [](const auto& p) {
      double total = 0;
      for (std::size_t i = 0; i < p.size(); i += 4)
        total += .2126 * p[i] + .7152 * p[i + 1] + .0722 * p[i + 2];
      return total;
    };
    CHECK(luminance(a) > luminance(b));
    // A 10 cm matte box; every surface is farther than the 2 m cut-off.
    io::LabDoc boxDoc;
    boxDoc.anchors.push_back({"gantry.centre", {0, 0, 0}});
    io::LabPart box;
    box.size = {.1f, .1f, .1f};
    box.color = {1, 1, 1, 1};
    boxDoc.parts.push_back(box);
    auto boxScene = LabScene::create(boxDoc);
    REQUIRE(boxScene);
    set = {};
    boxScene->appendTo(set, renderer->theme());
    view::OrbitCamera boxCamera;
    boxCamera.distance = 8;
    auto outside = neutralLightBlock();
    outside.header[0] = 1;
    outside.header[2] = 1 / kCellsPerMetre;
    outside.lights[0] = {{60, 0, 0, 2}, {1, 1, 1, 1}, {0, 0, 0, -2}};
    renderer->setLights(outside);
    REQUIRE(renderer->render(*target, set, boxCamera));
    auto far = target->readPixels();
    REQUIRE(far);
    renderer->setLights(neutralLightBlock());
    REQUIRE(renderer->render(*target, set, boxCamera));
    auto zero = target->readPixels();
    REQUIRE(zero);
    CHECK(*far == *zero);
  }
  CHECK(ctx->takeValidationMessages().empty());
}
TEST_CASE("GPU skylight contains stars above three times its sky base",
          "[render][light]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto scene = LabScene::create(*doc);
  REQUIRE(scene);
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
    scene->setFileRanges(renderer->addLabMeshes(scene->fileMeshes()));
    renderer->setLights(labLightBlock(*doc, *scene, 1, kLabKey));
    InstanceSet set;
    scene->appendTo(set, renderer->theme());
    const auto camera = scene->station("sky");
    REQUIRE(camera);
    REQUIRE(renderer->render(*target, set, *camera));
    const auto stars = target->readPixels();
    REQUIRE(stars);
    // An emissive green skylight marks its visible pixels after depth testing;
    // gantry rails and other foreground parts cannot count as stars.
    bool found = false;
    for (auto& i : set.lab)
      if (i.material == 8) {
        found = true;
        i.material = 5;
        i.color[0] = 0;
        i.color[1] = 1;
        i.color[2] = 0;
      }
    REQUIRE(found);
    REQUIRE(renderer->render(*target, set, *camera));
    const auto mask = target->readPixels();
    REQUIRE(mask);
    std::size_t count = 0;
    for (std::size_t p = 0; p < mask->size(); p += 4) {
      if ((*mask)[p] != 0 || (*mask)[p + 1] < 128 || (*mask)[p + 2] != 0) continue;
      // Even three times the maximum base + city glow is below this threshold.
      if ((*stars)[p] > 56 && (*stars)[p + 1] > 56 && (*stars)[p + 2] > 56) ++count;
    }
    CAPTURE(count);
    CHECK(count >= 5);
  }
  CHECK(ctx->takeValidationMessages().empty());
}
TEST_CASE("GPU salience affects only the focused projector rectangle", "[render][rig]") {
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
    auto block = labLightBlock(*doc, *lab, 1, kLabKey);
    CHECK(block.implant[0] == renderer->theme().implant.r);
    CHECK(neutralLightBlock().implant[0] == 0);
    renderer->setLights(block);
    lab->setFileRanges(renderer->addLabMeshes(lab->fileMeshes()));
    InstanceSet set;
    const auto camera = *lab->station("menu");
    const auto render = [&](float sal) {
      lab->appendTo(set, renderer->theme(), [&](std::string_view focus) {
        return focus == "projector" ? sal : 0.f;
      });
      REQUIRE(renderer->render(*target, set, camera));
      auto pixels = target->readPixels();
      REQUIRE(pixels);
      return *pixels;
    };
    const auto off = render(0), on = render(1);
    CHECK(on != off);
    // All projector parts fit in this authored 40cm cube; double its projected
    // half-extents for the same bloom margin as the M24.3 locality test.
    auto lens = *doc->anchor("projector.lens");
    const auto centre = lab->toWorld(lens);
    float x0 = 640, y0 = 360, x1 = 0, y1 = 0;
    for (float x : {-4.f, 4.f})
      for (float y : {-4.f, 4.f})
        for (float z : {-4.f, 4.f}) {
          const auto p =
              camera.project(centre + view::Vec3{x, y, z}, 640.f / 360, 640, 360);
          x0 = std::min(x0, p.x);
          x1 = std::max(x1, p.x);
          y0 = std::min(y0, p.y);
          y1 = std::max(y1, p.y);
        }
    std::size_t changed = 0, outside = 0;
    for (int y = 0; y < 360; ++y)
      for (int x = 0; x < 640; ++x) {
        const auto i = static_cast<std::size_t>((y * 640 + x) * 4);
        if (on[i] == off[i] && on[i + 1] == off[i + 1] && on[i + 2] == off[i + 2])
          continue;
        ++changed;
        if (std::abs(static_cast<float>(x) - (x0 + x1) / 2) > x1 - x0 ||
            std::abs(static_cast<float>(y) - (y0 + y1) / 2) > y1 - y0)
          ++outside;
      }
    CHECK(changed > 20);
    CHECK(outside == 0);
  }
  CHECK(ctx->takeValidationMessages().empty());
}
TEST_CASE("game lights: focus dims the room group, slab glows join, all placed",
          "[unit][render][light][rig]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  const auto plain = labLightBlock(*doc, *lab, 1, kLabKey);
  GpuLight glow{};
  glow.posRadius[3] = 2.5f;
  glow.colorIntensity[3] = .6f;
  glow.dirCone[3] = -2;
  const std::array<GpuLight, 2> extra{glow, glow};
  const view::RigXform x{{2, 3, 4}, 1.5f};
  const auto game = labLightBlock(*doc, *lab, 1, kLabKey, {}, 1, view::Theme::manifold(),
                                  1, kFocusLevelForTest, &x, extra);
  const auto authored = static_cast<std::size_t>(plain.header[0]);
  CHECK(game.header[0] == static_cast<float>(authored + 2));
  CHECK(game.header[2] == Catch::Approx(1 / (20 * 1.5f)));
  for (std::size_t i = 0; i < authored; ++i) {
    CAPTURE(doc->lights[i].name);
    const float scale = doc->lights[i].group == "room" ? kFocusLevelForTest : 1;
    CHECK(game.lights[i].colorIntensity[3] ==
          Catch::Approx(plain.lights[i].colorIntensity[3] * scale));
    const auto p = x.toBoard({plain.lights[i].posRadius[0], plain.lights[i].posRadius[1],
                              plain.lights[i].posRadius[2]});
    CHECK(game.lights[i].posRadius[0] == Catch::Approx(p.x));
    CHECK(game.lights[i].posRadius[2] == Catch::Approx(p.z));
    CHECK(game.lights[i].posRadius[3] == plain.lights[i].posRadius[3]);
  }
  CHECK(game.lights[authored].colorIntensity[3] == .6f);
}
#endif
