// SPDX-License-Identifier: GPL-3.0-or-later
// M22.5: picking and hover on the tiled view's copies. A pixel on a copy resolves to the
// central cell it shows, so the player can reach across a seam; with the switch off, or
// with copies ignored, the same pixel is nothing.
#include <algorithm>

#include <catch2/catch_test_macros.hpp>

#include "app/session.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"
#include "view/adjacent.hpp"

using namespace cb;
using namespace cb::app;

namespace {

constexpr float kW = 1600.0f, kH = 900.0f;

std::unique_ptr<Session> open(const char* variantName) {
  auto s = Session::create(test::loadVariant(variantName));
  REQUIRE(s.has_value());
  return std::move(*s);
}

CellId cell(const Session& s, const char* name) {
  auto c = parseCell(s.variant().dims, name);
  REQUIRE(c.has_value());
  return *c;
}

Session::AdjacentView on(int rings = 1) {
  Session::AdjacentView a;
  a.on = true;
  a.rings = rings;
  return a;
}

/// The pixel at which the copy at (su, sv) draws its local cell `local`.
std::pair<float, float> pixelOf(const Session& s, int su, int sv, CellId local) {
  const auto copies = view::adjacentCopies(s.variant(), s.viewConfig(), 3);
  for (const auto& copy : copies) {
    if (copy.stepU != su || copy.stepV != sv) continue;
    for (const auto& pl : s.placements()) {
      if (pl.cell != local) continue;
      const auto at = view::copyPlacement(copy, pl, s.variant(), s.viewConfig());
      const auto sp = s.camera().project(view::Vec3{at.x, at.y, at.z}, kW / kH, kW, kH);
      return {sp.x, sp.y};
    }
  }
  FAIL("no such copy");
  return {};
}

}  // namespace

TEST_CASE("a pixel on a copy picks the cell it shows", "[unit][app]") {
  auto s = open("cylinder");
  s->setAdjacent(on(2));
  for (const char* name : {"a1", "d4", "h8"}) {
    const CellId local = cell(*s, name);
    const auto [cx, cy] = pixelOf(*s, 0, 0, local);
    REQUIRE(s->pickPixel(cx, cy, kW, kH) == local);
    for (const int su : {-2, -1, 1, 2}) {
      INFO(name << " at " << su);
      const auto [x, y] = pixelOf(*s, su, 0, local);
      // A cylinder's seam is a translation: the copy shows the same cell.
      CHECK(s->pickPixel(x, y, kW, kH) == local);
    }
  }
}

TEST_CASE("a copy through a twisted seam picks the reflected source", "[unit][app]") {
  auto s = open("mobius");
  s->setAdjacent(on(1));
  const auto copies = view::adjacentCopies(s->variant(), s->viewConfig(), 1);
  int checked = 0;
  for (const auto& copy : copies) {
    if (copy.order == 0) continue;
    for (const auto& pl : s->placements()) {
      const CellId source = view::sourceCell(copy, pl.cell, s->variant());
      const auto [x, y] = pixelOf(*s, copy.stepU, copy.stepV, pl.cell);
      INFO("copy " << int(copy.stepU) << "," << int(copy.stepV));
      CHECK(s->pickPixel(x, y, kW, kH) == source);
      ++checked;
    }
  }
  CHECK(checked > 0);
}

