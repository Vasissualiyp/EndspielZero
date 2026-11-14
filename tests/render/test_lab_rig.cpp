// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_VULKAN
#include <cstring>
#include <numeric>
#include <random>

#include "app/session.hpp"
#include "render/lab_rig.hpp"
#include "support/variants.hpp"
#include "view/layout.hpp"

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
std::vector<view::Placement> placementsOf(const char* name) {
  const auto v = test::loadVariant(name);
  return view::layout(v.dims, view::ViewConfig::forBoard(v.dims));
}
std::vector<view::Placement> grid2x2() {
  std::vector<view::Placement> p;
  for (std::uint32_t s = 0; s < 4; ++s)
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x)
        p.push_back({static_cast<CellId>(p.size()),
                     static_cast<float>(x) + 7.f * static_cast<float>(s % 2),
                     static_cast<float>(y) + 7.f * static_cast<float>(s / 2), 0, s});
  return p;
}
/// Overlap deeper than 1 mm (0.02 cells) on every axis.
bool overlaps(const view::Bounds& a, const view::Bounds& b) {
  constexpr float tol = .02f;
  return std::min(a.maxX, b.maxX) - std::max(a.minX, b.minX) > tol &&
         std::min(a.maxY, b.maxY) - std::max(a.minY, b.minY) > tol &&
         std::min(a.maxZ, b.maxZ) - std::max(a.minZ, b.minZ) > tol;
}
/// Joints by design: a yoke is bolted to its slab's back and its mast's clamp; the inner
/// tube slides in the outer one; a carriage carries its mast and bridges.
bool joint(const RigPiece& a, const RigPiece& b) {
  using K = RigPiece::Kind;
  const auto is = [&](K x, K y) {
    return (a.kind == x && b.kind == y) || (a.kind == y && b.kind == x);
  };
  // Carriages on one pair of rails share its bridges.
  if (is(K::Bridge, K::Bridge) || is(K::Carriage, K::Bridge)) return true;
  if (a.group == b.group && a.group >= 0) {
    if (is(K::MastInner, K::MastOuter) || is(K::Carriage, K::MastOuter) ||
        is(K::Yoke, K::MastInner) || is(K::Clamp, K::MastInner) || is(K::Clamp, K::Yoke))
      return true;
  }
  return a.slab == b.slab && a.slab >= 0 && is(K::Yoke, K::Back);
}
}  // namespace

TEST_CASE("deployed slab backs, yokes and masts clear each other and the cells",
          "[render][rig]") {
  const auto lab = shippedLab();
  for (const auto& [name, placements] :
       std::vector<std::pair<std::string, std::vector<view::Placement>>>{
           {"standard", placementsOf("standard")},
           {"cube5", placementsOf("cube5")},
           {"grid2x2", grid2x2()}}) {
    CAPTURE(name);
    const auto layout = rigLayout(lab, placements);
    REQUIRE(layout);
    CHECK(layout->travels);
    const auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
    InstanceSet set;
    const auto pieces = appendRig(set, lab, *layout, poses);
    REQUIRE_FALSE(pieces.empty());
    CHECK(set.lab.size() >= pieces.size());
    const auto half = BoardRenderer::cellHalfExtent();
    for (std::size_t i = 0; i < pieces.size(); ++i) {
      CAPTURE(i, static_cast<int>(pieces[i].kind), pieces[i].group, pieces[i].slab);
      if (pieces[i].kind == RigPiece::Kind::Back || pieces[i].kind == RigPiece::Kind::Yoke ||
          pieces[i].kind == RigPiece::Kind::MastInner ||
          pieces[i].kind == RigPiece::Kind::MastOuter) {
        for (const auto& c : placements) {
          const view::Bounds cell{c.x - half.x, c.y - half.y, c.z - half.z,
                                  c.x + half.x, c.y + half.y, c.z + half.z};
          CHECK_FALSE(overlaps(pieces[i].bounds, cell));
        }
      }
      for (std::size_t j = i + 1; j < pieces.size(); ++j) {
        if (joint(pieces[i], pieces[j])) continue;
        CAPTURE(j, static_cast<int>(pieces[j].kind), pieces[j].group, pieces[j].slab);
        CHECK_FALSE(overlaps(pieces[i].bounds, pieces[j].bounds));
      }
    }
  }
}

