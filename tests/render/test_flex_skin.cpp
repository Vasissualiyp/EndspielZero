// SPDX-License-Identifier: GPL-3.0-or-later
//
// The flexible skin (M25.4): in shape view the squares themselves bend into the shape, as
// one finely tessellated sheet with smooth normals and tile borders drawn in the
// material, while the flat state stays the M24.5b slab. No GPU: the tests read the
// tessellation, the surface it samples, and the instance set the renderer builds
// headlessly.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#ifdef CB_HAVE_IMGUI

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

#include "app/session.hpp"
#include "render/board_renderer.hpp"
#include "render/lab_rig.hpp"
#include "render/play_surface.hpp"
#include "render/slab_skin.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::render;
using Catch::Matchers::WithinAbs;

namespace {

const VariantSpec& variant(const char* name) {
  static std::vector<std::pair<std::string, VariantSpec*>> cache;
  for (const auto& [n, v] : cache)
    if (n == name) return *v;
  cache.emplace_back(name, new VariantSpec(test::loadVariant(name)));
  return *cache.back().second;
}

PlaySurface flexAt(const char* name, float formed, float flex) {
  SurfacePose pose;
  pose.formed = formed;
  pose.flex = flex;
  return PlaySurface::build(variant(name), pose);
}

const FlexTile& tileAt(const PlaySurface& s, int file, int rank) {
  for (const FlexTile& t : s.flexTiles())
    if (t.file == file && t.rank == rank) return t;
  FAIL("no tile at " << file << "," << rank);
  return s.flexTiles().front();
}

std::size_t at(int i, int j) {
  return static_cast<std::size_t>(i * kFlexSide + j);
}

float angleDeg(const view::Vec3& a, const view::Vec3& b) {
  return std::acos(std::clamp(view::dot(a, b), -1.0f, 1.0f)) * 57.29578f;
}

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

constexpr const char* kShapes[] = {"cylinder", "torus", "mobius", "klein"};

}  // namespace

TEST_CASE("the flexible skin takes over from the slab as the shape forms",
          "[render][skin][flex]") {
  CHECK(flexBlend(0.0f) == 0.0f);
  CHECK(flexBlend(-1.0f) == 0.0f);
  CHECK(flexBlend(kFlexFormEnd) == 1.0f);
  CHECK(flexBlend(1.0f) == 1.0f);
  CHECK(flexBlend(1e-3f) > 0.0f);
  float last = 0.0f;
  for (int i = 1; i <= 1000; ++i) {
    const float b = flexBlend(static_cast<float>(i) / 1000.0f);
    CHECK(b >= last);
    CHECK(b - last < 0.02f);  // continuous: no step at any morph point
    last = b;
  }
}

TEST_CASE("the flexible skin is built only when asked, one tile per cell",
          "[render][skin][flex]") {
  for (const char* name : kShapes) {
    CAPTURE(name);
    CHECK(flexAt(name, 1.0f, 0.0f).flexTiles().empty());
    const auto s = flexAt(name, 1.0f, 1.0f);
    REQUIRE(s.flexTiles().size() == 64);
    // The flat fractions: both borders, the seam line just inside them, an even grid.
    const auto& f = flexFractions();
    CHECK(f.front() == 0.0f);
    CHECK(f.back() == 1.0f);
    CHECK_THAT(f[1], WithinAbs(kFlexSeam, 1e-6));
    CHECK_THAT(f[kFlexSide - 2], WithinAbs(1.0f - kFlexSeam, 1e-6));
    for (std::size_t k = 1; k < f.size(); ++k) CHECK(f[k] > f[k - 1]);
    // No M24.5b hardware alongside it.
    CHECK(s.membranePatches().empty());
  }
  // A stacked D >= 3 shape is not a sheet.
  SurfacePose pose;
  pose.flex = 1.0f;
  CHECK(PlaySurface::build(variant("torus3d"), pose).flexTiles().empty());
}

