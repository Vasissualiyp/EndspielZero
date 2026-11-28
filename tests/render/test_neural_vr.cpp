// SPDX-License-Identifier: GPL-3.0-or-later
//
// M25.5 the neural VR, render side: the rack's bays as authored, the boards as light, the
// stand-in slab and its neural code, and the boards' glide through a VR relayout.
// M25.6: the bridges are quiet guides, a shape is lit like the flat layout, and a shape
// never rides the lattice layout's glide.
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_VULKAN
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <set>
#include <string_view>

#include "app/session.hpp"
#include "render/board_renderer.hpp"
#include "render/lab_rig.hpp"
#include "render/neural_vr.hpp"
#include "render/overture_scene.hpp"
#include "render/play_surface.hpp"
#include "render/slab_skin.hpp"
#include "support/variants.hpp"
#include "view/neural_vr.hpp"

using namespace cb;
using namespace cb::render;

namespace {
LabScene shippedLab() {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  std::vector<std::uint32_t> ranges(lab->fileMeshes().size());
  std::iota(ranges.begin(), ranges.end(), 0u);
  lab->setFileRanges(ranges);
  return std::move(*lab);
}
std::size_t labCount(const InstanceSet& set) {
  std::size_t n = 0;
  for (const auto& b : set.labBatches) n += b.count;
  return n;
}
}  // namespace

TEST_CASE("neural vr: the authored rack has the bays the VR switch counts",
          "[render][vr]") {
  const LabScene lab = shippedLab();
  CHECK(rackBayCount(lab.doc()) == view::kRackBays);
}

TEST_CASE("neural vr: the boards are light - no slab under them, every cell a hologram",
          "[render][vr]") {
  for (const char* name : {"standard", "hyper4", "t6"}) {
    CAPTURE(name);
    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session);
    auto& s = **session;
    BoardRenderer renderer;
    renderer.setTheme(view::Theme::manifold());
    BoardOptions o;
    renderer.setOptions(o);
    const auto solid = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
    o.vrLight = true;
    renderer.setOptions(o);
    const auto light = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
    const auto count = [](const InstanceSet& set, auto pred) {
      return std::count_if(set.instances.begin(), set.instances.end(), pred);
    };
    const auto led = [](const Instance& i) { return static_cast<int>(i.material) == 7; };
    const auto plinth = [](const Instance& i) { return i.edgeMask == 15.0f; };
    const auto lightCell = [](const Instance& i) {
      return static_cast<int>(i.material) == 4 && i.panelSignal[2] == -1.0f &&
             i.fxCell > 0 && static_cast<int>(i.fxCell) % 2 == 0;
    };
    const auto cells = static_cast<long>(s.placements().size());
    CHECK(count(solid, led) == cells);
    CHECK(count(solid, plinth) > 0);
    CHECK(count(light, led) == 0);
    CHECK(count(light, plinth) == 0);
    CHECK(count(light, lightCell) == cells);
    for (const auto& i : light.instances)
      if (lightCell(i)) CHECK(i.color[3] == kVrLightOpacity);
    // Cell, seam and piece identities survive the light path. Only the plinth leaves.
    CHECK(light.instances.size() ==
          solid.instances.size() - static_cast<std::size_t>(count(solid, plinth)));
    if (std::string_view(name) == "t6") {
      CHECK(cells == 4096);
      CHECK(light.batches[static_cast<std::size_t>(Archetype::Cell)].count == 8192);
      CHECK(light.instances.size() == 8198);
    }
  }
}

