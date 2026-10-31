// SPDX-License-Identifier: GPL-3.0-or-later
// Renders real frames on a real device and asserts on the pixels.
//
// The reason this is possible at all is that the renderer is headless by construction:
// no window, no surface, no display server. A validation-layer message is a failing
// test, not console noise - which is the only way a Vulkan renderer stays correct.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <numbers>
#include <utility>
#include <vector>

#include "app/session.hpp"
#include "app/shell.hpp"
#include "io/fen.hpp"
#include "render/board_renderer.hpp"
#include "render/offscreen_target.hpp"
#include "render/piece_icon.hpp"
#include "render/vulkan_context.hpp"
#ifdef CB_HAVE_IMGUI
#include <SDL3/SDL.h>
#include <imgui_internal.h>

#include "render/overture_scene.hpp"  // derivedSurfaceAt, for the geometry view (M17)
#include "render/play_surface.hpp"
#include "render/ui.hpp"
#endif
#include "support/variants.hpp"
#include "view/theme.hpp"

using namespace cb;
using namespace cb::render;

namespace {

/// A context shared by the tests in this file: creating one per test would multiply the
/// (slow) device setup for no benefit, and validation state is checked per render.
struct Gpu {
  VulkanContext ctx;
  bool available{false};
  std::string reason;

  Gpu() {
    auto c = VulkanContext::create();
    if (c.has_value()) {
      ctx = std::move(*c);
      available = true;
    } else {
      reason = c.error().format();
    }
  }
};

Gpu& gpu() {
  static Gpu g;
  return g;
}

/// Where debug captures land, so a human can look at what a failing test saw.
std::filesystem::path capturePath(const std::string& name) {
  const std::filesystem::path dir =
      std::filesystem::path(CB_BINARY_DIR) / "render-captures";
  std::filesystem::create_directories(dir);
  return dir / (name + ".ppm");
}

struct Frame {
  Image image;
  std::size_t validationErrors{0};
  std::vector<std::string> messages;
};

Frame renderVariant(const std::string& name, std::uint32_t w = 512, std::uint32_t h = 384,
                    bool highlight = false, double visualTime = 0) {
  const VariantSpec& v = *new VariantSpec(test::loadVariant(name));  // outlives the frame
  Position p = Position::startPosition(v);
  view::PositionView snap = view::PositionView::capture(p);
  if (highlight) {
    snap.setSelected(0);
    snap.setHighlighted({1, 2, 3});
  }

  auto target = OffscreenTarget::create(gpu().ctx, w, h);
  REQUIRE(target.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(renderer.has_value());

  renderer->setVisualTime(visualTime);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));

  (void)gpu().ctx.takeValidationMessages();  // start from a clean slate
  auto img = renderer->renderToImage(*target, snap, cfg, cam);
  REQUIRE(img.has_value());

  Frame f;
  f.image = std::move(*img);
  f.validationErrors = gpu().ctx.validationErrorCount();
  f.messages = gpu().ctx.takeValidationMessages();
  return f;
}

}  // namespace

TEST_CASE("a Vulkan device is available and validation is on", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  REQUIRE_FALSE(gpu().ctx.deviceName().empty());
  WARN("rendering on: " << gpu().ctx.deviceName()
                        << (gpu().ctx.validationEnabled() ? " (validation on)"
                                                          : " (validation UNAVAILABLE)"));
  REQUIRE(gpu().ctx.device() != VK_NULL_HANDLE);
}

TEST_CASE("instances are built from a snapshot without a GPU", "[render]") {
  // Pure data in, pure data out - so the part of the renderer most likely to be wrong is
  // testable with no device at all.
  const VariantSpec v = test::loadVariant("standard");
  const Position p = Position::startPosition(v);
  view::PositionView snap = view::PositionView::capture(p);

  BoardRenderer renderer;  // no device needed for buildInstances
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const InstanceSet set = renderer.buildInstances(snap, cfg);

  // 64 cells, 32 pieces, and the plinth the board stands on - which is drawn with the
  // same slab mesh as a cell, so it lands in the cell batch.
  REQUIRE(set.size() == 64 + 32 + 1);
  REQUIRE(set.batches[static_cast<std::size_t>(Archetype::Cell)].count == 64 + 1);
  std::uint32_t counted = 0;
  for (const auto& b : set.batches) counted += b.count;
  REQUIRE(counted == set.size());

  SECTION("highlighting recolours exactly the cells the engine named") {
    snap.setSelected(v.dims.toCell(Coord::of({4, 1})));
    snap.setHighlighted(
        {v.dims.toCell(Coord::of({4, 2})), v.dims.toCell(Coord::of({4, 3}))});
    const InstanceSet lit = renderer.buildInstances(snap, cfg);
    REQUIRE(lit.size() == set.size());

    // The selected cell takes the accent outright; the destinations are *tinted*
    // toward the legal-move colour so the checkerboard still reads underneath.
    int changed = 0;
    for (std::size_t i = 0; i < lit.instances.size(); ++i) {
      const bool differs =
          std::abs(lit.instances[i].color[0] - set.instances[i].color[0]) > 0.02f ||
          std::abs(lit.instances[i].color[1] - set.instances[i].color[1]) > 0.02f;
      if (differs) ++changed;
    }
    // Three cells, and the selected piece itself, which takes the stronger accent tint
    // (M20).
    REQUIRE(changed == 4);

    const auto& ember = renderer.theme().ember;
    int selected = 0;
    for (const Instance& inst : lit.instances) {
      if (std::abs(inst.color[0] - ember.r) < 0.01f &&
          std::abs(inst.color[1] - ember.g) < 0.01f) {
        ++selected;
      }
    }
    REQUIRE(selected == 1);
  }
}

TEST_CASE("wood is confined to board and pieces and never moves with a piece",
          "[render]") {
  const VariantSpec v = test::loadVariant("standard");
  const Position p = Position::startPosition(v);
  const auto snap = view::PositionView::capture(p);
  const auto cfg = view::ViewConfig::forBoard(v.dims);
  BoardRenderer renderer;
  auto wood = view::Theme::console();
  renderer.setTheme(wood);
  const InstanceSet set = renderer.buildInstances(snap, cfg);
  wood.materials = view::Materials{};
  renderer.setTheme(wood);
  const InstanceSet matte = renderer.buildInstances(snap, cfg);
  REQUIRE(set.size() == matte.size());
  REQUIRE(set.size() == 97);
  CHECK(std::memcmp(matte.instances.data(),
                    renderer.buildInstances(snap, cfg).instances.data(),
                    set.size() * sizeof(Instance)) == 0);
  int cells = 0, pieces = 0, plinths = 0;
  for (std::size_t i = 0; i < set.size(); ++i) {
    Instance a = set.instances[i], b = matte.instances[i];
    CHECK(b.material == 0.0f);
    a.material = b.material;
    CHECK(std::memcmp(&a, &b, sizeof(Instance)) == 0);
    const int id = static_cast<int>(set.instances[i].material);
    if (id == 2) {
      if (set.instances[i].material == 2.25f)
        ++plinths;
      else
        ++cells;
    } else if (id == 6)
      ++pieces;
  }
  CHECK(cells == 64);
  CHECK(plinths == 1);
  CHECK(pieces == 32);
  std::vector<float> variations;
  for (const Instance& i : set.instances) {
    if (static_cast<int>(i.material) == 2 && i.material != 2.25f)
      variations.push_back(i.material);
  }
  REQUIRE(variations.size() == 64);
  CHECK(std::adjacent_find(variations.begin(), variations.end(),
                           std::not_equal_to<float>{}) != variations.end());

  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session.has_value());
  renderer.setTheme(view::Theme::console());
  const auto before =
      renderer.buildInstances((*session)->snapshot(), (*session)->viewConfig());
  const auto pawnMaterial = [&] {
    for (const Instance& i : before.instances) {
      if (static_cast<int>(i.material) == 6 &&
          std::abs(i.color[0] - renderer.theme().whitePiece.r) < 1e-4f)
        return i.material;
    }
    return -1.0f;
  }();
  REQUIRE(pawnMaterial >= 6.0f);
  for (const auto coords : {Coord::of({4, 1}), Coord::of({4, 3})}) {
    app::Action click;
    click.kind = app::ActionKind::ClickCell;
    click.cell = (*session)->variant().dims.toCell(coords);
    REQUIRE((*session)->apply(click).has_value());
  }
  for (float t : {0.2f, 0.5f, 0.8f}) {
    (*session)->setMoveProgress(t);
    const auto at = (*session)->animation().sample();
    const auto moving =
        renderer.buildInstances((*session)->snapshot(), (*session)->viewConfig(),
                                &(*session)->seams(), &(*session)->animation());
    const auto found = std::find_if(moving.instances.begin(), moving.instances.end(),
                                    [&](const Instance& i) {
                                      return static_cast<int>(i.material) == 6 &&
                                             std::abs(i.center[0] - at.x) < 1e-4f &&
                                             std::abs(i.center[1] - at.y) < 1e-4f;
                                    });
    REQUIRE(found != moving.instances.end());
    CHECK(found->material == pawnMaterial);
  }
}

TEST_CASE("manifold dispatches glass, holograms and portal glow", "[render]") {
  const VariantSpec v = test::loadVariant("torus");
  const Position p = Position::startPosition(v);
  const auto snap = view::PositionView::capture(p);
  BoardRenderer renderer;
  renderer.setTheme(view::Theme::manifold());
  const auto set = renderer.buildInstances(snap, view::ViewConfig::forBoard(v.dims));
  int glass = 0, crystal = 0, led = 0;
  for (const Instance& i : set.instances) {
    if (i.material == 3.0f) ++glass;
    if (i.material == 4.0f) ++crystal;
    if (i.material == 7.0f) ++led;
  }
  CHECK(glass == 1);
  CHECK(led == 64);
  CHECK(crystal == 32);
  auto glued = app::Session::create(test::loadVariant("torus"));
  REQUIRE(glued.has_value());
  const auto portals = renderer.buildInstances(
      (*glued)->snapshot(), (*glued)->viewConfig(), &(*glued)->seams());
  CHECK(std::any_of(portals.instances.begin(), portals.instances.end(),
                    [](const Instance& i) { return i.material == 5.0f; }));
  auto mirrored = app::Session::create(test::loadVariant("mirrorbox"));
  REQUIRE(mirrored.has_value());
  auto matte = view::Theme::console();
  matte.materials = view::Materials{};
  renderer.setTheme(matte);
  const auto rails = renderer.buildInstances(
      (*mirrored)->snapshot(), (*mirrored)->viewConfig(), &(*mirrored)->seams());
  CHECK(std::any_of(rails.instances.begin(), rails.instances.end(),
                    [](const Instance& i) { return i.material == 1.0f; }));
  CHECK(std::all_of(
      rails.instances.begin(), rails.instances.end(),
      [](const Instance& i) { return i.material == 0.0f || i.material == 1.0f; }));
}

#ifdef CB_HAVE_IMGUI
TEST_CASE("manifold LED shape is opaque unless explicitly ghosted", "[render]") {
  const VariantSpec v = test::loadVariant("torus");
  const Position p = Position::startPosition(v);
  BoardRenderer renderer;
  renderer.setTheme(view::Theme::manifold());
  BoardOptions options;
  options.surface = true;
  renderer.setOptions(options);
  const auto set = renderer.buildInstances(view::PositionView::capture(p),
                                           view::ViewConfig::forBoard(v.dims));
  REQUIRE_FALSE(set.surfaceVertices.empty());
  CHECK(set.instances[set.surfaceInstance].material == 7.0f);
  CHECK(set.surfaceVertices.front().color[3] == 1.0f);
  options.surfaceGhost = 0.4f;
  renderer.setOptions(options);
  const auto ghost = renderer.buildInstances(view::PositionView::capture(p),
                                             view::ViewConfig::forBoard(v.dims));
  CHECK(ghost.surfaceVertices.front().color[3] == 0.4f);
}
#endif