TEST_CASE("the flexible skin has no gaps and its normals run smoothly across tiles",
          "[render][skin][flex]") {
  for (const char* name : kShapes) {
    for (const float formed : {0.5f, 1.0f}) {
      CAPTURE(name, formed);
      const auto s = flexAt(name, formed, 1.0f);
      REQUIRE(s.flexTiles().size() == 64);
      const int nx = s.files(), nz = s.ranks();
      const int last = kFlexSide - 1;
      float worstGap = 0.0f, worstTurn = 0.0f;
      // Along the files: the high-u border of (f, r) is the low-u border of (f + 1, r);
      // across a wrap that does not flip (cylinder, torus) the last file meets the first.
      for (int f = 0; f < nx; ++f) {
        // A wrap's two edges meet only once its axis has closed (formed 1).
        if (f + 1 == nx && (!s.wraps(0) || s.flipsAcross(0) || formed < 1.0f)) continue;
        for (int r = 0; r < nz; ++r) {
          const auto& a = tileAt(s, f, r);
          const auto& b = tileAt(s, (f + 1) % nx, r);
          for (int j = 0; j < kFlexSide; ++j) {
            worstGap =
                std::max(worstGap, view::length(a.pos[at(last, j)] - b.pos[at(0, j)]));
            worstTurn =
                std::max(worstTurn, angleDeg(a.normal[at(last, j)], b.normal[at(0, j)]));
          }
        }
      }
      for (int f = 0; f < nx; ++f) {
        for (int r = 0; r < nz; ++r) {
          if (r + 1 == nz && (!s.wraps(1) || s.flipsAcross(1) || formed < 1.0f)) continue;
          const auto& a = tileAt(s, f, r);
          const auto& b = tileAt(s, f, (r + 1) % nz);
          for (int i = 0; i < kFlexSide; ++i) {
            worstGap =
                std::max(worstGap, view::length(a.pos[at(i, last)] - b.pos[at(i, 0)]));
            worstTurn =
                std::max(worstTurn, angleDeg(a.normal[at(i, last)], b.normal[at(i, 0)]));
          }
        }
      }
      CAPTURE(worstGap, worstTurn);
      CHECK(worstGap < 1e-3f);
      CHECK(worstTurn < 1.0f);
    }
  }
}

TEST_CASE("the flexible skin is fine enough that no facet shows",
          "[render][skin][flex]") {
  // Chord error: the midpoint of every fine edge against the surface itself, in cells.
  // At 1280x720 the formed shapes span at most ~45 px per cell from the game camera, so
  // a 2x crop is ~90 px per cell and kFlexChordCells (0.02) is under 2 px.
  const auto& fractions = flexFractions();
  const auto fr = [&](int k) { return fractions[static_cast<std::size_t>(k)]; };
  for (const char* name : kShapes) {
    for (const float formed : {0.5f, 1.0f}) {
      CAPTURE(name, formed);
      const auto s = flexAt(name, formed, 1.0f);
      float worstChord = 0.0f, worstStep = 0.0f;
      int worstFile = -1, worstRank = -1;
      for (const FlexTile& t : s.flexTiles()) {
        // The Klein bottle's pass-through band is drawn as light, not skin.
        if (std::binary_search(s.passThrough().begin(), s.passThrough().end(), t.cell))
          continue;
        const float before = worstStep;
        const auto param = [&](float a, float b) {
          view::Vec3 p{};
          REQUIRE(s.pointAt(static_cast<float>(t.file) + a,
                            static_cast<float>(t.rank) + b, p));
          return p;
        };
        for (int i = 0; i < kFlexSide; ++i) {
          for (int j = 0; j < kFlexSide; ++j) {
            const auto k = at(i, j);
            if (i + 1 < kFlexSide) {
              const view::Vec3 mid = (t.pos[k] + t.pos[at(i + 1, j)]) * 0.5f;
              const view::Vec3 truth = param(0.5f * (fr(i) + fr(i + 1)), fr(j));
              worstChord = std::max(worstChord, view::length(mid - truth));
              worstStep =
                  std::max(worstStep, angleDeg(t.normal[k], t.normal[at(i + 1, j)]));
            }
            if (j + 1 < kFlexSide) {
              const view::Vec3 mid = (t.pos[k] + t.pos[at(i, j + 1)]) * 0.5f;
              const view::Vec3 truth = param(fr(i), 0.5f * (fr(j) + fr(j + 1)));
              worstChord = std::max(worstChord, view::length(mid - truth));
              worstStep =
                  std::max(worstStep, angleDeg(t.normal[k], t.normal[at(i, j + 1)]));
            }
            if (i + 1 < kFlexSide && j + 1 < kFlexSide) {
              const view::Vec3 mid = (t.pos[k] + t.pos[at(i + 1, j + 1)]) * 0.5f;
              const view::Vec3 truth =
                  param(0.5f * (fr(i) + fr(i + 1)), 0.5f * (fr(j) + fr(j + 1)));
              worstChord = std::max(worstChord, view::length(mid - truth));
            }
          }
        }
        if (worstStep > before) {
          worstFile = t.file;
          worstRank = t.rank;
        }
      }
      CAPTURE(worstChord, worstStep, worstFile, worstRank, s.passThrough().size());
      CHECK(worstChord <= kFlexChordCells);
      // Normals turn gently between grid lines; the Klein figure-eight folds tightly
      // near its lobes (true curvature: the step halves as the grid doubles), where the
      // chord error above is still the measure that matters.
      CHECK(worstStep < (std::string(name) == "klein" ? 45.0f : 15.0f));
    }
  }
}

