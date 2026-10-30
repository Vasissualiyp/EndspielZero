// SPDX-License-Identifier: GPL-3.0-or-later
//
// The flat board's pieces are data, so the things that make an icon set usable can be
// checked rather than eyeballed: every archetype has one, nothing escapes its box, and
// the cheap set is actually cheap.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "assets/piece_model.hpp"
#include "render/piece_icon.hpp"
#include "render/piece_mesh.hpp"
#include "support/variants.hpp"
#include "render/ui_widgets.hpp"

using namespace cb;
using namespace cb::render;

namespace {

constexpr Archetype kPieces[] = {Archetype::Dome,  Archetype::Tower, Archetype::Wedge,
                                 Archetype::Spire, Archetype::Crown, Archetype::Monolith,
                                 Archetype::Horn};

/// Twice the signed area: zero means the outline has no interior to fill.
float doubleArea(IconPoly poly) {
  float a = 0;
  for (std::size_t i = 0; i < poly.size(); ++i) {
    const IconPoint& p = poly[i];
    const IconPoint& q = poly[(i + 1) % poly.size()];
    a += p.x * q.y - q.x * p.y;
  }
  return a;
}

}  // namespace

TEST_CASE("every piece archetype has an icon in both styles", "[render]") {
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    for (const Archetype a : kPieces) {
      const PieceIcon icon = pieceIcon(style, a);
      INFO(archetypeName(a) << " / " << iconStyleName(style));
      REQUIRE_FALSE(icon.fills.empty());
      for (const IconPoly& poly : icon.fills) {
        CHECK(poly.size() >= 3);
        CHECK(std::abs(doubleArea(poly)) > 1.0f);
      }
    }
  }
}

TEST_CASE("an archetype nobody anticipated still gets a piece", "[render]") {
  // A variant may declare something the renderer has never heard of. Drawing nothing
  // would make it unplayable in the flat view, which is worse than drawing a slab.
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    CHECK_FALSE(pieceIcon(style, static_cast<Archetype>(200)).fills.empty());
  }
  // The board's own cell is not a piece, and neither is a portal.
  CHECK(pieceIcon(IconStyle::Faceted, Archetype::Cell).fills.empty());
  CHECK(pieceIcon(IconStyle::Faceted, Archetype::Portal).fills.empty());
}

TEST_CASE("no icon escapes its box", "[render]") {
  // Everything is authored in a 100x100 box and scaled to the cell. A stray point is a
  // piece that overlaps its neighbours, which is exactly how the old set went wrong.
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    for (const Archetype a : kPieces) {
      const PieceIcon icon = pieceIcon(style, a);
      for (const IconPoly& poly : icon.fills) {
        for (const IconPoint& p : poly) {
          INFO(archetypeName(a) << " point " << p.x << "," << p.y);
          CHECK(p.x >= 0.0f);
          CHECK(p.x <= 100.0f);
          CHECK(p.y >= 0.0f);
          CHECK(p.y <= 100.0f);
        }
      }
    }
  }
}

TEST_CASE("the cheap set stays cheap", "[render]") {
  // The reason it exists at all. If it drifts towards the faceted set in complexity it
  // has stopped being an option and become a second default.
  for (const Archetype a : kPieces) {
    const PieceIcon icon = pieceIcon(IconStyle::Primitive, a);
    INFO(archetypeName(a));
    CHECK(icon.fills.size() <= 4);
    CHECK(icon.cuts.empty());
    std::size_t points = 0;
    for (const IconPoly& poly : icon.fills) points += poly.size();
    CHECK(points <= 24);
  }
}

TEST_CASE("every icon stands on the same line", "[render]") {
  // A set whose pieces sit at different heights reads as a set of unrelated drawings.
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    float foot = -1.0f;
    for (const Archetype a : kPieces) {
      float lowest = 0;
      for (const IconPoly& poly : pieceIcon(style, a).fills) {
        for (const IconPoint& p : poly) lowest = std::max(lowest, p.y);
      }
      if (foot < 0) foot = lowest;
      INFO(archetypeName(a) << " foot " << lowest << " expected " << foot);
      CHECK(std::abs(lowest - foot) < 0.01f);
    }
  }
}