TEST_CASE("hologram scan descends along the piece in upright and rotated poses",
          "[render][gpu][hologram]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  auto target = OffscreenTarget::create(gpu().ctx, 512, 512);
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(target.has_value());
  REQUIRE(renderer.has_value());
  renderer->setTheme(view::Theme::manifold());
  InstanceSet set;
  Instance body;
  body.scale[0] = body.scale[1] = 1;
  body.scale[2] = 2;
  body.color[0] = 0.32f;
  body.color[1] = 0.48f;
  body.color[2] = 0.66f;
  body.color[3] = 1;
  body.material = 4;
  set.instances.push_back(body);
  set.batches[static_cast<std::size_t>(Archetype::Tower)] = {0, 1};
  for (bool rotated : {false, true}) {
    const std::string pose = rotated ? "sideways" : "upright";
    INFO(pose);
    set.instances[0].quat[1] = rotated ? std::sqrt(0.5f) : 0;
    set.instances[0].quat[3] = rotated ? std::sqrt(0.5f) : 1;
    view::OrbitCamera cam;
    cam.target = rotated ? view::Vec3{0.86f, 0, 0} : view::Vec3{0, 0, 0.86f};
    cam.distance = 3.2f;
    cam.yaw = cam.pitch = 0;
    cam.orthographic = true;
    const auto foot = cam.project({0, 0, 0}, 1, 512, 512);
    const auto crown = cam.project(
        rotated ? view::Vec3{1.72f, 0, 0} : view::Vec3{0, 0, 1.72f}, 1, 512, 512);
    REQUIRE(foot.visible);
    REQUIRE(crown.visible);
    const float dx = crown.x - foot.x, dy = crown.y - foot.y;
    const float span = dx * dx + dy * dy;
    REQUIRE(span > 200 * 200);
    if (rotated) {
      CHECK(std::abs(dx) > 200);
      CHECK(std::abs(dy) < 2);
    }
    const auto shot = [&](double seconds) {
      renderer->setVisualTime(seconds);
      REQUIRE(renderer->render(*target, set, cam).has_value());
      auto pixels = target->readPixels();
      REQUIRE(pixels.has_value());
      REQUIRE(gpu().ctx.takeValidationMessages().empty());
      return Image{512, 512, std::move(*pixels)};
    };
    (void)gpu().ctx.takeValidationMessages();
    const auto baseline = shot(0);
    const auto upper = shot(0.9);
    const auto lower = shot(2.8);
    CHECK(shot(0.9).rgba == upper.rgba);
    CHECK(shot(4).rgba == baseline.rgba);
    CHECK(shot(120.9).rgba == upper.rgba);
    CHECK(shot(-1).rgba == baseline.rgba);
    CHECK(shot(std::numeric_limits<double>::quiet_NaN()).rgba == baseline.rgba);
    CHECK(shot(std::numeric_limits<double>::infinity()).rgba == baseline.rgba);
    REQUIRE(writePpm(baseline, capturePath("hologram-" + pose + "-rest")).has_value());
    REQUIRE(writePpm(upper, capturePath("hologram-" + pose + "-upper")).has_value());
    REQUIRE(writePpm(lower, capturePath("hologram-" + pose + "-lower")).has_value());
    struct Energy {
      double total{0}, height{0}, high{0}, low{0};
      int pixels{0};
    };
    const auto measure = [&](const Image& img) {
      Energy e;
      const auto corner = baseline.at(0, 0);
      const int bg = corner.r + corner.g + corner.b;
      for (std::uint32_t y = 1; y < 511; ++y) {
        for (std::uint32_t x = 1; x < 511; ++x) {
          const float px = static_cast<float>(x) + 0.5f - foot.x;
          const float py = static_cast<float>(y) + 0.5f - foot.y;
          const float u = (px * dx + py * dy) / span;
          if (u <= 0.08f || u >= 0.95f ||
              std::abs(px * dy - py * dx) / std::sqrt(span) >= 18)
            continue;
          bool interior = true;
          for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
              const auto p =
                  baseline.at(static_cast<std::uint32_t>(static_cast<int>(x) + ox),
                              static_cast<std::uint32_t>(static_cast<int>(y) + oy));
              interior = interior && p.r + p.g + p.b > bg + 30;
            }
          }
          if (!interior) continue;
          const auto a = baseline.at(x, y), b = img.at(x, y);
          const int d = std::max(int(b.r) - int(a.r) - 1, 0) +
                        std::max(int(b.g) - int(a.g) - 1, 0) +
                        std::max(int(b.b) - int(a.b) - 1, 0);
          e.total += d;
          e.height += static_cast<double>(u) * d;
          if (u > 0.55f) e.high += d;
          if (u < 0.50f) e.low += d;
          if (d >= 6) ++e.pixels;
        }
      }
      return e;
    };
    const auto hi = measure(upper), lo = measure(lower), wrap = measure(shot(3.95));
    CAPTURE(hi.total, hi.pixels, lo.total, lo.pixels);
    REQUIRE(hi.total > 500);
    REQUIRE(lo.total > 500);
    CHECK(hi.pixels >= 30);
    CHECK(lo.pixels >= 30);
    CHECK(hi.height / hi.total > 0.60);
    CHECK(lo.height / lo.total < 0.45);
    CHECK(hi.height / hi.total - lo.height / lo.total > 0.30);
    CHECK(hi.high / hi.total > 0.75);
    CHECK(lo.low / lo.total > 0.75);
    CHECK(wrap.high < 1);  // reset is outside the body, never a whole-body flash
    for (float material : {0.0f, 6.0f}) {
      set.instances[0].material = material;
      CHECK(shot(0.75).rgba == shot(2.75).rgba);
    }
    set.instances[0].material = 4;
  }
}

TEST_CASE("hologram scans leave portal colours visually dominant",
          "[render][gpu][hologram]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  for (double seconds : {0.0, 0.75, 1.5, 2.75, 3.95}) {
    CAPTURE(seconds);
    const auto frame = renderVariant("standard", 256, 192, false, seconds);
    CHECK(frame.validationErrors == 0);
    CHECK(frame.image.saturatedPixels() < 256u * 192u / 100u);
  }
}

TEST_CASE("visual time changes hologram pixels without changing instances",
          "[render][gpu][hologram]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const auto v = test::loadVariant("standard");
  const auto snap = view::PositionView::capture(Position::startPosition(v));
  const auto cfg = view::ViewConfig::forBoard(v.dims);
  const auto cam = view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));
  auto target = OffscreenTarget::create(gpu().ctx, 640, 400);
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(target.has_value());
  REQUIRE(renderer.has_value());
  for (auto theme : {view::Theme::manifold(), view::Theme::console()}) {
    renderer->setTheme(theme);
    const auto shot = [&](double t) {
      renderer->setVisualTime(t);
      auto img = renderer->renderToImage(*target, snap, cfg, cam);
      REQUIRE(img.has_value());
      REQUIRE(gpu().ctx.takeValidationMessages().empty());
      return *img;
    };
    const auto before = renderer->buildInstances(snap, cfg);
    const auto first = shot(0.75), second = shot(2.75), again = shot(0.75);
    CHECK(first.rgba == again.rgba);
    if (theme.materials.pieces == view::Material::Wood)
      CHECK(first.rgba == second.rgba);
    else
      CHECK(first.rgba != second.rgba);
    const auto after = renderer->buildInstances(snap, cfg);
    REQUIRE(before.instances.size() == after.instances.size());
    CHECK(std::memcmp(before.instances.data(), after.instances.data(),
                      before.instances.size() * sizeof(Instance)) == 0);
  }
}

#ifdef CB_HAVE_IMGUI
TEST_CASE("LED patch coordinates stay attached through slide and inversion",
          "[render][led]") {
  constexpr int side = PlaySurface::kSubdiv + 1;
  constexpr std::size_t faceVertices = side * side;
  constexpr std::size_t perPatch = 2 * faceVertices + 16 * PlaySurface::kSubdiv;
  for (const char* name : {"torus", "klein"}) {
    INFO(name);
    const auto v = test::loadVariant(name);
    const auto snap = view::PositionView::capture(Position::startPosition(v));
    const auto cfg = view::ViewConfig::forBoard(v.dims);
    BoardRenderer renderer;
    BoardOptions opts;
    opts.surface = true;
    renderer.setOptions(opts);
    const auto first = renderer.buildInstances(snap, cfg);
    REQUIRE(first.surfaceVertices.size() == perPatch * v.dims.cellCount());
    for (std::size_t base = 0; base < first.surfaceVertices.size(); base += perPatch) {
      for (int i = 0; i < side; ++i) {
        for (int j = 0; j < side; ++j) {
          const auto& top =
              first.surfaceVertices[base + static_cast<std::size_t>(i * side + j)];
          const auto& bottom =
              first.surfaceVertices[base + faceVertices +
                                    static_cast<std::size_t>(i * side + j)];
          CHECK(top.panelCoord[0] == static_cast<float>(j) / PlaySurface::kSubdiv);
          CHECK(top.panelCoord[1] == static_cast<float>(i) / PlaySurface::kSubdiv);
          CHECK(top.panelCoord[2] == 1);
          CHECK(std::equal(std::begin(top.panelCoord), std::end(top.panelCoord),
                           bottom.panelCoord));
        }
      }
      for (std::size_t k = 2 * faceVertices; k < perPatch; ++k) {
        const auto& vert = first.surfaceVertices[base + k];
        CHECK(vert.panelCoord[2] == -1);
        CHECK(vert.panelCoord[0] >= 0);
        CHECK(vert.panelCoord[0] <= 1);
        CHECK(vert.panelCoord[1] >= 0);
        CHECK(vert.panelCoord[1] <= 1);
      }
    }
    for (std::size_t k = 0; k < first.surfaceIndices.size(); k += 3) {
      const float tag = first.surfaceVertices[first.surfaceIndices[k]].panelCoord[2];
      CHECK(first.surfaceVertices[first.surfaceIndices[k + 1]].panelCoord[2] == tag);
      CHECK(first.surfaceVertices[first.surfaceIndices[k + 2]].panelCoord[2] == tag);
    }
    opts.surfaceSlideU = 0.6f;
    opts.surfaceEvert = 1;
    renderer.setOptions(opts);
    const auto posed = renderer.buildInstances(snap, cfg);
    REQUIRE(posed.surfaceVertices.size() == first.surfaceVertices.size());
    CHECK(posed.surfaceIndices == first.surfaceIndices);
    bool moved = false;
    for (std::size_t k = 0; k < first.surfaceVertices.size(); ++k) {
      const auto& a = first.surfaceVertices[k];
      const auto& b = posed.surfaceVertices[k];
      CHECK(std::equal(std::begin(a.panelCoord), std::end(a.panelCoord), b.panelCoord));
      moved = moved || !std::equal(std::begin(a.pos), std::end(a.pos), b.pos);
    }
    CHECK(moved);
  }
  const auto lib = MeshLibrary::build();
  for (const auto& v : lib.vertices)
    CHECK(std::all_of(std::begin(v.panelCoord), std::end(v.panelCoord),
                      [](float f) { return f == 0; }));
}
#endif

TEST_CASE("LED cells resolve a pixel matrix nearby and filter to a steady mean afar",
          "[render][gpu][led]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  auto target = OffscreenTarget::create(gpu().ctx, 512, 512);
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(target.has_value());
  REQUIRE(renderer.has_value());
  InstanceSet set;
  Instance cell;
  cell.scale[0] = cell.scale[1] = cell.scale[2] = 1;
  cell.color[0] = cell.color[1] = cell.color[2] = 0.4f;
  cell.color[3] = 1;
  cell.material = 7;
  set.instances.push_back(cell);
  set.batches[static_cast<std::size_t>(Archetype::Cell)] = {0, 1};
  view::OrbitCamera cam;
  cam.yaw = 0;
  cam.pitch = std::numbers::pi_v<float> * 0.5f;
  cam.orthographic = true;
  const auto shot = [&](float distance) {
    cam.distance = distance;
    (void)gpu().ctx.takeValidationMessages();
    REQUIRE(renderer->render(*target, set, cam).has_value());
    auto pixels = target->readPixels();
    REQUIRE(pixels.has_value());
    REQUIRE(gpu().ctx.takeValidationMessages().empty());
    return Image{512, 512, std::move(*pixels)};
  };
  const auto near = shot(1.4f);
  REQUIRE(writePpm(near, capturePath("led-near")).has_value());
  const auto transitions = [](const Image& img, bool horizontal, bool patterned = true) {
    int count = 0, lo = 255, hi = 0, previous = -1;
    for (std::uint32_t t = 136; t < 376; ++t) {
      const int value = horizontal ? img.at(t, 261).r : img.at(261, t).r;
      lo = std::min(lo, value);
      hi = std::max(hi, value);
      if (previous >= 0 && std::abs(value - previous) >= 4) ++count;
      previous = value;
    }
    if (patterned) CHECK(hi - lo >= 8);
    return count;
  };
  CHECK(transitions(near, true) > 15);
  CHECK(transitions(near, false) > 15);
  const auto far = shot(30);
  REQUIRE(writePpm(far, capturePath("led-far")).has_value());
  int lo = 255, hi = 0;
  for (std::uint32_t y = 254; y <= 258; ++y) {
    for (std::uint32_t x = 254; x <= 258; ++x) {
      const int value = far.at(x, y).r;
      lo = std::min(lo, value);
      hi = std::max(hi, value);
    }
  }
  CHECK(hi - lo <= 2);
  CHECK(lo > 75);
  CHECK(hi < 110);
  set.instances[0].material = 3;  // the plinth remains glass, without an emitter matrix
  const auto glass = shot(1.4f);
  CHECK(transitions(glass, true, false) == 0);
  // A sampled face standing vertically cannot use world XY as its panel UV.
  set.instances[0].material = 7;
  set.batches = {};
  for (const auto& uv :
       std::array<std::array<float, 2>, 4>{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}}) {
    MeshVertex v{};
    v.pos[0] = uv[0] - 0.5f;
    v.pos[2] = uv[1] - 0.5f;
    v.normal[1] = -1;
    v.panelCoord[0] = uv[0];
    v.panelCoord[1] = uv[1];
    v.panelCoord[2] = 1;
    set.surfaceVertices.push_back(v);
  }
  set.surfaceIndices = {0, 1, 2, 0, 2, 3};
  cam.pitch = 0;
  const auto vertical = shot(1.4f);
  REQUIRE(writePpm(vertical, capturePath("led-sampled-vertical")).has_value());
  CHECK(transitions(vertical, true) > 15);
  CHECK(transitions(vertical, false) > 15);
  for (auto& v : set.surfaceVertices) v.panelCoord[2] = -1;
  const auto sidewall = shot(1.4f);
  CHECK(transitions(sidewall, true, false) == 0);
  CHECK(transitions(sidewall, false, false) == 0);
}

