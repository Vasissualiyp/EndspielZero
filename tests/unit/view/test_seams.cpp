// SPDX-License-Identifier: GPL-3.0-or-later
//
// A glued board is only readable if the seams say where you come out. These tests pin
// the one property that makes that true: both ends of an identification are the same
// colour, and a twist therefore shows up as a ramp that runs the other way.
#include <catch2/catch_test_macros.hpp>

#include "support/variants.hpp"
#include "view/seams.hpp"

using namespace cb;
using namespace cb::view;

namespace {

Theme theme() {
  return Theme::console();
}

/// The colour drawn on one face of one cell, or nullopt if that face is not a seam.
const SeamFace* faceOf(const SeamMap& m, CellId cell, std::uint8_t screenAxis,
                       Side side) {
  for (const SeamFace& f : m.at(cell)) {
    if (f.screenAxis == screenAxis && f.side == side) return &f;
  }
  return nullptr;
}

bool sameColor(const Rgba& a, const Rgba& b) {
  const float e = 1e-4f;
  return std::abs(a.r - b.r) < e && std::abs(a.g - b.g) < e && std::abs(a.b - b.b) < e;
}

}  // namespace

TEST_CASE("a plain box has no seams at all", "[unit][view]") {
  const VariantSpec v = test::loadVariant("standard");
  const SeamMap m = SeamMap::build(v, ViewConfig::forBoard(v.dims), theme());
  CHECK(m.empty());
}

TEST_CASE("a cylinder's two edges carry the same ramp in the same direction",
          "[unit][view]") {
  const VariantSpec v = test::loadVariant("cylinder");
  const SeamMap m = SeamMap::build(v, ViewConfig::forBoard(v.dims), theme());
  REQUIRE_FALSE(m.empty());

  std::vector<Rgba> left, right;
  for (int rank = 0; rank < 8; ++rank) {
    const CellId lo = v.dims.toCell(Coord::of({0, rank}));
    const CellId hi = v.dims.toCell(Coord::of({7, rank}));
    const SeamFace* a = faceOf(m, lo, 0, Side::Min);
    const SeamFace* b = faceOf(m, hi, 0, Side::Max);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->kind == SeamKind::Glued);
    CHECK(a->partner == hi);
    CHECK(b->partner == lo);
    // The two ends of one portal are one colour: that is the whole idea.
    CHECK(sameColor(a->color, b->color));
    left.push_back(a->color);
    right.push_back(b->color);
  }
  // And the ramp actually varies, or it would say nothing.
  CHECK_FALSE(sameColor(left.front(), left.back()));
  for (std::size_t i = 0; i < left.size(); ++i) CHECK(sameColor(left[i], right[i]));
}

TEST_CASE("a Moebius seam shows its twist as a reversed ramp", "[unit][view]") {
  const VariantSpec v = test::loadVariant("mobius");
  const SeamMap m = SeamMap::build(v, ViewConfig::forBoard(v.dims), theme());
  REQUIRE_FALSE(m.empty());

  for (int rank = 0; rank < 8; ++rank) {
    const CellId hi = v.dims.toCell(Coord::of({7, rank}));
    const SeamFace* b = faceOf(m, hi, 0, Side::Max);
    REQUIRE(b != nullptr);
    // Leaving the h-file at this rank arrives on the a-file at the mirrored rank, so
    // that is the cell whose colour must match - and it is *not* the same rank, which
    // is exactly what distinguishes this board from a cylinder.
    const CellId lo = v.dims.toCell(Coord::of({0, 7 - rank}));
    CHECK(b->partner == lo);
    const SeamFace* a = faceOf(m, lo, 0, Side::Min);
    REQUIRE(a != nullptr);
    CHECK(sameColor(a->color, b->color));
  }

  // The distinguishing fact, stated directly: on a cylinder the same rank matches,
  // here it does not.
  const VariantSpec cyl = test::loadVariant("cylinder");
  const SeamMap cm = SeamMap::build(cyl, ViewConfig::forBoard(cyl.dims), theme());
  const SeamFace* mobiusRight = faceOf(m, v.dims.toCell(Coord::of({7, 1})), 0, Side::Max);
  const SeamFace* mobiusLeftSameRank =
      faceOf(m, v.dims.toCell(Coord::of({0, 1})), 0, Side::Min);
  REQUIRE(mobiusRight != nullptr);
  REQUIRE(mobiusLeftSameRank != nullptr);
  CHECK_FALSE(sameColor(mobiusRight->color, mobiusLeftSameRank->color));
  CHECK_FALSE(cm.empty());
}

TEST_CASE("a mirror is silver and sends you back to yourself", "[unit][view]") {
  const VariantSpec v = test::loadVariant("mirrorbox");
  const SeamMap m = SeamMap::build(v, ViewConfig::forBoard(v.dims), theme());
  REQUIRE_FALSE(m.empty());
  for (const SeamFace& f : m.faces()) {
    CHECK(f.kind == SeamKind::Mirror);
    CHECK(f.partner == f.cell);
    CHECK(sameColor(f.color, theme().mirrorEdge));
  }
}

TEST_CASE("the legend names the axis of each glued edge, and hides mirrors",
          "[unit][view]") {
  const auto legendFor = [](const char* name) {
    const VariantSpec v = test::loadVariant(name);
    const SeamMap m = SeamMap::build(v, ViewConfig::forBoard(v.dims), theme());
    return seamLegend(m, ViewConfig::forBoard(v.dims), v.dims);
  };

  // A plain box glues nothing, so there is nothing to explain.
  CHECK(legendFor("standard").empty());
  // A cylinder glues one axis; a torus both.
  const auto cyl = legendFor("cylinder");
  REQUIRE(cyl.size() == 1);
  CHECK(cyl[0].axis == "file");
  const auto torus = legendFor("torus");
  REQUIRE(torus.size() == 2);
  CHECK(torus[0].axis == "file");
  CHECK(torus[1].axis == "rank");
  // A Moebius band has one glued axis, twist and all; the legend still names it.
  const auto mob = legendFor("mobius");
  REQUIRE(mob.size() == 1);
  CHECK(mob[0].axis == "file");
  // A mirror leads nowhere, so a colour that promised a destination would be a lie.
  CHECK(legendFor("mirrorbox").empty());
}

TEST_CASE("seamLegend: each entry names its drawn axis", "[unit][view][seams]") {
  for (const char* name : {"cylinder", "torus", "klein", "mirrorbox"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const ViewConfig cfg = ViewConfig::forBoard(v.dims);
    const SeamMap m = SeamMap::build(v, cfg, theme());
    const auto legend = seamLegend(m, cfg, v.dims);
    if (std::string_view(name) == "mirrorbox") {
      CHECK(legend.empty());
    } else if (std::string_view(name) == "cylinder") {
      REQUIRE(legend.size() == 1);
      for (const SeamFace& f : m.faces()) {
        if (f.kind == SeamKind::Glued) CHECK(legend[0].screenAxis == f.screenAxis);
      }
    } else {
      REQUIRE(legend.size() == 2);
      for (std::size_t i = 0; i < legend.size(); ++i) {
        CHECK(legend[i].screenAxis == i);
        CHECK(legend[i].axis == v.dims.name(cfg.screenAxes[i]));
      }
    }
  }
}
