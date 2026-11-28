// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tiled view draws a board's neighbours through its glued edges. These tests pin the one
// thing that makes the picture honest: what a copy shows across an edge is what the engine
// says is across that edge.
#include <algorithm>
#include <cmath>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "support/variants.hpp"
#include "view/adjacent.hpp"
#include "view/snapshot.hpp"

using namespace cb;
using namespace cb::view;

namespace {

const AdjacentCopy* find(const std::vector<AdjacentCopy>& cs, int u, int v) {
  for (const AdjacentCopy& c : cs)
    if (c.stepU == u && c.stepV == v) return &c;
  return nullptr;
}

std::vector<AdjacentCopy> copiesOf(const VariantSpec& v, int rings) {
  return adjacentCopies(v, ViewConfig::forBoard(v.dims), rings);
}

}  // namespace

TEST_CASE("a cylinder repeats along its glued axis only", "[unit][view]") {
  const VariantSpec v = test::loadVariant("cylinder");
  const auto one = copiesOf(v, 1);
  REQUIRE(one.size() == 3);
  CHECK(find(one, 0, 0) != nullptr);
  CHECK(find(one, -1, 0) != nullptr);
  CHECK(find(one, 1, 0) != nullptr);

  const auto two = copiesOf(v, 2);
  REQUIRE(two.size() == 5);
  // Farthest first, so translucent rings blend correctly; the centre is last.
  for (std::size_t i = 1; i < two.size(); ++i) CHECK(two[i - 1].order >= two[i].order);
  CHECK(two.back().order == 0);
  CHECK(two.back().stepU == 0);
  CHECK(two.front().order == 2);

  const AdjacentCopy* right = find(one, 1, 0);
  REQUIRE(right != nullptr);
  CHECK(right->order == 1);
  // A translation gluing: the copy shows the same cell, so the seams cancel the shift.
  CHECK(right->viaSeams.isIdentity());
  for (int j = 0; j < v.dims.extent(1); ++j) {
    const Coord local = Coord::of({2, j});
    CHECK(right->viaSeams.applyToCoord(local) == local);
    CHECK(sourceCell(*right, v.dims.toCell(local), v) == v.dims.toCell(local));
  }
  CHECK(find(one, 0, 0)->viaSeams.isIdentity());
}

TEST_CASE("a torus repeats in the plane", "[unit][view]") {
  const VariantSpec v = test::loadVariant("torus");
  CHECK(copiesOf(v, 1).size() == 9);
  CHECK(copiesOf(v, 2).size() == 25);
  CHECK(copiesOf(v, 3).size() == 49);
  for (const AdjacentCopy& c : copiesOf(v, 2)) {
    CHECK(c.order == std::max(std::abs(c.stepU), std::abs(c.stepV)));
    CHECK(c.viaSeams.reversesOrientation() == false);
  }
}

TEST_CASE("a mobius copy is rank-reversed, and two files on is the centre again",
          "[unit][view]") {
  const VariantSpec v = test::loadVariant("mobius");
  const auto cs = copiesOf(v, 2);
  REQUIRE(cs.size() == 5);
  const std::int16_t ny = v.dims.extent(1);
  for (int u : {-1, 1}) {
    const AdjacentCopy* c = find(cs, u, 0);
    REQUIRE(c != nullptr);
    CHECK(c->viaSeams.reversesOrientation());
    const CellId local = v.dims.toCell(Coord::of({3, 1}));
    const Coord src = v.dims.toCoord(sourceCell(*c, local, v));
    CHECK(src.c[0] == 3);
    CHECK(src.c[1] == ny - 1 - 1);
  }
  for (int u : {-2, 2}) {
    const AdjacentCopy* c = find(cs, u, 0);
    REQUIRE(c != nullptr);
    CHECK_FALSE(c->viaSeams.reversesOrientation());
    const CellId local = v.dims.toCell(Coord::of({3, 1}));
    CHECK(sourceCell(*c, local, v) == local);
  }
}