TEST_CASE("wood and glass change the rendered board without changing its geometry",
          "[render]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const VariantSpec v = test::loadVariant("standard");
  const Position p = Position::startPosition(v);
  const auto snap = view::PositionView::capture(p);
  const auto cfg = view::ViewConfig::forBoard(v.dims);
  const auto cam = view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));
  auto target = OffscreenTarget::create(gpu().ctx, 640, 400);
  REQUIRE(target.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(renderer.has_value());
  const auto shot = [&](const view::Theme& theme) {
    renderer->setTheme(theme);
    auto img = renderer->renderToImage(*target, snap, cfg, cam);
    REQUIRE(img.has_value());
    REQUIRE(gpu().ctx.takeValidationMessages().empty());
    return *img;
  };
  auto matte = view::Theme::console();
  matte.materials = view::Materials{};
  const auto plain = shot(matte);
  const auto wood = shot(view::Theme::console());
  const auto glass = shot(view::Theme::manifold());
  auto unshadedManifold = view::Theme::manifold();
  unshadedManifold.materials = view::Materials{};
  const auto plainManifold = shot(unshadedManifold);
  REQUIRE(plain.rgba.size() == wood.rgba.size());
  std::size_t changed = 0;
  for (std::size_t i = 0; i < plain.rgba.size(); ++i)
    if (plain.rgba[i] != wood.rgba[i]) ++changed;
  CHECK(changed > plain.rgba.size() / 50);
  const bool otherTheme = glass.rgba != wood.rgba;
  const bool glassShaded = glass.rgba != plainManifold.rgba;
  CHECK(otherTheme);
  CHECK(glassShaded);
}

TEST_CASE("a capture leaves a flourish and a quiet move leaves none", "[render]") {
  // M18.1. The position is already post-move when the renderer sees it, so the captured
  // piece is not on the board - the flourish is rebuilt from the animation's own copy of
  // what was taken, shrunk and faded as a pure function of `t`, plus a blood flash at the
  // square it stood on. A move that takes nothing has none of it.
  const auto click = [](app::Session& s, std::initializer_list<int> at) {
    app::Action a;
    a.kind = app::ActionKind::ClickCell;
    a.cell = s.variant().dims.toCell(Coord::of(at));
    REQUIRE(s.apply(a).has_value());
  };

  BoardRenderer renderer;
  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session.has_value());
  app::Session& s = **session;
  REQUIRE(s.loadFen("8/8/8/3p4/4P3/8/8/8 w - - 0 1").has_value());
  click(s, {4, 3});  // e4
  click(s, {3, 4});  // x d5
  REQUIRE(s.animation().active());
  REQUIRE(s.animation().captures());

  const view::ViewConfig cfg = s.viewConfig();
  const auto buildAt = [&](float t) {
    s.setMoveProgress(t);
    return renderer.buildInstances(s.snapshot(), cfg, &s.seams(), &s.animation());
  };

  // Before the fade begins the captured piece is whole: one instance, fully opaque.
  const InstanceSet whole = buildAt(0.0f);
  REQUIRE(whole.flourish.size() == 1);
  CHECK(std::abs(whole.flourish[0].color[3] - 1.0f) < 1e-5f);

  // Mid-fade the piece is still there but translucent, and the flash is rising.
  const InstanceSet mid = buildAt(0.6f);
  CHECK_FALSE(mid.flourish.empty());
  bool sawBlood = false;
  for (const Instance& f : mid.flourish) {
    if (std::abs(f.color[0] - renderer.theme().blood.r) < 1e-4f &&
        std::abs(f.color[1] - renderer.theme().blood.g) < 1e-4f &&
        std::abs(f.color[2] - renderer.theme().blood.b) < 1e-4f) {
      sawBlood = true;
      CHECK(f.color[3] > 0.0f);
    }
  }
  CHECK(sawBlood);

  // Determinism: the same `t` twice is the same frame, byte for byte.
  const InstanceSet again = buildAt(0.6f);
  REQUIRE(again.flourish.size() == mid.flourish.size());
  CHECK(std::memcmp(again.flourish.data(), mid.flourish.data(),
                    mid.flourish.size() * sizeof(Instance)) == 0);

  // By the time the capturing piece has arrived, the flourish is spent.
  CHECK(buildAt(1.0f).flourish.empty());

  // A move that takes nothing triggers nothing.
  auto quiet = app::Session::create(test::loadVariant("standard"));
  REQUIRE(quiet.has_value());
  click(**quiet, {4, 1});  // e2
  click(**quiet, {4, 3});  // e4
  REQUIRE((*quiet)->animation().active());
  CHECK_FALSE((*quiet)->animation().captures());
  const InstanceSet none =
      renderer.buildInstances((*quiet)->snapshot(), (*quiet)->viewConfig(),
                              &(*quiet)->seams(), &(*quiet)->animation());
  CHECK(none.flourish.empty());
}

TEST_CASE("an occupied cell gets one contact shadow and an empty cell gets none",
          "[render]") {
  // M18.2. One soft decal per occupied cell, at the cell's own surface point; a cell with
  // nothing on it has nothing to ground. No device: this is the instance builder.
  BoardRenderer renderer;

  auto full = app::Session::create(test::loadVariant("standard"));
  REQUIRE(full.has_value());
  const InstanceSet start =
      renderer.buildInstances((*full)->snapshot(), (*full)->viewConfig());
  // The standard opening: 32 pieces, so 32 shadows.
  REQUIRE(start.shadows.size() == 32);
  // Deterministic: the same position twice is the same shadow data, byte for byte.
  const InstanceSet again =
      renderer.buildInstances((*full)->snapshot(), (*full)->viewConfig());
  REQUIRE(again.shadows.size() == start.shadows.size());
  CHECK(std::memcmp(again.shadows.data(), start.shadows.data(),
                    start.shadows.size() * sizeof(Instance)) == 0);
  // The decal is dark and translucent - alpha is what makes it a shadow rather than
  // paint.
  for (const Instance& sh : start.shadows) {
    CHECK(sh.color[3] > 0.0f);
    CHECK(sh.color[3] < 1.0f);
  }

  // An almost-empty board shadows only its pieces.
  auto sparse = app::Session::create(test::loadVariant("standard"));
  REQUIRE(sparse.has_value());
  REQUIRE((*sparse)->loadFen("4k3/8/8/8/8/8/8/4K3 w - - 0 1").has_value());
  const InstanceSet two =
      renderer.buildInstances((*sparse)->snapshot(), (*sparse)->viewConfig());
  REQUIRE(two.shadows.size() == 2);
}

TEST_CASE("the contact shadow mesh is a soft disc, not a hard slab", "[render]") {
  // The point of a dedicated mesh: the alpha is baked into its vertices as a radial ramp,
  // so the existing shader - which multiplies the vertex colour into the instance colour
  // - draws a soft edge with no branch and no second pipeline (M18.2).
  const MeshLibrary lib = MeshLibrary::build();
  const MeshRange& range = lib.ranges[static_cast<std::size_t>(Archetype::Disc)];
  REQUIRE(range.indexCount > 0);
  REQUIRE(range.indexCount % 3 == 0);

  float innerAlpha = -1.0f;
  float rimAlpha = 2.0f;
  float maxRadius = 0.0f;
  for (std::uint32_t k = 0; k < range.indexCount; ++k) {
    const std::uint32_t idx = static_cast<std::uint32_t>(range.vertexOffset) +
                              lib.indices[range.firstIndex + k];
    const MeshVertex& v = lib.vertices[idx];
    const float radius = std::sqrt(v.pos[0] * v.pos[0] + v.pos[1] * v.pos[1]);
    maxRadius = std::max(maxRadius, radius);
    if (radius < 1e-4f) innerAlpha = std::max(innerAlpha, v.color[3]);
    if (radius > maxRadius - 1e-3f) rimAlpha = std::min(rimAlpha, v.color[3]);
  }
  CHECK(innerAlpha > 0.9f);  // solid at the centre
  CHECK(rimAlpha < 0.1f);    // faded to nothing at the rim
}

#ifdef CB_HAVE_IMGUI
TEST_CASE("a capture on the shape leaves a flourish on its own square", "[render]") {
  // The geometry view builds its own pieces from `PlaySurface` seats rather than from the
  // flat placements, so it needs its own version of the M18.1 flourish. No device: this
  // is the instance builder, not a render.
  BoardRenderer renderer;
  auto session = app::Session::create(test::loadVariant("torus"));
  REQUIRE(session.has_value());
  app::Session& s = **session;
  REQUIRE(s.loadFen("8/8/8/3p4/4P3/8/8/8 w - - 0 1").has_value());
  const auto click = [&](std::initializer_list<int> at) {
    app::Action a;
    a.kind = app::ActionKind::ClickCell;
    a.cell = s.variant().dims.toCell(Coord::of(at));
    REQUIRE(s.apply(a).has_value());
  };
  click({4, 3});  // e4
  click({3, 4});  // x d5
  REQUIRE(s.animation().active());
  REQUIRE(s.animation().captures());

  BoardOptions options = renderer.options();
  options.surface = true;
  renderer.setOptions(options);
  s.setMoveProgress(0.45f);
  const InstanceSet set =
      renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams(), &s.animation());
  CHECK_FALSE(set.flourish.empty());
  bool translucent = false;
  for (const Instance& f : set.flourish) {
    if (f.color[3] > 0.0f && f.color[3] < 1.0f) translucent = true;
  }
  CHECK(translucent);
}

TEST_CASE("a shaped board shadows every occupied seat", "[render]") {
  // The geometry view builds its own pieces from `PlaySurface` seats, so it needs its own
  // shadows, placed on the surface and turned to its normal. No device: instance data.
  BoardRenderer renderer;
  const VariantSpec v = test::loadVariant("torus");
  const Position p = Position::startPosition(v);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  BoardOptions options = renderer.options();
  options.surface = true;
  renderer.setOptions(options);
  const InstanceSet set = renderer.buildInstances(view::PositionView::capture(p), cfg);

  REQUIRE(set.shadows.size() == 32);
  // Each shadow lies on the surface under its seat, facing the seat's own normal.
  const PlaySurface surf = PlaySurface::build(v);
  for (const Instance& sh : set.shadows) {
    CHECK(sh.color[3] > 0.0f);
    CHECK(sh.color[3] < 1.0f);
    // A unit quaternion: the decal was turned onto the surface, not left flat.
    const float qlen = std::sqrt(sh.quat[0] * sh.quat[0] + sh.quat[1] * sh.quat[1] +
                                 sh.quat[2] * sh.quat[2] + sh.quat[3] * sh.quat[3]);
    CHECK(std::abs(qlen - 1.0f) < 1e-3f);
  }
  // ...and there is one near each seat that holds a piece.
  REQUIRE_FALSE(surf.empty());
}

TEST_CASE("flat 2D wins over the shape when both are set", "[render]") {
  // M18.6: a settings-file edit (or a script) can set `flatView` and `geometryView` at
  // once, even though the interface greys each control out in the other's state. The
  // resolution is flat - a shape has no 2D rendering - so the renderer refuses the
  // surface under `options.flat` rather than drawing a donut the 2D control cannot
  // describe.
  BoardRenderer renderer;
  const VariantSpec v = test::loadVariant("torus");
  const Position p = Position::startPosition(v);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  BoardOptions options = renderer.options();
  options.surface = true;
  options.flat = true;
  renderer.setOptions(options);
  const InstanceSet set = renderer.buildInstances(view::PositionView::capture(p), cfg);
  CHECK(set.surfaceVertices.empty());
  CHECK(set.surfaceIndices.empty());
  // It is the ordinary board underneath, not nothing at all.
  CHECK_FALSE(set.instances.empty());
}
#endif

TEST_CASE("a glued board marks each seam with the colour of where it leads", "[render]") {
  // One cyan rail per edge is not enough information: a cylinder and a Moebius band
  // draw the identical picture, and the difference between them is the whole game. So
  // every glued face gets its own rail, coloured by the portal it belongs to, and the
  // two ends of one portal match.
  BoardRenderer renderer;
  const view::Theme theme = renderer.theme();

  const VariantSpec box = test::loadVariant("standard");
  const Position bp = Position::startPosition(box);
  const view::ViewConfig boxCfg = view::ViewConfig::forBoard(box.dims);
  const view::SeamMap boxSeams = view::SeamMap::build(box, boxCfg, theme);
  const InstanceSet plain =
      renderer.buildInstances(view::PositionView::capture(bp), boxCfg, &boxSeams);
  CHECK(boxSeams.empty());

  const VariantSpec torus = test::loadVariant("torus");
  const Position tp = Position::startPosition(torus);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(torus.dims);
  const view::SeamMap seams = view::SeamMap::build(torus, cfg, theme);
  const InstanceSet glued =
      renderer.buildInstances(view::PositionView::capture(tp), cfg, &seams);

  // Both axes are glued, so every cell on the border carries a rail, and the four
  // corners carry two.
  CHECK(seams.faces().size() == 8 * 4);
  CHECK(glued.size() == plain.size() + seams.faces().size());

  // Switching the marks off leaves the board alone.
  BoardOptions quiet = renderer.options();
  quiet.showSeams = false;
  renderer.setOptions(quiet);
  const InstanceSet bare =
      renderer.buildInstances(view::PositionView::capture(tp), cfg, &seams);
  CHECK(bare.size() == plain.size());
}