TEST_CASE("neural vr: the stand-in is one standard slab, and its code sits on its face",
          "[render][vr]") {
  CHECK(view::deployedSlabCount(vrStandIn()) == 1);
  CHECK(vrStandIn().size() == 64);
  const LabScene lab = shippedLab();
  const auto layout = rigLayout(lab, vrStandIn());
  REQUIRE(layout);
  REQUIRE(layout->slabs.size() == 1);
  const auto theme = view::Theme::manifold();
  const auto hung = rigPoses(*layout, RigPhase::Rest, 0, 1);
  REQUIRE(hung[0].clamped);
  int lit = 0;
  const int n = view::kNeuralCodeModules;
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) lit += view::neuralCodeModule(i, j, n, 1.5f) ? 1 : 0;
  InstanceSet set;
  const std::size_t before = labCount(set);
  CHECK(appendNeuralCode(set, *layout, hung, 1.0f, 1.5f, theme) ==
        static_cast<std::size_t>(lit));
  CHECK(labCount(set) == before + static_cast<std::size_t>(lit));
  const auto& slab = layout->slabs[0];
  for (const auto& m : set.lab) {
    CHECK(static_cast<int>(m.material) == 5);  // glow: the LEDs emit
    CHECK(m.center[0] > slab.minX);
    CHECK(m.center[0] < slab.maxX);
    CHECK(m.center[1] > slab.minY);
    CHECK(m.center[1] < slab.maxY);
    CHECK(m.center[2] > hung[0].centre.z);  // on the face, not the back
    CHECK(m.center[2] < hung[0].centre.z + 0.5f);
  }
  // Dim code is dimmer light; no code and a slab still in its bay draw nothing.
  InstanceSet dim;
  (void)appendNeuralCode(dim, *layout, hung, 0.5f, 1.5f, theme);
  REQUIRE_FALSE(dim.lab.empty());
  CHECK(dim.lab[0].color[0] + dim.lab[0].color[1] + dim.lab[0].color[2] <
        set.lab[0].color[0] + set.lab[0].color[1] + set.lab[0].color[2]);
  InstanceSet none;
  CHECK(appendNeuralCode(none, *layout, hung, 0.0f, 1.5f, theme) == 0);
  CHECK(appendNeuralCode(none, *layout, rigPoses(*layout, RigPhase::Deploy, 0, 1), 1.0f,
                         1.5f, theme) == 0);
}

TEST_CASE("neural vr: pieces stay holograms with either hardware palette",
          "[render][vr]") {
  for (const char* name : {"standard", "torus"}) {
    CAPTURE(name);
    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session);
    BoardRenderer renderer;
    renderer.setTheme(view::Theme::console());
    BoardOptions o;
    o.vrLight = true;
    o.surface = std::string_view(name) == "torus";
    renderer.setOptions(o);
    const auto set = renderer.buildInstances(
        (*session)->snapshot(), (*session)->viewConfig(), &(*session)->seams());
    int pieces = 0;
    for (const auto& i : set.instances)
      if (static_cast<int>(i.fxCell) % 2 == 1) {
        CHECK(static_cast<int>(i.material) == 4);
        ++pieces;
      }
    CHECK(pieces > 0);
  }
}