TEST_CASE("a style name survives a round trip through the settings file", "[render]") {
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    CHECK(iconStyleFromName(iconStyleName(style)) == style);
  }
  // An unrecognised name is the default, not an error: a settings file from a newer
  // build must still load.
  CHECK(iconStyleFromName("nonsense") == IconStyle::Faceted);
}

// ---------------------------------------------------------------------------
// Time's arrow. A grid of boards does not say which way the game runs through it, and on
// a board with a turn axis that is the first thing a player has to know.
// ---------------------------------------------------------------------------
#ifdef CB_HAVE_VULKAN
#include "render/board_renderer.hpp"
#include "support/variants.hpp"
#include "view/snapshot.hpp"

TEST_CASE("a temporal board draws a rail under each timeline", "[render]") {
  using namespace cb;
  BoardRenderer renderer;

  const VariantSpec flat = test::loadVariant("standard");
  const Position fp = Position::startPosition(flat);
  const InstanceSet plain = renderer.buildInstances(
      view::PositionView::capture(fp), view::ViewConfig::forBoard(flat.dims));
  // A board with no turn axis has no arrow to draw at all.
  CHECK(plain.batches[static_cast<std::size_t>(Archetype::Arrow)].count == 0);

  const VariantSpec five = test::loadVariant("5d");
  const Position sp = Position::startPosition(five);
  const InstanceSet temporal = renderer.buildInstances(
      view::PositionView::capture(sp), view::ViewConfig::forBoard(five.dims));
  // Every timeline gets a head, and the head is its own shape rather than a chess
  // piece's wedge - so this cannot pass just because a variant happens to field knights.
  CHECK(temporal.batches[static_cast<std::size_t>(Archetype::Arrow)].count > 0);
}

TEST_CASE("a valley in an outline stays a valley", "[render]") {
  // The bug this exists for: a notch cut into a piece's outline came back filled, so the
  // notch disappeared. A fill that triangulates properly conserves the polygon's area;
  // one that treats the shape as convex invents the area of the notch.
  const auto area = [](const std::vector<ImVec2>& p) {
    float a = 0.0f;
    for (std::size_t i = 0; i < p.size(); ++i) {
      const ImVec2& u = p[i];
      const ImVec2& v = p[(i + 1) % p.size()];
      a += u.x * v.y - v.x * u.y;
    }
    return std::abs(a) * 0.5f;
  };
  const auto triArea = [](const std::vector<ImVec2>& p, const std::vector<int>& idx) {
    float a = 0.0f;
    for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
      const ImVec2& u = p[static_cast<std::size_t>(idx[i])];
      const ImVec2& v = p[static_cast<std::size_t>(idx[i + 1])];
      const ImVec2& w = p[static_cast<std::size_t>(idx[i + 2])];
      a += std::abs((v.x - u.x) * (w.y - u.y) - (w.x - u.x) * (v.y - u.y)) * 0.5f;
    }
    return a;
  };

  SECTION("a square notch cut into the top of a block") {
    const std::vector<ImVec2> p{{0, 100}, {100, 100}, {100, 0}, {70, 0},
                                {70, 60}, {30, 60},   {30, 0},  {0, 0}};
    std::vector<int> idx;
    widgets::triangulate(p.data(), static_cast<int>(p.size()), idx);
    REQUIRE(idx.size() == (p.size() - 2) * 3);
    CHECK(std::abs(triArea(p, idx) - area(p)) < 1.0f);
    // And the notch really is missing area: a convex reading would give the full block.
    CHECK(area(p) < 100.0f * 100.0f - 100.0f);
  }

  SECTION("wound the other way round, it fills the same") {
    std::vector<ImVec2> p{{0, 100}, {100, 100}, {100, 0}, {70, 0},
                          {70, 60}, {30, 60},   {30, 0},  {0, 0}};
    std::reverse(p.begin(), p.end());
    std::vector<int> idx;
    widgets::triangulate(p.data(), static_cast<int>(p.size()), idx);
    REQUIRE(idx.size() == (p.size() - 2) * 3);
    CHECK(std::abs(triArea(p, idx) - area(p)) < 1.0f);
  }

  SECTION("every shipped icon outline triangulates") {
    for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
      for (int a = 1; a <= 7; ++a) {
        const PieceIcon icon = pieceIcon(style, static_cast<Archetype>(a));
        for (const IconPoly& poly : icon.fills) {
          std::vector<ImVec2> pts;
          for (const IconPoint& q : poly) pts.push_back(ImVec2(q.x, q.y));
          std::vector<int> idx;
          widgets::triangulate(pts.data(), static_cast<int>(pts.size()), idx);
          INFO("archetype " << a << " with " << pts.size() << " points");
          CHECK(idx.size() == (pts.size() - 2) * 3);
        }
      }
    }
  }

  SECTION("every body profile the designer draws fills without overlap") {
    // The designer fills the profile with this triangulator on a y-up screen mapping. A
    // fill that conserves area has no overlapping triangles, which is what showed up as
    // dark wedges across the rook's profile.
    for (const char* name : {"dome", "tower", "wedge", "spire", "crown", "monolith", "horn"}) {
      const assets::PieceModel m = assets::archetypeModel(name);
      std::vector<ImVec2> pts;
      for (const assets::ModelPoint& q : m.profile.points) {
        pts.push_back(ImVec2(static_cast<float>(q.x) * 0.22f,
                             220.0f - static_cast<float>(q.y) * 0.22f));
      }
      std::vector<int> idx;
      widgets::triangulate(pts.data(), static_cast<int>(pts.size()), idx);
      INFO(name);
      REQUIRE(idx.size() == (pts.size() - 2) * 3);
      CHECK(std::abs(triArea(pts, idx) - area(pts)) < 1.0f);
    }
  }

  SECTION("an outline that is not a shape is caught before it is drawn") {
    // Not by the filler - ear clipping fills, it does not validate, and on four points
    // it will happily halve a bow tie. Whether an outline crosses itself is a question
    // about the *model*, and the designers ask it there before an author can save.
    assets::IconModel bad;
    bad.fills.push_back(assets::Outline{{{0, 0}, {400, 0}, {0, 400}, {400, 400}}});
    CHECK_FALSE(assets::validate(bad).has_value());
  }
}