TEST_CASE("a portal is a doorway standing across its seam", "[render]") {
  // The portal used to be drawn with the flat cell slab, which lies on the board however
  // it is scaled - so a portal on a vertical file seam came out horizontal. It is a cube
  // now, scaled thin on the axis it faces.
  const VariantSpec v = test::loadVariant("cylinder");
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::Theme theme = BoardRenderer{}.theme();
  const view::SeamMap seams = view::SeamMap::build(v, cfg, theme);

  auto pos = fromFen(v, "8/8/8/8/8/8/8/R7 w - - 0 1");
  REQUIRE(pos.has_value());
  const PieceTypeId rook = v.findPiece("rook");
  Move m;
  m.from = v.dims.toCell(Coord::of({0, 0}));
  m.to = v.dims.toCell(Coord::of({6, 0}));
  const view::MovePath path = view::tracePath(v, *pos, rook, Color::White, m);

  view::MoveAnimation anim;
  anim.start(cfg, view::layout(v.dims, cfg), seams, theme, path, 0.12f);
  for (int i = 0; i < 500 && anim.active() && anim.openPortals().empty(); ++i) {
    anim.advance(0.005f);
  }
  REQUIRE_FALSE(anim.openPortals().empty());

  const InstanceSet set = BoardRenderer{}.buildInstances(
      view::PositionView::capture(*pos), cfg, &seams, &anim);
  const auto& batch = set.batches[static_cast<std::size_t>(Archetype::Portal)];
  REQUIRE(batch.count > 0);
  for (std::uint32_t i = 0; i < batch.count; ++i) {
    const Instance& iris = set.instances[batch.first + i];
    // Thin across the file seam (X), wide along it (Y), tall (Z): a vertical doorway.
    CHECK(iris.scale[0] < iris.scale[1]);
    CHECK(iris.scale[0] < iris.scale[2]);
  }
}

TEST_CASE("pieces get a shape from how they move", "[render]") {
  // A variant can declare a piece nobody anticipated, so a shape has to be derivable.
  const VariantSpec v = test::loadVariant("standard");
  REQUIRE(archetypeFor(v.pieces[v.findPiece("pawn")]) == Archetype::Dome);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("king")]) == Archetype::Monolith);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("rook")]) == Archetype::Tower);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("bishop")]) == Archetype::Spire);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("queen")]) == Archetype::Crown);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("knight")]) == Archetype::Wedge);

  // A piece that needs three axes at once only exists above two dimensions, and gets a
  // shape that says so.
  const VariantSpec cube = test::loadVariant("cube5");
  REQUIRE(archetypeFor(cube.pieces[cube.findPiece("unicorn")]) == Archetype::Horn);

  // Height encodes value, so an unfamiliar army still reads at a glance.
  REQUIRE(heightFor(v.pieces[v.findPiece("king")]) >
          heightFor(v.pieces[v.findPiece("pawn")]));
  REQUIRE(heightFor(v.pieces[v.findPiece("queen")]) >
          heightFor(v.pieces[v.findPiece("pawn")]));

  // An explicit declaration always wins over the inference.
  PieceTypeDef custom;
  custom.name = "thing";
  custom.shape = "horn";
  custom.heightPermille = 1500;
  REQUIRE(archetypeFor(custom) == Archetype::Horn);
  REQUIRE(heightFor(custom) == 1.5f);
  // And an unknown name falls back rather than failing - a variant is never unplayable
  // for want of a model.
  custom.shape = "tesseract";
  REQUIRE(archetypeFor(custom) == Archetype::Tower);
}

TEST_CASE("every archetype produces a usable mesh", "[render]") {
  const MeshLibrary lib = MeshLibrary::build();
  REQUIRE_FALSE(lib.vertices.empty());
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    CAPTURE(archetypeName(static_cast<Archetype>(i)));
    const MeshRange& r = lib.ranges[i];
    REQUIRE(r.indexCount > 0);
    REQUIRE(r.indexCount % 3 == 0);
    // Indices are relative to the range's vertex offset, so every one must land inside
    // that archetype's own vertices - a stray index would draw another piece's geometry.
    std::uint16_t maxIndex = 0;
    for (std::uint32_t k = 0; k < r.indexCount; ++k) {
      maxIndex = std::max(maxIndex, lib.indices[r.firstIndex + k]);
    }
    REQUIRE(static_cast<std::size_t>(r.vertexOffset) + maxIndex < lib.vertices.size());
  }

  // M17.25: every archetype that is a piece also builds a valid icon-faced cylinder,
  // and every archetype that is *not* a piece has an empty range rather than a stray
  // mesh. Same buffer, so the same index-bounds check applies.
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    CAPTURE(archetypeName(static_cast<Archetype>(i)));
    const PieceIcon icon = pieceIcon(IconStyle::Primitive, static_cast<Archetype>(i));
    const MeshRange& r = lib.cylinderRanges[i];
    if (icon.fills.empty()) {
      CHECK(r.indexCount == 0);
      continue;
    }
    REQUIRE(r.indexCount > 0);
    REQUIRE(r.indexCount % 3 == 0);
    std::uint16_t maxIndex = 0;
    for (std::uint32_t k = 0; k < r.indexCount; ++k) {
      maxIndex = std::max(maxIndex, lib.indices[r.firstIndex + k]);
    }
    REQUIRE(static_cast<std::size_t>(r.vertexOffset) + maxIndex < lib.vertices.size());
  }

  // M17.26: every archetype that is a piece also builds a valid token disc, with the
  // same buffer/index-bounds contract; a non-piece has an empty range.
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    CAPTURE(archetypeName(static_cast<Archetype>(i)));
    const PieceIcon icon = pieceIcon(IconStyle::Primitive, static_cast<Archetype>(i));
    const MeshRange& r = lib.discRanges[i];
    if (icon.fills.empty()) {
      CHECK(r.indexCount == 0);
      continue;
    }
    REQUIRE(r.indexCount > 0);
    REQUIRE(r.indexCount % 3 == 0);
    std::uint16_t maxIndex = 0;
    for (std::uint32_t k = 0; k < r.indexCount; ++k) {
      maxIndex = std::max(maxIndex, lib.indices[r.firstIndex + k]);
    }
    REQUIRE(static_cast<std::size_t>(r.vertexOffset) + maxIndex < lib.vertices.size());
  }
}

TEST_CASE("a token disc is a closed, round, plain-white solid", "[render]") {
  // M17.26. The disc is the flat view's token circle carried into the shape view: a real
  // cylinder - two round caps and a round wall - not the icon's own outline stamped into
  // a slab. Its vertices are plain white, exactly like the icon mesh's, because the token
  // colour is the *instance's* and is read fresh off the theme every frame; baking it
  // here would freeze the disc to whichever theme was live when the mesh was built.
  const MeshLibrary lib = MeshLibrary::build();
  constexpr Archetype kPieces[] = {
      Archetype::Dome,  Archetype::Tower,    Archetype::Wedge, Archetype::Spire,
      Archetype::Crown, Archetype::Monolith, Archetype::Horn};
  using Key = std::array<int, 3>;
  using Edge = std::pair<Key, Key>;
  const auto key = [](float x, float y, float z) {
    return Key{static_cast<int>(std::lround(x * 10000.0f)),
               static_cast<int>(std::lround(y * 10000.0f)),
               static_cast<int>(std::lround(z * 10000.0f))};
  };
  for (const Archetype a : kPieces) {
    CAPTURE(archetypeName(a));
    const auto i = static_cast<std::size_t>(a);
    const MeshRange& r = lib.discRanges[i];
    REQUIRE(r.indexCount > 0);

    float maxRadius = 0.0f;
    float minPositiveRadius = 1e9f;
    for (std::uint32_t k = 0; k < r.indexCount; ++k) {
      const std::uint16_t local = lib.indices[r.firstIndex + k];
      const MeshVertex& v =
          lib.vertices[static_cast<std::size_t>(r.vertexOffset) + local];
      // Colour lives in the instance, never the mesh. Full white, every vertex.
      CHECK(v.color[0] == 1.0f);
      CHECK(v.color[1] == 1.0f);
      CHECK(v.color[2] == 1.0f);
      CHECK(v.color[3] == 1.0f);
      // The wall is a true circle: every *rim* vertex sits at one radius. Cap centres
      // are at radius zero and are allowed to be the only ones there.
      const float radius = std::sqrt(v.pos[0] * v.pos[0] + v.pos[1] * v.pos[1]);
      if (radius > 1e-4f) {
        maxRadius = std::max(maxRadius, radius);
        minPositiveRadius = std::min(minPositiveRadius, radius);
      }
    }
    // Round, not the icon's outline: all rim vertices share the disc's radius.
    CHECK(std::abs(maxRadius - minPositiveRadius) < 1e-4f);
    CHECK(maxRadius > 0.05f);
  }
  // The surface is closed: every geometric edge is shared by an even number of
  // triangles, so the caps and wall really meet rather than leaving a crack.
  for (const Archetype a : kPieces) {
    CAPTURE(archetypeName(a));
    const auto i = static_cast<std::size_t>(a);
    const MeshRange& r = lib.discRanges[i];
    std::map<Edge, int> edges;
    for (std::uint32_t k = 0; k + 2 < r.indexCount; k += 3) {
      Key tri[3];
      for (int c = 0; c < 3; ++c) {
        const std::uint16_t local =
            lib.indices[r.firstIndex + k + static_cast<std::uint32_t>(c)];
        const MeshVertex& v =
            lib.vertices[static_cast<std::size_t>(r.vertexOffset) + local];
        tri[c] = key(v.pos[0], v.pos[1], v.pos[2]);
      }
      for (int c = 0; c < 3; ++c) {
        Edge e{tri[c], tri[(c + 1) % 3]};
        if (e.second < e.first) std::swap(e.first, e.second);
        edges[e] += 1;
      }
    }
    for (const auto& [edge, uses] : edges) {
      INFO("edge (" << edge.first[0] << "," << edge.first[1] << "," << edge.first[2]
                    << ")-(" << edge.second[0] << "," << edge.second[1] << ","
                    << edge.second[2] << ")");
      CHECK(uses >= 2);
      CHECK(uses % 2 == 0);
    }
  }
}

TEST_CASE("the disc instance carries the token colour live from the theme", "[render]") {
  // M17.26. The token colour must come from the disc *instance*, read off the current
  // theme each time instances are built - the same way the icon instance reads
  // whitePiece/blackPiece - so a palette switch repaints the disc with no mesh rebuild.
  // Baking theme_.pieceToken into the mesh would have failed this: MeshLibrary::build()
  // runs once and setTheme never reruns it.
  BoardRenderer renderer;
  const VariantSpec v = test::loadVariant("torus");
  const Position p = Position::startPosition(v);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  BoardOptions options = renderer.options();
  options.surface = true;
  options.shapePieceCylinders = true;
  renderer.setOptions(options);

  const auto near = [](float x, float y) { return std::abs(x - y) < 1e-5f; };
  const auto countsDiscs = [&](const InstanceSet& set) {
    std::size_t discs = 0;
    for (std::size_t sh = 0; sh < static_cast<std::size_t>(Archetype::Count); ++sh) {
      CHECK(set.discCounts[sh] <= set.batches[sh].count);
      discs += set.discCounts[sh];
    }
    return discs;
  };

  const view::Theme firstTheme = renderer.theme();
  const InstanceSet first = renderer.buildInstances(view::PositionView::capture(p), cfg);
  REQUIRE(countsDiscs(first) == 32);  // one disc per surface piece
  for (std::size_t sh = 0; sh < static_cast<std::size_t>(Archetype::Count); ++sh) {
    const auto& batch = first.batches[sh];
    for (std::uint32_t k = 0; k < first.discCounts[sh]; ++k) {
      const Instance& disc = first.instances[batch.first + k];
      CHECK(near(disc.color[0], firstTheme.pieceToken.r));
      CHECK(near(disc.color[1], firstTheme.pieceToken.g));
      CHECK(near(disc.color[2], firstTheme.pieceToken.b));
      CHECK(near(disc.color[3], 1.0f));
      // No check tint on the disc: it is furniture, exactly as in the flat view.
      const bool bloodTinted = near(disc.color[0], firstTheme.blood.r) &&
                               near(disc.color[1], firstTheme.blood.g);
      CHECK_FALSE(bloodTinted);
    }
    // The icon decal after the discs keeps the piece colour, not the token.
    for (std::uint32_t k = first.discCounts[sh]; k < batch.count; ++k) {
      const Instance& icon = first.instances[batch.first + k];
      const bool tokenColoured = near(icon.color[0], firstTheme.pieceToken.r) &&
                                 near(icon.color[1], firstTheme.pieceToken.g) &&
                                 near(icon.color[2], firstTheme.pieceToken.b);
      CHECK_FALSE(tokenColoured);
    }
  }

  // Switching theme repaints the disc on the next build, with no mesh rebuild anywhere.
  const view::Theme other = view::Theme::console();
  REQUIRE_FALSE(near(other.pieceToken.r, firstTheme.pieceToken.r));
  renderer.setTheme(other);
  const InstanceSet second = renderer.buildInstances(view::PositionView::capture(p), cfg);
  REQUIRE(countsDiscs(second) == 32);
  for (std::size_t sh = 0; sh < static_cast<std::size_t>(Archetype::Count); ++sh) {
    const auto& batch = second.batches[sh];
    for (std::uint32_t k = 0; k < second.discCounts[sh]; ++k) {
      const Instance& disc = second.instances[batch.first + k];
      CHECK(near(disc.color[0], other.pieceToken.r));
      CHECK(near(disc.color[1], other.pieceToken.g));
      CHECK(near(disc.color[2], other.pieceToken.b));
    }
  }
}