TEST_CASE("neural vr: a VR relayout carries every cell's light by its own offset",
          "[render][vr]") {
  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session);
  auto& s = **session;
  BoardRenderer renderer;
  renderer.setTheme(view::Theme::manifold());
  BoardOptions o;
  o.vrLight = true;
  renderer.setOptions(o);
  const auto still = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
  auto moved = still;
  std::vector<view::Vec3> offset(s.snapshot().cellCount());
  for (std::size_t c = 0; c < offset.size(); ++c)
    offset[c] = {static_cast<float>(c % 3), 0.5f, static_cast<float>(c % 5) * .25f};
  carryVr(moved, s.placements(), offset);
  REQUIRE(moved.instances.size() == still.instances.size());
  int carried = 0;
  for (std::size_t k = 0; k < still.instances.size(); ++k) {
    const auto& a = still.instances[k];
    const auto& b = moved.instances[k];
    if (a.fxCell <= 0) continue;
    const auto cell = static_cast<std::size_t>(a.fxCell) / 2 - 1;
    CHECK(b.center[0] == a.center[0] + offset[cell].x);
    CHECK(b.center[1] == a.center[1] + offset[cell].y);
    CHECK(b.center[2] == a.center[2] + offset[cell].z);
    ++carried;
  }
  CHECK(carried >= 64 + 32);  // every cell and every piece
  // Instances with no cell of their own (shadows) follow the nearest cell.
  REQUIRE(still.shadows.size() == moved.shadows.size());
  for (std::size_t k = 0; k < still.shadows.size(); ++k)
    CHECK(moved.shadows[k].center[1] == still.shadows[k].center[1] + 0.5f);
}
TEST_CASE("neural vr: shaped boards are light and their mesh follows the relayout",
          "[render][vr]") {
  for (const char* name : {"torus", "klein", "hyper4", "torus3d"}) {
    CAPTURE(name);
    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session);
    auto& s = **session;
    BoardRenderer renderer;
    renderer.setTheme(view::Theme::manifold());
    BoardOptions o;
    o.vrLight = true;
    o.surface = true;
    renderer.setOptions(o);
    const auto still = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
    REQUIRE_FALSE(still.surfaceVertices.empty());
    REQUIRE_FALSE(still.surfaceDraws.empty());
    for (const auto& draw : still.surfaceDraws) {
      if (draw.indexCount > 0)
        CHECK(static_cast<int>(still.instances[draw.instance].material) == 4);
    }
    auto moved = still;
    std::vector<view::Vec3> offset(s.snapshot().cellCount(), {2, 3, 4});
    carryVr(moved, s.placements(), offset);
    for (std::size_t k = 0; k < still.surfaceVertices.size(); ++k) {
      const auto& a = still.surfaceVertices[k];
      const auto& b = moved.surfaceVertices[k];
      REQUIRE(b.pos[0] == a.pos[0] + 2);
      REQUIRE(b.pos[1] == a.pos[1] + 3);
      REQUIRE(b.pos[2] == a.pos[2] + 4);
      for (int axis = 0; axis < 3; ++axis) CHECK(b.normal[axis] == a.normal[axis]);
    }
    // The mesh identity instance stays fixed: moving it as well carries twice.
    const auto& identity = moved.instances[moved.surfaceInstance];
    CHECK(identity.center[0] == still.instances[still.surfaceInstance].center[0]);
  }
}
TEST_CASE("neural vr: the implant picture fills the view without perspective",
          "[render][vr]") {
  for (const char* name : {"standard", "hyper4"}) {
    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session);
    (*session)->setFlatView(true);
    const auto b = view::boundsOf((*session)->placements());
    const auto cam = vrPictureCamera(b, 16.0f / 9.0f);
    REQUIRE(cam.orthographic);
    const auto a = cam.project({b.minX - .5f, b.minY - .5f, 0}, 16.0f / 9.0f, 1280, 720);
    const auto z = cam.project({b.maxX + .5f, b.maxY + .5f, 0}, 16.0f / 9.0f, 1280, 720);
    REQUIRE(a.visible);
    REQUIRE(z.visible);
    CHECK(std::abs(z.y - a.y) > 720 * .80f);
    CHECK(std::abs(z.y - a.y) < 720 * .90f);
  }
}
TEST_CASE("neural vr: bridges are light streaks without cylindrical hardware",
          "[render][vr]") {
  auto session = app::Session::create(test::loadVariant("t6"));
  REQUIRE(session);
  auto& s = **session;
  const auto bridges = view::lightBridges(s.variant(), s.viewConfig(), s.placements(),
                                          view::Theme::manifold());
  REQUIRE_FALSE(bridges.empty());
  InstanceSet set;
  const auto theme = view::Theme::manifold();
  CHECK(appendVrBridges(set, bridges, 1, theme) == bridges.size() * kBridgeSegments);
  CHECK(set.labBatches[static_cast<std::size_t>(LabShape::Cylinder)].count == 0);
  CHECK(set.labFileBatches.empty());
  for (const auto& i : set.lab) CHECK(static_cast<int>(i.material) == 5);
  InstanceSet dark;
  CHECK(appendVrBridges(dark, bridges, 0, theme) == 0);
}