TEST_CASE("the flexible skin closes its reversing seams without cracks",
          "[render][skin][flex]") {
  for (const char* name : {"mobius", "klein"}) {
    CAPTURE(name);
    const auto s = flexAt(name, 1.0f, 1.0f);
    const int last = kFlexSide - 1;
    const bool fileFlip = s.flipsAcross(0);
    const int count = fileFlip ? s.ranks() : s.files();
    for (int c = 0; c < count; ++c) {
      const auto& a =
          fileFlip ? tileAt(s, s.files() - 1, c) : tileAt(s, c, s.ranks() - 1);
      const auto& b =
          fileFlip ? tileAt(s, 0, count - 1 - c) : tileAt(s, count - 1 - c, 0);
      for (int k = 0; k <= last; ++k) {
        const auto ka = fileFlip ? at(last, k) : at(k, last);
        const auto kb = fileFlip ? at(0, last - k) : at(last - k, 0);
        CHECK(view::length(a.pos[ka] - b.pos[kb]) < 1e-3f);
        // A non-orientable seam can exchange front and back. Both faces are drawn;
        // their tangent planes must agree even where the chosen normals reverse.
        CHECK(std::abs(view::dot(a.normal[ka], b.normal[kb])) > 0.999f);
      }
    }
  }
}

TEST_CASE("the flexible skin's normals face the side the pieces stand on",
          "[render][skin][flex]") {
  for (const char* name : kShapes) {
    for (const float evert : {0.0f, 1.0f}) {
      CAPTURE(name, evert);
      SurfacePose pose;
      pose.flex = 1.0f;
      pose.evert = evert;
      const auto s = PlaySurface::build(variant(name), pose);
      REQUIRE(s.flexTiles().size() == s.seats().size());
      for (const FlexTile& t : s.flexTiles()) {
        const auto seat = std::find_if(s.seats().begin(), s.seats().end(),
                                       [&](const auto& x) { return x.cell == t.cell; });
        REQUIRE(seat != s.seats().end());
        const auto c = at(kFlexSide / 2, kFlexSide / 2);
        CHECK(view::dot(t.normal[c], seat->normal) > 0.99f);
        CHECK(view::length(t.pos[c] - seat->centre) < 1e-3f);
      }
    }
  }
}