#endif

TEST_CASE("a queen with triagonals is still a queen, and only a unicorn is a horn",
          "[render]") {
  // The queen rides every straight line, cube diagonals included, so her atoms contain
  // the unicorn's. Classifying by "has a triagonal" turned every queen on a 3-D board
  // into a unicorn; what makes a unicorn a unicorn is having nothing else.
  for (const char* name : {"cube5", "hyper4_8x8", "torus3d_8x8"}) {
    const VariantSpec v = test::loadVariant(name);
    INFO(name);
    CHECK(archetypeFor(v.pieces[v.findPiece("queen")]) == Archetype::Crown);
  }
  const VariantSpec cube = test::loadVariant("cube5");
  CHECK(archetypeFor(cube.pieces[cube.findPiece("unicorn")]) == Archetype::Horn);
}

TEST_CASE("an authored body assembles into a sound, deterministic mesh", "[render][model]") {
  for (const char* name : {"dome", "tower", "wedge", "spire", "crown", "monolith", "horn"}) {
    INFO(name);
    const assets::PieceModel m = assets::archetypeModel(name);
    const ModelMesh a = assembleModel(m);
    const ModelMesh b = assembleModel(m);
    REQUIRE_FALSE(a.vertices.empty());
    REQUIRE(a.indices.size() % 3 == 0);
    CHECK(a.vertices.size() < 65536);
    CHECK(a.indices == b.indices);
    CHECK(a.height > 0.5f);
    for (const std::uint16_t i : a.indices) REQUIRE(i < a.vertices.size());
    // Every triangle faces the way its vertices say it does: nothing is inside out.
    for (std::size_t t = 0; t + 2 < a.indices.size(); t += 3) {
      const auto& p = a.vertices[a.indices[t]];
      const auto& q = a.vertices[a.indices[t + 1]];
      const auto& r = a.vertices[a.indices[t + 2]];
      const float ux = q.pos[0] - p.pos[0], uy = q.pos[1] - p.pos[1], uz = q.pos[2] - p.pos[2];
      const float vx = r.pos[0] - p.pos[0], vy = r.pos[1] - p.pos[1], vz = r.pos[2] - p.pos[2];
      const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      const float agree = nx * (p.normal[0] + q.normal[0] + r.normal[0]) +
                          ny * (p.normal[1] + q.normal[1] + r.normal[1]) +
                          nz * (p.normal[2] + q.normal[2] + r.normal[2]);
      REQUIRE(agree >= 0.0f);
    }
  }

  SECTION("a k-fold symmetry places k copies, and a mirror places a reflected pair") {
    assets::PieceModel rook = assets::archetypeModel("tower");
    const std::size_t bare = [&] {
      assets::PieceModel p = rook;
      p.elements.clear();
      return assembleModel(p).vertices.size();
    }();
    const std::size_t four = assembleModel(rook).vertices.size() - bare;
    rook.symmetry.k = 8;
    const std::size_t eight = assembleModel(rook).vertices.size() - bare;
    CHECK(eight == four * 2);

    assets::PieceModel knight = assets::archetypeModel("wedge");
    const std::size_t one = assembleModel(knight).vertices.size();
    knight.elements[0].lateral = 100;
    const ModelMesh pair = assembleModel(knight);
    CHECK(pair.vertices.size() > one);
    // The pair really is a reflection: every point has a partner across the plane y = 0.
    for (const MeshVertex& v : pair.vertices) {
      bool found = false;
      for (const MeshVertex& w : pair.vertices) {
        if (std::abs(w.pos[0] - v.pos[0]) < 1e-4f && std::abs(w.pos[1] + v.pos[1]) < 1e-4f &&
            std::abs(w.pos[2] - v.pos[2]) < 1e-4f) {
          found = true;
          break;
        }
      }
      REQUIRE(found);
    }
  }
}