namespace {
float peak(const float* rgb) {
  return std::max({rgb[0], rgb[1], rgb[2]});
}
float luma(const float* rgb) {
  return .2126f * rgb[0] + .7152f * rgb[1] + .0722f * rgb[2];
}
/// The two thin axes of a streak's box: its cross-section.
float crossSection(const Instance& i) {
  float s[3]{i.scale[0], i.scale[1], i.scale[2]};
  std::sort(s, s + 3);
  return s[0] * s[1];
}
}  // namespace

TEST_CASE("neural vr M25.6: bridges are faint guides under the cells, all still drawn",
          "[render][vr][quiet]") {
  auto session = app::Session::create(test::loadVariant("t6"));
  REQUIRE(session);
  auto& s = **session;
  const auto theme = view::Theme::manifold();
  const auto bridges =
      view::lightBridges(s.variant(), s.viewConfig(), s.placements(), theme);
  REQUIRE_FALSE(bridges.empty());
  // The dimmest light tile the implant draws for this game.
  BoardRenderer renderer;
  renderer.setTheme(theme);
  BoardOptions o;
  o.vrLight = true;
  renderer.setOptions(o);
  const auto board = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
  float dimmest = 1e9f;
  for (const auto& i : board.instances)
    if (static_cast<int>(i.material) == 4 && i.fxCell > 0 &&
        static_cast<int>(i.fxCell) % 2 == 0)
      dimmest = std::min(dimmest, peak(i.color));
  REQUIRE(dimmest < 1e9f);
  CHECK(peak(vrLightColor(theme.boardDark, theme).array().data()) == dimmest);

  const view::Vec3 eye = s.camera().eye();
  InstanceSet set;
  REQUIRE(appendVrBridges(set, bridges, 1, theme, eye) ==
          bridges.size() * kBridgeSegments);
  // Thinner than the physical rig's glow core.
  const auto core =
      cylinderBetween({0, 0, 0}, {1, 0, 0}, kBridgeCoreMetres * kCellsPerMetre);
  float nearest = 1e9f, farthest = 0;
  float nearGain = 0, farGain = 0;
  for (std::size_t k = 0; k < set.lab.size(); ++k) {
    const auto& streak = set.lab[k];
    CHECK(crossSection(streak) <= .25f * crossSection(core) + 1e-9f);
    // Capped well below the cell emission: never more than half the dimmest tile.
    CHECK(peak(streak.color) <= .5f * dimmest);
    // ...and fading with depth: the farthest streak is the faintest relative to its
    // own seam colour.
    const auto& c = bridges[k / kBridgeSegments].color;
    const float base = std::max({c.r, c.g, c.b});
    const float gain = peak(streak.color) / base;
    const view::Vec3 at{streak.center[0], streak.center[1], streak.center[2]};
    const float d = view::length(at - eye);
    if (d < nearest) nearest = d, nearGain = gain;
    if (d > farthest) farthest = d, farGain = gain;
  }
  CHECK(farGain < .5f * nearGain);
  // Without an eye there is no depth to fade by; the cap still holds.
  InstanceSet flat;
  REQUIRE(appendVrBridges(flat, bridges, 1, theme) == bridges.size() * kBridgeSegments);
  for (const auto& streak : flat.lab) CHECK(peak(streak.color) <= .5f * dimmest);
}