TEST_CASE("an icon-faced cylinder is a closed double-sided puck", "[render]") {
  // The puck is a union of one prism per icon sub-polygon, so it is a watertight solid:
  // every *geometric* edge is shared by an even number of triangles, with no crack
  // between a cap and its rim or between two rim quads. Two is the clean count; four is
  // two sub-polygons meeting at a shared corner. Edges are matched by position, not by
  // vertex index, because a hard edge deliberately duplicates vertices for its own normal
  // - the same thing `addBox` does for every face of an ordinary mesh.
  const MeshLibrary lib = MeshLibrary::build();
  constexpr Archetype kPieces[] = {
      Archetype::Dome,  Archetype::Tower,    Archetype::Wedge, Archetype::Spire,
      Archetype::Crown, Archetype::Monolith, Archetype::Horn};
  using Key = std::array<int, 3>;
  using Edge = std::pair<Key, Key>;
  const auto key = [](float x, float y, float z) {
    return Key{static_cast<int>(std::lround(x * 10000.0f)),
               static_cast<int>(std::lround(y * 10000.0f)),
               static_cast<int>(std::lround(z * 10000.0f))};
  };
  for (const Archetype a : kPieces) {
    const auto i = static_cast<std::size_t>(a);
    const MeshRange& r = lib.cylinderRanges[i];
    REQUIRE(r.indexCount > 0);
    std::map<Edge, int> edges;
    for (std::uint32_t k = 0; k + 2 < r.indexCount; k += 3) {
      Key tri[3];
      for (int c = 0; c < 3; ++c) {
        const std::uint16_t local =
            lib.indices[r.firstIndex + k + static_cast<std::uint32_t>(c)];
        const MeshVertex& v =
            lib.vertices[static_cast<std::size_t>(r.vertexOffset) + local];
        tri[c] = key(v.pos[0], v.pos[1], v.pos[2]);
      }
      for (int c = 0; c < 3; ++c) {
        Edge e{tri[c], tri[(c + 1) % 3]};
        if (e.second < e.first) std::swap(e.first, e.second);
        edges[e] += 1;
      }
    }
    CAPTURE(archetypeName(a));
    for (const auto& [edge, uses] : edges) {
      INFO("edge (" << edge.first[0] << "," << edge.first[1] << "," << edge.first[2]
                    << ")-(" << edge.second[0] << "," << edge.second[1] << ","
                    << edge.second[2] << ")");
      // Two is the clean count; four is two sub-polygons whose shared corner each
      // contribute their own rim wall. What is forbidden is *one* - a dangling edge, a
      // crack between a cap and its rim or between two rim quads.
      CHECK(uses >= 2);
      CHECK(uses % 2 == 0);
    }
  }
}

TEST_CASE("the puck stamps the same icon on both faces", "[render]") {
  // A flipped seat is a seat the tangent-field walk has turned about-face (M17.18). The
  // whole point of the puck is that a piece reads the same from either side, so both caps
  // must carry the *same* outline - the identical polygon, not a mirrored or reversed
  // pair. A mirrored pair would still pass a naive silhouette/congruence test for a
  // symmetric glyph, so this compares the two caps point-for-point in buffer order, and
  // against the Primitive icon itself.
  const MeshLibrary lib = MeshLibrary::build();
  constexpr Archetype kPieces[] = {
      Archetype::Dome,  Archetype::Tower,    Archetype::Wedge, Archetype::Spire,
      Archetype::Crown, Archetype::Monolith, Archetype::Horn};
  for (const Archetype a : kPieces) {
    CAPTURE(archetypeName(a));
    const auto i = static_cast<std::size_t>(a);
    const PieceIcon icon = pieceIcon(IconStyle::Primitive, a);
    std::vector<IconPoint> flat;
    for (const IconPoly& poly : icon.fills) {
      for (const IconPoint& p : poly) flat.push_back(p);
    }

    // The archetype's own vertex span in the shared buffer: contiguous, in build order.
    const std::size_t begin =
        static_cast<std::size_t>(lib.cylinderRanges[i].vertexOffset);
    const std::size_t end =
        i + 1 < static_cast<std::size_t>(Archetype::Count)
            ? static_cast<std::size_t>(lib.cylinderRanges[i + 1].vertexOffset)
            : lib.vertices.size();
    std::vector<MeshVertex> top;
    std::vector<MeshVertex> bottom;
    for (std::size_t v = begin; v < end; ++v) {
      if (lib.vertices[v].normal[2] > 0.9f) {
        top.push_back(lib.vertices[v]);
      } else if (lib.vertices[v].normal[2] < -0.9f) {
        bottom.push_back(lib.vertices[v]);
      }
    }

    REQUIRE(top.size() == flat.size());
    REQUIRE(bottom.size() == flat.size());

    // Recentring: the icon's own centroid, exactly as `addIconCylinder` does.
    float cx = 0.0f;
    float cy = 0.0f;
    for (const IconPoint& p : flat) {
      cx += p.x;
      cy += p.y;
    }
    cx /= static_cast<float>(flat.size());
    cy /= static_cast<float>(flat.size());

    const auto near = [](float x, float y) { return std::abs(x - y) < 1e-5f; };
    for (std::size_t k = 0; k < flat.size(); ++k) {
      // M17.26: the icon is now the decal on the token disc, shrunk to the flat view's
      // glyph-to-circle ratio.
      const float ex = (flat[k].x - cx) * kCylinderDecalScale;
      const float ey = (cy - flat[k].y) * kCylinderDecalScale;
      // The top cap is the icon, the bottom cap is the *same* polygon pushed down.
      CHECK(near(top[k].pos[0], ex));
      CHECK(near(top[k].pos[1], ey));
      CHECK(near(top[k].pos[2], kCylinderHalfThick));
      CHECK(near(bottom[k].pos[0], ex));
      CHECK(near(bottom[k].pos[1], ey));
      CHECK(near(bottom[k].pos[2], -kCylinderHalfThick));
    }
  }
}

TEST_CASE("a 2-D board renders with no validation errors", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const Frame f = renderVariant("standard");
  CAPTURE(f.messages);
  REQUIRE(f.validationErrors == 0);

  REQUIRE(f.image.width == 512);
  REQUIRE(f.image.height == 384);
  REQUIRE(f.image.rgba.size() == 512u * 384u * 4u);

  // Something was actually drawn, and it is not a solid fill. The ground comes from the
  // theme rather than being written out here: a retheme is allowed, a board that stops
  // being drawn is not.
  const view::Theme theme = BoardRenderer{}.theme();
  const auto byte = [](float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  const Image::Rgba background{byte(theme.ink.r), byte(theme.ink.g), byte(theme.ink.b),
                               255};
  const int coverage = f.image.coveragePerMille(background);
  CAPTURE(coverage);
  REQUIRE(coverage > 200);
  REQUIRE(coverage < 1000);
  // Shading over two cell colours and two piece colours gives a wide palette; a single
  // flat colour would mean the instance data never reached the shader.
  REQUIRE(f.image.distinctColors() > 8);
  // The camera frames the board with margin, so the corners stay background.
  REQUIRE(f.image.at(0, 0) == background);
  REQUIRE(f.image.at(511, 0) == background);

  REQUIRE(writePpm(f.image, capturePath("standard")).has_value());
}

TEST_CASE("rendering is deterministic", "[render][gpu]") {
  // Two identical frames must be bit-identical. Without this, an image assertion is
  // untrustworthy and every later golden would be flaky.
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const Frame a = renderVariant("standard");
  const Frame b = renderVariant("standard");
  REQUIRE(a.image.rgba == b.image.rgba);
}

TEST_CASE("highlighting changes the picture", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const Frame plain = renderVariant("standard", 512, 384, /*highlight=*/false);
  const Frame lit = renderVariant("standard", 512, 384, /*highlight=*/true);
  REQUIRE(plain.image.rgba != lit.image.rgba);
  REQUIRE(lit.validationErrors == 0);
}

TEST_CASE("3-D and 4-D boards render, and look different from each other",
          "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  // The theme's ground: a brown-biased near-black, not a neutral grey.
  const Image::Rgba background{13, 11, 10, 255};

  const Frame cube = renderVariant("cube5");
  CAPTURE(cube.messages);
  REQUIRE(cube.validationErrors == 0);
  REQUIRE(cube.image.coveragePerMille(background) > 150);
  REQUIRE(writePpm(cube.image, capturePath("cube5")).has_value());

  const Frame hyper = renderVariant("hyper4");
  CAPTURE(hyper.messages);
  REQUIRE(hyper.validationErrors == 0);
  // A 4-D board is drawn as a row of sub-boards, so it covers the frame differently.
  REQUIRE(hyper.image.coveragePerMille(background) > 100);
  REQUIRE(writePpm(hyper.image, capturePath("hyper4")).has_value());

  REQUIRE(cube.image.rgba != hyper.image.rgba);
}

TEST_CASE("every shipped variant renders cleanly", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  for (const char* name : {"standard", "cylinder", "torus", "mobius", "klein",
                           "mirrorbox", "cube5", "hyper4", "torus3d"}) {
    CAPTURE(name);
    const Frame f = renderVariant(name, 256, 192);
    CAPTURE(f.messages);
    REQUIRE(f.validationErrors == 0);
    REQUIRE(f.image.coveragePerMille(Image::Rgba{13, 11, 10, 255}) > 50);
    REQUIRE(writePpm(f.image, capturePath(name)).has_value());
  }
}

TEST_CASE("a branch connector turns into its rail on a rounded corner", "[render]") {
  // The corner of a timeline elbow used to be two rectangles overlapping at a right
  // angle. A quarter-round fillet is placed in the empty inside of the bend, turned by
  // the instance's roll into the right quadrant. Built with no GPU: it is instance data.
  auto session = app::Session::create(test::loadVariant("5d"));
  REQUIRE(session.has_value());
  app::Session& s = **session;
  const auto click = [&](CellId c) {
    app::Action a;
    a.kind = app::ActionKind::ClickCell;
    a.cell = c;
    REQUIRE(s.apply(a).has_value());
  };
  const auto boardOf = [&](CellId c) {
    const Coord co = s.variant().dims.toCoord(c);
    return std::pair<int, int>{co.c[2], co.c[3]};
  };
  const auto playFirst = [&](bool normal) {
    Move chosen{};
    bool found = false;
    for (const Move& m : s.game().legalMoves()) {
      if ((boardOf(m.from) == boardOf(m.to)) == normal) {
        chosen = m;
        found = true;
        break;
      }
    }
    REQUIRE(found);
    click(chosen.from);
    click(chosen.to);
  };
  playFirst(true);   // White advances.
  playFirst(true);   // Black advances.
  playFirst(false);  // Black travels into the past and branches.
  REQUIRE_FALSE(s.timelineLinks().empty());

  BoardRenderer renderer;
  const view::ViewConfig cfg = view::ViewConfig::forBoard(s.variant().dims);
  const InstanceSet set = renderer.buildInstances(
      s.snapshot(), cfg, nullptr, nullptr, [&](CellId c) { return s.boardVisible(c); },
      {}, s.timelineLinks());
  const auto& batch = set.batches[static_cast<std::size_t>(Archetype::Fillet)];
  REQUIRE(batch.count >= 1);
  const Instance& fil = set.instances[batch.first];
  // A quarter-turn multiple of the roll, and the fillet as thick as the rail it sits on.
  const float quarter = fil.roll / (0.5f * std::numbers::pi_v<float>);
  CHECK(std::abs(quarter - std::round(quarter)) < 0.01f);
  CHECK(std::abs(fil.scale[2] - 0.025f / 0.055f) < 0.05f);
}

TEST_CASE("a board renders a picture, not a fill", "[render][gpu]") {
  // The screenshot tests elsewhere only check the exit code; this checks the pixels are
  // a board - structure, and no saturated colour on a flat board (the theme's own law).
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  for (const char* name : {"standard", "cube5", "hyper4", "torus", "5d", "klein"}) {
    CAPTURE(name);
    const Frame f = renderVariant(name, 256, 192);
    CAPTURE(f.messages);
    REQUIRE(f.validationErrors == 0);
    // Structure: a blank or solid frame has near-zero luminance variance.
    CHECK(f.image.luminanceVariance() > 50.0);
    CHECK(f.image.distinctColors() > 8);
    // Faceted indigo pieces may now be saturated even on a flat board, but the
    // frame remains restrained: saturated pixels cannot dominate it.
    if (std::string(name) == "standard") {
      CHECK(f.image.saturatedPixels() < 256u * 192u / 100u);
    }
  }
}

TEST_CASE("the renderer draws straight into an image it does not own", "[render][gpu]") {
  // The interactive path no longer renders offscreen and blits: it draws into the
  // acquired swapchain image, whose colour-attachment, sampled and transfer-source usages
  // this image imitates. Without a window this is the closest a test can get to it
  // (ADR-0018); only `vkAcquireNextImageKHR` / `vkQueuePresentKHR` themselves go
  // unexercised. The async `record` path is driven here, including both frame slots, so
  // the per-frame instance buffer and blur descriptors are covered too.
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);

  constexpr std::uint32_t w = 256;
  constexpr std::uint32_t h = 192;

  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const Position p = Position::startPosition(v);
  const view::PositionView snap = view::PositionView::capture(p);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));
  const InstanceSet set = BoardRenderer{}.buildInstances(snap, cfg);

  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = OffscreenTarget::kColorFormat;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
              VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImage image = VK_NULL_HANDLE;
  REQUIRE(vkCreateImage(gpu().ctx.device(), &ici, nullptr, &image) == VK_SUCCESS);
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(gpu().ctx.device(), image, &req);
  const auto type =
      gpu().ctx.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  REQUIRE(type.has_value());
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = *type;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  REQUIRE(vkAllocateMemory(gpu().ctx.device(), &mai, nullptr, &mem) == VK_SUCCESS);
  REQUIRE(vkBindImageMemory(gpu().ctx.device(), image, mem, 0) == VK_SUCCESS);

  auto target =
      OffscreenTarget::wrapColor(gpu().ctx, image, OffscreenTarget::kColorFormat, w, h);
  REQUIRE(target.has_value());
  CHECK(target->width() == w);
  CHECK(target->colorFormat() == OffscreenTarget::kColorFormat);

  auto renderer = BoardRenderer::create(gpu().ctx, OffscreenTarget::kColorFormat);
  REQUIRE(renderer.has_value());

  (void)gpu().ctx.takeValidationMessages();
  std::vector<std::uint8_t> first;
  for (std::uint32_t frame = 0; frame < BoardRenderer::kFramesInFlight; ++frame) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = gpu().ctx.commandPool();
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    REQUIRE(vkAllocateCommandBuffers(gpu().ctx.device(), &ai, &cmd) == VK_SUCCESS);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    REQUIRE(renderer->record(cmd, *target, set, cam, {}, {}, {}, frame).has_value());

    // The renderer leaves the image a colour attachment; the test wants it readable.
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    REQUIRE(vkCreateFence(gpu().ctx.device(), &fci, nullptr, &fence) == VK_SUCCESS);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    REQUIRE(vkQueueSubmit(gpu().ctx.queue(), 1, &si, fence) == VK_SUCCESS);
    vkWaitForFences(gpu().ctx.device(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(gpu().ctx.device(), fence, nullptr);
    vkFreeCommandBuffers(gpu().ctx.device(), gpu().ctx.commandPool(), 1, &cmd);

    auto pixels = target->readPixels();
    REQUIRE(pixels.has_value());
    if (frame == 0) {
      first = std::move(*pixels);
    } else {
      // Both frame slots must produce the identical board: the per-frame resources are
      // interchangeable.
      REQUIRE(*pixels == first);
    }
  }

  CAPTURE(gpu().ctx.takeValidationMessages());
  REQUIRE(gpu().ctx.validationErrorCount() == 0);

  Image img;
  img.width = w;
  img.height = h;
  img.rgba = std::move(first);
  CHECK(img.distinctColors() > 8);

  vkDestroyImage(gpu().ctx.device(), image, nullptr);
  vkFreeMemory(gpu().ctx.device(), mem, nullptr);
}

