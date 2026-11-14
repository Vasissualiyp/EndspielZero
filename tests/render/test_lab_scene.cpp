// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_VULKAN
#include <cmath>
#include <fstream>
#include <numbers>
#include <numeric>
#include "render/lab_scene.hpp"
#include "render/vulkan_context.hpp"
#include "support/variants.hpp"
#include "view/rig_move.hpp"
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>
#include "app/shell.hpp"
#include "render/ui.hpp"
#endif
using namespace cb;
using namespace cb::render;
TEST_CASE("lab rotations are right handed Z Y X", "[unit][render][lab]") {
  const auto rotateX = [](const std::array<float, 4>& q) {
    return std::array<float, 3>{1 - 2 * (q[1] * q[1] + q[2] * q[2]),
                                2 * (q[0] * q[1] + q[3] * q[2]),
                                2 * (q[0] * q[2] - q[3] * q[1])};
  };
  const auto id = quatFromYawPitchRoll(0, 0, 0);
  CHECK(id == std::array<float, 4>{0, 0, 0, 1});
  const auto yaw = rotateX(quatFromYawPitchRoll(std::numbers::pi_v<float> / 2, 0, 0));
  CHECK(yaw[0] == Catch::Approx(0).margin(1e-6));
  CHECK(yaw[1] == Catch::Approx(1));
  const auto pitch = rotateX(quatFromYawPitchRoll(0, std::numbers::pi_v<float> / 2, 0));
  CHECK(pitch[0] == Catch::Approx(0).margin(1e-6));
  CHECK(pitch[2] == Catch::Approx(-1));
}
TEST_CASE("lab coordinates cameras and batches use metres", "[unit][render][lab]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc.has_value());
  auto lab = LabScene::create(*doc);
  REQUIRE(lab.has_value());
  auto centre = *doc->anchor("gantry.centre");
  const auto origin = lab->toWorld(centre);
  CHECK(origin.x == 0);
  CHECK(origin.y == 0);
  CHECK(origin.z == 0);
  centre[0] += 1;
  CHECK(lab->toWorld(centre).x == 20);
  const auto camera = lab->station("menu");
  REQUIRE(camera.has_value());
  const auto* st = doc->station("menu");
  CHECK(camera->distance == st->distance * 20);
  CHECK(camera->yaw == Catch::Approx(st->yaw * std::numbers::pi_v<float> / 180));
  CHECK(camera->pitch == Catch::Approx(st->pitch * std::numbers::pi_v<float> / 180));
  CHECK(camera->fovY == Catch::Approx(st->fov * std::numbers::pi_v<float> / 180));
  CHECK(camera->target.y == lab->toWorld(st->target).y);
  CHECK(camera->nearZ == 1);
  CHECK(camera->farZ == 600);
  CHECK_FALSE(lab->station("missing").has_value());
  // File ranges are normally assigned by BoardRenderer::addLabMeshes at load time.
  std::vector<std::uint32_t> ranges(lab->fileMeshes().size());
  std::iota(ranges.begin(), ranges.end(), 0u);
  lab->setFileRanges(ranges);
  InstanceSet set;
  lab->appendTo(set, view::Theme::manifold());
  REQUIRE(set.lab.size() == doc->parts.size());
  std::size_t count = 0;
  for (auto b : set.labBatches) {
    CHECK(b.first == count);
    count += b.count;
  }
  for (auto b : set.labFileBatches) {
    CHECK(b.first == count);
    count += b.count;
  }
  CHECK(count == set.lab.size());
  for (const auto& i : set.lab) {
    CHECK(i.fxCell == 0);
    for (auto signal : i.panelSignal) CHECK(signal == 0);
  }
}
TEST_CASE("lab file meshes upload and survive a piece-body rebuild", "[render][lab]") {
  const auto path = std::filesystem::path(CB_BINARY_DIR) / "lab-test.obj";
  {
    std::ofstream out(path);
    out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }
  io::LabDoc doc;
  doc.anchors.push_back({"gantry.centre", {0, 0, 0}});
  io::LabPart p;
  p.name = "file";
  p.mesh = io::LabMesh::File;
  p.file = path;
  p.size = {1, 2, 3};
  doc.parts.push_back(p);
  auto lab = LabScene::create(doc);
  REQUIRE(lab.has_value());
  CHECK(lab->fileMeshes().size() == 1);
  auto ctx = VulkanContext::create();
  REQUIRE(ctx.has_value());
  auto renderer = BoardRenderer::create(*ctx);
  REQUIRE(renderer.has_value());
  lab->setFileRanges(renderer->addLabMeshes(lab->fileMeshes()));
  InstanceSet set;
  lab->appendTo(set, view::Theme::manifold());
  REQUIRE(set.labFileBatches.size() == 1);
  CHECK(set.labFileBatches[0].count == 1);
  CHECK(set.lab[0].scale[0] == 20);
  CHECK(set.lab[0].scale[1] == 40);
  auto target = OffscreenTarget::create(*ctx, 64, 64);
  REQUIRE(target.has_value());
  view::OrbitCamera cam;
  cam.distance = 40;
  REQUIRE(renderer->render(*target, set, cam).has_value());
  auto before = target->readPixels();
  REQUIRE(before.has_value());
  REQUIRE(renderer->render(*target, {}, cam).has_value());
  auto empty = target->readPixels();
  REQUIRE(empty.has_value());
  CHECK(*before != *empty);
  auto variant = test::loadVariant("standard");
  REQUIRE(renderer->syncBodies(variant).has_value());
  REQUIRE(renderer->render(*target, set, cam).has_value());
  auto after = target->readPixels();
  REQUIRE(after.has_value());
  CHECK(*before == *after);
  CHECK(ctx->takeValidationMessages().empty());
  std::filesystem::remove(path);
}
TEST_CASE("lab station framing keeps the hardware visible and eyes inside the room",
          "[unit][render][lab]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc.has_value());
  auto lab = LabScene::create(*doc);
  REQUIRE(lab.has_value());
  const auto centre = *doc->anchor("gantry.centre");
  for (const auto& st : doc->stations) {
    CAPTURE(st.name);
    const auto camera = *lab->station(st.name);
    const auto eye =
        camera.eye() * (1 / kCellsPerMetre) + view::Vec3{centre[0], centre[1], centre[2]};
    CHECK(std::abs(eye.x) < 2.5f);
    CHECK(std::abs(eye.y) < 3.f);
    CHECK(eye.z > 0);
    CHECK(eye.z < 2.7f);
    if (st.name == "menu" || st.name == "library" || st.name == "watch") {
      for (auto anchor : {st.name == "library" ? "wallprojector.lens" : "projector.lens",
                          "magazine.front"}) {
        if (st.name == "menu" && std::string_view(anchor) == "magazine.front") continue;
        CAPTURE(anchor);
        const auto projected = view::transformPoint(camera.viewProj(16.f / 9),
                                                    lab->toWorld(*doc->anchor(anchor)));
        // The menu panels occupy the right half; hardware must clear them.
        CHECK(projected.x > -.9f);
        CHECK(projected.x < 0);
        CHECK(std::abs(projected.y) < .9f);
        if (st.name != "menu" && std::string_view(anchor) == "projector.lens")
          CHECK(projected.y > .5f);  // Below the retained 2-D library decoration.
      }
    }
  }
}
TEST_CASE("the magazine slabs fit inside the authored cassette and its slot guides",
          "[unit][render][lab]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc.has_value());
  const auto frame =
      std::find_if(doc->parts.begin(), doc->parts.end(),
                   [](const auto& p) { return p.name == "magazine.frame"; });
  REQUIRE(frame != doc->parts.end());
  REQUIRE(frame->mesh == io::LabMesh::File);
  std::size_t count = 0;
  for (const auto& slab : doc->parts) {
    if (slab.mesh != io::LabMesh::Slab) continue;
    CAPTURE(slab.name);
    REQUIRE(slab.rot == std::array<float, 3>{90, 90, 0});
    // Z*Y maps local (X,Y,Z) extents to world (Y,Z,X). Keep the slabs
    // between the cassette front and back, within its 30 mm slot openings.
    CHECK(slab.pos[0] - slab.size[1] / 2 >= frame->pos[0] - .25f);
    CHECK(slab.pos[0] + slab.size[1] / 2 <= frame->pos[0] - .012f);
    CHECK(slab.size[2] <= .03f);
    CHECK(slab.pos[1] ==
          Catch::Approx(frame->pos[1] + (static_cast<float>(count) - 3.5f) * .11f));
    CHECK(slab.pos[2] - slab.size[0] / 2 > frame->pos[2] + .05f);
    CHECK(slab.pos[2] + slab.size[0] / 2 < frame->pos[2] + 1.f);
    ++count;
  }
  CHECK(count == 8);
}
TEST_CASE("every lab station renders a nonuniform room", "[render][lab]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc.has_value());
  auto lab = LabScene::create(*doc);
  REQUIRE(lab.has_value());
  REQUIRE(lab->fileMeshes().size() == 14);
  auto ctx = VulkanContext::create();
  REQUIRE(ctx.has_value());
  auto renderer = BoardRenderer::create(*ctx);
  auto target = OffscreenTarget::create(*ctx, 640, 360);
  REQUIRE(renderer.has_value());
  REQUIRE(target.has_value());
  lab->setFileRanges(renderer->addLabMeshes(lab->fileMeshes()));
  renderer->setTheme(view::Theme::manifold());
  BoardOptions options;
  options.labMode = true;
  renderer->setOptions(options);
  InstanceSet set;
  lab->appendTo(set, renderer->theme());
#ifdef CB_HAVE_IMGUI
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct SdlGuard {
    ~SdlGuard() { SDL_Quit(); }
  } sdl;
  auto* window = SDL_CreateWindow("lab-test", 640, 360, SDL_WINDOW_HIDDEN);
  REQUIRE(window);
  struct WindowGuard {
    SDL_Window* window;
    ~WindowGuard() { SDL_DestroyWindow(window); }
  } guard{window};
  auto ui = Ui::create(*ctx, window, renderer->theme());
  REQUIRE(ui.has_value());
  auto shell = app::Shell::create({}, {});
  REQUIRE(shell);
  shell->dismissWelcome();
  (*ui)->setLabBackdrop(true);
#endif
  for (float scale : {0.f, 1.f, 3.f})
    for (const auto& s : doc->stations) {
      CAPTURE(scale);
#ifdef CB_HAVE_IMGUI
      REQUIRE((*ui)->setScale(scale > 0 ? scale : 1).has_value());
      (*ui)->tick(1);
      (*ui)->newFrame();
      (void)(*ui)->build(*shell, 60);
      (*ui)->endFrame();
#endif
      CAPTURE(s.name);
      std::function<void(VkCommandBuffer)> overlay;
#ifdef CB_HAVE_IMGUI
      if (scale > 0) overlay = [&](VkCommandBuffer cmd) { (*ui)->record(cmd); };
#endif
      REQUIRE(renderer->render(*target, set, *lab->station(s.name), overlay).has_value());
      auto pixels = target->readPixels();
      REQUIRE(pixels.has_value());
      double sum = 0, sq = 0;
      for (std::size_t i = 0; i < pixels->size(); i += 4) {
        const double l =
            .2126 * (*pixels)[i] + .7152 * (*pixels)[i + 1] + .0722 * (*pixels)[i + 2];
        sum += l;
        sq += l * l;
      }
      const double count = static_cast<double>(pixels->size() / 4);
      CHECK(std::sqrt(sq / count - (sum / count) * (sum / count)) > 2);
    }
  CHECK(ctx->takeValidationMessages().empty());
}
TEST_CASE("salience marks only authored focus parts with positive material payload",
          "[unit][render][rig]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  std::vector<std::uint32_t> ranges(lab->fileMeshes().size());
  std::iota(ranges.begin(), ranges.end(), 0);
  lab->setFileRanges(ranges);
  InstanceSet set;
  lab->appendTo(set, view::Theme::manifold(),
                [](std::string_view focus) { return focus == "projector" ? .75f : 0.f; });
  std::size_t marked = 0, authored = 0;
  for (const auto& p : doc->parts)
    if (p.focus == "projector") ++authored;
  REQUIRE(authored == 4);
  for (const auto& i : set.lab)
    if (i.panelSignal[2] > 0) {
      ++marked;
      CHECK(i.panelSignal[2] == .75f);
    }
  CHECK(marked == authored);
  lab->appendTo(set, view::Theme::manifold());
  for (const auto& i : set.lab) CHECK(i.panelSignal[2] == 0);
}
TEST_CASE("menu station flights keep their eyes within the room", "[unit][render][rig]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  const auto centre = *doc->anchor("gantry.centre");
  for (const auto from : {"menu", "library", "watch", "tutorial", "settings", "quit"})
    for (const auto to : {"menu", "library", "watch", "tutorial", "settings", "quit"}) {
      CAPTURE(from, to);
      const auto a = *lab->station(from), b = *lab->station(to);
      for (int i = 0; i <= 100; ++i) {
        const auto camera =
            view::blendCamera(a, b, view::rigEase(static_cast<float>(i) / 100));
        const auto eye = camera.eye() * (1 / kCellsPerMetre) +
                         view::Vec3{centre[0], centre[1], centre[2]};
        CAPTURE(i, eye.x, eye.y, eye.z);
        CHECK(std::abs(eye.x) < 2.5f);
        CHECK(std::abs(eye.y) < 3.f);
        CHECK(eye.z > 0);
        CHECK(eye.z < 2.7f);
      }
    }
}