TEST_CASE("neural vr M25.6: a shape in VR is lit like the flat layout's light tiles",
          "[render][vr][quiet]") {
  const auto theme = view::Theme::manifold();
  for (const char* name : {"torus", "hyper4"}) {
    CAPTURE(name);
    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session);
    auto& s = **session;
    BoardRenderer renderer;
    renderer.setTheme(theme);
    BoardOptions o;
    o.vrLight = true;
    renderer.setOptions(o);
    const auto lattice =
        renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
    std::vector<float> tiles;
    for (const auto& i : lattice.instances)
      if (static_cast<int>(i.material) == 4 && i.fxCell > 0 &&
          static_cast<int>(i.fxCell) % 2 == 0)
        tiles.push_back(luma(i.color));
    REQUIRE_FALSE(tiles.empty());
    o.surface = true;
    if (std::string_view(name) == "hyper4") o.stackedLight = kStackedLightOpacity;
    renderer.setOptions(o);
    const auto shape = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
    // The faces (positive tag) of every light patch.
    std::vector<float> faces;
    for (const auto& v : shape.surfaceVertices)
      if (v.panelCoord[2] > 0.0f) {
        faces.push_back(luma(v.color));
        // Light at the tiles' opacity; a seam rail mixes toward its opaque colour.
        CHECK(v.color[3] >= kVrLightOpacity);
      }
    REQUIRE_FALSE(faces.empty());
    const auto median = [](std::vector<float> x) {
      std::nth_element(x.begin(), x.begin() + static_cast<long>(x.size() / 2), x.end());
      return x[x.size() / 2];
    };
    // The same light as the flat layout's tiles, not the LED fill they emit through.
    CHECK(median(faces) >= .9f * median(tiles));
    CHECK(median(faces) <= 1.1f * median(tiles));
  }
}

TEST_CASE("neural vr M25.6: a drawn shape does not ride the lattice layout's glide",
          "[render][vr][quiet]") {
  BoardOptions o;
  o.vrLight = true;
  for (const char* name : {"standard", "torus", "hyper4", "t6"}) {
    CAPTURE(name);
    const auto v = test::loadVariant(name);
    o.surface = false;
    o.flat = false;
    CHECK(vrCarriesLayout(o, v));
    o.surface = true;
    // A shape is placed by its own sampling, never by the layout: nothing to carry.
    CHECK(vrCarriesLayout(o, v) == !hasPlaySurface(v));
    o.flat = true;  // the 2-D picture wins over SHAPE, as in the renderer
    CHECK(vrCarriesLayout(o, v));
  }
}