TEST_CASE("the rig groups shipped layouts and keeps standard at lab scale",
          "[render][rig]") {
  const auto lab = shippedLab();
  const auto standard = rigLayout(lab, placementsOf("standard"));
  REQUIRE(standard);
  CHECK(standard->xform.labScale == 1.0f);
  CHECK(standard->groups.size() == 1);
  CHECK(standard->lastLock == Catch::Approx(1.62f));
  const auto cube5 = rigLayout(lab, placementsOf("cube5"));
  REQUIRE(cube5);
  CHECK(cube5->groups.size() == 1);
  CHECK(cube5->slabs.size() == 5);
  // The room contains the deployed board.
  CHECK(standard->room.minZ < -1);
  CHECK(standard->room.maxZ > 20);
}

TEST_CASE("a travelling slab is the board, and its bay is empty while it is out",
          "[render][rig]") {
  const auto lab = shippedLab();
  const auto placements = placementsOf("standard");
  const auto layout = rigLayout(lab, placements);
  REQUIRE(layout);
  const float lock = layout->lastLock;
  const auto before = rigPoses(*layout, RigPhase::Deploy, lock - .002f, 1);
  const auto after = rigPoses(*layout, RigPhase::Deploy, lock + .002f, 1);
  CHECK_FALSE(slabsShown(*layout, before)[0]);
  CHECK(slabsShown(*layout, after)[0]);
  // No separate stand-in: the board's own instances ride the slab (carryBoard), so the
  // lock swaps nothing. The rig draws the slab's back under them either way.
  for (const auto& poses : {before, after}) {
    InstanceSet set;
    const auto pieces = appendRig(set, lab, *layout, poses);
    CHECK(std::any_of(pieces.begin(), pieces.end(), [](const RigPiece& p) {
      return p.kind == RigPiece::Kind::Back;
    }));
  }
  // The parked bay it came from is not drawn while it is out.
  const auto* bay = lab.part("magazine.slab.0");
  REQUIRE(bay);
  CHECK(hiddenInGame(*bay, *layout, before));
  CHECK_FALSE(hiddenInGame(*bay, *layout, rigPoses(*layout, RigPhase::Deploy, 0, 1)));
  CHECK(hiddenInGame(*lab.part("gantry.carriage.0"), *layout, before));
}