TEST_CASE("a klein bottle flips across its twisted axis", "[unit][view]") {
  const VariantSpec v = test::loadVariant("klein");
  const auto cs = copiesOf(v, 1);
  REQUIRE(cs.size() == 9);
  const std::int16_t nx = v.dims.extent(0);
  const AdjacentCopy* up = find(cs, 0, 1);
  REQUIRE(up != nullptr);
  CHECK(up->viaSeams.reversesOrientation());
  const Coord src = v.dims.toCoord(sourceCell(*up, v.dims.toCell(Coord::of({2, 5})), v));
  CHECK(src.c[0] == nx - 1 - 2);
  CHECK(src.c[1] == 5);
  CHECK(find(cs, 1, 0)->viaSeams.reversesOrientation() == false);
}

TEST_CASE("boards with no periodic drawn face, or too many axes, have no copies",
          "[unit][view]") {
  for (const char* name : {"standard", "mirrorbox", "cube5", "hyper4", "t6", "torus3d"}) {
    CAPTURE(name);
    CHECK(copiesOf(test::loadVariant(name), 2).empty());
  }
}

TEST_CASE("copyPlacement shifts by the board's own extent and shows the source cell",
          "[unit][view]") {
  const VariantSpec v = test::loadVariant("mobius");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const auto cs = adjacentCopies(v, cfg, 1);
  const AdjacentCopy* right = find(cs, 1, 0);
  REQUIRE(right != nullptr);
  Placement central;
  central.cell = v.dims.toCell(Coord::of({2, 1}));
  central.x = 2;
  central.y = 1;
  central.z = 0.5f;
  central.slice = 3;
  const Placement p = copyPlacement(*right, central, v, cfg);
  CHECK(p.x == 2 + static_cast<float>(v.dims.extent(0)));
  CHECK(p.y == 1);
  CHECK(p.z == 0.5f);
  CHECK(p.slice == 3);
  CHECK(p.cell == sourceCell(*right, central.cell, v));
  CHECK(p.cell != central.cell);
}

TEST_CASE("the picture and the engine agree about what is across an edge",
          "[property][view]") {
  int checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator(test::repoRoot() / "variants")) {
    if (entry.path().extension() != ".toml") continue;
    const VariantSpec v = test::loadVariant(entry.path().stem().string());
    const ViewConfig cfg = ViewConfig::forBoard(v.dims);
    const auto cs = adjacentCopies(v, cfg, 1);
    if (cs.empty()) continue;
    CAPTURE(entry.path().stem().string());
    ++checked;
    const DimSpec& d = v.dims;
    for (CellId cell = 0; cell < d.cellCount(); ++cell) {
      const Coord c = d.toCoord(cell);
      // Every one-cell step, orthogonal and diagonal, that leaves the board on a drawn axis.
      for (int du = -1; du <= 1; ++du) {
        for (int dv = -1; dv <= 1; ++dv) {
          if (du == 0 && dv == 0) continue;
          const int qu = c.c[cfg.screenAxes[0]] + du;
          const int qv = c.c[cfg.screenAxes[1]] + dv;
          const int nu = d.extent(cfg.screenAxes[0]);
          const int nv = d.extent(cfg.screenAxes[1]);
          const int su = qu < 0 ? -1 : (qu >= nu ? 1 : 0);
          const int sv = qv < 0 ? -1 : (qv >= nv ? 1 : 0);
          if (su == 0 && sv == 0) continue;
          std::array<std::int16_t, kMaxDims> vec{};
          vec[cfg.screenAxes[0]] = static_cast<std::int16_t>(du);
          vec[cfg.screenAxes[1]] = static_cast<std::int16_t>(dv);
          Walker w = v.geom.start(cell, Direction::make(vec, d.dims()));
          const bool engine = v.geom.step(w);
          const AdjacentCopy* copy = find(cs, su, sv);
          if (copy == nullptr) {
            // No copy there: the engine must not carry the step across either.
            CHECK_FALSE(engine);
            continue;
          }
          REQUIRE(engine);
          Coord local = c;
          local.c[cfg.screenAxes[0]] = static_cast<std::int16_t>(qu - su * nu);
          local.c[cfg.screenAxes[1]] = static_cast<std::int16_t>(qv - sv * nv);
          CHECK(sourceCell(*copy, d.toCell(local), v) == w.cell);
        }
      }
    }
  }
  CHECK(checked >= 4);
}