TEST_CASE("with the switch off, or copies ignored, a copy's pixel is nothing",
          "[unit][app]") {
  auto s = open("cylinder");
  const CellId c = cell(*s, "d4");
  s->setAdjacent(on(1));
  const auto [x, y] = pixelOf(*s, 1, 0, c);
  CHECK(s->pickPixel(x, y, kW, kH) == c);

  auto off = on(1);
  off.on = false;
  s->setAdjacent(off);
  CHECK(s->pickPixel(x, y, kW, kH) == kInvalidCell);

  auto ignore = on(1);
  ignore.pick = false;
  s->setAdjacent(ignore);
  CHECK(s->pickPixel(x, y, kW, kH) == kInvalidCell);
  // The centre is untouched either way.
  const auto [cx, cy] = pixelOf(*s, 0, 0, c);
  CHECK(s->pickPixel(cx, cy, kW, kH) == c);

  // Mid-reveal the copies are moving: they are not pickable until they have arrived.
  auto reveal = on(1);
  reveal.amount = 0.5f;
  s->setAdjacent(reveal);
  CHECK(s->pickPixel(x, y, kW, kH) == kInvalidCell);
}

TEST_CASE("a variant with no copies picks as ever", "[unit][app]") {
  auto s = open("standard");
  s->setAdjacent(on(2));
  const CellId e4 = cell(*s, "e4");
  for (const auto& pl : s->placements()) {
    if (pl.cell != e4) continue;
    const auto sp = s->camera().project(view::Vec3{pl.x, pl.y, pl.z}, kW / kH, kW, kH);
    CHECK(s->pickPixel(sp.x, sp.y, kW, kH) == e4);
  }
  CHECK(view::adjacentCopies(s->variant(), s->viewConfig(), 2).empty());
}

TEST_CASE("hovering a copy marks the source and names the copy", "[unit][app]") {
  auto s = open("torus");
  s->setAdjacent(on(1));
  const CellId c = cell(*s, "b2");
  const auto [x, y] = pixelOf(*s, 1, -1, c);
  const Session::Pick hit = s->pick(x, y, kW, kH);
  REQUIRE(hit.cell == c);
  CHECK(hit.stepU == 1);
  CHECK(hit.stepV == -1);
  s->setHover(hit.cell, false, hit.stepU, hit.stepV);
  CHECK(s->snapshot().hover() == c);
  CHECK(s->hoverCopyU() == 1);
  CHECK(s->hoverCopyV() == -1);
  // Back on the centre: no copy is named.
  const auto [cx, cy] = pixelOf(*s, 0, 0, c);
  const Session::Pick centre = s->pick(cx, cy, kW, kH);
  CHECK(centre.cell == c);
  CHECK(centre.stepU == 0);
  CHECK(centre.stepV == 0);
}

TEST_CASE("a click on a copy selects and moves the source piece", "[unit][app]") {
  auto s = open("torus");
  s->setAdjacent(on(1));
  const CellId a2 = cell(*s, "a2");
  CHECK(s->selected() == kInvalidCell);
  // The a2 pawn, through the copy one board to the right.
  const auto [x, y] = pixelOf(*s, 1, 0, a2);
  CHECK(s->clickPixel(x, y, kW, kH) == a2);
  CHECK(s->selected() == a2);
  // And its destination through another copy.
  const CellId a3 = cell(*s, "a3");
  const auto [dx, dy] = pixelOf(*s, -1, 0, a3);
  CHECK(s->clickPixel(dx, dy, kW, kH) == a3);
  CHECK(s->selected() == kInvalidCell);  // the move was played
  CHECK_FALSE(s->game().position().at(a3).empty());
}

TEST_CASE("the hover script verb names a copy", "[unit][app]") {
  auto s = open("torus");
  REQUIRE(s->applyScript("hover b2 copy 1 -1").has_value());
  CHECK(s->snapshot().hover() == cell(*s, "b2"));
  CHECK(s->hoverCopyU() == 1);
  CHECK(s->hoverCopyV() == -1);
  REQUIRE(s->applyScript("hover b2 ctrl").has_value());
  CHECK(s->hoverCopyU() == 0);
  REQUIRE(s->applyScript("hover b2 copy 2 0\nhover off").has_value());
  CHECK(s->hoverCopyU() == 0);
  CHECK_FALSE(s->applyScript("hover b2 copy 9 0").has_value());
  CHECK_FALSE(s->applyScript("hover b2 sideways").has_value());
}
