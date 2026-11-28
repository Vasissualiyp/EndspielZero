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