TEST_CASE("neural vr M25.6: pieces on a shape ride their own cell's carry",
          "[render][vr][quiet]") {
  // M25.6 item 3: pieces carry by their cell exactly as the mesh does, so the forced-VR
  // torus's pieces cannot leave their squares (a non-uniform offset, unlike M25.5's).
  auto session = app::Session::create(test::loadVariant("torus"));
  REQUIRE(session);
  auto& s = **session;
  BoardRenderer renderer;
  renderer.setTheme(view::Theme::manifold());
  BoardOptions o;
  o.vrLight = true;
  o.surface = true;
  renderer.setOptions(o);
  const auto still = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
  std::vector<view::Vec3> offset(s.snapshot().cellCount());
  for (std::size_t c = 0; c < offset.size(); ++c)
    offset[c] = {static_cast<float>(c % 7), static_cast<float>(c % 3) * .5f, 0.25f};
  auto moved = still;
  carryVr(moved, s.placements(), offset);
  std::vector<int> seen(offset.size(), 0);
  for (std::size_t k = 0; k < still.surfaceVertices.size(); ++k) {
    const float tag = std::abs(still.surfaceVertices[k].panelCoord[2]);
    if (tag < 3.0f) continue;
    const auto cell = static_cast<std::size_t>((tag - 1.0f) * .5f) - 1;
    CHECK(std::abs(moved.surfaceVertices[k].pos[0] - still.surfaceVertices[k].pos[0] -
                   offset[cell].x) < 1e-4f);
    seen[cell] = 1;
  }
  int pieces = 0;
  for (std::size_t k = 0; k < still.instances.size(); ++k) {
    const auto& a = still.instances[k];
    if (a.fxCell <= 0 || static_cast<int>(a.fxCell) % 2 == 0) continue;
    const auto cell = static_cast<std::size_t>(a.fxCell) / 2 - 1;
    REQUIRE(seen[cell] == 1);
    CHECK(moved.instances[k].center[0] == a.center[0] + offset[cell].x);
    CHECK(moved.instances[k].center[1] == a.center[1] + offset[cell].y);
    CHECK(moved.instances[k].center[2] == a.center[2] + offset[cell].z);
    ++pieces;
  }
  CHECK(pieces == 32);
}
TEST_CASE("neural vr: opaque lattice tiles draw only their original top face",
          "[render][vr][vrlightmesh]") {
  const auto meshes = MeshLibrary::build();
  const auto cell = static_cast<std::size_t>(Archetype::Cell);
  BoardOptions o;
  InstanceSet set;
  CHECK(meshes.ranges[cell].indexCount == 36);
  for (const bool light : {false, true}) {
    o.vrLight = light;
    for (const bool surface : {false, true}) {
      set.surfaceIndices =
          surface ? std::vector<std::uint32_t>{0, 1, 2} : std::vector<std::uint32_t>{};
      for (std::size_t s = 0; s < kShapeSlots; ++s) {
        const auto& selected = opaqueBoardRange(meshes, s, o, set);
        const bool sheet = light && !surface && s == cell;
        CHECK(selected.firstIndex == meshes.ranges[s].firstIndex);
        CHECK(selected.vertexOffset == meshes.ranges[s].vertexOffset);
        CHECK(selected.indexCount == (sheet ? 6 : meshes.ranges[s].indexCount));
        if (!sheet) CHECK(&selected == &meshes.ranges[s]);
      }
    }
  }
  o.vrLight = true;
  set.surfaceIndices.clear();
  const auto& range = opaqueBoardRange(meshes, cell, o, set);
  REQUIRE(range.indexCount == 6);
  std::set<std::uint16_t> unique;
  for (std::uint32_t i = 0; i < range.indexCount; ++i) {
    const auto index = meshes.indices[range.firstIndex + i];
    unique.insert(index);
    const auto& v = meshes.vertices[static_cast<std::size_t>(range.vertexOffset) + index];
    CHECK(std::abs(v.pos[0]) == .46f);
    CHECK(std::abs(v.pos[1]) == .46f);
    CHECK(v.pos[2] == .055f);
    CHECK(v.normal[0] == 0);
    CHECK(v.normal[1] == 0);
    CHECK(v.normal[2] == 1);
  }
  CHECK(unique.size() == 4);
  for (std::uint32_t i = 0; i < range.indexCount; i += 3) {
    const auto point = [&](std::uint32_t j) {
      const auto& v = meshes.vertices[static_cast<std::size_t>(range.vertexOffset) +
                                      meshes.indices[range.firstIndex + j]];
      return view::Vec3{v.pos[0], v.pos[1], v.pos[2]};
    };
    CHECK(view::cross(point(i + 1) - point(i), point(i + 2) - point(i)).z > .84f);
  }
}

TEST_CASE("neural vr CPU profile", "[.vrprofile]") {
  auto session = app::Session::create(test::loadVariant("t6"));
  REQUIRE(session);
  auto& s = **session;
  BoardRenderer r;
  r.setTheme(view::Theme::manifold());
  BoardOptions o;
  o.vrLight = true;
  r.setOptions(o);
  auto stamp = std::chrono::steady_clock::now();
  const auto elapsed = [&](const char* label) {
    const auto now = std::chrono::steady_clock::now();
    std::printf("%s %.3f ms\n", label,
                std::chrono::duration<double, std::milli>(now - stamp).count());
    stamp = now;
  };
  const auto n = view::deployedSlabCount(s.placements());
  elapsed("slabs");
  const auto b =
      view::lightBridges(s.variant(), s.viewConfig(), s.placements(), r.theme());
  elapsed("bridges");
  auto instances = r.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
  elapsed("instances");
  const auto count = appendBridges(instances, b, 1);
  elapsed("append bridges");
  std::printf("slabs %d bridges %zu segments %zu instances %zu\n", n, b.size(), count,
              instances.instances.size());
}
#endif
