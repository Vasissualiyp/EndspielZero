// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_VULKAN
#include <cstring>
#include <filesystem>
#include <map>
#include <numeric>
#include <random>

#include "app/session.hpp"
#include "render/lab_rig.hpp"
#include "render/vulkan_context.hpp"
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
  // A rack's T-bars meet at its mast and carry its yokes (M24.6).
  if (a.group == b.group && a.group >= 0 &&
      (is(K::TBar, K::TBar) || is(K::TBar, K::MastInner) || is(K::TBar, K::Yoke) ||
       is(K::TBar, K::Clamp)))
    return true;
  return a.slab == b.slab && a.slab >= 0 && is(K::Yoke, K::Back);
}
/// Every shipped variant's name (variants/*.toml), sorted.
std::vector<std::string> shippedVariants() {
  std::vector<std::string> names;
  for (const auto& e : std::filesystem::directory_iterator(
           std::filesystem::path(CB_SOURCE_DIR) / "variants"))
    if (e.is_regular_file() && e.path().extension() == ".toml")
      names.push_back(e.path().stem().string());
  std::sort(names.begin(), names.end());
  return names;
}
/// A variant's placements in its default view: what a new game's session lays out.
std::vector<view::Placement> defaultView(const std::string& name) {
  auto s = app::Session::create(test::loadVariant(name));
  REQUIRE(s);
  return (*s)->placements();
}
/// The rig pieces of a deployed layout clear each other (joints aside) and the cells.
void checkClear(const LabScene& lab, const std::vector<view::Placement>& placements,
                const std::vector<RigPiece>& pieces) {
  (void)lab;
  const auto half = BoardRenderer::cellHalfExtent();
  for (std::size_t i = 0; i < pieces.size(); ++i) {
    CAPTURE(i, static_cast<int>(pieces[i].kind), pieces[i].group, pieces[i].slab);
    using K = RigPiece::Kind;
    const auto k = pieces[i].kind;
    if (k == K::Back || k == K::Yoke || k == K::MastInner || k == K::MastOuter ||
        k == K::TBar) {
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
}  // namespace

TEST_CASE("every shipped variant deploys: spaced masts, nothing intersects (M24.6)",
          "[render][rig]") {
  const auto lab = shippedLab();
  for (const auto& name : shippedVariants()) {
    CAPTURE(name);
    const auto placements = defaultView(name);
    const auto layout = rigLayout(lab, placements);
    REQUIRE(layout);
    CHECK(layout->travels);
    CHECK(view::spacingHolds(layout->groups, layout->xform.labScale));
    // The slab phase of a deploy stays within 1.6 s of its first departure.
    CHECK(layout->lastLock - view::kDeployFirstDeparture - view::kSlabLockSeconds <=
          1.2f + 1e-4f);
    // Every slab is carried exactly once.
    std::vector<int> carried(layout->slabs.size(), 0);
    for (const auto& g : layout->groups)
      for (const auto s : g.slabs) ++carried[s];
    for (const int c : carried) CHECK(c == 1);
    const auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
    InstanceSet set;
    const auto pieces = appendRig(set, lab, *layout, poses);
    REQUIRE_FALSE(pieces.empty());
    checkClear(lab, placements, pieces);
    WARN(name << ": " << layout->slabs.size() << " slabs, " << layout->groups.size()
              << " groups, " << layout->slots << " slots, scale "
              << layout->xform.labScale);
  }
}

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
      if (pieces[i].kind == RigPiece::Kind::Back ||
          pieces[i].kind == RigPiece::Kind::Yoke ||
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
    CHECK(std::any_of(pieces.begin(), pieces.end(),
                      [](const RigPiece& p) { return p.kind == RigPiece::Kind::Back; }));
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
      const auto p =
          std::find_if(placements.begin(), placements.end(),
                       [&](const auto& q) { return layout->cellSlab[q.cell] == s; });
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
      CHECK(moved.instances[i].center[0] ==
            Catch::Approx(set.instances[i].center[0] + d.x));
      CHECK(moved.instances[i].center[1] ==
            Catch::Approx(set.instances[i].center[1] + d.y));
      CHECK(moved.instances[i].center[2] ==
            Catch::Approx(set.instances[i].center[2] + d.z));
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
  // The last group leaves first. M24.6: "last" is the last departure slot (columns
  // nearest the magazine depart first), no longer the last slice.
  const auto bySlot = [&](int slot) {
    for (std::size_t g = 0; g < layout->groups.size(); ++g)
      if (layout->groupSlot[g] == slot) return layout->groups[g].slabs[0];
    FAIL("no group in slot " << slot);
    return 0u;
  };
  const auto early = rigPoses(*layout, RigPhase::Stow, .05f, 1);
  CHECK(early[bySlot(layout->slots - 1)].clamped == false);
  CHECK(early[bySlot(0)].clamped == true);
  const auto home =
      rigPoses(*layout, RigPhase::Stow, view::stowTravelSeconds(4) + .01f, 1);
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
      off.push_back(
          session.pickPixel(pixels.back().first, pixels.back().second, 1280, 720));
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

// ---------------------------------------------------------------------------------------
// M24.6: layout changes move the hardware.
#ifdef CB_HAVE_VULKAN
namespace {
/// A session's placements after a script line (an `axes` change).
std::vector<view::Placement> afterAction(const char* variant, const char* line) {
  auto s = app::Session::create(test::loadVariant(variant));
  REQUIRE(s);
  auto a = app::parseAction((*s)->variant(), line);
  REQUIRE(a);
  REQUIRE((*s)->apply(*a));
  return (*s)->placements();
}
/// One instance per placement, at the cell.
InstanceSet cellsOf(const std::vector<view::Placement>& p) {
  InstanceSet set;
  for (const auto& c : p) {
    Instance i;
    i.center[0] = c.x;
    i.center[1] = c.y;
    i.center[2] = c.z;
    i.scale[0] = i.scale[1] = i.scale[2] = 1;
    i.quat[3] = 1;
    set.instances.push_back(i);
  }
  return set;
}
}  // namespace

TEST_CASE("a relayout carries every slab from its old place to its new one",
          "[render][rig]") {
  const auto lab = shippedLab();
  for (const auto& [variant, line] : std::vector<std::pair<const char*, const char*>>{
           {"hyper4", "axes file,aeon vertical"},
           {"cube5", "axes file,level"},
           {"standard", "axes rank,file"}}) {
    CAPTURE(variant, line);
    const auto before = defaultView(variant);
    const auto after = afterAction(variant, line);
    const auto from = rigLayout(lab, before);
    const auto to = rigLayout(lab, after);
    REQUIRE(from);
    REQUIRE(to);
    const auto move = relayoutOf(lab, *from, before, *to, after);
    REQUIRE(move.seconds >= 1.0f);
    // Every slab is matched: nothing comes from or goes back to the rack.
    for (const auto& m : move.matches) {
      CHECK(m.from >= 0);
      CHECK(m.to >= 0);
    }
    // At 0 every slab hangs exactly where its old one did (in the old board space, which
    // the eased lab still stands in) and its cells ride it rigidly: turned back by the
    // match's yaw about the new centre.
    const auto start = relayoutFrame(lab, move, after, 0, 1);
    CHECK(start.layout.xform.labOrigin.x == Catch::Approx(from->xform.labOrigin.x));
    CHECK(start.layout.xform.labScale == Catch::Approx(from->xform.labScale));
    auto atStart = cellsOf(after);
    carryRelayout(atStart, start);
    for (const auto& m : move.matches) {
      const auto t = static_cast<std::size_t>(m.to);
      const auto& was = from->paths[static_cast<std::size_t>(m.from)].hangCentre;
      CHECK(view::length(start.poses[t].centre - was) < 2e-3f);
    }
    for (std::size_t i = 0; i < after.size(); ++i) {
      const auto s = to->cellSlab[after[i].cell];
      const auto& m =
          *std::find_if(move.matches.begin(), move.matches.end(),
                        [&](const auto& x) { return x.to == static_cast<int>(s); });
      const auto pivot = to->paths[s].hangCentre;
      const auto was = from->paths[static_cast<std::size_t>(m.from)].hangCentre;
      const float c = std::cos(-m.yaw), sn = std::sin(-m.yaw);
      const float dx = after[i].x - pivot.x, dy = after[i].y - pivot.y;
      CAPTURE(i);
      CHECK(atStart.instances[i].center[0] ==
            Catch::Approx(was.x + c * dx - sn * dy).margin(2e-3));
      CHECK(atStart.instances[i].center[1] ==
            Catch::Approx(was.y + sn * dx + c * dy).margin(2e-3));
    }
    const auto end = relayoutFrame(lab, move, after, move.seconds, 1);
    CHECK(end.done);
    auto atEnd = cellsOf(after);
    const auto untouched = atEnd;
    carryRelayout(atEnd, end);
    for (std::size_t i = 0; i < after.size(); ++i)
      CHECK(std::memcmp(&atEnd.instances[i], &untouched.instances[i], sizeof(Instance)) ==
            0);
    // Continuous in between: no cell jumps more than a cell in 10 ms.
    auto last = atStart;
    for (float t = .01f; t <= move.seconds; t += .01f) {
      auto now = cellsOf(after);
      carryRelayout(now, relayoutFrame(lab, move, after, t, 1));
      for (std::size_t i = 0; i < after.size(); i += 7) {
        const view::Vec3 a{last.instances[i].center[0], last.instances[i].center[1],
                           last.instances[i].center[2]};
        const view::Vec3 b{now.instances[i].center[0], now.instances[i].center[1],
                           now.instances[i].center[2]};
        CHECK(view::length(b - a) < 1.0f);
      }
      last = now;
    }
    // Calm: the final pose at once.
    CHECK(relayoutFrame(lab, move, after, .1f, .3f).done);
  }
}

TEST_CASE("a new board arrives from the rack, one slab per new board", "[render][rig]") {
  const auto lab = shippedLab();
  auto before = defaultView("5d");
  // The multiverse grows by one board beside the first: new cells, a new slice.
  auto after = before;
  float maxX = -1e9f;
  for (const auto& p : before) maxX = std::max(maxX, p.x);
  for (const auto& p : before)
    after.push_back({p.cell + 100000, p.x + (maxX + 3), p.y, p.z, p.slice + 1});
  const auto from = rigLayout(lab, before);
  REQUIRE(from);
  // The layout still fits where the first board stood: the lab does not move.
  const auto to = rigLayoutAfter(lab, *from, after);
  REQUIRE(to);
  CHECK(to->xform.labOrigin.x == from->xform.labOrigin.x);
  const auto move = relayoutOf(lab, *from, before, *to, after);
  int arrivals = 0;
  for (const auto& m : move.matches) arrivals += m.from < 0 ? 1 : 0;
  CHECK(arrivals == 1);
  CHECK(move.arrivals.size() == 1);
  // It leaves its bay, and its cells boot once it locks: dark before, booting after.
  const auto& path = to->paths[static_cast<std::size_t>(move.arrivals.front())];
  const float lock = path.departure + view::kSlabLockSeconds;
  const auto early = relayoutFrame(lab, move, after, .05f, 1);
  CHECK(early.poses[static_cast<std::size_t>(move.arrivals.front())].inBay);
  CHECK(relayoutBootAge(move, lock - .1f, 1) == Catch::Approx(0));
  CHECK(relayoutBootAge(move, lock + .5f, 1) == Catch::Approx(.5f));
  CHECK_FALSE(relayoutBootAge(move, lock + 9, 1).has_value());
  // The first board's slab stays where it hangs: nothing lifts it.
  const auto mid = relayoutFrame(lab, move, after, .6f, 1);
  const auto stay = static_cast<std::size_t>(move.matches.front().to);
  CHECK(view::length(mid.poses[stay].centre - to->paths[stay].hangCentre) < 1e-4f);
}
#endif

#ifdef CB_HAVE_VULKAN
TEST_CASE("a 5D light-guide cable runs between the two masts and sags", "[render][rig]") {
  const auto lab = shippedLab();
  const auto v = test::loadVariant("5d");
  const auto cfg = view::ViewConfig::forBoard(v.dims);
  const auto all = view::layout(v.dims, cfg);
  // Two boards: the origin timeline's first and a branch's, by their grid coordinates.
  const auto slices = view::enumerateSlices(v.dims, cfg);
  int turnGrid = -1, lineGrid = -1;
  for (std::size_t i = 0; i < cfg.gridAxes.size(); ++i) {
    if (v.dims.kind(cfg.gridAxes[i]) == AxisKind::Temporal)
      turnGrid = static_cast<int>(i);
    if (v.dims.kind(cfg.gridAxes[i]) == AxisKind::Multiverse)
      lineGrid = static_cast<int>(i);
  }
  REQUIRE(turnGrid >= 0);
  REQUIRE(lineGrid >= 0);
  const auto coord = [&](std::uint32_t slice, int g) {
    return static_cast<int>(slices[slice].at[static_cast<std::size_t>(g)]);
  };
  const std::uint32_t parent = 0;
  std::uint32_t child = 0;
  for (std::uint32_t s = 0; s < slices.size(); ++s)
    if (coord(s, lineGrid) == coord(parent, lineGrid) + 2 &&
        coord(s, turnGrid) == coord(parent, turnGrid) + 1)
      child = s;
  REQUIRE(child != 0);
  std::vector<view::Placement> two;
  for (const auto& p : all)
    if (p.slice == parent || p.slice == child) two.push_back(p);
  const auto layout = rigLayout(lab, two);
  REQUIRE(layout);
  const auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
  const view::TimelineLink link{static_cast<std::int16_t>(coord(parent, lineGrid)),
                                static_cast<std::int16_t>(coord(child, lineGrid)),
                                static_cast<std::int16_t>(coord(parent, turnGrid))};
  const auto cables = rigCables(*layout, poses, v, cfg, {link});
  REQUIRE(cables.size() == 1);
  const auto& c = cables[0];
  REQUIRE(c.points.size() == 13);
  // The endpoints sit on the two masts, 0.15 m above the slabs' faces.
  const auto& pa = poses[static_cast<std::size_t>(c.slabA)];
  const auto& pb = poses[static_cast<std::size_t>(c.slabB)];
  CHECK(c.points.front().x == Catch::Approx(pa.carriage.x));
  CHECK(c.points.front().y == Catch::Approx(pa.carriage.y));
  CHECK(c.points.back().x == Catch::Approx(pb.carriage.x));
  CHECK(c.points.back().y == Catch::Approx(pb.carriage.y));
  CHECK(c.points.front().z ==
        Catch::Approx(layout->slabs[0].z + kCableAboveMetres * kCellsPerMetre)
            .margin(1e-3));
  // The sag is 8 % of the span, within tolerance.
  const float span = view::length(c.points.back() - c.points.front());
  const auto mid = (c.points.front() + c.points.back()) * .5f;
  CHECK(mid.z - c.points[6].z == Catch::Approx(.08f * span).margin(.02f * span));
  // Drawn: 12 glass segments and 3 pulses while powered, none of the pulses when dark.
  InstanceSet set;
  CHECK(appendCables(set, cables, .3f, 1) == 15);
  InstanceSet dark;
  CHECK(appendCables(dark, cables, .3f, 0) == 12);
}

TEST_CASE("light bridges are beams of light in the lab's cylinder batch",
          "[render][rig]") {
  view::LightBridge b;
  b.a = {0, 0, 0};
  b.b = {10, 0, 0};
  b.control = {5, 0, 2.5f};
  b.color = view::Rgba::hex(0x40C0FF);
  InstanceSet set;
  CHECK(appendBridges(set, {b}, 1) == 2u * kBridgeSegments);
  const auto& batch = set.labBatches[static_cast<std::size_t>(LabShape::Cylinder)];
  CHECK(batch.count == 2u * kBridgeSegments);
  CHECK(set.lab[batch.first].material == 4.0f);
  CHECK(set.lab[batch.first].panelSignal[2] == -1.0f);
  CHECK(set.lab[batch.first].color[3] == Catch::Approx(kBridgeAlpha));
  InstanceSet dark;
  CHECK(appendBridges(dark, {b}, 0) == 0u);
}

TEST_CASE("a stacked shape view parks the slabs high on short masts", "[render][rig]") {
  const auto lab = shippedLab();
  for (const char* name : {"torus3d", "hyper4", "standard"}) {
    CAPTURE(name);
    const auto layout = rigLayout(lab, defaultView(name));
    REQUIRE(layout);
    auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
    const auto rest = poses;
    parkHigh(*layout, poses, 1);
    for (const auto& g : layout->groups) {
      const auto top = g.slabs.front();
      const float yoke = poses[top].centre.z - layout->paths[top].yokeBelow;
      CHECK(layout->paths[top].railZ - yoke ==
            Catch::Approx(kParkMastMetres * kCellsPerMetre).margin(1e-3));
      for (const auto s : g.slabs) CHECK(poses[s].centre.z > rest[s].centre.z);
    }
    auto none = rest;
    parkHigh(*layout, none, 0);
    for (std::size_t s = 0; s < none.size(); ++s)
      CHECK(none[s].centre.z == rest[s].centre.z);
  }
}
#endif

#ifdef CB_HAVE_VULKAN
TEST_CASE("a stacked shape view in the lab is light, its slabs parked high",
          "[render][gpu][rig]") {
  auto ctx = VulkanContext::create();
  REQUIRE(ctx.has_value());
  auto renderer = BoardRenderer::create(*ctx);
  REQUIRE(renderer.has_value());
  renderer->setTheme(view::Theme::manifold());
  const auto lab = shippedLab();
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s.has_value());
    BoardOptions options;
    options.surface = true;
    options.surfaceFormed = 1.0f;
    renderer->setOptions(options);
    // Off the lab path: the stacked shape's tiles are LEDs, one draw.
    const auto leds = renderer->buildInstances((*s)->snapshot(), (*s)->viewConfig());
    REQUIRE_FALSE(leds.surfaceIndices.empty());
    CHECK(leds.surfaceDraws.empty());
    // In the lab: no LED patches, every patch hologram light at 0.8.
    options.stackedLight = kStackedLightOpacity;
    renderer->setOptions(options);
    const auto set = renderer->buildInstances((*s)->snapshot(), (*s)->viewConfig());
    REQUIRE(set.surfaceDraws.size() >= 2);
    CHECK(set.surfaceDraws[0].indexCount == 0);  // the LED range is empty
    const auto& light = set.surfaceDraws[1];
    CHECK(light.indexCount > 0);
    CHECK(set.instances[light.instance].material == 4.0f);
    for (std::uint32_t i = light.firstIndex; i < light.firstIndex + light.indexCount;
         ++i) {
      const auto& v = set.surfaceVertices[set.surfaceIndices[i]];
      if (v.panelSignal[2] != -1.0f ||
          v.color[3] != Catch::Approx(kStackedLightOpacity)) {
        FAIL("a stacked tile is not drawn as light");
        break;
      }
    }
    // The slabs are parked on 0.15 m masts.
    const auto layout = rigLayout(lab, (*s)->placements());
    REQUIRE(layout);
    auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
    parkHigh(*layout, poses, parkFraction(1.0f));
    for (const auto& g : layout->groups) {
      const auto top = g.slabs.front();
      CHECK(layout->paths[top].railZ -
                (poses[top].centre.z - layout->paths[top].yokeBelow) ==
            Catch::Approx(kParkMastMetres * kCellsPerMetre).margin(1e-3));
    }
  }
  CHECK(ctx->takeValidationMessages().empty());
}
#endif