TEST_CASE("the yoke holds the skin at one point, from behind", "[render][skin][flex]") {
  const auto lab = shippedLab();
  for (const char* name : kShapes) {
    CAPTURE(name);
    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session);
    const auto& v = (*session)->variant();
    const auto layout = rigLayout(lab, shapeRigPlacements(v, (*session)->placements()));
    REQUIRE(layout);
    const auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
    const view::Vec3 boss = yokeBoss(*layout, poses, 0);
    const float metre = kCellsPerMetre * layout->xform.labScale;
    for (const float formed : {0.05f, 0.5f, 1.0f}) {
      CAPTURE(formed);
      SurfacePose pose;
      pose.formed = formed;
      pose.flex = flexBlend(formed);
      const auto surf = PlaySurface::build(v, pose);
      const auto hold = skinHold(surf, boss);
      REQUIRE(hold.points.size() == 3);
      CHECK(view::length(hold.points[0] - boss) < 1e-4f);
      const float len = view::length(hold.points[1] - hold.points[0]) +
                        view::length(hold.points[2] - hold.points[1]);
      CAPTURE(len / metre);
      CHECK(len <= 0.35f * metre);
      // It meets the skin's back: the seat it holds faces away from the yoke, and its
      // last piece is a foot along that seat's normal whose end cap (a quarter diameter
      // past the point, `cylinderBetween`) just reaches the back face, never through.
      const SurfaceSeat* best = nullptr;
      for (const auto& seat : surf.seats())
        if (!best || view::length(seat.centre - hold.points[2]) <
                         view::length(best->centre - hold.points[2]))
          best = &seat;
      REQUIRE(best);
      CHECK(view::dot(boss - best->centre, best->normal) < 0.0f);
      const view::Vec3 foot = view::normalize(hold.points[2] - hold.points[1]);
      CHECK(view::dot(foot, best->normal) > 0.999f);
      const float reach =
          view::dot(hold.points[2] - best->centre, best->normal) + kHoldDiameter * 0.25f;
      CHECK_THAT(reach, WithinAbs(-PlaySurface::kThickness * 0.5f, 1e-4));
      // The slanted piece's rim stops short of the sheet.
      CHECK(view::dot(hold.points[1] - best->centre, best->normal) +
                kHoldDiameter * 0.75f <
            -PlaySurface::kThickness * 0.5f);
    }
  }
}

TEST_CASE("the renderer draws the flexible skin in shape view and the slab when flat",
          "[render][skin][flex]") {
  for (const char* name : kShapes) {
    CAPTURE(name);
    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session);
    auto& s = **session;
    BoardRenderer renderer;
    renderer.setTheme(view::Theme::manifold());
    BoardOptions o;
    o.surface = true;
    o.slabSkin = true;
    const auto build = [&](float formed) {
      o.surfaceFormed = formed;
      renderer.setOptions(o);
      return renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
    };
    const auto materialOf = [](const InstanceSet& set, float material) {
      return std::count_if(
          set.surfaceDraws.begin(), set.surfaceDraws.end(),
          [&](const auto& d) { return set.instances[d.instance].material == material; });
    };
    // Flat: the M24.5b slab, membrane and all.
    const auto flat = build(0.0f);
    CHECK(materialOf(flat, 0.5f) == 1);
    // Formed: one high-poly sheet, no membrane behind it.
    const auto formed = build(1.0f);
    CHECK(materialOf(formed, 0.5f) == 0);
    CHECK(formed.surfaceVertices.size() >=
          static_cast<std::size_t>(64 * 2 * kFlexCorners));
    CHECK(formed.surfaceVertices.size() > 2 * flat.surfaceVertices.size());
    // Smooth: wherever two tiles' face vertices meet, they share one normal.
    float worst = 0.0f;
    std::vector<std::pair<view::Vec3, view::Vec3>> faces;
    for (const MeshVertex& mv : formed.surfaceVertices)
      if (mv.height == 1.0f && mv.panelCoord[2] > 0.0f)
        faces.push_back({{mv.pos[0], mv.pos[1], mv.pos[2]},
                         {mv.normal[0], mv.normal[1], mv.normal[2]}});
    std::sort(faces.begin(), faces.end(),
              [](const auto& a, const auto& b) { return a.first.x < b.first.x; });
    for (std::size_t i = 0; i < faces.size(); ++i)
      for (std::size_t j = i + 1;
           j < faces.size() && faces[j].first.x - faces[i].first.x < 1e-4f; ++j)
        if (view::length(faces[j].first - faces[i].first) < 1e-4f &&
            view::dot(faces[j].second, faces[i].second) > 0.0f)
          worst = std::max(worst, angleDeg(faces[i].second, faces[j].second));
    CAPTURE(worst);
    CHECK(worst < 1.0f);
    // Lab off (no slab skin) is the plain surface, as before.
    o.slabSkin = false;
    renderer.setOptions(o);
    const auto plain = renderer.buildInstances(s.snapshot(), s.viewConfig(), &s.seams());
    CHECK(plain.surfaceDraws.empty());
    CHECK(plain.surfaceVertices.size() < formed.surfaceVertices.size());
  }
}