// M22.2: the drawing's shared rules - the fade by order, the instance budget, and the flat
// tokens - are pure here so the 3-D board and the flat tokens cannot disagree.

TEST_CASE("a copy is fainter the higher its order, and only the centre is opaque",
          "[unit][view]") {
  CHECK(adjacentAlpha(0) == 1.0f);
  CHECK(adjacentAlpha(1) == kAdjacentAlpha);
  CHECK(std::abs(adjacentAlpha(1) - 0.85f) < 1e-6f);
  CHECK(std::abs(adjacentAlpha(2) - 0.85f * 0.58f) < 1e-6f);
  CHECK(std::abs(adjacentAlpha(3) - 0.85f * 0.58f * 0.58f) < 1e-6f);
  for (int r = 1; r <= 3; ++r) {
    CHECK(adjacentAlpha(r) < adjacentAlpha(r - 1));
    CHECK(adjacentAlpha(r) < 1.0f);
  }
}

TEST_CASE("the tiled view is drawn off the shape only", "[unit][view]") {
  CHECK(adjacentDrawn(true, false));
  CHECK_FALSE(adjacentDrawn(true, true));
  CHECK_FALSE(adjacentDrawn(false, false));
}

TEST_CASE("the instance budget drops whole outer rings, never a partial one",
          "[unit][view]") {
  const VariantSpec v = test::loadVariant("torus");
  const auto three = copiesOf(v, 3);
  REQUIRE(three.size() == 49);
  // Room for every copy but the centre (48 boards): nothing is dropped.
  CHECK(fitAdjacent(three, 10, 48 * 10).size() == 49);
  // One instance short: the whole third ring goes, leaving 25.
  const auto two = fitAdjacent(three, 10, 48 * 10 - 1);
  CHECK(two.size() == 25);
  for (const AdjacentCopy& c : two) CHECK(c.order <= 2);
  // Too little even for the first ring: the centre alone stays.
  const auto none = fitAdjacent(three, 10, 5);
  REQUIRE(none.size() == 1);
  CHECK(none.front().order == 0);
  // An 8x8 torus fits three rings at two instances per cell.
  CHECK(fitAdjacent(three, 2 * 64).size() == 49);
}

TEST_CASE("flat tokens show the source cell's piece at the copy's place", "[unit][view]") {
  const VariantSpec v = test::loadVariant("mobius");
  const Position pos = Position::startPosition(v);
  const PositionView snap = PositionView::capture(pos);
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const auto central = layout(v.dims, cfg);
  const auto copies = adjacentCopies(v, cfg, 2);
  const auto tokens = adjacentTokens(snap, copies, central, cfg, 1.0f);

  std::size_t occupied = 0;
  for (const Placement& pl : central)
    if (!snap.at(pl.cell).empty()) ++occupied;
  // Four copies (orders 2,1,1,2), each showing every piece once; never the centre.
  REQUIRE(tokens.size() == 4 * occupied);
  for (const AdjacentToken& t : tokens) {
    CHECK(t.order >= 1);
    CHECK(t.alpha == adjacentAlpha(t.order));
    CHECK(t.piece == snap.at(t.at.cell));
    CHECK_FALSE(t.piece.empty());
  }
  // Farthest first, so translucent tokens layer back to front.
  for (std::size_t i = 1; i < tokens.size(); ++i) CHECK(tokens[i - 1].order >= tokens[i].order);
  // Each token stands where copyPlacement puts its copy-local cell.
  const AdjacentCopy* right = find(copies, 1, 0);
  REQUIRE(right != nullptr);
  std::size_t onRight = 0;
  for (const Placement& pl : central) {
    const Placement at = copyPlacement(*right, pl, v, cfg);
    if (snap.at(at.cell).empty()) continue;
    const bool found = std::any_of(tokens.begin(), tokens.end(), [&](const AdjacentToken& t) {
      return t.at.x == at.x && t.at.y == at.y && t.at.cell == at.cell;
    });
    CHECK(found);
    ++onRight;
  }
  CHECK(onRight == occupied);

  // The reveal scales every alpha; zero draws nothing; a skipped cell is left out.
  const auto half = adjacentTokens(snap, copies, central, cfg, 0.5f);
  REQUIRE(half.size() == tokens.size());
  for (std::size_t i = 0; i < half.size(); ++i)
    CHECK(std::abs(half[i].alpha - 0.5f * tokens[i].alpha) < 1e-6f);
  CHECK(adjacentTokens(snap, copies, central, cfg, 0.0f).empty());
  const CellId moving = central.front().cell;
  REQUIRE_FALSE(snap.at(moving).empty());
  CHECK(adjacentTokens(snap, copies, central, cfg, 1.0f, moving).size() == 4 * (occupied - 1));
}