TEST_CASE("the background callback draws behind the board, not over it",
          "[render][gpu]") {
  // The board's own pass takes a `background` callback invoked before its viewport and
  // draws. A solid fill there must be covered by the board's opaque geometry; the same
  // fill submitted as the late `overlay` must instead cover the board. No ImGui here - a
  // raw attachment clear is enough to prove the ordering - and the ImGui field itself is
  // driven end to end in the test below.
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);

  constexpr std::uint32_t w = 256;
  constexpr std::uint32_t h = 192;
  const VariantSpec& v = *new VariantSpec(test::loadVariant("standard"));
  const Position p = Position::startPosition(v);
  const view::PositionView snap = view::PositionView::capture(p);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));
  const InstanceSet set = BoardRenderer{}.buildInstances(snap, cfg);

  auto target = OffscreenTarget::create(gpu().ctx, w, h);
  REQUIRE(target.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(renderer.has_value());

  const auto read = [&](const std::function<void(VkCommandBuffer)>& overlay,
                        const std::function<void(VkCommandBuffer)>& background) {
    REQUIRE(renderer->render(*target, set, cam, overlay, background).has_value());
    auto px = target->readPixels();
    REQUIRE(px.has_value());
    Image img;
    img.width = target->width();
    img.height = target->height();
    img.rgba = std::move(*px);
    return img;
  };

  const Image::Rgba magenta{255, 0, 255, 255};
  const auto fill = [&](VkCommandBuffer cmd) {
    VkClearAttachment att{};
    att.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    att.colorAttachment = 0;
    att.clearValue.color.float32[0] = 1.0f;
    att.clearValue.color.float32[1] = 0.0f;
    att.clearValue.color.float32[2] = 1.0f;
    att.clearValue.color.float32[3] = 1.0f;
    VkClearRect rect{};
    rect.rect = {{0, 0}, {w, h}};
    rect.layerCount = 1;
    vkCmdClearAttachments(cmd, 1, &att, 1, &rect);
  };

  (void)gpu().ctx.takeValidationMessages();
  const Image none = read({}, {});
  const Image early = read({}, fill);
  const Image late = read(fill, {});

  // A pixel well inside the board: every neighbour in `none` is board, not the ground.
  const auto byte = [](float c) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
  };
  const view::Theme theme = BoardRenderer{}.theme();
  const Image::Rgba ground{byte(theme.ink.r), byte(theme.ink.g), byte(theme.ink.b), 255};
  const auto solidAt = [&](std::uint32_t x, std::uint32_t y) {
    for (int dy = -3; dy <= 3; ++dy) {
      for (int dx = -3; dx <= 3; ++dx) {
        const auto sx = static_cast<std::uint32_t>(static_cast<int>(x) + dx);
        const auto sy = static_cast<std::uint32_t>(static_cast<int>(y) + dy);
        if (none.at(sx, sy) == ground) return false;
      }
    }
    return true;
  };
  std::uint32_t bx = w / 2;
  std::uint32_t by = h / 2;
  bool found = false;
  for (std::uint32_t y = 4; y + 4 < h && !found; ++y) {
    for (std::uint32_t x = 4; x + 4 < w; ++x) {
      if (solidAt(x, y)) {
        bx = x;
        by = y;
        found = true;
        break;
      }
    }
  }
  CAPTURE(found);
  REQUIRE(found);

  // Behind the board: the fill is invisible where the board sits.
  CHECK(early.at(bx, by) == none.at(bx, by));
  // But it did run: a frame corner has no board, so the fill shows there.
  CHECK(early.at(0, 0) == magenta);
  // Over the board: the same fill drawn last covers the board - the bug being fixed.
  CHECK(late.at(bx, by) != none.at(bx, by));

  CAPTURE(gpu().ctx.takeValidationMessages());
  REQUIRE(gpu().ctx.validationErrorCount() == 0);
  REQUIRE(writePpm(early, capturePath("background-behind")).has_value());
}

#ifdef CB_HAVE_IMGUI
// The surface's own placement, frames, sizes, seams, eversion and picking are pinned
// in `test_play_surface.cpp`: they are arithmetic and need no device. What is left here
// is the one thing that does - that the instances it produces actually draw.
TEST_CASE("the geometry view renders the board on its surface", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const Position p = Position::startPosition(v);
  const view::PositionView snap = view::PositionView::capture(p);
  auto target = OffscreenTarget::create(gpu().ctx, 512, 384);
  REQUIRE(target.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(renderer.has_value());
  BoardOptions options = renderer->options();
  options.surface = true;
  renderer->setOptions(options);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));

  (void)gpu().ctx.takeValidationMessages();
  auto img = renderer->renderToImage(*target, snap, cfg, cam);
  REQUIRE(img.has_value());
  CAPTURE(gpu().ctx.takeValidationMessages());
  REQUIRE(gpu().ctx.validationErrorCount() == 0);
  CHECK(img->distinctColors() > 8);
  CHECK(img->luminanceVariance() > 20.0);
  REQUIRE(writePpm(*img, capturePath("surface-torus")).has_value());
}

TEST_CASE("a ghosted board shows the far side through the near side", "[render][gpu]") {
  // M17.10. The board mesh is drawn after the opaque pieces with depth writes off, so the
  // far side blends over the near one. The picture gains structure; the pieces, drawn
  // opaque first, are unaffected.
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const Position p = Position::startPosition(v);
  const view::PositionView snap = view::PositionView::capture(p);
  auto target = OffscreenTarget::create(gpu().ctx, 512, 384);
  REQUIRE(target.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(renderer.has_value());
  BoardOptions options = renderer->options();
  options.surface = true;
  renderer->setOptions(options);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const PlaySurface surf = PlaySurface::build(v);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(surf.bounds(), 512.0f / 384.0f, 0.0f);

  (void)gpu().ctx.takeValidationMessages();
  const auto opaque = renderer->renderToImage(*target, snap, cfg, cam);
  REQUIRE(opaque.has_value());
  REQUIRE(gpu().ctx.validationErrorCount() == 0);
  const auto opaquePixels = *opaque;  // keep the image, the target is drawn over

  options.surfaceGhost = 0.35f;
  renderer->setOptions(options);
  const auto ghost = renderer->renderToImage(*target, snap, cfg, cam);
  REQUIRE(ghost.has_value());
  CAPTURE(gpu().ctx.takeValidationMessages());
  REQUIRE(gpu().ctx.validationErrorCount() == 0);
  // The far side now contributes, so the picture is different (blending averages the two
  // sheets, so its variance is lower, not higher - the plan guessed the sign).
  CHECK(ghost->rgba != opaquePixels.rgba);
  CHECK(ghost->distinctColors() > 8);
  REQUIRE(writePpm(*ghost, capturePath("surface-ghost")).has_value());

  // And 1 is a strict no-op: the ghost alpha is the only thing that changed.
  options.surfaceGhost = 1.0f;
  renderer->setOptions(options);
  const auto restored = renderer->renderToImage(*target, snap, cfg, cam);
  REQUIRE(restored.has_value());
  CHECK(restored->rgba == opaquePixels.rgba);
}

TEST_CASE("the shapes above two dimensions render on the board", "[render][gpu]") {
  // M17.12: `torus3d`'s nested shells and `hyper4`'s tesseract, drawn through the same
  // renderer and camera as the 2-D surfaces.
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const Position p = Position::startPosition(v);
    const view::PositionView snap = view::PositionView::capture(p);
    auto target = OffscreenTarget::create(gpu().ctx, 512, 384);
    REQUIRE(target.has_value());
    auto renderer = BoardRenderer::create(gpu().ctx);
    REQUIRE(renderer.has_value());
    BoardOptions options = renderer->options();
    options.surface = true;
    renderer->setOptions(options);
    const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
    const PlaySurface surf = PlaySurface::build(v);
    REQUIRE_FALSE(surf.empty());
    const view::OrbitCamera cam =
        view::OrbitCamera::frame(surf.bounds(), 512.0f / 384.0f, 0.0f);

    (void)gpu().ctx.takeValidationMessages();
    const auto img = renderer->renderToImage(*target, snap, cfg, cam);
    REQUIRE(img.has_value());
    CAPTURE(gpu().ctx.takeValidationMessages());
    REQUIRE(gpu().ctx.validationErrorCount() == 0);
    CHECK(img->distinctColors() > 8);
    CHECK(img->luminanceVariance() > 20.0);
    REQUIRE(writePpm(*img, capturePath(std::string("surface-") + name)).has_value());
  }
}