TEST_CASE("the slab body dims away as the tiles lift into the skin",
          "[render][skin][flex][rig]") {
  const auto lab = shippedLab();
  auto session = app::Session::create(test::loadVariant("torus"));
  REQUIRE(session);
  const auto& v = (*session)->variant();
  const auto layout = rigLayout(lab, shapeRigPlacements(v, (*session)->placements()));
  REQUIRE(layout);
  const auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
  const auto count = [&](float backFade, RigPiece::Kind kind) {
    InstanceSet set;
    const auto pieces = appendRig(set, lab, *layout, poses, backFade);
    return std::count_if(pieces.begin(), pieces.end(),
                         [&](const RigPiece& p) { return p.kind == kind; });
  };
  CHECK(count(1.0f, RigPiece::Kind::Back) == 1);
  CHECK(count(0.5f, RigPiece::Kind::Back) == 1);
  CHECK(count(0.0f, RigPiece::Kind::Back) == 0);
  // The yoke stays: it holds the skin.
  CHECK(count(0.0f, RigPiece::Kind::Yoke) == 1);
  CHECK(slabBackFade(0.0f) == 1.0f);
  CHECK(slabBackFade(1.0f) == 0.0f);
  CHECK_THAT(slabBackFade(kFlexFormEnd * 0.5f),
             WithinAbs(1.0f - flexBlend(kFlexFormEnd * 0.5f), 1e-6));
}

TEST_CASE("rig hardware in play has round sections and bevelled bars",
          "[render][rig][flex]") {
  const auto lab = shippedLab();
  REQUIRE(lab.part("rig.yoke"));
  CHECK(lab.part("rig.yoke")->mesh == io::LabMesh::Cylinder);
  for (const char* bar : {"gantry.bridge.a", "gantry.bridge.b"}) {
    CAPTURE(bar);
    REQUIRE(lab.part(bar));
    CHECK(lab.part(bar)->mesh == io::LabMesh::Bevel);
  }
  // Every rail and bar the rig group draws is bevelled, never a raw box.
  for (const auto& p : lab.doc().parts)
    if (p.group == "rig") {
      CAPTURE(p.name);
      CHECK(p.mesh != io::LabMesh::Box);
    }
  // The round yoke still runs from the mast axis to the back's boss: its long axis is
  // horizontal and its length is the bar's.
  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session);
  const auto layout = rigLayout(lab, (*session)->placements());
  REQUIRE(layout);
  const auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
  InstanceSet set;
  const auto pieces = appendRig(set, lab, *layout, poses);
  const auto yoke = std::find_if(pieces.begin(), pieces.end(), [](const RigPiece& p) {
    return p.kind == RigPiece::Kind::Yoke;
  });
  REQUIRE(yoke != pieces.end());
  const auto& b = yoke->bounds;
  const float section = 0.03f * kCellsPerMetre;
  CHECK(b.maxZ - b.minZ < section * 1.05f);
  CHECK(std::max(b.maxX - b.minX, b.maxY - b.minY) > section * 2.0f);
}

#endif