// M22.4 the arrival: the copies come in from "infinity" as one eased number grows.

TEST_CASE("an arriving copy comes from far boards out and lands exactly home",
          "[unit][view]") {
  for (int order = 1; order <= 3; ++order) {
    CAPTURE(order);
    CHECK(adjacentOffset(1.0f, order) == 0.0f);
    CHECK(adjacentOffset(0.0f, order) == kAdjacentFar);
    float last = adjacentOffset(0.0f, order);
    for (int i = 1; i <= 100; ++i) {
      const float now = adjacentOffset(static_cast<float>(i) / 100.0f, order);
      CHECK(now <= last);
      last = now;
    }
  }
  CHECK(kAdjacentFar == 12.0f);
  // The centre never moves.
  CHECK(adjacentOffset(0.0f, 0) == 0.0f);
}

TEST_CASE("an arriving copy fades in to its ring's alpha and dims as it merges",
          "[unit][view]") {
  for (int order = 1; order <= 3; ++order) {
    CAPTURE(order);
    CHECK(adjacentRevealAlpha(0.0f, order) == 0.0f);
    CHECK(adjacentRevealAlpha(1.0f, order) == adjacentAlpha(order));
    // The dimming settles over the last tenth of the copy's own reveal, never before.
    CHECK(adjacentDimAt(1.0f, order) == kAdjacentDim);
    CHECK(adjacentDimAt(0.0f, order) == 0.0f);
    float lastDim = 0.0f;
    for (int i = 0; i <= 100; ++i) {
      const float a = static_cast<float>(i) / 100.0f;
      const float dim = adjacentDimAt(a, order);
      CHECK(dim >= lastDim);
      if (adjacentReveal(a, order) < 1.0f - kAdjacentMerge) CHECK(dim == 0.0f);
      lastDim = dim;
    }
  }
  // Strictly fainter with the order; one only on the centre.
  CHECK(adjacentAlpha(0) == 1.0f);
  CHECK(1.0f > adjacentAlpha(1));
  CHECK(adjacentAlpha(1) > adjacentAlpha(2));
  CHECK(adjacentAlpha(2) > adjacentAlpha(3));
  CHECK(std::abs(adjacentAlpha(2) - 0.49f) < 0.005f);
  CHECK(std::abs(adjacentAlpha(3) - 0.29f) < 0.005f);
}

TEST_CASE("an outer ring never starts arriving before the ring inside it", "[unit][view]") {
  for (int order = 1; order < 3; ++order) {
    CAPTURE(order);
    float startInner = -1.0f;
    float startOuter = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
      const float a = static_cast<float>(i) / 1000.0f;
      const float inner = adjacentReveal(a, order);
      const float outer = adjacentReveal(a, order + 1);
      CHECK(outer <= inner);
      if (startInner < 0.0f && inner > 0.0f) startInner = a;
      if (startOuter < 0.0f && outer > 0.0f) startOuter = a;
    }
    CHECK(startOuter > startInner);
  }
  CHECK(adjacentReveal(0.5f, 0) == 1.0f);
  CHECK(adjacentReveal(1.0f, 3) == 1.0f);
}