TEST_CASE("a move in flight on the shape renders cleanly", "[render][gpu]") {
  // M17.15: the moving piece is drawn from the surface sampler, not left at its landing
  // seat. This drives the renderer's travelling-piece path under the validation layers.
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  auto session = app::Session::create(test::loadVariant("torus"));
  REQUIRE(session.has_value());
  app::Session& s = **session;
  REQUIRE_FALSE(s.game().legalMoves().empty());
  const Move m = s.game().legalMoves().front();
  const auto click = [&](CellId c) {
    app::Action a;
    a.kind = app::ActionKind::ClickCell;
    a.cell = c;
    REQUIRE(s.apply(a).has_value());
  };
  click(m.from);
  click(m.to);
  REQUIRE(s.animation().active());
  s.setMoveProgress(0.5f);

  auto target = OffscreenTarget::create(gpu().ctx, 512, 384);
  REQUIRE(target.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(renderer.has_value());
  BoardOptions options = renderer->options();
  options.surface = true;
  renderer->setOptions(options);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const PlaySurface surf = PlaySurface::build(v);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(surf.bounds(), 512.0f / 384.0f, 0.0f);
  const InstanceSet set =
      renderer->buildInstances(s.snapshot(), cfg, &s.seams(), &s.animation(),
                               [&](CellId c) { return s.boardVisible(c); });

  (void)gpu().ctx.takeValidationMessages();
  REQUIRE(renderer->render(*target, set, cam).has_value());
  CAPTURE(gpu().ctx.takeValidationMessages());
  REQUIRE(gpu().ctx.validationErrorCount() == 0);
  const auto px = target->readPixels();
  REQUIRE(px.has_value());
}

TEST_CASE("the shape tints its seam rails into the board mesh", "[render]") {
  // M17.21. The rail is not a pipeline or an edge: it is the seam's own colour written
  // into the patch's vertex colours at the wrap edge, the same one mesh the ghost rides.
  // No device needed - `buildInstances` is pure data in, pure data out.
  BoardRenderer renderer;
  const view::Theme theme = renderer.theme();
  const VariantSpec v = test::loadVariant("torus");
  const Position p = Position::startPosition(v);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::SeamMap seams = view::SeamMap::build(v, cfg, theme);

  BoardOptions options = renderer.options();
  options.surface = true;
  renderer.setOptions(options);
  const view::PositionView snap = view::PositionView::capture(p);
  const InstanceSet set = renderer.buildInstances(snap, cfg, &seams);

  // The flat view's own colour for the file seam at (0,3); the shape must carry it.
  const CellId railCell = v.dims.toCell(Coord::of({0, 3}));
  view::Rgba want{};
  bool foundFace = false;
  for (const view::SeamFace& f : seams.at(railCell)) {
    if (f.screenAxis == 0 && f.side == Side::Min) {
      want = f.color;
      foundFace = true;
    }
  }
  REQUIRE(foundFace);
  const auto carriesRail = [&](const InstanceSet& s) {
    int n = 0;
    for (const MeshVertex& mv : s.surfaceVertices) {
      if (std::abs(mv.color[0] - want.r) < 1e-4f &&
          std::abs(mv.color[1] - want.g) < 1e-4f &&
          std::abs(mv.color[2] - want.b) < 1e-4f) {
        ++n;
      }
    }
    return n;
  };
  CHECK(carriesRail(set) > 0);

  // Turning the seam marks off leaves no rail colour in the mesh - the same toggle the
  // flat view's rim answers to.
  options.showSeams = false;
  renderer.setOptions(options);
  const InstanceSet bare = renderer.buildInstances(snap, cfg, &seams);
  CHECK(carriesRail(bare) == 0);
}

TEST_CASE("the tesseract scaffold is drawn only for hyper4 and only with seams on",
          "[render]") {
  // M17.12 part D. The wireframe is a structural (non-gameplay) marking, so it rides the
  // same `showSeams` toggle every other one does, and it is specific to the tesseract -
  // `torus3d`'s nested shells get none. It lives in its own blended batch, so it never
  // touches the pick path (`PlaySurface::pick` reads `patches_` alone). No device needed:
  // `buildInstances` is pure data in, pure data out.
  BoardRenderer renderer;
  const auto scaffoldCells = [&](const char* name, bool seams) {
    const VariantSpec v = test::loadVariant(name);
    const Position p = Position::startPosition(v);
    const view::PositionView snap = view::PositionView::capture(p);
    const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
    BoardOptions options = renderer.options();
    options.surface = true;
    options.showSeams = seams;
    renderer.setOptions(options);
    const InstanceSet set = renderer.buildInstances(snap, cfg);
    return set.scaffoldBatches[static_cast<std::size_t>(Archetype::Cell)].count;
  };
  CHECK(scaffoldCells("hyper4", true) == 32u);
  CHECK(scaffoldCells("hyper4", false) == 0u);
  CHECK(scaffoldCells("torus3d", true) == 0u);
}

TEST_CASE("the first HUD frame measures current turn and recent rows",
          "[render][gpu][capture-hud]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct SdlGuard {
    ~SdlGuard() { SDL_Quit(); }
  } sdl;
  SDL_Window* window = SDL_CreateWindow("hud-history", 1920, 1080, SDL_WINDOW_HIDDEN);
  REQUIRE(window != nullptr);
  struct WindowGuard {
    SDL_Window* window;
    ~WindowGuard() { SDL_DestroyWindow(window); }
  } guard{window};
  for (const auto& theme : {view::Theme::console(), view::Theme::manifold()}) {
    for (float scale : {1.0f, 3.0f}) {
      CAPTURE(scale, theme.liveControls);
      auto shell = app::Shell::create(
          {"standard"}, std::filesystem::path(CB_BINARY_DIR) / "hud-history.conf");
      shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
        return test::loadVariant(name);
      });
      REQUIRE(shell->startGame("standard").has_value());
      auto ui = Ui::create(gpu().ctx, window, theme);
      REQUIRE(ui.has_value());
      REQUIRE((*ui)->setScale(scale).has_value());
      (*ui)->setVisualTime(0.75);
      auto target = OffscreenTarget::create(gpu().ctx, 1920, 1080);
      auto renderer = BoardRenderer::create(gpu().ctx);
      REQUIRE(target.has_value());
      REQUIRE(renderer.has_value());
      renderer->setTheme(theme);
      renderer->setVisualTime(0.75);
      const auto cam = view::OrbitCamera::frame(
          view::boundsOf(view::layout(shell->session()->variant().dims,
                                      shell->session()->viewConfig())),
          1920.0f / 1080.0f);
      const auto frame = [&](float dt, bool inspect) {
        (*ui)->tick(dt);
        (*ui)->newFrame();
        (void)(*ui)->build(*shell, 60);
        if (inspect) {
          const auto* status = ImGui::FindWindowByName("##status");
          REQUIRE(status != nullptr);
          CHECK_FALSE(status->Hidden);
          CHECK(status->DrawList->VtxBuffer.Size > 0);
          const auto* recent = ImGui::FindWindowByName("##recent");
          if (shell->session()->game().plyCount() > 0) {
            REQUIRE(recent != nullptr);
            CHECK(recent->Active);
            CHECK_FALSE(recent->Hidden);
            CHECK(recent->DrawList->VtxBuffer.Size >
                  0);  // normal text glyphs, no window background
            CHECK(recent->Pos.x >= 0);
            CHECK(recent->Pos.y >= 0);
            CHECK(recent->Pos.x + recent->Size.x <= 1920);
            CHECK(recent->Pos.y + recent->Size.y <= 1080);
          } else if (recent != nullptr)
            CHECK_FALSE(recent->Active);
        }
        (*ui)->endFrame();
        const auto set = renderer->buildInstances(shell->session()->snapshot(),
                                                  shell->session()->viewConfig());
        REQUIRE(renderer
                    ->render(
                        *target, set, cam,
                        [&](VkCommandBuffer cmd) { (*ui)->record(cmd); },
                        [&](VkCommandBuffer cmd) { (*ui)->recordBackground(cmd); })
                    .has_value());
        auto pixels = target->readPixels();
        REQUIRE(pixels.has_value());
        REQUIRE(gpu().ctx.takeValidationMessages().empty());
        return Image{1920, 1080, std::move(*pixels)};
      };
      for (int i = 0; i < 3; ++i) (void)frame(1, false);
      const auto equalFrames = [&] {
        const auto first = frame(0, true), next = frame(0, true);
        if (first.rgba != next.rgba) {
          REQUIRE(writePpm(first, capturePath("m237-hud-first")).has_value());
          REQUIRE(writePpm(next, capturePath("m237-hud-next")).has_value());
        }
        CHECK(bool(first.rgba == next.rgba));
      };
      for (const char* move :
           {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6"}) {
        INFO(move);
        REQUIRE(shell->session()->playMoveText(move).has_value());
        shell->session()->setMoveProgress(1);
        equalFrames();
      }
      for (int i = 0; i < 3; ++i) {
        REQUIRE(shell->session()
                    ->apply(app::Action{.kind = app::ActionKind::Undo, .text = {}})
                    .has_value());
        equalFrames();
      }
      REQUIRE(shell->session()
                  ->apply(app::Action{.kind = app::ActionKind::Reset, .text = {}})
                  .has_value());
      equalFrames();
      REQUIRE(shell->session()->loadFen("4k3/8/8/8/8/8/4R3/4K3 b - - 0 1").has_value());
      REQUIRE(shell->session()->game().inCheck());
      equalFrames();
      REQUIRE(shell->session()
                  ->apply(app::Action{.kind = app::ActionKind::Resign, .text = {}})
                  .has_value());
      REQUIRE(shell->session()->game().result() != GameResult::InProgress);
      equalFrames();
    }
  }
}