TEST_CASE("wall projector reuses mounted hardware with its lens facing minus X",
          "[unit][render][lab]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  const auto part = [&](std::string_view name) -> const io::LabPart& {
    auto p = std::find_if(doc->parts.begin(), doc->parts.end(),
                          [&](const auto& item) { return item.name == name; });
    REQUIRE(p != doc->parts.end());
    return *p;
  };
  for (const auto suffix : {"body", "lens", "leds"}) {
    const auto& bench = part(std::string("projector.") + suffix);
    const auto& wall = part(std::string("wallprojector.") + suffix);
    CHECK(wall.file == bench.file);
    CHECK(wall.material == bench.material);
    CHECK(wall.group == "rig");
    CHECK(wall.focus == "wallprojector");
    CHECK(wall.rot == std::array<float, 3>{0, -90, 0});
  }
  const auto& body = part("wallprojector.body");
  const auto& bracket = part("wallprojector.bracket");
  CHECK(bracket.pos[0] + bracket.size[0] / 2 == Catch::Approx(2.5f));
  CHECK(bracket.pos[0] - bracket.size[0] / 2 == Catch::Approx(body.pos[0]));
  const auto lens = doc->anchor("wallprojector.lens");
  REQUIRE(lens);
  CHECK((*lens)[0] == Catch::Approx(body.pos[0] - .075f));
  CHECK((*lens)[1] == body.pos[1]);
  CHECK((*lens)[2] == body.pos[2]);
}