TEST_CASE("an arriving copy is pushed out along its step, corners along the diagonal",
          "[unit][view]") {
  const VariantSpec v = test::loadVariant("torus");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const auto central = layout(v.dims, cfg);
  const auto copies = adjacentCopies(v, cfg, 2);
  const float w = static_cast<float>(v.dims.extent(cfg.screenAxes[0]));
  const float h = static_cast<float>(v.dims.extent(cfg.screenAxes[1]));
  for (const AdjacentCopy& c : copies) {
    CAPTURE(c.stepU, c.stepV);
    const Placement home = copyPlacement(c, central.front(), v, cfg);
    const Placement arrived = copyPlacement(c, central.front(), v, cfg, 1.0f);
    CHECK(arrived.x == home.x);
    CHECK(arrived.y == home.y);
    CHECK(arrived.cell == home.cell);
    const Placement far = copyPlacement(c, central.front(), v, cfg, 0.0f);
    CHECK(far.cell == home.cell);
    const float r = c.order == 0 ? 1.0f : static_cast<float>(c.order);
    CHECK(std::abs(far.x - home.x - kAdjacentFar * w * c.stepU / r) < 1e-3f);
    CHECK(std::abs(far.y - home.y - kAdjacentFar * h * c.stepV / r) < 1e-3f);
    CHECK(far.z == home.z);
  }
}

TEST_CASE("arriving flat tokens follow their copy in and fade with it", "[unit][view]") {
  const VariantSpec v = test::loadVariant("torus");
  const Position pos = Position::startPosition(v);
  const PositionView snap = PositionView::capture(pos);
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const auto central = layout(v.dims, cfg);
  const auto copies = adjacentCopies(v, cfg, 2);
  // Arrived, they are exactly the steady tokens.
  const auto steady = adjacentTokens(snap, copies, central, cfg, 1.0f);
  const auto home = arrivingTokens(snap, copies, central, cfg, 1.0f);
  REQUIRE(home.size() == steady.size());
  for (std::size_t i = 0; i < home.size(); ++i) {
    CHECK(home[i].at.x == steady[i].at.x);
    CHECK(home[i].at.y == steady[i].at.y);
    CHECK(home[i].alpha == steady[i].alpha);
    CHECK(home[i].dim == kAdjacentDim);
  }
  CHECK(arrivingTokens(snap, copies, central, cfg, 0.0f).empty());
  // Mid-way, ring 1 is on its way in and ring 2 behind it, fainter and farther.
  const auto mid = arrivingTokens(snap, copies, central, cfg, 0.5f);
  REQUIRE_FALSE(mid.empty());
  for (const AdjacentToken& t : mid) {
    CHECK(t.alpha == adjacentRevealAlpha(0.5f, t.order));
    CHECK(t.alpha < adjacentAlpha(t.order));
  }
  for (std::size_t i = 1; i < mid.size(); ++i) CHECK(mid[i - 1].order >= mid[i].order);
}