TEST_CASE("SDL keyboard ownership distinguishes navigation from active UI",
          "[render][gpu][keyboard-routing]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct SdlGuard {
    ~SdlGuard() { SDL_Quit(); }
  } sdl;
  SDL_Window* window = SDL_CreateWindow("keyboard-routing", 1280, 720, SDL_WINDOW_HIDDEN);
  REQUIRE(window != nullptr);
  struct WindowGuard {
    SDL_Window* window;
    ~WindowGuard() { SDL_DestroyWindow(window); }
  } guard{window};
  for (const auto& theme : {view::Theme::console(), view::Theme::manifold()}) {
    DYNAMIC_SECTION("live controls " << theme.liveControls) {
      auto shell = app::Shell::create(
          {"standard"}, std::filesystem::path(CB_BINARY_DIR) / "keyboard-routing.conf");
      shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
        return test::loadVariant(name);
      });
      REQUIRE(shell->startGame("standard").has_value());
      auto ui = Ui::create(gpu().ctx, window, theme);
      REQUIRE(ui.has_value());
      auto& u = **ui;
      auto& session = *shell->session();
      auto& io = ImGui::GetIO();
      const auto frame = [&] {
        u.tick(0.25f);
        u.newFrame();
        (void)u.build(*shell, 60.0f);
        u.endFrame();
      };
      const auto settle = [&] {
        for (int i = 0; i < 4; ++i) frame();
      };
      const auto key = [&](SDL_Keycode code, SDL_Scancode scan, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window);
        event.key.key = code;
        event.key.scancode = scan;
        event.key.down = down;
        event.key.repeat = false;
        const bool consumed = u.processEvent(event);
        CHECK(consumed == u.capturesKeyboard());
        return consumed;
      };
      const auto press = [&](SDL_Keycode code, SDL_Scancode scan) {
        const bool consumed = key(code, scan, true);
        frame();
        (void)key(code, scan, false);
        settle();
        return consumed;
      };
      const auto mouse = [&](bool down) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = SDL_GetWindowID(window);
        event.motion.x = 220;
        event.motion.y = 658;
        (void)u.processEvent(event);
        event = {};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = SDL_GetWindowID(window);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down;
        event.button.x = 220;
        event.button.y = 658;
        (void)u.processEvent(event);
      };
      const auto idleKeys = [&] {
        REQUIRE_FALSE(io.WantTextInput);
        REQUIRE(GImGui->ActiveId == 0);
        for (const auto& [code, scan] :
             {std::pair{SDLK_U, SDL_SCANCODE_U},
              {SDLK_R, SDL_SCANCODE_R},
              {SDLK_F, SDL_SCANCODE_F},
              {SDLK_G, SDL_SCANCODE_G},
              {SDLK_LEFTBRACKET, SDL_SCANCODE_LEFTBRACKET},
              {SDLK_RIGHTBRACKET, SDL_SCANCODE_RIGHTBRACKET}}) {
          CAPTURE(code);
          CHECK_FALSE(press(code, scan));
        }
      };
      settle();
      REQUIRE(session.playMoveText("e2e4").has_value());
      settle();

      SECTION(
          "idle pointer and visible navigation focus pass app keys; Enter undoes once") {
        // Hold and release the view-mode button without changing the game.
        mouse(false);
        frame();
        mouse(true);
        frame();
        frame();  // capture flags describe the owner established by the previous frame
        CHECK(io.WantCaptureKeyboard);  // a held widget still owns keys
        CHECK(key(SDLK_U, SDL_SCANCODE_U, true));
        (void)key(SDLK_U, SDL_SCANCODE_U, false);
        mouse(false);
        settle();
        REQUIRE(session.game().plyCount() == 1);
        REQUIRE(io.NavActive);
        idleKeys();
        (void)press(SDLK_TAB, SDL_SCANCODE_TAB);
        REQUIRE(io.NavVisible);
        const ImGuiID first = GImGui->NavId;
        (void)press(SDLK_TAB, SDL_SCANCODE_TAB);
        CHECK(GImGui->NavId != first);
        idleKeys();
        ImGuiWindow* controls = ImGui::FindWindowByName("##controls");
        REQUIRE(controls != nullptr);
        const ImGuiID undo = controls->GetID("UNDO");
        for (int i = 0; i < 8 && GImGui->NavId != undo; ++i)
          (void)press(SDLK_TAB, SDL_SCANCODE_TAB);
        REQUIRE(GImGui->NavId == undo);
        (void)press(SDLK_RETURN, SDL_SCANCODE_RETURN);
        CHECK(session.game().plyCount() == 0);
        settle();
        CHECK(session.game().plyCount() == 0);
        CHECK_FALSE(press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE));
      }

      SECTION("the real F12 text field captures shortcuts and accepts SDL text") {
        (void)press(SDLK_F12, SDL_SCANCODE_F12);
        REQUIRE(io.WantTextInput);
        REQUIRE(GImGui->ActiveId != 0);
        REQUIRE(key(SDLK_U, SDL_SCANCODE_U, true));
        SDL_Event text{};
        text.type = SDL_EVENT_TEXT_INPUT;
        text.text.windowID = SDL_GetWindowID(window);
        text.text.text = "u";
        CHECK(u.processEvent(text));
        frame();
        (void)key(SDLK_U, SDL_SCANCODE_U, false);
        settle();
        const ImGuiInputTextState* input = ImGui::GetInputTextState(GImGui->ActiveId);
        REQUIRE(input != nullptr);
        CHECK(std::string(input->TextA.Data) == "u");
        CHECK(session.game().plyCount() == 1);
        (void)press(SDLK_F12, SDL_SCANCODE_F12);
        idleKeys();
      }

      SECTION("hot-seat Tab belongs to the board; menus restore navigation") {
        session.setHotSeat(true);
        settle();
        CHECK_FALSE(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        const ImGuiID focus = GImGui->NavId;
        CHECK_FALSE(press(SDLK_Y, SDL_SCANCODE_Y));
        CHECK_FALSE(press(SDLK_TAB, SDL_SCANCODE_TAB));
        CHECK_FALSE(press(SDLK_BACKSLASH, SDL_SCANCODE_BACKSLASH));
        CHECK(GImGui->NavId == focus);
        CHECK(session.game().plyCount() == 1);
        shell->pause();
        settle();
        CHECK(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        (void)press(SDLK_TAB, SDL_SCANCODE_TAB);
        CHECK(io.NavVisible);
        shell->resume();
        settle();
        CHECK_FALSE(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        (void)press(SDLK_F12, SDL_SCANCODE_F12);
        CHECK(io.WantTextInput);
        CHECK(press(SDLK_R, SDL_SCANCODE_R));
      }

      SECTION(
          "hot-seat move confirmation captures app keys and permits modal navigation") {
        session.setHotSeat(true);
        session.setConfirmMoves(true);
        REQUIRE(session.applyScript("click e7\nclick e5").has_value());
        REQUIRE(session.pendingMove().active);
        frame();
        CHECK(u.capturesKeyboard());
        CHECK(key(SDLK_U, SDL_SCANCODE_U, true));
        (void)key(SDLK_U, SDL_SCANCODE_U, false);
        CHECK(key(SDLK_R, SDL_SCANCODE_R, true));
        (void)key(SDLK_R, SDL_SCANCODE_R, false);
        settle();
        CHECK(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        CHECK(io.WantCaptureKeyboard);
        CHECK(press(SDLK_U, SDL_SCANCODE_U));
        CHECK(press(SDLK_R, SDL_SCANCODE_R));
        CHECK(press(SDLK_Y, SDL_SCANCODE_Y));
        CHECK(session.game().plyCount() == 1);
        auto* modal = ImGui::FindWindowByName("confirmmove");
        REQUIRE(modal != nullptr);
        const ImGuiID play = modal->GetID("PLAY");
        (void)press(SDLK_TAB, SDL_SCANCODE_TAB);
        for (int i = 0; i < 5 && GImGui->NavId != play; ++i)
          (void)press(SDLK_TAB, SDL_SCANCODE_TAB);
        REQUIRE(GImGui->NavId == play);
        (void)key(SDLK_RETURN, SDL_SCANCODE_RETURN, true);
        frame();
        CHECK_FALSE(session.pendingMove().active);
        CHECK(ImGui::GetTopMostPopupModal() == nullptr);
        CHECK_FALSE(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        (void)key(SDLK_RETURN, SDL_SCANCODE_RETURN, false);
        const ImGuiID closedFocus = GImGui->NavId;
        (void)key(SDLK_TAB, SDL_SCANCODE_TAB, true);
        frame();
        CHECK(GImGui->NavId == closedFocus);
        (void)key(SDLK_TAB, SDL_SCANCODE_TAB, false);
        frame();
        CHECK_FALSE(u.capturesKeyboard());
        CHECK_FALSE(press(SDLK_TAB, SDL_SCANCODE_TAB));
        CHECK(session.game().plyCount() == 2);
        CHECK_FALSE(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
      }

      SECTION("trailer authoring keys do not also activate HUD navigation") {
        app::AttractStep step;
        step.variant = "standard";
        step.moves = {"e2e4"};
        shell->startAttract({step});
        REQUIRE(shell->attracting());
        settle();
        CHECK_FALSE(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
        const bool paused = shell->trailerPaused();
        CHECK_FALSE(press(SDLK_SPACE, SDL_SCANCODE_SPACE));
        CHECK_FALSE(press(SDLK_LEFT, SDL_SCANCODE_LEFT));
        CHECK_FALSE(press(SDLK_RIGHT, SDL_SCANCODE_RIGHT));
        CHECK_FALSE(press(SDLK_RETURN, SDL_SCANCODE_RETURN));
        CHECK_FALSE(press(SDLK_DELETE, SDL_SCANCODE_DELETE));
        CHECK(shell->attracting());
        CHECK(shell->trailerPaused() == paused);
        CHECK_FALSE(io.NavVisible);
        shell->stopAttract();
        settle();
        CHECK(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
      }

      SECTION("promotion choices retain modal capture") {
        REQUIRE(session.loadFen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1").has_value());
        REQUIRE(session.applyScript("click b7\nclick b8").has_value());
        REQUIRE(session.pendingPromotion().active);
        frame();
        CHECK(u.capturesKeyboard());
        CHECK(key(SDLK_U, SDL_SCANCODE_U, true));
        (void)key(SDLK_U, SDL_SCANCODE_U, false);
        CHECK(key(SDLK_R, SDL_SCANCODE_R, true));
        (void)key(SDLK_R, SDL_SCANCODE_R, false);
        settle();
        CHECK(press(SDLK_U, SDL_SCANCODE_U));
        CHECK(press(SDLK_R, SDL_SCANCODE_R));
        CHECK(session.game().plyCount() == 0);
        auto* modal = ImGui::FindWindowByName("promotion");
        REQUIRE(modal != nullptr);
        const ImGuiID queen = modal->GetID("QUEEN");
        for (int i = 0; i < 7 && GImGui->NavId != queen; ++i)
          (void)press(SDLK_TAB, SDL_SCANCODE_TAB);
        REQUIRE(GImGui->NavId == queen);
        (void)press(SDLK_SPACE, SDL_SCANCODE_SPACE);
        CHECK_FALSE(session.pendingPromotion().active);
        CHECK(session.game().plyCount() == 1);
      }
    }
  }
  CHECK(gpu().ctx.validationErrorCount() == 0);
}

TEST_CASE("manifold settings footer actions remain clickable after responsive reflow",
          "[render][gpu][control-layout]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct SdlGuard {
    ~SdlGuard() { SDL_Quit(); }
  } sdl;
  for (const auto& size :
       {std::array{1920, 1080, 1}, std::array{1920, 1080, 3}, std::array{960, 540, 3}}) {
    for (bool reset : {false, true}) {
      CAPTURE(size, reset);
      SDL_Window* window =
          SDL_CreateWindow("responsive-footer", size[0], size[1], SDL_WINDOW_HIDDEN);
      REQUIRE(window != nullptr);
      struct WindowGuard {
        SDL_Window* window;
        ~WindowGuard() { SDL_DestroyWindow(window); }
      } guard{window};
      auto shell = app::Shell::create(
          {"standard"}, std::filesystem::path(CB_BINARY_DIR) / "responsive-footer.conf");
      shell->go(app::Screen::Settings);
      shell->settings().guiScale = static_cast<float>(size[2]);
      shell->settings().vsync = false;  // Reset has a measurable independent result.
      auto ui = Ui::create(gpu().ctx, window, view::Theme::manifold());
      REQUIRE(ui.has_value());
      REQUIRE((*ui)->setScale(static_cast<float>(size[2])).has_value());
      const auto frame = [&] {
        (*ui)->tick(1);
        (*ui)->newFrame();
        auto request = (*ui)->build(*shell, 60);
        (*ui)->endFrame();
        return request;
      };
      for (int i = 0; i < 4; ++i) (void)frame();
      ImGuiWindow* footer = nullptr;
      for (auto* candidate : GImGui->Windows)
        if (std::strstr(candidate->Name, "settingsfooter")) footer = candidate;
      REQUIRE(footer != nullptr);
      const ImGuiID id = footer->GetID(reset ? "RESET TO DEFAULTS" : "BACK");
      // Locate the actual real widget via its navigation rectangle, and use normal
      // scrolling to bring it into view. No assumed screen pixel or forced capture.
      ImGui::FocusWindow(footer);
      ImGui::SetFocusID(id, footer);
      (void)frame();
      const auto rect = [&] {
        ImRect r = footer->NavRectRel[ImGuiNavLayer_Main];
        r.Translate(footer->Pos);
        return r;
      };
      for (int i = 0; i < 3; ++i) {
        ImGui::ScrollToRect(footer, rect(), ImGuiScrollFlags_KeepVisibleCenterY);
        (void)frame();
      }
      const ImRect target = rect();
      REQUIRE(target.GetWidth() > 0);
      REQUIRE(target.GetHeight() > 0);
      const ImVec2 center = target.GetCenter();
      CHECK(center.x >= 0);
      CHECK(center.x < static_cast<float>(size[0]));
      CHECK(center.y >= 0);
      CHECK(center.y < static_cast<float>(size[1]));
      (*ui)->injectMouse(center.x, center.y, false);
      (void)frame();
      (*ui)->injectMouse(center.x, center.y, true);
      (void)frame();
      (*ui)->injectMouse(center.x, center.y, false);
      const auto request = frame();
      if (reset) {
        CHECK(request.applyScale);
        CHECK(request.settingsChanged);
        CHECK(shell->settings().vsync);
      } else {
        CHECK(shell->screen() == app::Screen::MainMenu);
      }
    }
  }
  CHECK(gpu().ctx.validationErrorCount() == 0);
}

TEST_CASE("the library Start action is clickable at three times UI scale",
          "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct SdlGuard {
    ~SdlGuard() { SDL_Quit(); }
  } sdl;
  SDL_Window* window = SDL_CreateWindow("library-footer", 1920, 1080, SDL_WINDOW_HIDDEN);
  REQUIRE(window != nullptr);
  struct WindowGuard {
    SDL_Window* window;
    ~WindowGuard() { SDL_DestroyWindow(window); }
  } guard{window};
  for (const auto& theme : {view::Theme::console(), view::Theme::manifold()}) {
    auto shell = app::Shell::create(
        {"standard"}, std::filesystem::path(CB_BINARY_DIR) / "library-footer.conf");
    shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
      return test::loadVariant(name);
    });
    shell->go(app::Screen::NewGame);
    auto ui = Ui::create(gpu().ctx, window, theme);
    REQUIRE(ui.has_value());
    REQUIRE((*ui)->setScale(3.0f).has_value());
    const auto frame = [&] {
      (*ui)->tick(1.0f);
      (*ui)->newFrame();
      auto request = (*ui)->build(*shell, 60.0f);
      (*ui)->endFrame();
      return request;
    };
    for (int i = 0; i < 3; ++i) (void)frame();
    (*ui)->injectMouse(1200.0f, 850.0f, false);
    (void)frame();
    (*ui)->injectMouse(1200.0f, 850.0f, true);
    (void)frame();
    (*ui)->injectMouse(1200.0f, 850.0f, false);
    const auto request = frame();
    CHECK(request.loadVariant == "standard");
  }
  CHECK(gpu().ctx.validationErrorCount() == 0);
}

TEST_CASE("the board screen's real ambient field is submitted behind the board",
          "[render][gpu]") {
  // The synthetic fill above proves the callback ordering; this proves the real wiring -
  // `Ui::build` fills the field's dedicated list and `Ui::recordBackground` submits it -
  // by comparing the field drawn before the board against the same field drawn after it.
  // With the field behind, the board's opaque geometry hides part of it; drawn last, the
  // field covers the board instead, so strictly more field pixels survive.
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);

  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  REQUIRE(SDL_Init(SDL_INIT_VIDEO));
  struct SdlGuard {
    ~SdlGuard() { SDL_Quit(); }
  } sdlGuard;
  constexpr int kSize = 256;
  SDL_Window* window =
      SDL_CreateWindow("field-test", kSize, kSize * 3 / 4, SDL_WINDOW_HIDDEN);
  REQUIRE(window != nullptr);
  struct WinGuard {
    SDL_Window* window;
    ~WinGuard() { SDL_DestroyWindow(window); }
  } winGuard{window};

  auto shell = app::Shell::create(
      {"standard"}, std::filesystem::path(CB_BINARY_DIR) / "field-test.conf");
  REQUIRE(shell != nullptr);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    try {
      return test::loadVariant(name);
    } catch (const std::exception& e) {
      return fail(ErrorCode::ParseError, e.what());
    }
  });
  REQUIRE(shell->startGame("standard").has_value());

  auto ui = Ui::create(gpu().ctx, window, view::Theme::manifold());
  REQUIRE(ui.has_value());
  Ui& u = **ui;

  const VariantSpec& v = shell->session()->variant();
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));
  const InstanceSet set =
      BoardRenderer{}.buildInstances(shell->session()->snapshot(), cfg);

  auto target = OffscreenTarget::create(gpu().ctx, kSize, kSize * 3 / 4);
  REQUIRE(target.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(renderer.has_value());

  const auto read = [&](const std::function<void(VkCommandBuffer)>& overlay,
                        const std::function<void(VkCommandBuffer)>& background) {
    REQUIRE(renderer->render(*target, set, cam, overlay, background).has_value());
    auto px = target->readPixels();
    REQUIRE(px.has_value());
    Image img;
    img.width = target->width();
    img.height = target->height();
    img.rgba = std::move(*px);
    return img;
  };
  const auto diffCount = [](const Image& a, const Image& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
      if (std::memcmp(&a.rgba[i], &b.rgba[i], 4) != 0) ++n;
    }
    return n;
  };

  (void)gpu().ctx.takeValidationMessages();
  Image none{}, bestNone{}, bestEarly{}, bestLate{};
  std::size_t bestGain = 0;
  std::size_t fieldPixels = 0;
  // A handful of field frames: bodies orbit, so at least one puts a stroke over the
  // board, which is the case the fix has to get right.
  for (int frame = 0; frame < 24; ++frame) {
    u.tick(0.15f);
    u.newFrame();
    (void)u.build(*shell, 60.0f);
    u.endFrame();
    none = read({}, {});
    const Image early = read({}, [&](VkCommandBuffer c) { u.recordBackground(c); });
    const Image late = read([&](VkCommandBuffer c) { u.recordBackground(c); }, {});
    const std::size_t earlyDiff = diffCount(early, none);
    const std::size_t lateDiff = diffCount(late, none);
    if (earlyDiff > 0) fieldPixels = earlyDiff;
    if (lateDiff > earlyDiff && lateDiff - earlyDiff > bestGain) {
      bestGain = lateDiff - earlyDiff;
      bestNone = none;
      bestEarly = early;
      bestLate = late;
    }
  }

  // The field reaches the frame at all through `recordBackground`...
  REQUIRE(fieldPixels > 0);
  // ...and the board occludes it: drawn first it loses pixels to the board, drawn last it
  // does not.
  REQUIRE(bestGain > 0);
  CHECK(diffCount(bestEarly, none) < diffCount(bestLate, none));

  CAPTURE(gpu().ctx.takeValidationMessages());
  REQUIRE(gpu().ctx.validationErrorCount() == 0);
  // Three frames of the same moment, for a human: the board alone, the field drawn behind
  // it (the fix), and the field drawn over it (the bug). `field-behind` must show the
  // board intact where `field-over` shows the wireframe lying across it.
  REQUIRE(writePpm(bestNone, capturePath("field-none")).has_value());
  REQUIRE(writePpm(bestEarly, capturePath("field-behind")).has_value());
  REQUIRE(writePpm(bestLate, capturePath("field-over")).has_value());
}

#endif  // CB_HAVE_IMGUI

#endif  // CB_HAVE_VULKAN