TEST_CASE("authored bodies take custom slots after the archetypes", "[render][model]") {
  const MeshLibrary plain = MeshLibrary::build();
  CustomBody knight;
  knight.slot = 2;
  knight.fallback = Archetype::Wedge;
  knight.body = assets::archetypeModel("wedge");
  const MeshLibrary with = MeshLibrary::build({knight});

  // The shipped shapes are exactly what they were: nothing is renumbered or rebuilt.
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    CHECK(with.ranges[i].firstIndex == plain.ranges[i].firstIndex);
    CHECK(with.ranges[i].indexCount == plain.ranges[i].indexCount);
    CHECK(with.ranges[i].vertexOffset == plain.ranges[i].vertexOffset);
  }
  const std::size_t at = static_cast<std::size_t>(customShape(2));
  CHECK(with.ranges[at].indexCount > 0);
  CHECK(with.cylinderRanges[at].indexCount > 0);  // its puck, from the fallback icon
  CHECK(with.discRanges[at].indexCount > 0);
  // An unused slot draws nothing.
  CHECK(with.ranges[static_cast<std::size_t>(customShape(0))].indexCount == 0);
  CHECK(with.vertices.size() > plain.vertices.size());
  for (const std::uint16_t i : with.indices) REQUIRE(i < 65535);
}

TEST_CASE("an authored icon becomes a drawable table", "[render][model]") {
  assets::IconModel model;
  assets::Outline fill;
  fill.points = {{100, 100}, {900, 100}, {900, 900}, {100, 900}};
  assets::Outline cut;
  cut.points = {{400, 400}, {600, 400}, {600, 600}, {400, 600}};
  model.fills.push_back(fill);
  model.cuts.push_back(cut);
  const OwnedIcon icon(model);
  const PieceIcon view = icon.view();
  REQUIRE(view.fills.size() == 1);
  REQUIRE(view.cuts.size() == 1);
  CHECK(view.fills[0][0].x == 10.0f);  // permille to the 100-unit glyph box
  CHECK(view.fills[0][2].y == 90.0f);
  CHECK(view.cuts[0].size() == 4);
  // Moving it keeps the points the spans refer to.
  const OwnedIcon moved = OwnedIcon(model);
  CHECK(moved.view().fills[0][1].x == 90.0f);
}