TEST_CASE("rack offsets move selected slabs toward the room only",
          "[unit][render][lab]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  InstanceSet parked, moved;
  lab->appendTo(parked, view::Theme::manifold());
  std::array<float, 8> slides{};
  slides[0] = .06f;
  lab->appendTo(moved, view::Theme::manifold(), {}, slides);
  REQUIRE(parked.lab.size() == moved.lab.size());
  int changed = 0;
  for (std::size_t i = 0; i < parked.lab.size(); ++i) {
    CHECK(moved.lab[i].center[1] == parked.lab[i].center[1]);
    CHECK(moved.lab[i].center[2] == parked.lab[i].center[2]);
    if (moved.lab[i].center[0] != parked.lab[i].center[0]) {
      ++changed;
      CHECK(moved.lab[i].center[0] == Catch::Approx(parked.lab[i].center[0] - 1.2f));
    }
  }
  CHECK(changed == 1);
}
namespace {
struct ProjectedBox {
  float left{1}, right{0}, top{1}, bottom{0};
  void add(const view::OrbitCamera& cam, float aspect, view::Vec3 point) {
    const auto p = view::transformPoint(cam.viewProj(aspect), point);
    left = std::min(left, .5f + .5f * p.x);
    right = std::max(right, .5f + .5f * p.x);
    top = std::min(top, .5f + .5f * p.y);
    bottom = std::max(bottom, .5f + .5f * p.y);
  }
  void cube(const view::OrbitCamera& cam, float aspect, view::Vec3 lo, view::Vec3 hi) {
    for (int i = 0; i < 8; ++i)
      add(cam, aspect, {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z});
  }
  float area() const { return (right - left) * (bottom - top); }
};
void partBox(ProjectedBox& box, const LabScene& lab, const view::OrbitCamera& cam,
             float aspect, std::string_view name) {
  const auto& parts = lab.doc().parts;
  const auto p = std::find_if(parts.begin(), parts.end(),
                              [&](const auto& part) { return part.name == name; });
  REQUIRE(p != parts.end());
  std::ifstream file(p->file);
  const std::string text((std::istreambuf_iterator<char>(file)), {});
  auto mesh = io::parseObj(text, p->file.string());
  REQUIRE(mesh);
  view::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
  const float rad = std::numbers::pi_v<float> / 180;
  const auto q = quatFromYawPitchRoll(p->rot[0] * rad, p->rot[1] * rad, p->rot[2] * rad);
  for (const auto& v : mesh->positions) {
    const view::Vec3 raw{v[0] * p->size[0], v[1] * p->size[1], v[2] * p->size[2]};
    const view::Vec3 axis{q[0], q[1], q[2]};
    const auto t = view::cross(axis, raw) * 2;
    const auto r = raw + t * q[3] + view::cross(axis, t);
    const auto w = lab.toWorld({p->pos[0] + r.x, p->pos[1] + r.y, p->pos[2] + r.z});
    lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
    hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
  }
  box.cube(cam, aspect, lo, hi);
}
}  // namespace
TEST_CASE("menu projector is a fifth of the viewport and clears its panel",
          "[unit][render][lab]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  const auto cam = *lab->station("menu");
  auto lens = *doc->anchor("projector.lens");
  lens[2] += .32f - .20f + .26f;
  const auto centre = lab->toWorld(lens);
  const view::Vec3 r{5.2f, 5.2f, 5.2f};
  for (float aspect : {16.f / 9, 16.f / 10, 4.f / 3}) {
    ProjectedBox box;
    box.cube(cam, aspect, centre - r, centre + r);
    partBox(box, *lab, cam, aspect, "projector.body");
    CAPTURE(aspect, box.left, box.right, box.top, box.bottom, box.area());
    if (aspect == 16.f / 9) {
      CHECK(box.area() >= .17f);
      CHECK(box.area() <= .23f);
      CHECK(box.left >= .05f);
      CHECK(box.right <= .47f);
      CHECK(box.top >= .04f);
    } else
      CHECK(box.right <= .50f);
  }
}
TEST_CASE("library looks into the rack beside a large wall hologram",
          "[unit][render][lab]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  const auto cam = *lab->station("library");
  // M24.4c-3d-fix measures the view into the rack below, rather than the
  // optical-axis yaw: the subjects deliberately occupy the left half.
  CHECK(std::abs(cam.pitch) <= 12.f * std::numbers::pi_v<float> / 180);
  const auto origin = *doc->anchor("gantry.centre");
  const float eyeZ = cam.eye().z / kCellsPerMetre + origin[2];
  CHECK(eyeZ >= 1.3f);
  CHECK(eyeZ <= 1.6f);
  const auto front = lab->toWorld(*doc->anchor("magazine.front"));
  CHECK(view::dot(view::normalize(cam.eye() - front), view::Vec3{-1, 0, 0}) >=
        std::cos(50.f * std::numbers::pi_v<float> / 180));
  ProjectedBox rack;
  partBox(rack, *lab, cam, 16.f / 9, "magazine.frame");
  CAPTURE(rack.left, rack.right, rack.area());
  CHECK(rack.left >= .02f);
  CHECK(rack.right <= .56f);
  CHECK(rack.area() >= .07f);
  CHECK(rack.area() <= .25f);
}
#endif