TEST_CASE("a travelling slab carries the board itself, so the lock is no swap",
          "[render][rig]") {
  const auto lab = shippedLab();
  for (const char* name : {"standard", "cube5"}) {
    CAPTURE(name);
    const auto placements = placementsOf(name);
    const auto layout = rigLayout(lab, placements);
    REQUIRE(layout);
    // One cell, the piece standing on it and its shadow, per slab (its first cell).
    InstanceSet set;
    std::vector<std::uint32_t> slabOf;
    for (std::uint32_t s = 0; s < layout->slabs.size(); ++s) {
      const auto p = std::find_if(placements.begin(), placements.end(), [&](const auto& q) {
        return layout->cellSlab[q.cell] == s;
      });
      REQUIRE(p != placements.end());
      Instance cell;
      cell.center[0] = p->x;
      cell.center[1] = p->y;
      cell.center[2] = p->z;
      cell.scale[0] = cell.scale[1] = cell.scale[2] = 1;
      Instance piece = cell;
      piece.center[2] += .8f;
      set.instances.push_back(cell);
      set.instances.push_back(piece);
      set.shadows.push_back(cell);
      slabOf.insert(slabOf.end(), {s, s});
    }
    const float lock = layout->lastLock;
    // Clamped: the board is exactly where the game put it.
    auto rest = set;
    carryBoard(rest, *layout, rigPoses(*layout, RigPhase::Deploy, lock + .01f, 1));
    for (std::size_t i = 0; i < set.instances.size(); ++i)
      CHECK(std::memcmp(&rest.instances[i], &set.instances[i], sizeof(Instance)) == 0);
    // Lowering (face up): every instance moves with its own slab, rigidly.
    const float departure = layout->paths[0].departure;
    const auto lowering = rigPoses(*layout, RigPhase::Deploy, departure + 1.2f, 1);
    auto moved = set;
    carryBoard(moved, *layout, lowering);
    for (std::size_t i = 0; i < set.instances.size(); ++i) {
      const auto s = slabOf[i];
      const auto d = lowering[s].centre - layout->paths[s].hangCentre;
      CHECK(moved.instances[i].center[0] == Catch::Approx(set.instances[i].center[0] + d.x));
      CHECK(moved.instances[i].center[1] == Catch::Approx(set.instances[i].center[1] + d.y));
      CHECK(moved.instances[i].center[2] == Catch::Approx(set.instances[i].center[2] + d.z));
      CHECK(moved.instances[i].scale[0] == Catch::Approx(1));
    }
    CHECK(moved.shadows[0].center[2] == Catch::Approx(moved.instances[0].center[2]));
    // Tilting: the slab's orientation composes onto each instance's own.
    const auto tilting = rigPoses(*layout, RigPhase::Deploy, departure + .42f, 1);
    auto tilted = set;
    carryBoard(tilted, *layout, tilting);
    const auto& q = tilted.instances[0].quat;
    CHECK(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] == Catch::Approx(1));
    CHECK(std::abs(q[3]) < .9999f);
    // Parked in the bay before it leaves: the board is not drawn (the bay slab is).
    auto parked = set;
    carryBoard(parked, *layout, rigPoses(*layout, RigPhase::Deploy, 0, 1));
    for (const auto& i : parked.instances) CHECK(i.scale[0] == 0);
  }
}

TEST_CASE("stow reverses the path, last group first, back into the bays",
          "[render][rig]") {
  const auto lab = shippedLab();
  const auto layout = rigLayout(lab, grid2x2());
  REQUIRE(layout);
  REQUIRE(layout->groups.size() == 4);
  const auto start = rigPoses(*layout, RigPhase::Stow, 0, 1);
  for (const auto& p : start) CHECK(p.clamped);
  // The last group leaves first.
  const auto early = rigPoses(*layout, RigPhase::Stow, .05f, 1);
  CHECK(early[layout->groups[3].slabs[0]].clamped == false);
  CHECK(early[layout->groups[0].slabs[0]].clamped == true);
  const auto home = rigPoses(*layout, RigPhase::Stow, view::stowTravelSeconds(4) + .01f, 1);
  for (const auto& p : home) CHECK(p.inBay);
}

TEST_CASE("the lab never changes what a click picks", "[render][rig]") {
  const auto lab = shippedLab();
  for (const char* name : {"standard", "cube5"}) {
    CAPTURE(name);
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s);
    auto& session = **s;
    session.setBoardAspect(16.f / 9);
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> px(0, 1280), py(0, 720);
    std::vector<std::pair<float, float>> pixels;
    std::vector<CellId> off;
    for (int i = 0; i < 50; ++i) {
      pixels.emplace_back(px(rng), py(rng));
      off.push_back(session.pickPixel(pixels.back().first, pixels.back().second, 1280, 720));
    }
    const auto layout = rigLayout(lab, session.placements());
    REQUIRE(layout);
    constrainGameCamera(session, *layout);
    InstanceSet set;
    LabDraw draw;
    draw.xform = &layout->xform;
    lab.appendTo(set, view::Theme::manifold(), {}, {}, draw);
    (void)appendRig(set, lab, *layout, rigPoses(*layout, RigPhase::Rest, 0, 1));
    for (int i = 0; i < 50; ++i)
      CHECK(session.pickPixel(pixels[static_cast<std::size_t>(i)].first,
                              pixels[static_cast<std::size_t>(i)].second, 1280,
                              720) == off[static_cast<std::size_t>(i)]);
  }
}
#endif