TEST_CASE("the reveal eases in on switch-on and out again on switch-off", "[unit][view]") {
  constexpr float kDt = 1.0f / 60.0f;
  // Launched with the switch already on: arrived, no replay.
  AdjacentReveal r{1.0f, false, true};
  r = advanceAdjacentReveal(r, true, kDt, 1.0f);
  CHECK(r.formed == 1.0f);
  CHECK_FALSE(r.leaving);

  // Off and settled (the default reveal of 1 is not drawn while off), then on: the
  // copies start at infinity and arrive.
  r = AdjacentReveal{1.0f, false, false};
  r = advanceAdjacentReveal(r, false, kDt, 1.0f);
  CHECK_FALSE(r.leaving);
  CHECK(r.formed == 0.0f);
  // A zero step only takes the flip in: on from fully off is at infinity, not drawn yet.
  const AdjacentReveal flipped = advanceAdjacentReveal(r, true, 0.0f, 1.0f);
  CHECK(flipped.formed == 0.0f);
  CHECK(flipped.wasOn);
  r = advanceAdjacentReveal(r, true, kDt, 1.0f);
  CHECK(r.formed > 0.0f);
  CHECK(r.formed < 0.1f);
  int frames = 0;
  while (r.formed < 1.0f && frames < 600) {
    r = advanceAdjacentReveal(r, true, kDt, 1.0f);
    ++frames;
  }
  CHECK(r.formed == 1.0f);
  // Half a second at speed 1, as the shape morph.
  CHECK(frames < 40);

  // Off: they leave, still drawn until the reveal reaches zero. A zero step already
  // marks them leaving, so the flip's own frame still draws them.
  CHECK(advanceAdjacentReveal(r, false, 0.0f, 1.0f).leaving);
  CHECK(advanceAdjacentReveal(r, false, 0.0f, 1.0f).formed == 1.0f);
  r = advanceAdjacentReveal(r, false, kDt, 1.0f);
  CHECK(r.leaving);
  CHECK(r.formed < 1.0f);
  const float leavingAt = r.formed;
  // Reversed mid-way: on again continues from where it is, not from infinity.
  r = advanceAdjacentReveal(r, true, kDt, 1.0f);
  CHECK(r.formed > leavingAt);
  CHECK_FALSE(r.leaving);
  r = advanceAdjacentReveal(r, false, kDt, 1.0f);
  frames = 0;
  while (r.leaving && frames < 600) {
    r = advanceAdjacentReveal(r, false, kDt, 1.0f);
    ++frames;
  }
  CHECK(r.formed == 0.0f);
  CHECK_FALSE(r.leaving);
  // Faster with the shared speed setting.
  const AdjacentReveal slow = advanceAdjacentReveal({0.0f, false, true}, true, kDt, 1.0f);
  const AdjacentReveal fast = advanceAdjacentReveal({0.0f, false, true}, true, kDt, 2.0f);
  CHECK(fast.formed > slow.formed);
}

TEST_CASE("leaving copies are still drawn, and never on the shape", "[unit][view]") {
  CHECK(adjacentDrawn(true, false, false));
  CHECK(adjacentDrawn(false, true, false));
  CHECK_FALSE(adjacentDrawn(false, false, false));
  CHECK_FALSE(adjacentDrawn(true, true, true));
}

TEST_CASE("the far plane reaches every copy all the way out at infinity (M22.6)",
          "[unit][view]") {
  for (const char* name : {"torus", "cylinder", "klein"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const ViewConfig cfg = ViewConfig::forBoard(v.dims);
    const auto central = layout(v.dims, cfg);
    OrbitCamera cam;
    cam.target = {3.5f, 3.5f, 0.0f};
    cam.distance = 20.0f;
    cam.pitch = 0.3f;
    cam.farZ = 60.0f;
    for (int rings = 1; rings <= 3; ++rings) {
      CAPTURE(rings);
      const float far = adjacentFarZ(cam, central, v, cfg, rings);
      CHECK(far >= cam.farZ);
      const Vec3 eye = cam.eye();
      for (const AdjacentCopy& c : adjacentCopies(v, cfg, rings)) {
        for (const Placement& pl : central) {
          const Placement at = copyPlacement(c, pl, v, cfg, 0.0f);
          const float d = std::hypot(at.x - eye.x, at.y - eye.y, at.z + 1.0f - eye.z);
          CHECK(d < far);
        }
      }
    }
  }
  // No copies, no change: the plain board keeps its framed far plane.
  const VariantSpec standard = test::loadVariant("standard");
  const ViewConfig cfg = ViewConfig::forBoard(standard.dims);
  OrbitCamera cam;
  cam.farZ = 60.0f;
  CHECK(adjacentFarZ(cam, layout(standard.dims, cfg), standard, cfg, 2) == 60.0f);
}
