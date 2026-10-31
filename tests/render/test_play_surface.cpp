// SPDX-License-Identifier: GPL-3.0-or-later
//
// The play board as its own shape (M17), as geometry. No GPU: a tile's seat, its turn
// and its size are arithmetic, and that is exactly where the first cut of this went
// wrong - the tiles were placed on the surface but spun to whatever the shortest arc
// onto the normal happened to be, and sized as though the board were still flat.
//
// The claims here are the ones a player can see: a tile lies *along* the surface's own
// axes, a cell is as big as the step to its neighbour, the seams the variant declares
// close, the shape stands up in the board's own world, and the eversion turns the
// surface through itself without moving a cell off it.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#ifdef CB_HAVE_IMGUI

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "render/board_renderer.hpp"

#include "position/position.hpp"
#include "render/overture_scene.hpp"
#include "render/piece_mesh.hpp"
#include "render/play_surface.hpp"
#include "support/variants.hpp"
#include "view/layout.hpp"
#include "view/snapshot.hpp"

using namespace cb;
using namespace cb::render;
using Catch::Matchers::WithinAbs;

namespace {

float dist(const view::Vec3& a, const view::Vec3& b) {
  return view::length(a - b);
}

/// Rotate a vector by a unit quaternion (x, y, z, w) - the same arithmetic the vertex
/// shader applies, so what the test checks is what the card draws.
view::Vec3 byQuat(const std::array<float, 4>& q, const view::Vec3& v) {
  const view::Vec3 qv{q[0], q[1], q[2]};
  const view::Vec3 t = view::cross(qv, v) * 2.0f;
  return v + t * q[3] + view::cross(qv, t);
}

const SurfaceSeat& seatAt(const PlaySurface& s, const VariantSpec& v, int f, int r) {
  const CellId want = v.dims.toCell(Coord::of({f, r}));
  for (const SurfaceSeat& t : s.seats()) {
    if (t.cell == want) return t;
  }
  throw std::runtime_error("no such cell on the surface");
}

const SurfacePatch& patchAt(const PlaySurface& s, const VariantSpec& v, int f, int r) {
  const CellId want = v.dims.toCell(Coord::of({f, r}));
  for (const SurfacePatch& t : s.patches()) {
    if (t.cell == want) return t;
  }
  throw std::runtime_error("no such cell on the surface");
}

/// A patch corner, by its grid position.
view::Vec3 cornerOf(const SurfacePatch& p, int i, int j) {
  return p.pos[static_cast<std::size_t>(i * (kSurfaceSubdiv + 1) + j)];
}

const std::vector<std::string> kShapes{"cylinder", "torus", "mobius", "klein"};

/// How closely a seat's own +X may be expected to point at the next square along.
///
/// On a gently curved surface those are the same direction to a degree or two. A Klein
/// bottle's figure-eight cross-section is not gently curved: eight squares have to get
/// all the way round a lemniscate, which turns through more than a full circle *twice*,
/// and the chord to the next square leaves the tangent plane as it goes. There the claim
/// is only that the seat faces along the files rather than against them - the bug this
/// exists for put them at arbitrary angles, backwards among them.
float alongTolerance(const std::string& name) {
  return name == "klein" ? 0.0f : 0.9f;
}

}  // namespace

TEST_CASE("a surface tile lies along the surface's own axes", "[render]") {
  // The bug this pins: a quaternion that only takes local +Z onto the normal leaves the
  // tile free to spin in its own plane, and a board of squares turned to random angles
  // is not a board. The frame is the surface's: +X along the files, +Y along the ranks,
  // +Z out of it, right-handed - so a tile's edges run along the board's own lines.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    REQUIRE_FALSE(s.empty());
    CAPTURE(name);
    for (const SurfaceSeat& t : s.seats()) {
      const view::Vec3 ex = byQuat(t.quat, {1, 0, 0});
      const view::Vec3 ey = byQuat(t.quat, {0, 1, 0});
      const view::Vec3 ez = byQuat(t.quat, {0, 0, 1});
      // Orthonormal and right-handed: a rotation, never a squash or a mirror.
      CHECK_THAT(view::length(ex), WithinAbs(1.0f, 1e-3f));
      CHECK_THAT(view::dot(ex, ey), WithinAbs(0.0f, 1e-3f));
      CHECK_THAT(view::dot(ex, ez), WithinAbs(0.0f, 1e-3f));
      CHECK(view::dot(view::cross(ex, ey), ez) > 0.99f);
      // And it is the *surface's* frame: +Z is the outward normal the tile was given.
      CHECK_THAT(dist(ez, t.normal), WithinAbs(0.0f, 1e-3f));
      // +X follows the files: stepping one cell along the file axis moves that way. The
      // Klein figure-eight's own chord reverses at its crossing, so there the claim is
      // the frame's *continuity* - it agrees with its neighbour rather than with the
      // chord (M17.18).
      const Coord co = v.dims.toCoord(t.cell);
      const int f =
          co.c[0] + 1 < static_cast<int>(v.dims.extent(0)) ? co.c[0] + 1 : co.c[0] - 1;
      if (name == "klein") {
        const view::Vec3 nextEx =
            byQuat(seatAt(s, v, f, co.c[1]).quat, view::Vec3{1, 0, 0});
        CHECK(view::dot(ex, nextEx) > 0.0f);
        continue;
      }
      const float sign = co.c[0] + 1 < static_cast<int>(v.dims.extent(0)) ? 1.0f : -1.0f;
      const view::Vec3 along =
          view::normalize((seatAt(s, v, f, co.c[1]).centre - t.centre) * sign);
      CHECK(view::dot(along, ex) > alongTolerance(name));
    }
  }
}

TEST_CASE("a square is a patch of the surface, and the patches tile it", "[render]") {
  // The thing that makes this a board rather than fish scales. A square is not a
  // rectangle turned to face the surface - a rectangle is tangent at one point and
  // overlaps its neighbour at the other end - but a patch cut from the surface itself.
  // So: every patch has the shape of the surface under it, and two patches either side of
  // a lattice edge come within a hair of each other without ever crossing.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    const int side = kSurfaceSubdiv + 1;
    CAPTURE(name);
    CHECK(s.patches().size() == s.seats().size());
    for (int f = 0; f + 1 < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        const SurfacePatch& a = patchAt(s, v, f, r);
        const SurfacePatch& b = patchAt(s, v, f + 1, r);
        const float step = seatAt(s, v, f, r).stepU;
        for (int j = 0; j < side; ++j) {
          // The far edge of one square and the near edge of the next: a gap, because a
          // board needs a line between its squares, but a thin one - and never a
          // crossing, which is what an overlap would be.
          const view::Vec3 lastA = cornerOf(a, side - 1, j);
          const view::Vec3 firstB = cornerOf(b, 0, j);
          const float gap = dist(lastA, firstB);
          CAPTURE(f, r, j, gap, step);
          CHECK(gap > 0.0f);
          CHECK(gap < 0.30f * step);
        }
      }
    }
    // And the patch really is curved where the surface is: its corners do not all lie in
    // one plane. A flat quad would pass the gap test above and still be the old bug.
    float worst = 0.0f;
    for (const SurfacePatch& patch : s.patches()) {
      const view::Vec3 n = patch.normal[0];
      const view::Vec3 o = patch.pos[0];
      for (const view::Vec3& c : patch.pos) {
        worst = std::max(worst, std::abs(view::dot(c - o, n)));
      }
    }
    CHECK(worst > 0.01f);
  }
}

TEST_CASE("a seat is sized by the squares around it", "[render]") {
  // The seat is only there to stand a piece on, and a piece has to fit its square. An
  // embedding stretches the board where it opens a hole and squeezes it where it closes
  // one, so the size is read from the distance to the neighbouring seats.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    CAPTURE(name);
    for (int f = 1; f + 1 < nx; ++f) {
      for (int r = 1; r + 1 < nz; ++r) {
        const SurfaceSeat& t = seatAt(s, v, f, r);
        const float du = 0.5f * (dist(t.centre, seatAt(s, v, f + 1, r).centre) +
                                 dist(t.centre, seatAt(s, v, f - 1, r).centre));
        const float dv = 0.5f * (dist(t.centre, seatAt(s, v, f, r + 1).centre) +
                                 dist(t.centre, seatAt(s, v, f, r - 1).centre));
        CAPTURE(f, r, t.stepU, t.stepV, du, dv);
        CHECK_THAT(t.stepU, WithinAbs(du, 1e-4f));
        CHECK_THAT(t.stepV, WithinAbs(dv, 1e-4f));
      }
    }
  }
}

TEST_CASE("the surface closes the seams the variant declares", "[render]") {
  // The geometry check: a cell on one side of a seam is one step from the cell the
  // identification says is next to it - including the two that reverse a coordinate,
  // where getting the flip wrong lands the neighbour half a board away.
  struct Case {
    std::string name;
    bool fileFlipsRank{false};  ///< crossing the file seam reverses the rank (mobius)
    bool rankGlued{false};      ///< the ranks are glued too (torus, klein)
    bool rankFlipsFile{false};  ///< crossing the rank seam reverses the file (klein)
  };
  const std::vector<Case> cases{{"cylinder", false, false, false},
                                {"torus", false, true, false},
                                {"mobius", true, false, false},
                                {"klein", false, true, true}};
  for (const Case& c : cases) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(c.name));
    const PlaySurface s = PlaySurface::build(v);
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    CAPTURE(c.name);
    // The yardstick is the board's own widest step, not a fixed distance: a Klein
    // bottle's files are unevenly spaced whatever is done to them, and the claim is that
    // the seam is an ordinary step among them rather than a jump across the shape.
    float widestU = 0.0f;
    float widestV = 0.0f;
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        if (f + 1 < nx)
          widestU = std::max(
              widestU, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f + 1, r).centre));
        if (r + 1 < nz)
          widestV = std::max(
              widestV, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f, r + 1).centre));
      }
    }
    for (int r = 0; r < nz; ++r) {
      const SurfaceSeat& a = seatAt(s, v, 0, r);
      const SurfaceSeat& b = seatAt(s, v, nx - 1, c.fileFlipsRank ? nz - 1 - r : r);
      CAPTURE(r, widestU);
      CHECK(dist(a.centre, b.centre) < 1.1f * widestU);
    }
    if (!c.rankGlued) continue;
    for (int f = 0; f < nx; ++f) {
      const SurfaceSeat& a = seatAt(s, v, f, 0);
      const SurfaceSeat& b = seatAt(s, v, c.rankFlipsFile ? nx - 1 - f : f, nz - 1);
      CAPTURE(f, widestV);
      CHECK(dist(a.centre, b.centre) < 1.1f * widestV);
    }
  }
}

TEST_CASE("a Klein bottle's seam stays closed under a square shift and a slide",
          "[render]") {
  // The bug this exists for, reported directly: dragging a Klein bottle with a nonzero
  // `kleinShift` set visibly pulls the seam's two sides apart - "the purple edge touches
  // the pink edge" - worse the further the drag goes, even though the same board at rest
  // (no drag) can be tuned to look aligned. `kleinSurf` samples
  // `lemniscateAngle(u + collapsePhase * v)`; the seam's true partner of `(u, rank 0)` is
  // not the naive `(nx - 1 - f, rank nz - 1)` once `collapsePhase` is nonzero - it moves
  // with how far the whole board has slid, because sliding adds the same offset to every
  // sample's own `v`. `PlaySurface::build` compensates by folding a matching correction
  // into `su`; this is the differential check that it actually lands on the real
  // partner, not just close to it, across a few shifts and a few slide amounts including
  // several whole laps.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  for (const float shift : {1.0f, 2.0f, 3.0f}) {
    for (const float slideVLaps : {0.0f, 1.0f, 2.0f, 6.0f}) {
      SurfacePose pose;
      pose.collapsePhase = shift / static_cast<float>(nx);
      pose.slideV = slideVLaps * static_cast<float>(nz);
      const PlaySurface s = PlaySurface::build(v, pose);
      CAPTURE(shift, slideVLaps);
      float widestV = 0.0f;
      for (int f = 0; f < nx; ++f) {
        for (int r = 0; r + 1 < nz; ++r) {
          widestV = std::max(
              widestV, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f, r + 1).centre));
        }
      }
      for (int f = 0; f < nx; ++f) {
        const SurfaceSeat& a = seatAt(s, v, f, 0);
        const SurfaceSeat& b = seatAt(s, v, nx - 1 - f, nz - 1);
        CAPTURE(f, widestV);
        CHECK(dist(a.centre, b.centre) < 1.1f * widestV);
      }
    }
  }
}

TEST_CASE("the shape stands up in the board's own world", "[render]") {
  // The overtures are modelled Y-up and the board's world is Z-up. Handing one straight
  // to the other stands the torus on its rim like a wheel and lays the cylinder on its
  // side, under a camera that orbits about Z. So a donut lies on the table: its hole
  // points up, and it is wider than it is tall.
  const VariantSpec& torus = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface s = PlaySurface::build(torus);
  const view::Bounds b = s.bounds();
  CHECK((b.maxX - b.minX) > 2.0f * (b.maxZ - b.minZ));
  CHECK((b.maxY - b.minY) > 2.0f * (b.maxZ - b.minZ));
  // And the hole is a hole: some cell faces straight up and some straight down.
  float up = -1.0f;
  float down = 1.0f;
  for (const SurfaceSeat& t : s.seats()) {
    up = std::max(up, t.normal.z);
    down = std::min(down, t.normal.z);
  }
  CHECK(up > 0.8f);
  CHECK(down < -0.8f);
}

TEST_CASE("the board slides round its own surface", "[render]") {
  // What the middle button does. Sliding one cell along the files puts a1 exactly where
  // b1 was - exactly, because the slide is the same sampling one cell further along, not
  // an interpolation between two placements. Nothing about the board changes: the same
  // cells are there, each still next to the same neighbours.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const int nx = static_cast<int>(v.dims.extent(0));
    SurfacePose one;
    one.slideU = 1.0f;
    const PlaySurface rest = PlaySurface::build(v);
    const PlaySurface slid = PlaySurface::build(v, one);
    CAPTURE(name);
    REQUIRE(slid.seats().size() == rest.seats().size());
    for (int f = 0; f + 1 < nx; ++f) {
      for (int r = 0; r < static_cast<int>(v.dims.extent(1)); ++r) {
        CHECK_THAT(dist(seatAt(slid, v, f, r).centre, seatAt(rest, v, f + 1, r).centre),
                   WithinAbs(0.0f, 1e-4f));
      }
    }
    // A full period - two laps, `2 * nx` cells - brings the board home exactly. The old
    // wrap was `fmod(s, 2.0f)` in cells, so the board snapped home after *two* squares;
    // this is the assertion that catches it (M17.9).
    SurfacePose period;
    period.slideU = static_cast<float>(2 * nx);
    const PlaySurface home = PlaySurface::build(v, period);
    for (std::size_t i = 0; i < rest.seats().size(); ++i) {
      CHECK(home.seats()[i].cell == rest.seats()[i].cell);
      CHECK_THAT(dist(home.seats()[i].centre, rest.seats()[i].centre),
                 WithinAbs(0.0f, 1e-3f));
    }
    // Two cells is not a lap: the board has moved, it has not come back.
    SurfacePose two;
    two.slideU = 2.0f;
    const PlaySurface moved = PlaySurface::build(v, two);
    bool differs = false;
    for (std::size_t i = 0; i < rest.seats().size(); ++i) {
      if (dist(moved.seats()[i].centre, rest.seats()[i].centre) > 0.1f) differs = true;
    }
    CHECK(differs);
    // Continuity: a slide of a tenth of a cell moves every seat by well under a cell, so
    // the board travels rather than teleporting.
    const PlaySurface tiny = PlaySurface::build(v, [] {
      SurfacePose p;
      p.slideU = 0.1f;
      return p;
    }());
    for (std::size_t i = 0; i < rest.seats().size(); ++i) {
      CHECK(dist(tiny.seats()[i].centre, rest.seats()[i].centre) < 0.5f);
    }
  }
}

TEST_CASE("a Klein bottle's rank slide period is two laps, shift or no shift",
          "[render]") {
  // M17.22 revised the rank wrap window to `2 * nz * nx` on the reasoning that a nonzero
  // `kleinShift` adds an independent spiral (`kleinSurf` samples `lemniscateAngle(u +
  // (shift / nx) * v)`) needing `nx / gcd(nx, shift)` laps to close on its own. True of
  // the spiral alone - but the later `su` compensation (the seam-alignment fix) makes the
  // *rendered* pattern not depend on the slide amount at all: its own `phase * sv` term
  // exactly cancels the sample's `phase * v`'s `sv` contribution, so the spiral never
  // drifts with the slide in the first place, and the wider window is not needed to
  // protect it - the plain two laps every other warp already uses is both correct and
  // sufficient. This test now pins exactly that: a shifted Klein bottle comes home every
  // two laps, the same as an unshifted one.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  REQUIRE(nx == 8);
  const int shift =
      1;  // coprime with nx: a spiral period of nx laps, if it still drifted
  const auto build = [&](int k, int s) {
    SurfacePose p;
    p.slideV = static_cast<float>(k * nz);
    p.collapsePhase = static_cast<float>(s) / static_cast<float>(nx);
    return PlaySurface::build(v, p);
  };
  const auto sameCorners = [&](const PlaySurface& a, const PlaySurface& b, float tol) {
    if (a.patches().size() != b.patches().size()) return false;
    for (std::size_t c = 0; c < a.patches().size(); ++c) {
      for (int ii = 0; ii <= kSurfaceSubdiv; ++ii) {
        for (int jj = 0; jj <= kSurfaceSubdiv; ++jj) {
          if (dist(cornerOf(a.patches()[c], ii, jj), cornerOf(b.patches()[c], ii, jj)) >
              tol) {
            return false;
          }
        }
      }
    }
    return true;
  };
  const PlaySurface home = build(0, shift);
  for (int k = 1; k <= nx; ++k) {
    CAPTURE(k);
    // Home every two laps, not just at a full nx-lap period - the compensation keeps the
    // pattern itself from ever depending on how far the board has been dragged.
    CHECK(sameCorners(build(k, shift), home, 1e-3f) == (k % 2 == 0));
  }
  // The shipped default (no shift) is the pre-change board at every slide, bit for bit:
  // its wrap is `k mod 2` laps, and the fix must not touch that path at all.
  for (int k = 0; k <= nx; ++k) {
    const PlaySurface got = build(k, 0);
    const PlaySurface want = build(k % 2, 0);
    REQUIRE(got.patches().size() == want.patches().size());
    for (std::size_t c = 0; c < want.patches().size(); ++c) {
      for (int ii = 0; ii <= kSurfaceSubdiv; ++ii) {
        for (int jj = 0; jj <= kSurfaceSubdiv; ++jj) {
          const view::Vec3 a = cornerOf(got.patches()[c], ii, jj);
          const view::Vec3 b = cornerOf(want.patches()[c], ii, jj);
          CAPTURE(k, c, ii, jj);
          CHECK(a.x == b.x);
          CHECK(a.y == b.y);
          CHECK(a.z == b.z);
        }
      }
    }
  }
}

TEST_CASE("sliding the ranks is offered only where the ranks are glued", "[render]") {
  // A cylinder's rank edges are free, so sliding along them would translate the whole
  // tube through space and change nothing about the board - a control that did that
  // would be a lie, so the surface ignores it.
  CHECK(PlaySurface::slidesAlongRanks(test::loadVariant("torus")));
  CHECK(PlaySurface::slidesAlongRanks(test::loadVariant("klein")));
  CHECK_FALSE(PlaySurface::slidesAlongRanks(test::loadVariant("cylinder")));
  CHECK_FALSE(PlaySurface::slidesAlongRanks(test::loadVariant("mobius")));

  const VariantSpec& torus = *new VariantSpec(test::loadVariant("torus"));
  SurfacePose one;
  one.slideV = 1.0f;
  const PlaySurface rest = PlaySurface::build(torus);
  const PlaySurface slid = PlaySurface::build(torus, one);
  for (int f = 0; f < 8; ++f) {
    for (int r = 0; r + 1 < 8; ++r) {
      CHECK_THAT(
          dist(seatAt(slid, torus, f, r).centre, seatAt(rest, torus, f, r + 1).centre),
          WithinAbs(0.0f, 1e-4f));
    }
  }
}

TEST_CASE("invert swaps the side a piece stands on, without moving the board",
          "[render]") {
  // M17.7, revised: INVERT does not mirror the geometry - the eversion that ships in the
  // library's shapes moves every cell about the origin, which is not what a board wants.
  // It swaps the outward side: the squares stay exactly where they were, and every piece
  // stands on the other face. At rest the surface is the library screen's, bit for bit,
  // which every existing capture depends on.
  const VariantSpec& torus = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface rest = PlaySurface::build(torus, SurfacePose{});
  for (const SurfaceSeat& t : rest.seats()) {
    const Coord co = torus.dims.toCoord(t.cell);
    const float u = (static_cast<float>(co.c[0]) + 0.5f) / 8.0f;
    const float vv = (static_cast<float>(co.c[1]) + 0.5f) / 8.0f;
    const OvVec3 want = derivedSurfaceAt(app::overtureSignature(torus).surface, u, vv);
    // The same point, in the board's world: the shape is the overture's, turned upright.
    CHECK_THAT(dist(t.centre, view::Vec3{want.x, -want.z, want.y}),
               WithinAbs(0.0f, 1e-5f));
  }
  SurfacePose full;
  full.evert = 1.0f;
  const PlaySurface turned = PlaySurface::build(torus, full);
  REQUIRE(turned.seats().size() == rest.seats().size());
  for (std::size_t i = 0; i < rest.seats().size(); ++i) {
    // The geometry is identical...
    CHECK_THAT(dist(turned.seats()[i].centre, rest.seats()[i].centre),
               WithinAbs(0.0f, 1e-6f));
    // ...and the normal is exactly reversed, so the piece stands on the other side.
    CHECK_THAT(view::dot(turned.seats()[i].normal, rest.seats()[i].normal),
               WithinAbs(-1.0f, 1e-4f));
    CHECK(turned.seats()[i].stepU > 0.05f);
    CHECK(turned.seats()[i].stepV > 0.05f);
  }
}

TEST_CASE("the invert is pure in its parameter and never moves the board", "[render]") {
  // A pose is a function of one number, so a capture reproduces and a toggle is
  // reversible. The side swap is discrete (past the halfway point), so this pins
  // determinism and that the squares do not move - which is the whole point of the
  // revised invert.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    for (int i = 0; i <= 10; ++i) {
      CAPTURE(name, i);
      SurfacePose p;
      p.evert = static_cast<float>(i) / 10.0f;
      const PlaySurface a = PlaySurface::build(v, p);
      const PlaySurface b = PlaySurface::build(v, p);
      REQUIRE(a.seats().size() == rest.seats().size());
      for (std::size_t c = 0; c < a.seats().size(); ++c) {
        // Pure...
        CHECK_THAT(dist(a.seats()[c].centre, b.seats()[c].centre),
                   WithinAbs(0.0f, 1e-6f));
        CHECK_THAT(view::dot(a.seats()[c].normal, b.seats()[c].normal),
                   WithinAbs(1.0f, 1e-6f));
        // ...and the squares never move, at any point of the turn.
        CHECK_THAT(dist(a.seats()[c].centre, rest.seats()[c].centre),
                   WithinAbs(0.0f, 1e-6f));
      }
    }
  }
}

TEST_CASE("picking the surface follows what was drawn", "[render]") {
  // ADR-0011's invariant: the ray tests the surface the tiles were built from, so a
  // click can never select a cell other than the one under the cursor. Every cell whose
  // centre is on screen and in front picks itself back.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const float w = 900.0f;
    const float h = 700.0f;
    const view::OrbitCamera cam = view::OrbitCamera::frame(s.bounds(), w / h);
    int hits = 0;
    int agreed = 0;
    CAPTURE(name);
    for (const SurfaceSeat& t : s.seats()) {
      const view::OrbitCamera::ScreenPoint sp = cam.project(t.centre, w / h, w, h);
      if (!sp.visible) continue;
      const CellId got = s.pick(cam, w, h, sp.x, sp.y);
      if (got == kInvalidCell) continue;
      ++hits;
      // Either this cell, or one in front of it - a cell round the back is hidden, and
      // picking the near one is the right answer, not a miss.
      if (got == t.cell) {
        ++agreed;
        continue;
      }
      const auto it = std::find_if(s.seats().begin(), s.seats().end(),
                                   [&](const SurfaceSeat& o) { return o.cell == got; });
      REQUIRE(it != s.seats().end());
      // Whatever came back is under that pixel too - it is the sheet of the surface in
      // front of this one.
      const view::OrbitCamera::ScreenPoint gp = cam.project(it->centre, w / h, w, h);
      CAPTURE(t.cell, got, sp.x, sp.y, gp.x, gp.y);
      CHECK(std::hypot(gp.x - sp.x, gp.y - sp.y) < 90.0f);
    }
    CHECK(hits > 40);
    // A Klein bottle is an immersion: it passes through itself, so a good number of its
    // cells are genuinely behind another sheet of the same board and picking the sheet in
    // front is the right answer, checked above.
    CHECK(agreed > hits / 3);
    // A pixel in the corner, off the shape entirely, is nothing at all.
    CHECK(s.pick(cam, w, h, 3.0f, 3.0f) == kInvalidCell);
  }
}

TEST_CASE("only a glued two-dimensional board has a play surface", "[render]") {
  CHECK(PlaySurface::build(test::loadVariant("standard")).empty());
  CHECK(PlaySurface::build(test::loadVariant("mirrorbox")).empty());
  CHECK(PlaySurface::build(test::loadVariant("cube5")).empty());
  CHECK_FALSE(PlaySurface::build(test::loadVariant("torus")).empty());
}

TEST_CASE("the shape is framed on its own centre", "[render]") {
  // M17.11. `PlaySurface::bounds()` already pads for the pieces, so the camera's headroom
  // must be 0 when framing on it - otherwise the look-at is lifted off the shape's centre
  // and the board drops down the window. The projected bounds centre is the window
  // centre.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const view::Bounds b = s.bounds();
    const view::Vec3 centre{(b.minX + b.maxX) * 0.5f, (b.minY + b.maxY) * 0.5f,
                            (b.minZ + b.maxZ) * 0.5f};
    for (const float aspect : {1.6f, 0.8f}) {
      CAPTURE(name, aspect);
      const view::OrbitCamera cam = view::OrbitCamera::frame(b, aspect, 0.0f);
      const float w = 1600.0f;
      const float h = w / aspect;
      const view::OrbitCamera::ScreenPoint sp = cam.project(centre, aspect, w, h);
      REQUIRE(sp.visible);
      CHECK(std::abs(sp.x - w * 0.5f) < 0.02f * w);
      CHECK(std::abs(sp.y - h * 0.5f) < 0.02f * h);
    }
  }
}

TEST_CASE("a three- and four-dimensional variant plays on its own shape", "[render]") {
  // M17.12, first two shapes: `torus3d`'s nested shells and `hyper4`'s tesseract. The
  // shape is authored, not derived, so it is keyed by name; `cube5` has none and stays on
  // the ordinary lattice. Every cell gets a tile and a seat, sized from its neighbours.
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    CHECK(hasPlaySurface(v));
    const PlaySurface s = PlaySurface::build(v);
    REQUIRE(s.seats().size() == v.dims.cellCount());
    REQUIRE(s.patches().size() == v.dims.cellCount());
    for (const SurfaceSeat& seat : s.seats()) {
      CHECK(seat.stepU > 0.01f);
      CHECK(seat.stepV > 0.01f);
      CHECK_THAT(view::length(seat.normal), WithinAbs(1.0f, 1e-4f));
    }
    // A real extent, so the camera can frame it.
    const view::Bounds b = s.bounds();
    CHECK(b.maxX - b.minX > 1.0f);
    CHECK(b.maxY - b.minY > 1.0f);

    if (std::string(name) == "torus3d") {
      // A periodic axis has no special cell (the variant's own comment: "no cell is
      // special"), so within one shell the *file* step - the cross-section circle - must
      // be the same everywhere. The clamp bug made file 0/3 differ from file 1/2 by 41%;
      // this fails on that and passes after M17.13. The rank step is not uniform (it
      // varies with the cross-section offset), and the shells differ from one another in
      // radius, so only `stepU` is compared, within a level.
      std::map<int, float> ref;
      for (const SurfaceSeat& seat : s.seats()) {
        const int level = v.dims.toCoord(seat.cell).c[2];
        const auto it = ref.find(level);
        if (it == ref.end()) {
          ref.emplace(level, seat.stepU);
          continue;
        }
        CHECK_THAT(seat.stepU, Catch::Matchers::WithinRel(it->second, 0.03f));
      }
    }
    if (std::string(name) == "hyper4") {
      // All four axes are bounded, so the boundary cell takes a one-sided step scaled to
      // one cell - not the clamped two-sided one halved to half a cell (M17.13). The
      // boundary must not be the systematic half-size the bug produced.
      const auto stepUAt = [&](int f, int r, int L, int A) {
        const CellId want = v.dims.toCell(Coord::of({f, r, L, A}));
        for (const SurfaceSeat& seat : s.seats()) {
          if (seat.cell == want) return seat.stepU;
        }
        return 0.0f;
      };
      CHECK(stepUAt(0, 1, 1, 1) > 0.6f * stepUAt(1, 1, 1, 1));
    }
  }
  CHECK_FALSE(hasPlaySurface(test::loadVariant("cube5")));
  CHECK(PlaySurface::build(test::loadVariant("cube5")).empty());
}

TEST_CASE("an eight-by-eight three-dimensional torus plays on the nested shells",
          "[render]") {
  // M13.9. The D >= 3 dispatch used to be gated on the literal name "torus3d", so a
  // second 3-torus - the trailer's 8x8x4, and `torus3d_twist` before it - fell back to
  // the flat lattice. The gate is now structural: three dimensions, every axis periodic.
  // The shape construction is `torus3d`'s, but each in-plane coordinate is normalised by
  // its own board extent, so an 8-wide axis wraps the tube once instead of nearly twice.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus3d_8x8"));
  REQUIRE(v.dims.dims() == 3);
  CHECK(hasPlaySurface(v));
  const PlaySurface s = PlaySurface::build(v);
  REQUIRE(s.seats().size() == v.dims.cellCount());
  REQUIRE(s.patches().size() == v.dims.cellCount());
  CHECK(s.seats().size() == 256u);

  // The file coordinate is periodic with the board's own extent: one full lap past the
  // first cell lands exactly back on it, and it is *not* the old four-cell period that
  // would fold the 8-wide board onto itself.
  const auto at = [&](float file) {
    std::array<float, kMaxDims> axis{};
    axis[0] = file;
    axis[1] = 0.5f;
    axis[2] = 0.5f;
    OvVec3 p;
    REQUIRE(playShapeAt(v, axis, p));
    return view::Vec3{p.x, p.y, p.z};
  };
  CHECK_THAT(dist(at(0.5f), at(8.5f)), WithinAbs(0.0f, 1e-4f));
  CHECK(dist(at(0.5f), at(4.5f)) > 0.25f);

  // Each level is its own nested shell: the four cells at one file and rank sit at four
  // strictly growing radii from the core, so the level axis reads as depth.
  std::array<float, 4> radius{};
  for (int level = 0; level < 4; ++level) {
    std::array<float, kMaxDims> axis{};
    axis[0] = 0.5f;
    axis[1] = 0.5f;
    axis[2] = static_cast<float>(level) + 0.5f;
    OvVec3 p;
    REQUIRE(playShapeAt(v, axis, p));
    radius[static_cast<std::size_t>(level)] = std::sqrt(p.x * p.x + p.z * p.z);
  }
  for (int level = 1; level < 4; ++level) {
    CHECK(radius[static_cast<std::size_t>(level)] >
          radius[static_cast<std::size_t>(level - 1)]);
  }

  // `torus3d_twist` gains the same play shape for free - the welcome consequence of the
  // structural gate, noted rather than chased (M13.9).
  CHECK(hasPlaySurface(test::loadVariant("torus3d_twist")));
}

TEST_CASE("the nested shells collapse onto one ring at shellSpread = 0", "[render]") {
  // M13.9 revision: the main-menu trailer's last cut, Klein folding back into a torus
  // and that torus "copying itself" into the 3-D torus's four shells, needs a real
  // waypoint where every level sits on the *same* ring - not a fifth shape, the same
  // `shellTube` with every level's own radius collapsed to the mean of the shipped four.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus3d_8x8"));
  const auto radiusAt = [&](int level, float spread) {
    std::array<float, kMaxDims> axis{};
    axis[0] = 0.5f;
    axis[1] = 0.5f;
    axis[2] = static_cast<float>(level) + 0.5f;
    SurfacePose pose;
    pose.shellSpread = spread;
    OvVec3 p;
    REQUIRE(playShapeAt(v, axis, p, pose));
    return std::sqrt(p.x * p.x + p.z * p.z);
  };

  // At spread = 0, all four levels land on the same radius - one ring, not four nested
  // ones - and it is a real ring (not a degenerate point).
  const float collapsed = radiusAt(0, 0.0f);
  for (int level = 1; level < 4; ++level) {
    CHECK_THAT(radiusAt(level, 0.0f), WithinAbs(collapsed, 1e-4f));
  }
  CHECK(collapsed > 0.5f);

  // At spread = 1 (the default), this is exactly the shipped per-level radii the
  // existing test above already pins - the collapse is not a second shape blended in,
  // it is the same formula at the other end of `shellSpread`.
  for (int level = 1; level < 4; ++level) {
    CHECK(radiusAt(level, 1.0f) > radiusAt(level - 1, 1.0f));
  }

  // Monotonic in between: spreading out never un-sorts the levels or overshoots past
  // the shipped radius.
  const float outerShipped = radiusAt(3, 1.0f);
  float prevOuter = collapsed;
  for (int i = 1; i <= 10; ++i) {
    const float spread = static_cast<float>(i) / 10.0f;
    const float outer = radiusAt(3, spread);
    CHECK(outer >= prevOuter);
    CHECK(outer <= outerShipped + 1e-4f);
    prevOuter = outer;
  }
}

TEST_CASE("a stacked square is a patch of the surface, not a tangent quad", "[render]") {
  // M17.12 part A. `buildStacked` used to place one flat quad per cell from the cell's
  // centre plus a tangent-plane frame (`axisFrame`). Where the embedding curves, or is
  // foreshortened by hyper4's perspective projection, that rectangle is not the cell's
  // true image: on `torus3d` the shell turns a whole quarter-circle per cell, so a
  // tangent quad is a visible facet; on `hyper4` a strongly foreshortened outer cell's
  // tangent rectangle can miss the true surface by several world units, which is the
  // "bunch of fans". The fix samples the shared continuous `playShapeAt` at a sub-grid,
  // exactly as the 2-D path cuts its patches from its surface, so a tile is a patch *of*
  // the manifold. The claim is the same one the 2-D test makes, in the form that holds
  // for both a curving shell and a projective one: adjacent sub-grid samples step by less
  // than the cell they sit in, and every patch corner is the continuous surface's own
  // point at that lattice coordinate (a tangent quad fails the second).
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const int side = kSurfaceSubdiv + 1;
    // The same `playShapeAt` sample the board must have cut the patch from, in the
    // board's own Z-up world.
    const auto want = [&](const Coord& c, float du, float dv) {
      std::array<float, kMaxDims> axis{};
      for (int k = 0; k < v.dims.dims(); ++k) {
        axis[static_cast<std::size_t>(k)] =
            static_cast<float>(c.c[static_cast<std::size_t>(k)]) + 0.5f;
      }
      axis[0] += du;
      axis[1] += dv;
      OvVec3 p;
      REQUIRE(playShapeAt(v, axis, p));
      return view::Vec3{p.x, -p.z, p.y};
    };
    for (const SurfaceSeat& seat : s.seats()) {
      const SurfacePatch* patch = nullptr;
      for (const SurfacePatch& p : s.patches()) {
        if (p.cell == seat.cell) {
          patch = &p;
          break;
        }
      }
      REQUIRE(patch != nullptr);
      const Coord c = v.dims.toCoord(seat.cell);
      const float cell = std::max(seat.stepU, seat.stepV);
      for (int i = 0; i < side; ++i) {
        for (int j = 0; j < side; ++j) {
          const float du =
              (static_cast<float>(i) / kSurfaceSubdiv - 0.5f) * PlaySurface::kCoverage;
          const float dv =
              (static_cast<float>(j) / kSurfaceSubdiv - 0.5f) * PlaySurface::kCoverage;
          // The corner is on the surface at its own fractional lattice coordinate.
          CHECK_THAT(dist(cornerOf(*patch, i, j), want(c, du, dv)),
                     WithinAbs(0.0f, 1e-4f));
          if (i + 1 < side) {
            const float d = dist(cornerOf(*patch, i, j), cornerOf(*patch, i + 1, j));
            CAPTURE(seat.cell, i, j, d, cell);
            CHECK(d > 0.0f);
            CHECK(d < cell);
          }
          if (j + 1 < side) {
            const float d = dist(cornerOf(*patch, i, j), cornerOf(*patch, i, j + 1));
            CAPTURE(seat.cell, i, j, d, cell);
            CHECK(d > 0.0f);
            CHECK(d < cell);
          }
        }
      }
    }
  }
}

TEST_CASE("picking a three- and four-dimensional shape finds the tile", "[render]") {
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const float w = 1000.0f;
    const float h = 760.0f;
    const view::OrbitCamera cam = view::OrbitCamera::frame(s.bounds(), w / h, 0.0f);
    int hits = 0;
    int agreed = 0;
    for (const SurfaceSeat& t : s.seats()) {
      const view::OrbitCamera::ScreenPoint sp = cam.project(t.centre, w / h, w, h);
      if (!sp.visible) continue;
      const CellId got = s.pick(cam, w, h, sp.x, sp.y);
      if (got == kInvalidCell) continue;
      ++hits;
      if (got == t.cell) {
        ++agreed;
        continue;
      }
      // Otherwise it is the sheet of the shape in front of this one, which is the right
      // answer - and it is still under the pixel.
      const auto it = std::find_if(s.seats().begin(), s.seats().end(),
                                   [&](const SurfaceSeat& o) { return o.cell == got; });
      REQUIRE(it != s.seats().end());
      const view::OrbitCamera::ScreenPoint gp = cam.project(it->centre, w / h, w, h);
      // Widened alongside the nested shells' own radial spacing (M13.9 revision: the
      // levels used to read as "a thicker torus" than four distinct shells, so the step
      // between them grew) - seats on different shells now project further apart on
      // screen too, which is the same change, not a new one.
      CHECK(std::hypot(gp.x - sp.x, gp.y - sp.y) < 260.0f);
    }
    CHECK(hits > 30);
    CAPTURE(hits, agreed);
    // A self-intersecting shape genuinely hides cells behind nearer sheets - the
    // tesseract more than the nested shells - so a pick to the *front* cell is the
    // correct answer, not a miss. The property that matters is the one checked above:
    // whatever comes back is under the pixel. The bound is deliberately loose: once the
    // tiles are cut from the real surface (M17.12 part A) the outer shells occlude the
    // inner ones properly, so far fewer seat centres see their own cell than the old
    // tangent tiles - which left gaps a ray could slip through - did. Lowered again
    // alongside the wider shell spacing above, for the same reason.
    CHECK(agreed > hits / 16);
    // A pixel off the shape is nothing.
    CHECK(s.pick(cam, w, h, 2.0f, 2.0f) == kInvalidCell);
  }
}

TEST_CASE("the camera reframes on the shape when the variant changes under it",
          "[render]") {
  // M17.14: `geometryView` is sticky across `Shell::startGame` (never reset), so a
  // one-shot "did the toggle flip" check misses a variant switch that happens while it is
  // already on - the camera is left on the previous variant's shape, or the new variant's
  // flat board. Framing unconditionally on the load fixes it.
  auto open = [](const char* name) {
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s.has_value());
    return std::move(*s);
  };
  BoardOptions opts;
  opts.surface = true;

  std::unique_ptr<app::Session> session = open("torus");
  frameGeometryCamera(*session, opts, SurfacePose{});
  session = open("torus3d");  // switched variant; opts.surface is still true throughout
  frameGeometryCamera(*session, opts, SurfacePose{});

  const view::Bounds want = PlaySurface::build(test::loadVariant("torus3d")).bounds();
  const view::Vec3 target = session->camera().target;
  CHECK_THAT(target.x, WithinAbs(want.centerX(), 0.05f));
  CHECK_THAT(target.y, WithinAbs(want.centerY(), 0.05f));
  CHECK_THAT(target.z, WithinAbs(want.centerZ(), 0.05f));
}

TEST_CASE("a move on the shape is sampled along the surface", "[render]") {
  // M17.15: the travelling piece is drawn part-way between its seats, not teleported to
  // its landing square. Pure geometry, no GPU.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface surf = PlaySurface::build(v);
  const auto seatOf = [&](CellId c) -> const SurfaceSeat* {
    for (const SurfaceSeat& s : surf.seats()) {
      if (s.cell == c) return &s;
    }
    return nullptr;
  };
  const auto cell = [&](int f, int r) { return v.dims.toCell(Coord::of({f, r})); };

  // A leap arcs from the start seat to the end, clear of the straight chord.
  {
    view::MovePath path;
    path.from = cell(0, 0);
    path.to = cell(0, 3);
    path.leap = true;
    const SurfaceSeat* a = seatOf(path.from);
    const SurfaceSeat* b = seatOf(path.to);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 0.0f).position, a->centre),
               WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 1.0f).position, b->centre),
               WithinAbs(0.0f, 1e-4f));
    const SurfaceMoveSample mid = surfaceMoveSample(path, surf, 0.5f);
    const view::Vec3 chord = (a->centre + b->centre) * 0.5f;
    CHECK(dist(mid.position, chord) > 0.05f);  // it arcs, it does not cut through
    CHECK_THAT(view::length(mid.normal), WithinAbs(1.0f, 1e-3f));
  }

  // A glide walks the seats the route passes through, in order, hugging the surface.
  {
    view::MovePath path;
    path.from = cell(0, 0);
    path.to = cell(2, 0);
    path.steps = {{cell(0, 0), cell(1, 0), view::StepKind::Interior, {}, 0, Side::Max},
                  {cell(1, 0), cell(2, 0), view::StepKind::Interior, {}, 0, Side::Max}};
    const SurfaceSeat* a = seatOf(path.from);
    const SurfaceSeat* mid = seatOf(cell(1, 0));
    const SurfaceSeat* z = seatOf(path.to);
    REQUIRE(a != nullptr);
    REQUIRE(mid != nullptr);
    REQUIRE(z != nullptr);
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 0.0f).position, a->centre),
               WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 1.0f).position, z->centre),
               WithinAbs(0.0f, 1e-4f));
    // It passes over the middle square (laterally, at some t) and hovers there
    // (vertically): the surface boundary waypoints make the arc-length midpoint a touch
    // off the exact centre, so scan rather than pin t = 0.5.
    float bestLateral = 1e9f;
    float bestLift = 0.0f;
    for (float tt = 0.0f; tt <= 1.0001f; tt += 0.002f) {
      const SurfaceMoveSample s = surfaceMoveSample(path, surf, tt);
      const view::Vec3 off = s.position - mid->centre;
      bestLateral =
          std::min(bestLateral, view::length(off - s.normal * view::dot(off, s.normal)));
      bestLift = std::max(bestLift, view::dot(off, s.normal));
    }
    CHECK(bestLateral < 0.01f);
    CHECK(bestLift > 0.05f);
    for (float t = 0.0f; t <= 1.0001f; t += 0.1f) {
      CHECK(view::length(surfaceMoveSample(path, surf, t).normal) > 0.9f);
    }
  }

  // A glide reaches the boundary between two squares and hovers over it, rather than
  // cutting a straight chord; the waypoint is the shared edge (orthogonal) or shared
  // corner (diagonal), lifted off the board so the base clears (M17.19).
  {
    for (const std::pair<int, int>& to : {std::pair{1, 0}, std::pair{1, 1}}) {
      view::MovePath path;
      path.from = cell(0, 0);
      path.to = cell(to.first, to.second);
      path.steps = {{cell(0, 0),
                     cell(to.first, to.second),
                     view::StepKind::Interior,
                     {},
                     0,
                     Side::Max}};
      const SurfaceSeat* a = seatOf(path.from);
      const SurfaceSeat* b = seatOf(path.to);
      REQUIRE(a != nullptr);
      REQUIRE(b != nullptr);
      const SurfaceMoveSample mid = surfaceMoveSample(path, surf, 0.5f);
      const view::Vec3 chord = (a->centre + b->centre) * 0.5f;
      CAPTURE(to.first, to.second);
      CHECK(view::dot(mid.position - chord, mid.normal) > 0.05f);  // lifted off the chord
      CHECK_THAT(dist(surfaceMoveSample(path, surf, 0.0f).position, a->centre),
                 WithinAbs(0.0f, 1e-4f));
      CHECK_THAT(dist(surfaceMoveSample(path, surf, 1.0f).position, b->centre),
                 WithinAbs(0.0f, 1e-4f));
    }
  }

  // `pointAt` samples the surface: a cell centre comes back exactly, and a boundary is a
  // point *on* the surface, not the chord midpoint of the two seats - the two differ on a
  // curving board, which is exactly why the chord midpoint clipped (M17.19).
  {
    view::Vec3 centre{};
    REQUIRE(surf.pointAt(0.5f, 0.5f, centre));
    const SurfaceSeat& s00 = *seatOf(cell(0, 0));
    CHECK_THAT(dist(centre, s00.centre), WithinAbs(0.0f, 1e-4f));
    float worst = 0.0f;
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    for (int f = 0; f + 1 < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        const SurfaceSeat& p = *seatOf(cell(f, r));
        const SurfaceSeat& q = *seatOf(cell(f + 1, r));
        view::Vec3 edge{};
        REQUIRE(surf.nearestBoundary(p, q, edge));
        worst = std::max(worst, dist(edge, (p.centre + q.centre) * 0.5f));
      }
    }
    CHECK(worst > 0.01f);  // the surface boundary is genuinely not the chord midpoint
  }

  // A glide across the glued file edge is not cut: on the surface the seam is one
  // continuous place, so consecutive samples stay within a cell of each other.
  {
    view::MovePath path;
    path.from = cell(0, 0);
    path.to = cell(7, 0);
    path.steps = {{cell(0, 0), cell(7, 0), view::StepKind::Portal, {}, 0, Side::Min}};
    view::Vec3 prev = surfaceMoveSample(path, surf, 0.0f).position;
    for (float t = 0.05f; t <= 1.0001f; t += 0.05f) {
      const view::Vec3 cur = surfaceMoveSample(path, surf, t).position;
      CHECK(dist(cur, prev) < 1.0f);
      prev = cur;
    }
  }
}

TEST_CASE("the move camera can follow on the shape", "[render]") {
  // M17.16: `cameraOver` is the one blend, asked against the shape's placements. With
  // following off it is the settled framing (a strict no-op); with it on the target
  // tracks the route the engine already computed.
  auto session = app::Session::create(test::loadVariant("torus"));
  REQUIRE(session.has_value());
  app::Session& s = **session;
  const PlaySurface surf = PlaySurface::build(s.variant());
  std::vector<view::Placement> places;
  places.reserve(surf.seats().size());
  for (const SurfaceSeat& seat : surf.seats()) {
    places.push_back({seat.cell, seat.centre.x, seat.centre.y, seat.centre.z, 0});
  }

  // Off is bit-identical to the session's own camera.
  const view::OrbitCamera off = s.cameraOver(places, surf.bounds(), view::ViewConfig{});
  const view::OrbitCamera plain = s.camera();
  CHECK_THAT(dist(off.target, plain.target), WithinAbs(0.0f, 1e-5f));

  // On, a shot's target moves with the route.
  s.setCameraMode("route");
  s.setFollowStrength(1.0f);
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
  REQUIRE(s.shotInFlight());
  s.setMoveProgress(0.0f);
  const view::Vec3 start = s.cameraOver(places, surf.bounds(), view::ViewConfig{}).target;
  s.setMoveProgress(0.5f);  // the shot envelope is zero at the ends, so sample mid-move
  const view::Vec3 mid = s.cameraOver(places, surf.bounds(), view::ViewConfig{}).target;
  CHECK(dist(start, mid) > 0.05f);
}

TEST_CASE("align turns an inner-ring cell toward the camera", "[render]") {
  // M17.17: a search over slide offsets that maximises the followed cell's `facing` -
  // whether its outward normal points toward the camera. An open tube has no inner/outer
  // side, so it returns 0.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface rest = PlaySurface::build(v);
  const auto seatFor = [&](const PlaySurface& s, CellId c) -> const SurfaceSeat* {
    for (const SurfaceSeat& seat : s.seats()) {
      if (seat.cell == c) return &seat;
    }
    return nullptr;
  };
  // The camera's own eye direction, shared with the anti-clip search so the two cannot
  // disagree (M17.20).
  const auto toEyeFor = [](const SurfaceSeat& seat, const view::Vec3& travel) {
    return chaseEyeDirection(seat.normal, travel, kDefaultFollowLift);
  };

  // The cell facing most away from a nominal camera at rest: the case to fix.
  const view::Vec3 restTravel{1.0f, 0.0f, 0.0f};
  CellId inner = kInvalidCell;
  float worst = 1e9f;
  for (const SurfaceSeat& seat : rest.seats()) {
    const float facing = view::dot(seat.normal, toEyeFor(seat, restTravel));
    if (facing < worst) {
      worst = facing;
      inner = seat.cell;
    }
  }
  REQUIRE(inner != kInvalidCell);

  const SurfaceSeat* base = seatFor(rest, inner);
  REQUIRE(base != nullptr);
  const view::Vec3 travel = byQuat(base->quat, view::Vec3{1, 0, 0});  // a real tangent
  const SlideOffset offset = alignSlideU(v, inner, travel, 6.0f);
  SurfacePose pose;
  pose.slideU = offset.u;
  pose.slideV = offset.v;
  const PlaySurface turned = PlaySurface::build(v, pose);
  const SurfaceSeat* fixed = seatFor(turned, inner);
  REQUIRE(fixed != nullptr);
  const view::Vec3 toEye = toEyeFor(*fixed, travel);
  const float eyeDist = clearEyeDistance(turned, fixed->centre, toEye, 6.0f);
  const view::Vec3 eye = fixed->centre + toEye * eyeDist;
  const float facing = view::dot(fixed->normal, toEye);
  CHECK(facing > worst);  // the search improves the facing it targets
  CHECK(facing > 0.0f);   // and lands the piece on the camera's side
  CHECK_FALSE(turned.blocked(eye, fixed->centre, 0.02f));  // nothing between the two

  // An open tube/ribbon has nothing to align.
  const SlideOffset none = alignSlideU(test::loadVariant("cylinder"), 0, travel, 6.0f);
  CHECK(none.u == 0.0f);
  CHECK(none.v == 0.0f);
  CHECK(alignSlideU(test::loadVariant("mobius"), 0, travel, 6.0f).u == 0.0f);
}

TEST_CASE("Klein's normal field is continuous except at its seam", "[render]") {
  // M17.18. A non-orientable shape has no globally consistent outward normal, so one flip
  // must exist; the defect was that it landed *twice*, mid-board, at the cross-section's
  // numerically unstable pinch. The continuity walk moves it to the one place a player
  // expects it - the glued file edge - and nowhere else.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  constexpr int kSide = kSurfaceSubdiv + 1;
  const auto patchOf = [&](const PlaySurface& s, int f, int r) -> const SurfacePatch& {
    const CellId want = v.dims.toCell(Coord::of({f, r}));
    for (const SurfacePatch& p : s.patches()) {
      if (p.cell == want) return p;
    }
    throw std::runtime_error("no patch");
  };
  const auto seatOf = [&](const PlaySurface& s, int f, int r) -> const SurfaceSeat& {
    const CellId want = v.dims.toCell(Coord::of({f, r}));
    for (const SurfaceSeat& t : s.seats()) {
      if (t.cell == want) return t;
    }
    throw std::runtime_error("no seat");
  };

  for (const float slide : {0.0f, 1.0f, 2.0f, 3.5f}) {
    CAPTURE(slide);
    SurfacePose pose;
    pose.slideU = slide;
    const PlaySurface s = PlaySurface::build(v, pose);
    for (int r = 0; r < nz; ++r) {
      for (int f = 0; f + 1 < nx; ++f) {
        // Within one square, and across the gap into the next file: never opposed.
        const SurfacePatch& p = patchOf(s, f, r);
        for (int j = 0; j < kSide; ++j) {
          for (int i = 0; i + 1 < kSide; ++i) {
            CHECK(view::dot(p.normal[static_cast<std::size_t>(i * kSide + j)],
                            p.normal[static_cast<std::size_t>((i + 1) * kSide + j)]) >
                  0.0f);
          }
        }
        const SurfacePatch& q = patchOf(s, f + 1, r);
        for (int j = 0; j < kSide; ++j) {
          CHECK(view::dot(p.normal[static_cast<std::size_t>(kSurfaceSubdiv * kSide + j)],
                          q.normal[static_cast<std::size_t>(j)]) > 0.0f);
        }
        // The piece's own frame is continuous too.
        const view::Vec3 ea = byQuat(seatOf(s, f, r).quat, view::Vec3{1, 0, 0});
        const view::Vec3 eb = byQuat(seatOf(s, f + 1, r).quat, view::Vec3{1, 0, 0});
        CHECK(view::dot(ea, eb) > 0.0f);
      }
      // The file axis is the orientation-preserving one here (klein glues its *rank* with
      // a file flip), so the field closes across the file wrap too. The single
      // unavoidable flip lives at the rank seam, which a per-rank walk deliberately does
      // not close.
      const SurfacePatch& last = patchOf(s, nx - 1, r);
      const SurfacePatch& first = patchOf(s, 0, r);
      for (int j = 0; j < kSide; ++j) {
        CHECK(view::dot(last.normal[static_cast<std::size_t>(kSurfaceSubdiv * kSide + j)],
                        first.normal[static_cast<std::size_t>(j)]) > 0.0f);
      }
    }
  }
}

TEST_CASE("the chase camera centres a piece, and stands it up or lets it tilt",
          "[render]") {
  // M17.16 (revised): following a move is one continuous chase, the piece centred.
  // Upright mode also rolls the frame so the piece's own up (surface normal) points up
  // the screen; the tilt mode does not, so the piece rides the shape (the other camera
  // mode).
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface surf = PlaySurface::build(v);
  const float w = 1000.0f;
  const float h = 700.0f;
  const float aspect = w / h;
  for (std::size_t i = 0; i < surf.seats().size(); i += 7) {
    const SurfaceSeat& seat = surf.seats()[i];
    SurfaceMoveSample piece;
    piece.position = seat.centre;
    piece.normal = seat.normal;
    const view::Vec3 travel = byQuat(seat.quat, view::Vec3{1, 0, 0});
    const view::OrbitCamera cam = surfaceChaseCamera(piece, travel, 4.0f, true);
    const view::OrbitCamera::ScreenPoint sp = cam.project(piece.position, aspect, w, h);
    REQUIRE(sp.visible);
    CHECK(std::abs(sp.x - w * 0.5f) < 1.0f);
    CHECK(std::abs(sp.y - h * 0.5f) < 1.0f);
    // The normal points up the screen, and not sideways.
    const view::OrbitCamera::ScreenPoint up =
        cam.project(piece.position + piece.normal, aspect, w, h);
    CHECK(up.y < sp.y);
    CHECK(std::abs(up.x - sp.x) < 8.0f);

    // The tilt mode centres the piece too, but does not roll: world up stays screen up,
    // so the piece is not forced vertical.
    const view::OrbitCamera tilt = surfaceChaseCamera(piece, travel, 4.0f, false);
    CHECK_THAT(tilt.roll, WithinAbs(0.0f, 1e-6f));
    const view::OrbitCamera::ScreenPoint tsp = tilt.project(piece.position, aspect, w, h);
    CHECK(std::abs(tsp.x - w * 0.5f) < 1.0f);
    CHECK(std::abs(tsp.y - h * 0.5f) < 1.0f);
  }
}

TEST_CASE("the chase camera's up really is the piece's up", "[render]") {
  // The stronger form of the claim above: not merely that the normal points *roughly*
  // up the screen, but that the view's up axis is the piece's normal projected
  // perpendicular to the view direction - the vectors are aligned. The loose check above
  // did not catch the inverted roll; this checks every seat and both surface tangents.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface surf = PlaySurface::build(v);
    const float w = 1000.0f;
    const float h = 700.0f;
    const float aspect = w / h;
    for (std::size_t i = 0; i < surf.seats().size(); ++i) {
      const SurfaceSeat& seat = surf.seats()[i];
      SurfaceMoveSample piece;
      piece.position = seat.centre;
      piece.normal = seat.normal;
      piece.quat = seat.quat;
      CAPTURE(name, i, seat.centre.x, seat.centre.y, seat.centre.z);
      // The frame the renderer gives the piece: local +Z is the surface normal.
      CHECK_THAT(view::dot(byQuat(seat.quat, view::Vec3{0, 0, 1}), seat.normal),
                 WithinAbs(1.0f, 1e-3f));
      for (const view::Vec3& local : {view::Vec3{1, 0, 0}, view::Vec3{0, 1, 0}}) {
        const view::Vec3 travel = byQuat(seat.quat, local);
        const view::OrbitCamera cam = surfaceChaseCamera(piece, travel, 4.0f, true);
        const view::OrbitCamera::ScreenPoint sp =
            cam.project(piece.position, aspect, w, h);
        REQUIRE(sp.visible);
        const view::OrbitCamera::ScreenPoint up =
            cam.project(piece.position + piece.normal, aspect, w, h);
        const float upLen = sp.y - up.y;  // positive: the normal points up the screen
        CHECK(upLen > 0.0f);
        // Straight up and not sideways: the horizontal offset is a rounding error
        // against the vertical run, never a real lean.
        CHECK(std::abs(up.x - sp.x) < 0.02f * std::abs(upLen) + 0.5f);
      }
    }
  }
}

TEST_CASE("the chase frame's axes are the piece's own: right a x b, up c x n",
          "[render]") {
  // The exact frame the camera is built from, not "the piece roughly upright": `a` is the
  // direction of motion, `b` the piece's upright, `n = a x b`, and `c` the back-and-up
  // vector (`-a` rotated up by the elevation: the piece-to-eye direction). The camera's
  // right must be `n` and its up `c x n`. The looser test above would not catch a wrong
  // horizontal, which is why this reads the basis out of the matrix the renderer uses.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface surf = PlaySurface::build(v);
    const float aspect = 1000.0f / 700.0f;
    for (std::size_t i = 0; i < surf.seats().size(); i += 5) {
      const SurfaceSeat& seat = surf.seats()[i];
      SurfaceMoveSample piece;
      piece.position = seat.centre;
      piece.normal = seat.normal;
      piece.quat = seat.quat;
      const view::Vec3 a0 = byQuat(seat.quat, view::Vec3{1, 0, 0});
      const view::OrbitCamera cam = surfaceChaseCamera(piece, a0, 4.0f, true);
      const view::Mat4 vp = cam.viewProj(aspect);
      // `right` is lookFrom's first row; the world direction that points *up* the screen
      // is the negation of the second row, because the projection flips Y for Vulkan.
      const view::Vec3 right = view::normalize(view::Vec3{vp[0], vp[4], vp[8]});
      const view::Vec3 up = view::normalize(view::Vec3{-vp[1], -vp[5], -vp[9]});
      // The frame the construction asks for.
      const view::Vec3 a = view::normalize(a0);
      view::Vec3 b = piece.normal - a * view::dot(piece.normal, a);
      b = view::normalize(b);
      const view::Vec3 n = view::normalize(view::cross(a, b));
      const float lift = kDefaultFollowLift;
      const float cosT = 1.0f / std::sqrt(1.0f + lift * lift);
      const float sinT = lift * cosT;
      const view::Vec3 c = view::normalize(a * -cosT + b * sinT);
      const view::Vec3 wantUp = view::normalize(view::cross(c, n));
      CAPTURE(name, i);
      CHECK_THAT(view::dot(right, n), WithinAbs(1.0f, 1e-3f));
      CHECK_THAT(view::dot(up, wantUp), WithinAbs(1.0f, 1e-3f));
    }
  }
}

namespace {

/// The camera's screen-up axis in world space, read from the matrix it actually projects
/// with (`viewProj`), so a basis the renderer would use is what is compared - not a
/// re-derivation from the pose fields.
view::Vec3 screenUp(const view::OrbitCamera& cam, float aspect) {
  const view::Mat4 vp = cam.viewProj(aspect);
  return view::normalize(view::Vec3{vp[1], vp[5], vp[9]});
}

float angleBetween(const view::Vec3& a, const view::Vec3& b) {
  return std::acos(std::clamp(view::dot(a, b), -1.0f, 1.0f));
}

}  // namespace

TEST_CASE("the follow camera is smooth along a move", "[render]") {
  // "Jerky" is the camera swivelling at a cell corner or flipping over a leap's apex.
  // The follow camera is pure in `t`, so smoothness is a property of the function: a
  // fine sweep turns the view by a small angle each step and never by a half-turn.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface surf = PlaySurface::build(v);
  const auto cell = [&](int f, int r) { return v.dims.toCell(Coord::of({f, r})); };
  const float aspect = 1000.0f / 700.0f;

  const auto smoothness = [&](const view::MovePath& path, const char* what) {
    CAPTURE(what);
    const int steps = 200;
    view::Vec3 prevEye{};
    view::Vec3 prevUp{};
    float worstEye = 0.0f;
    float worstUp = 0.0f;
    int worstAt = 0;
    for (int i = 0; i <= steps; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(steps);
      const view::OrbitCamera cam = surfaceFollowCamera(path, surf, t, 4.0f, true);
      const view::Vec3 eyeDir = view::normalize(cam.eye() - cam.target);
      const view::Vec3 up = screenUp(cam, aspect);
      if (i > 0) {
        const float de = angleBetween(eyeDir, prevEye);
        const float du = angleBetween(up, prevUp);
        if (de > worstEye) {
          worstEye = de;
          worstAt = i;
        }
        worstUp = std::max(worstUp, du);
      }
      prevEye = eyeDir;
      prevUp = up;
    }
    CAPTURE(worstAt, static_cast<float>(worstAt) / static_cast<float>(steps));
    // ~0.03 rad per step is an ordinary sweep over this move; a cell-corner swivel or a
    // leap flip is many times that.
    CHECK(worstEye < 0.12f);
    CHECK(worstUp < 0.12f);
  };

  // A rook's glide around the ring, at the tube position that faces most steadily up:
  // the outward side, which is where the anti-clip keeps a followed piece. (A route
  // across the underside swings any third-person camera through a pole; the shipped
  // anti-clip turns the shape so that does not happen, which is tested separately.)
  const int nx = static_cast<int>(v.dims.extent(0));
  int topFile = 0;
  float bestUp = -2.0f;
  for (int f = 0; f < nx; ++f) {
    if (seatAt(surf, v, f, 0).normal.z > bestUp) {
      bestUp = seatAt(surf, v, f, 0).normal.z;
      topFile = f;
    }
  }
  view::MovePath glide;
  glide.from = cell(topFile, 0);
  glide.to = cell(topFile, 6);
  for (int r = 0; r < 6; ++r) {
    glide.steps.push_back({cell(topFile, r),
                           cell(topFile, r + 1),
                           view::StepKind::Interior,
                           {},
                           0,
                           Side::Max});
  }
  smoothness(glide, "glide");

  // A knight's leap, whose arc reverses at its apex.
  view::MovePath leap;
  leap.from = cell(0, 0);
  leap.to = cell(2, 1);
  leap.leap = true;
  smoothness(leap, "leap");
}

TEST_CASE("the turntable align turns the piece to face the camera, unoccluded",
          "[render]") {
  // The turntable's anti-clip: the camera angle is fixed and the *shape* rotates so the
  // followed cell comes round to the near side. For every cell, the chosen offset must
  // leave the piece facing the eye and nothing of the shape between the two.
  for (const char* name : {"torus", "klein"}) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    const view::Bounds rb = rest.bounds();
    const float restSpan =
        std::max({rb.maxX - rb.minX, rb.maxY - rb.minY, rb.maxZ - rb.minZ});
    const view::Vec3 toCamera = view::normalize(view::Vec3{0.35f, -1.0f, 0.55f});
    int checked = 0;
    int unoccluded = 0;
    int facing = 0;
    for (const SurfaceSeat& seat : rest.seats()) {
      const SlideOffset offset =
          alignSlideToFace(v, seat.cell, toCamera, 0.6f * restSpan);
      SurfacePose pose;
      pose.slideU = offset.u;
      pose.slideV = offset.v;
      const PlaySurface turned = PlaySurface::build(v, pose);
      const SurfaceSeat* fixed = [&]() -> const SurfaceSeat* {
        for (const SurfaceSeat& s : turned.seats()) {
          if (s.cell == seat.cell) return &s;
        }
        return nullptr;
      }();
      REQUIRE(fixed != nullptr);
      ++checked;
      const view::Bounds b = turned.bounds();
      const float span = std::max({b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ});
      const view::Vec3 eye = fixed->centre + toCamera * (0.6f * span);
      if (view::dot(fixed->normal, toCamera) > 0.0f) ++facing;
      if (!turned.blocked(eye, fixed->centre, 0.02f)) ++unoccluded;
    }
    CAPTURE(name, checked, facing, unoccluded);
    // Every cell can be brought clear of the shape on a closed ring: the search prefers
    // an unoccluded seat over a better-facing one, so nothing clips.
    CHECK(unoccluded == checked);
    // ...and on the orientable torus every cell also faces the camera. On the Klein
    // bottle only the V offset is searched (the U offset was measured to tear the
    // tiling), so its cells are not all turned to face the eye - but none is hidden.
    const bool orientable = std::string(name) == "torus";
    CHECK(facing >= (orientable ? checked : checked / 2));
  }
}

TEST_CASE("the chase align clears the shape at the camera's own distance", "[render]") {
  // The search has to test occlusion at the distance the camera will actually sit: a
  // close chase at a fraction of the span was clearing seats at a *larger* test distance
  // and still looking through the tube. Here the eye is placed at the same distance the
  // search was given.
  for (const char* name : {"torus", "klein"}) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    const view::Bounds rb = rest.bounds();
    const float span =
        std::max({rb.maxX - rb.minX, rb.maxY - rb.minY, rb.maxZ - rb.minZ});
    const float eyeDistance = std::max(2.0f, 0.4f * span);
    int checked = 0;
    int unoccluded = 0;
    for (const SurfaceSeat& seat : rest.seats()) {
      const view::Vec3 travel = byQuat(seat.quat, view::Vec3{1, 0, 0});
      const SlideOffset offset = alignSlideU(v, seat.cell, travel, eyeDistance);
      SurfacePose pose;
      pose.slideU = offset.u;
      pose.slideV = offset.v;
      const PlaySurface turned = PlaySurface::build(v, pose);
      const SurfaceSeat* fixed = nullptr;
      for (const SurfaceSeat& s : turned.seats()) {
        if (s.cell == seat.cell) fixed = &s;
      }
      REQUIRE(fixed != nullptr);
      ++checked;
      // The search's own shared primitives, against the shape it actually turned: the eye
      // direction is the camera's and the achieved distance is measured at it (M17.20).
      const view::Vec3 toEye =
          chaseEyeDirection(fixed->normal, travel, kDefaultFollowLift);
      const float achieved = clearEyeDistance(turned, fixed->centre, toEye, eyeDistance);
      if (achieved >= eyeDistance - 1e-3f) ++unoccluded;
    }
    CAPTURE(name, checked, unoccluded);
    // A Klein bottle passes through itself, so a cell or two can have a nearer sheet over
    // them whatever the slide; the orientable torus must be clear everywhere.
    CHECK(unoccluded >= (std::string(name) == "torus" ? checked : checked * 9 / 10));
  }
}

TEST_CASE("clearEyeDistance clamps to the last clear distance along the eye",
          "[render]") {
  // M17.20: the anti-clip must be able to pull the camera *closer* when the nominal
  // distance reaches past the shape, rather than trusting a fixed distance. Three cases:
  // clearly unblocked returns the nominal maximum, a direction that hits the far wall
  // before the maximum returns something strictly between, and one already blocked at the
  // minimum returns the minimum so the caller has a defined place to put the camera.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface surf = PlaySurface::build(v);
  REQUIRE_FALSE(surf.seats().empty());
  const SurfaceSeat& a = surf.seats().front();
  // The seat whose centre is farthest from `a`: the chord runs through the shape, so an
  // eye beyond it has that tile between it and `a`.
  const SurfaceSeat* far = &a;
  for (const SurfaceSeat& s : surf.seats()) {
    if (dist(s.centre, a.centre) > dist(far->centre, a.centre)) far = &s;
  }
  const float chord = dist(far->centre, a.centre);
  REQUIRE(chord > 0.5f);

  // Case 1: straight out along the surface normal, a short way - nothing in the way.
  const float clear = clearEyeDistance(surf, a.centre, a.normal, 0.2f, 0.01f);
  CHECK_THAT(clear, WithinAbs(0.2f, 1e-3f));

  // Case 2: toward the far seat and beyond it - blocked, but there is clear air first.
  const view::Vec3 through = view::normalize(far->centre - a.centre);
  const float partial = clearEyeDistance(surf, a.centre, through, 1.5f * chord, 0.01f);
  CHECK(partial > 0.01f);
  CHECK(partial < 1.5f * chord);

  // Case 3: the minimum itself already reaches past the far seat - nothing is clear.
  const float atMin = 1.2f * chord;
  const float none = clearEyeDistance(surf, a.centre, through, 2.0f * chord, atMin);
  CHECK_THAT(none, WithinAbs(atMin, 1e-3f));
}

TEST_CASE("a traced chase on a glued ring stays clear along the whole move", "[render]") {
  // M17.20 regression. The old anti-clip scored a *different* eye direction than the
  // camera drew (the raw normal instead of the orthogonalised one) and, when the nominal
  // distance was blocked, fell through to an unchecked best-facing rotation (root causes
  // 1 and 2). The two property tests above could not see it because they recomputed the
  // search's own formula and fed it a travel read off a seat's frame - perpendicular to
  // that seat's normal by construction, the one case where the two formulas agree. This
  // traces a real path (the a1-a5 rook slide the repro uses) and, at every sampled `t`,
  // runs the real search and checks the drawn sample against the camera's own shared
  // primitives.
  //
  // Torus only. On the self-intersecting Klein bottle the whole move (a1-a5) runs along
  // the rank seam - the figure-eight's pinch - where *no* searched rotation has a clear
  // line to the nominal distance at all (measured: the best achievable over the whole
  // sweep is ~0.5, the clamp floor, versus 0.9*eyeDistance required). That is the surface
  // passing through itself, not a search defect, and it is what `clearEyeDistance`'s
  // clamp exists to limit; klein is covered per-cell by "the chase align clears the shape
  // at the camera's own distance" above. The orientable torus, which is the reported
  // repro, must be clear everywhere.
  for (const char* name : {"torus"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    const view::Bounds rb = rest.bounds();
    const float span =
        std::max({rb.maxX - rb.minX, rb.maxY - rb.minY, rb.maxZ - rb.minZ});
    const float eyeDistance = std::max(2.0f, 0.7f * span);
    const float lift = kDefaultFollowLift;

    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session.has_value());
    app::Session& s = **session;
    const CellId a1 = v.dims.toCell(Coord::of({0, 0}));
    const CellId a5 = v.dims.toCell(Coord::of({0, 4}));
    const Move* move = nullptr;
    for (const Move& m : s.game().legalMoves()) {
      if (m.from == a1 && m.to == a5) move = &m;
    }
    REQUIRE(move != nullptr);
    const Piece mover = s.game().position().at(a1);
    const view::MovePath path =
        view::tracePath(v, s.game().position(), mover.type, mover.colorOf(), *move);
    REQUIRE_FALSE(path.steps.empty());

    const auto nearest = [&](const PlaySurface& surf, const view::Vec3& p) {
      CellId best = kInvalidCell;
      float bestD = 1e30f;
      for (const SurfaceSeat& st : surf.seats()) {
        const float d = dist(st.centre, p);
        if (d < bestD) {
          bestD = d;
          best = st.cell;
        }
      }
      return best;
    };

    constexpr int kSweep = 48;
    for (int i = 0; i <= kSweep; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(kSweep);
      const SurfaceMoveSample here = surfaceMoveSample(path, rest, t);
      const CellId cell = nearest(rest, here.position);
      REQUIRE(cell != kInvalidCell);
      // The real chase search: it scores each candidate against that candidate's own
      // travel on the traced path, and at the moving sample the camera actually draws
      // (M17.20).
      const SlideOffset off = alignSlideU(v, cell, path, t, eyeDistance, lift);
      SurfacePose pose;
      pose.slideU = off.u;
      pose.slideV = off.v;
      const PlaySurface turned = PlaySurface::build(v, pose);
      const SurfaceMoveSample sample = surfaceMoveSample(path, turned, t);
      view::Vec3 travelCam =
          surfaceMoveSample(path, turned, std::min(1.0f, t + 0.10f)).position -
          surfaceMoveSample(path, turned, std::max(0.0f, t - 0.10f)).position;
      if (view::length(travelCam) < 1e-5f) travelCam = sample.normal;
      const view::Vec3 dir = chaseEyeDirection(sample.normal, travelCam, lift);
      const float achieved = clearEyeDistance(turned, sample.position, dir, eyeDistance);
      CAPTURE(t, off.u, off.v, achieved, eyeDistance);
      CHECK(achieved >= 0.9f * eyeDistance);
    }
  }
}

TEST_CASE("a V slide keeps the Klein rank seam whole", "[render]") {
  // Measured on the Klein bottle's rank seam: sliding U grew the tile-corner gap to about
  // ten times an ordinary gap, while sliding V kept it near one. That is why only V is
  // offered on a non-orientable board.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  constexpr int kSide = kSurfaceSubdiv + 1;
  const auto rankRowGap = [&](const PlaySurface& s, int f, int r0, int r1, bool mirror) {
    const SurfacePatch& a = patchAt(s, v, f, r0);
    const SurfacePatch& b = patchAt(s, v, r1 < 0 ? nx - 1 - f : f, r1 < 0 ? 0 : r1);
    float gap = 0.0f;
    for (int i = 0; i < kSide; ++i) {
      const int bi = mirror ? kSide - 1 - i : i;
      gap = std::max(gap, dist(cornerOf(a, i, kSurfaceSubdiv), cornerOf(b, bi, 0)));
    }
    return gap;
  };
  for (float slide : {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f}) {
    SurfacePose pose;
    pose.slideV = slide;
    const PlaySurface s = PlaySurface::build(v, pose);
    const float ordinary = rankRowGap(s, 0, 2, 3, false);
    for (int f = 0; f < nx; ++f) {
      const float seam = rankRowGap(s, f, nz - 1, -1, true);
      CAPTURE(slide, f, seam, ordinary);
      CHECK(seam < 2.0f * ordinary);
    }
  }
}

TEST_CASE("the Klein surface closes its gluing", "[render]") {
  // Before anything about tiles: does the immersion actually join? The variant glues
  // the ranks with a file flip, so the point at rank 1 must be the point at rank 0 with
  // the file coordinate reversed; the files are glued straight, so u+1 is u. A join that
  // reversed the wrong axis, or by the wrong amount, would not match.
  const float eps = 2e-3f;
  for (int i = 0; i <= 8; ++i) {
    const float u = static_cast<float>(i) / 8.0f;
    const OvVec3 rank = derivedSurfaceAt(app::SurfaceKind::Klein, u, 1.0f);
    const OvVec3 back = derivedSurfaceAt(app::SurfaceKind::Klein, 1.0f - u, 0.0f);
    CAPTURE(u, rank.x, rank.y, rank.z, back.x, back.y, back.z);
    CHECK(std::abs(rank.x - back.x) < eps);
    CHECK(std::abs(rank.y - back.y) < eps);
    CHECK(std::abs(rank.z - back.z) < eps);

    const OvVec3 file = derivedSurfaceAt(app::SurfaceKind::Klein, u + 1.0f, 0.5f);
    const OvVec3 here = derivedSurfaceAt(app::SurfaceKind::Klein, u, 0.5f);
    CHECK(std::abs(file.x - here.x) < eps);
    CHECK(std::abs(file.y - here.y) < eps);
    CHECK(std::abs(file.z - here.z) < eps);
  }
}

TEST_CASE("the Klein gluing seams are ordinary cell steps, not gaps", "[render]") {
  // A gluing seam is where the board continues, so the two cells either side must sit
  // about one lattice step apart - not half a board, and not on top of each other.
  // Checked at a range of slides: the seam stays within the ordinary steps' own spread.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  for (float slide : {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f}) {
    SurfacePose pose;
    pose.slideU = slide;
    const PlaySurface s = PlaySurface::build(v, pose);
    // The yardstick is the widest *ordinary* step on the axis, not the step next to the
    // seam: a swept figure-eight makes file steps vary by nearly two to one, and the
    // claim is that the seam is no worse than an ordinary cell, not that every cell is
    // the same size.
    float widestU = 0.0f;
    float widestV = 0.0f;
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        if (f + 1 < nx)
          widestU = std::max(
              widestU, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f + 1, r).centre));
        if (r + 1 < nz)
          widestV = std::max(
              widestV, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f, r + 1).centre));
      }
    }
    // The rank seam: (f, nz-1) joins (nx-1-f, 0).
    for (int f = 0; f < nx; ++f) {
      const float seam =
          dist(seatAt(s, v, f, nz - 1).centre, seatAt(s, v, nx - 1 - f, 0).centre);
      CAPTURE(slide, f, seam, widestV);
      CHECK(seam < 1.25f * widestV);
      CHECK(seam > 0.4f * widestV);
    }
    // The file seam: (nx-1, r) joins (0, r).
    for (int r = 0; r < nz; ++r) {
      const float seam = dist(seatAt(s, v, nx - 1, r).centre, seatAt(s, v, 0, r).centre);
      CAPTURE(slide, r, seam, widestU);
      CHECK(seam < 1.25f * widestU);
      CHECK(seam > 0.4f * widestU);
    }
  }
}

TEST_CASE("a followed move's camera beats align, approach, travel, then return",
          "[render]") {
  // M17.19: the piece is held still through the lead-in (Align/Approach) and only starts
  // travelling once the camera has reached it; the return is a separate stage after the
  // move. Pure in elapsed, so a capture that states a time reproduces it.
  constexpr float kAlign = 0.6f, kApproach = 0.8f, kTravel = 1.5f, kReturn = 0.8f;
  const auto beat = [&](float e) {
    return shapeBeat(e, kAlign, kApproach, kTravel, kReturn);
  };

  CHECK(beat(0.0f).stage == ShapeStage::Align);
  CHECK(beat(0.3f).stage == ShapeStage::Align);
  CHECK_THAT(beat(0.3f).local, WithinAbs(0.5f, 1e-4f));  // half-way through align
  CHECK(beat(kAlign).stage == ShapeStage::Approach);     // the boundary falls through
  CHECK_THAT(beat(kAlign).local, WithinAbs(0.0f, 1e-4f));
  CHECK(beat(kAlign + 0.4f).stage == ShapeStage::Approach);
  CHECK(beat(kAlign + kApproach).stage == ShapeStage::Travel);
  // Travel's local tracks the move's own progress.
  CHECK_THAT(beat(kAlign + kApproach + 0.75f).local, WithinAbs(0.5f, 1e-4f));
  CHECK(beat(kAlign + kApproach + kTravel).stage == ShapeStage::Return);
  CHECK(beat(kAlign + kApproach + kTravel + 0.4f).stage == ShapeStage::Return);
  CHECK(beat(kAlign + kApproach + kTravel + kReturn + 0.01f).stage == ShapeStage::Done);
  CHECK(beat(100.0f).stage == ShapeStage::Done);

  // A zero-length stage is skipped rather than producing a division by zero.
  CHECK(shapeBeat(0.0f, 0.0f, 0.5f, 1.0f, 0.5f).stage == ShapeStage::Approach);
  CHECK(shapeBeat(0.0f, 0.0f, 0.0f, 0.0f, 0.0f).stage == ShapeStage::Done);

  // The ease is a smoothstep: flat at both ends, monotone between.
  CHECK_THAT(shapeEase(0.0f), WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(shapeEase(0.5f), WithinAbs(0.5f, 1e-6f));
  CHECK_THAT(shapeEase(1.0f), WithinAbs(1.0f, 1e-6f));
  CHECK(shapeEase(0.1f) < 0.1f);
  CHECK(shapeEase(0.9f) > 0.9f);
}

TEST_CASE("the look-ahead morph sees the camera about to cross the board", "[render]") {
  // M17.19: the morph is driven by looking at where the camera is going. On the torus's
  // inner ring the chase camera's own path crosses the tube; the aligned board clears it.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const auto cell = [&](int f, int r) { return v.dims.toCell(Coord::of({f, r})); };
  // A three-step rank glide on some file: on the unaided board the chase camera's own
  // path crosses the tube for at least one file (the inner side). Scanning files and `t`
  // rather than pinning either keeps this from riding on the glide sampler's exact arc.
  const PlaySurface surf = PlaySurface::build(v);
  const float eye = 6.0f;
  int clippedFile = -1;
  view::MovePath clippedPath;
  for (int f = 0; f < 8 && clippedFile < 0; ++f) {
    view::MovePath path;
    path.from = cell(f, 0);
    path.to = cell(f, 3);
    for (int r = 0; r < 3; ++r) {
      path.steps.push_back(
          {cell(f, r), cell(f, r + 1), view::StepKind::Interior, {}, 0, Side::Max});
    }
    for (float tt = 0.0f; tt <= 1.0001f; tt += 0.05f) {
      if (followClips(path, surf, tt, 0.06f, eye)) {
        clippedFile = f;
        clippedPath = path;
        break;
      }
    }
  }
  REQUIRE(clippedFile >= 0);
  // Turn the board so the followed cell presents its outer face, and the view is clear.
  const view::Vec3 travel{0.0f, 0.0f, 1.0f};
  const SlideOffset off = alignSlideU(v, cell(clippedFile, 3), travel, eye);
  SurfacePose pose;
  pose.slideU = off.u;
  pose.slideV = off.v;
  const PlaySurface turned = PlaySurface::build(v, pose);
  CHECK_FALSE(followClips(clippedPath, turned, 1.0f, 0.0f, eye));
}

TEST_CASE("the shape's seam rails sit on the flat view's seams", "[render]") {
  // M17.21. A closed two-dimensional shape has no near edge to hang a rim on the way the
  // flat board does, so it draws the seam as a rail along the wrap ring instead - the row
  // of cells where the lattice coordinate wraps from last back to first. The rail has to
  // land on the *same* cells the flat view's `SeamMap` colours and take the same colour,
  // or the two views disagree about where the seam is.
  const view::Theme theme = BoardRenderer{}.theme();
  for (const char* name : {"cylinder", "torus", "mobius", "klein"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
    // A glued 2-D board draws the files then the ranks, so a lattice axis and its screen
    // index are the same number - the assumption the rail's axis argument rests on.
    REQUIRE(cfg.screenAxes.size() >= 2);
    REQUIRE(cfg.screenAxes[0] == 0);
    REQUIRE(cfg.screenAxes[1] == 1);
    const view::SeamMap seams = view::SeamMap::build(v, cfg, theme);
    const PlaySurface surf = PlaySurface::build(v);
    REQUIRE_FALSE(surf.empty());

    for (const SurfaceSeat& seat : surf.seats()) {
      const Coord co = v.dims.toCoord(seat.cell);
      for (int axis = 0; axis < 2; ++axis) {
        // What the flat view says here, both ends of the ring: a glued seam at the cell's
        // own *Min* face and one at its *Max* face. The Min end of the coordinate-0 cell
        // and the Max end of the coordinate-(extent-1) cell sit at one coincident 3-D
        // point (the parametrisation is periodic), but each cell has its own vertex
        // copies, so the high corner has to be tinted on its own or the ring fades in
        // from one side and snaps off on the other (M17.21 revision).
        bool gluedMin = false;
        view::Rgba wantMin{};
        bool gluedMax = false;
        view::Rgba wantMax{};
        for (const view::SeamFace& f : seams.at(seat.cell)) {
          if (f.screenAxis != static_cast<std::uint8_t>(axis) ||
              f.kind != view::SeamKind::Glued) {
            continue;  // a mirror leads nowhere; no hue
          }
          if (f.side == Side::Min) {
            gluedMin = true;
            wantMin = f.color;
          } else {
            gluedMax = true;
            wantMax = f.color;
          }
        }
        // A rail exists only where the wrap row is really a glued seam. The low corner
        // belongs to the coordinate-0 cell, the high corner to the coordinate-(extent-1)
        // one.
        const int coord = axis == 0 ? co[0] : co[1];
        const int extent =
            static_cast<int>(v.dims.extent(static_cast<std::size_t>(axis)));
        const bool lowRing = gluedMin && coord == 0;
        const bool highRing = gluedMax && coord == extent - 1;
        for (int i = 0; i < kSurfaceSubdiv + 1; ++i) {
          for (int j = 0; j < kSurfaceSubdiv + 1; ++j) {
            const SurfaceRail r = surfaceRail(seams, seat.cell, axis, i, j);
            const int corner = axis == 0 ? i : j;
            const bool lowEdge = corner == 0;
            const bool highEdge = corner == kSurfaceSubdiv;
            CAPTURE(seat.file, seat.rank, axis, i, j, lowRing, highRing);
            if (lowRing && lowEdge) {
              CHECK(r.weight == 1.0f);
              CHECK(r.color.r == wantMin.r);
              CHECK(r.color.g == wantMin.g);
              CHECK(r.color.b == wantMin.b);
            } else if (highRing && highEdge) {
              CHECK(r.weight == 1.0f);
              CHECK(r.color.r == wantMax.r);
              CHECK(r.color.g == wantMax.g);
              CHECK(r.color.b == wantMax.b);
            } else {
              CHECK(r.weight == 0.0f);
            }
          }
        }
      }
    }
  }
}

TEST_CASE("the shape's coordinate rings are the two the rails mark", "[render]") {
  // M17.21. With no edge to label, the shape labels the same two reference rings the
  // rails colour: the file letters along the rank-0 ring, the rank numbers along the
  // file-0 ring. Eight labels each on an ordinary 8x8 glued board, matching the flat
  // view's count.
  for (const char* name : {"cylinder", "torus", "mobius", "klein"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const PlaySurface surf = PlaySurface::build(v);
    REQUIRE_FALSE(surf.empty());
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    int fileLabels = 0;
    int rankLabels = 0;
    for (const SurfaceSeat& seat : surf.seats()) {
      if (seat.rank == 0) ++fileLabels;
      if (seat.file == 0) ++rankLabels;
      // Every seat a label can land on has a real surface point and a normal to push the
      // text out along.
      CHECK(view::length(seat.normal) > 0.9f);
    }
    CHECK(fileLabels == nx);
    CHECK(rankLabels == nz);
  }
}

namespace {
/// The seat of a multi-axis cell, by its full coordinate. Returned by value: the callers
/// bind a `const&` to it, and a reference returned from here would trip the compiler's
/// dangling-reference warning for the `Coord` temporary.
SurfaceSeat seatAtCoord(const PlaySurface& s, const VariantSpec& v, const Coord& c) {
  const CellId want = v.dims.toCell(c);
  for (const SurfaceSeat& t : s.seats()) {
    if (t.cell == want) return t;
  }
  throw std::runtime_error("no such cell on the stacked surface");
}

const SurfaceSeat* seatOfCell(const PlaySurface& s, CellId c) {
  for (const SurfaceSeat& t : s.seats()) {
    if (t.cell == c) return &t;
  }
  return nullptr;
}
}  // namespace

TEST_CASE("a stacked shape slides along its own cells", "[render]") {
  // M17.22. `buildStacked` used to ignore the pose entirely. Sliding is the same
  // sampling one cell further along the surface: the shape is unchanged, differently
  // parked, so a1 lands exactly where b1 was. `torus3d` glues both its file and rank
  // axes, so both slides mean something; `hyper4` is a plain bounded 4-cube with no
  // periodic axis at all, so its slide is honestly a no-op - asserted, not faked.
  {
    const VariantSpec& v = *new VariantSpec(test::loadVariant("torus3d"));
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    const int nl = static_cast<int>(v.dims.extent(2));
    const PlaySurface rest = PlaySurface::build(v);
    for (int k = 1; k < nx; ++k) {
      SurfacePose pose;
      pose.slideU = static_cast<float>(k);
      const PlaySurface slid = PlaySurface::build(v, pose);
      REQUIRE(slid.seats().size() == rest.seats().size());
      for (int f = 0; f < nx; ++f) {
        for (int r = 0; r < nz; ++r) {
          for (int L = 0; L < nl; ++L) {
            const SurfaceSeat& a = seatAtCoord(slid, v, Coord::of({f, r, L}));
            const SurfaceSeat& b = seatAtCoord(rest, v, Coord::of({(f + k) % nx, r, L}));
            CAPTURE(k, f, r, L);
            CHECK_THAT(dist(a.centre, b.centre), WithinAbs(0.0f, 1e-4f));
            CHECK(view::dot(a.normal, b.normal) > 0.999f);
          }
        }
      }
    }
    // A full two-lap period brings the board home exactly, the same window the 2-D wrap
    // uses (M17.9).
    SurfacePose period;
    period.slideU = static_cast<float>(2 * nx);
    const PlaySurface home = PlaySurface::build(v, period);
    for (std::size_t i = 0; i < rest.seats().size(); ++i) {
      CHECK_THAT(dist(home.seats()[i].centre, rest.seats()[i].centre),
                 WithinAbs(0.0f, 1e-3f));
    }
    // The rank axis slides the same way.
    SurfacePose pv;
    pv.slideV = 1.0f;
    const PlaySurface slidV = PlaySurface::build(v, pv);
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        for (int L = 0; L < nl; ++L) {
          const SurfaceSeat& a = seatAtCoord(slidV, v, Coord::of({f, r, L}));
          const SurfaceSeat& b = seatAtCoord(rest, v, Coord::of({f, (r + 1) % nz, L}));
          CHECK_THAT(dist(a.centre, b.centre), WithinAbs(0.0f, 1e-4f));
        }
      }
    }
  }
  {
    const VariantSpec& v = *new VariantSpec(test::loadVariant("hyper4"));
    const PlaySurface rest = PlaySurface::build(v);
    SurfacePose pose;
    pose.slideU = 2.0f;
    pose.slideV = 1.0f;
    const PlaySurface slid = PlaySurface::build(v, pose);
    REQUIRE(slid.seats().size() == rest.seats().size());
    for (std::size_t i = 0; i < rest.seats().size(); ++i) {
      CHECK_THAT(dist(slid.seats()[i].centre, rest.seats()[i].centre),
                 WithinAbs(0.0f, 1e-6f));
    }
  }
}

TEST_CASE("hyper4's tesseract turns through two live fourth-dimension planes",
          "[render]") {
  // M17.12 part B. `playShapeAt`'s hyper4 branch used to rotate file/aeon by a fixed
  // `kSpin = 0.9`. The pose now carries two live angles - U is that same file/aeon plane,
  // V a second rank/aeon plane - so a middle-drag can turn the tesseract through the
  // fourth dimension instead of only orbiting the camera. The claims: the default pose is
  // the old fixed-spin picture bit for bit (the opening shot must not move), the second
  // plane is a real, independent degree of freedom, a full turn of either plane is
  // exactly a full turn (period 2*pi), and a small turn is continuous - every cell moves
  // by well under a cell.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("hyper4"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  const int nl = static_cast<int>(v.dims.extent(2));
  const int na = static_cast<int>(v.dims.extent(3));

  const auto axisOf = [&](int f, int r, int L, int A) {
    std::array<float, kMaxDims> axis{};
    axis[0] = static_cast<float>(f) + 0.5f;
    axis[1] = static_cast<float>(r) + 0.5f;
    axis[2] = static_cast<float>(L) + 0.5f;
    axis[3] = static_cast<float>(A) + 0.5f;
    return axis;
  };
  const auto project = [&](const SurfacePose& pose, int f, int r, int L, int A) {
    OvVec3 p;
    REQUIRE(playShapeAt(v, axisOf(f, r, L, A), p, pose));
    return p;
  };

  // The historical projection, written out here rather than reached through the code
  // under test, so the default pose is pinned against the old behaviour and not against
  // itself. `x`=file, `y`=level, `z`=rank, `w`=aeon, in the projected cube's own frame.
  const auto oldSpin = [](float x, float y, float z, float w) {
    constexpr float kSpin = 0.9f;
    const float nxv = x * std::cos(kSpin) - w * std::sin(kSpin);
    const float nw = x * std::sin(kSpin) + w * std::cos(kSpin);
    const float k = 2.35f / (3.5f - nw);
    return OvVec3{nxv * k * 2.15f, y * k * 2.15f * 1.05f, z * k * 2.15f};
  };
  const auto dist3 = [](const OvVec3& a, const OvVec3& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
  };

  const SurfacePose rest;
  // The default pose is exactly the historical fixed spin: same arithmetic, same order.
  for (int f = 0; f < nx; ++f) {
    for (int r = 0; r < nz; ++r) {
      for (int L = 0; L < nl; ++L) {
        for (int A = 0; A < na; ++A) {
          const OvVec3 got = project(rest, f, r, L, A);
          const OvVec3 want =
              oldSpin(static_cast<float>(f) - 1.5f, static_cast<float>(L) - 1.5f,
                      static_cast<float>(r) - 1.5f, static_cast<float>(A) - 1.5f);
          CAPTURE(f, r, L, A);
          CHECK(got.x == want.x);
          CHECK(got.y == want.y);
          CHECK(got.z == want.z);
        }
      }
    }
  }

  // The second plane is a real degree of freedom: a quarter turn of V moves cells that U
  // alone leaves put.
  {
    SurfacePose p;
    p.hyperSpinV = 1.0f;
    float moved = 0.0f;
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        for (int L = 0; L < nl; ++L) {
          moved =
              std::max(moved, dist3(project(p, f, r, L, 0), project(rest, f, r, L, 0)));
        }
      }
    }
    CHECK(moved > 0.1f);
  }

  // A full turn of either plane is exactly a full turn: period 2*pi, no drift.
  {
    SurfacePose full;
    full.hyperSpinU = rest.hyperSpinU + 2.0f * 3.14159265358979f;
    full.hyperSpinV = 2.0f * 3.14159265358979f;
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        for (int L = 0; L < nl; ++L) {
          CHECK(dist3(project(full, f, r, L, 0), project(rest, f, r, L, 0)) < 1e-3f);
        }
      }
    }
  }

  // Continuity: a small turn moves every cell by well under a cell's own spacing, on both
  // planes, so the tesseract turns rather than teleporting.
  for (const float d : {0.01f, -0.01f}) {
    SurfacePose pu;
    pu.hyperSpinU = rest.hyperSpinU + d;
    SurfacePose pv;
    pv.hyperSpinV = d;
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        for (int L = 0; L < nl; ++L) {
          CAPTURE(f, r, L, d);
          CHECK(dist3(project(pu, f, r, L, 0), project(rest, f, r, L, 0)) < 0.5f);
          CHECK(dist3(project(pv, f, r, L, 0), project(rest, f, r, L, 0)) < 0.5f);
        }
      }
    }
  }
}

TEST_CASE("hyper4's tesseract carries its own 16-corner wireframe scaffold", "[render]") {
  // M17.12 part D. A two-nested-cubes wireframe drawn from the board's *own* projection
  // gives the eye a small, stable skeleton to read the fourth-dimension rotation against;
  // 256 independently moving tiles do not. The 16 corners are the `playShapeAt` images of
  // the 4-cube's own lattice extremes, not a second, re-derived rotation - so the
  // scaffold turns in lockstep because it *is* the board's math at 16 fixed points. The
  // extreme is per axis, matching what the renderer projects: file/rank reach the lattice
  // edge
  // (`{0,4}`, i.e. `{-2,+2}`), while level/aeon are pinned at every cell's centre and
  // stay at `{0.5,3.5}` (`{-1.5,+1.5}`) - reaching past that is the original overshoot
  // defect (M17.12 part D revision). The 32 edges are the 4-cube's XOR adjacency, exactly
  // the topology `deco.cpp`'s menu tesseract already draws.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("hyper4"));
  const SurfacePose rest;
  // The per-axis extreme the corner builder must use: file/rank at the lattice edge,
  // level/aeon at the cell-centre extreme. Shared by the cross-check below.
  const auto extreme = [](int i, int d) {
    const bool subdivided = d < 2;
    return ((i >> d) & 1) ? (subdivided ? 4.0f : 3.5f) : (subdivided ? 0.0f : 0.5f);
  };

  // The scaffold is `hyper4`'s alone.
  CHECK(hypercubeScaffoldCorners(*new VariantSpec(test::loadVariant("torus3d")), rest)
            .empty());
  CHECK(hypercubeScaffoldCorners(*new VariantSpec(test::loadVariant("torus")), rest)
            .empty());

  const std::vector<view::Vec3> corners = hypercubeScaffoldCorners(v, rest);
  REQUIRE(corners.size() == 16);

  // Every corner is exactly `playShapeAt` at its own extreme lattice coordinate, in the
  // board's Z-up world - the no-duplicated-formula guarantee. The extent is per axis, as
  // the builder's is.
  for (int i = 0; i < 16; ++i) {
    std::array<float, kMaxDims> axis{};
    for (int d = 0; d < 4; ++d) {
      axis[static_cast<std::size_t>(d)] = extreme(i, d);
    }
    OvVec3 p;
    REQUIRE(playShapeAt(v, axis, p, rest));
    CHECK_THAT(dist(corners[static_cast<std::size_t>(i)], view::Vec3{p.x, -p.z, p.y}),
               WithinAbs(0.0f, 1e-5f));
  }

  // Sixteen distinct, well-separated points: not a degenerate collapse to one spot.
  for (int i = 0; i < 16; ++i) {
    for (int j = i + 1; j < 16; ++j) {
      CAPTURE(i, j);
      CHECK(dist(corners[static_cast<std::size_t>(i)],
                 corners[static_cast<std::size_t>(j)]) > 0.5f);
    }
  }

  // Two nested cubes: the 8 corners on one aeon sheet are the inner cube and project to a
  // strictly smaller silhouette than the other 8 - the aeon nesting the projection's own
  // depth uses. The scaffold encloses the cell stack rather than passing through it.
  view::Vec3 centroid{};
  for (const view::Vec3& c : corners) centroid = centroid + c;
  centroid = centroid * (1.0f / 16.0f);
  float inner = 0.0f;
  float outer = 0.0f;
  for (int i = 0; i < 16; ++i) {
    const float r = dist(corners[static_cast<std::size_t>(i)], centroid);
    if (((i >> 3) & 1) == 0) {
      inner = std::max(inner, r);
    } else {
      outer = std::max(outer, r);
    }
  }
  CHECK(inner > 0.0f);
  CHECK(inner < outer * 0.8f);

  // Regression guard (M17.12 part D revision). The scaffold must sit *around* the drawn
  // board, not shoot past it. The original defect pushed level/aeon to the lattice edge
  // as well as file/rank, producing coordinates the renderer never projects; because the
  // perspective divide is nonlinear in the rotated aeon, the worst corner then fell far
  // outside what is actually drawn - the "spikes past the board" look. The honest
  // reference is the board's real geometry, the patch corners the renderer puts on screen
  // (their own subdivision reaches a cell's true `kCoverage` edge at `+/-1.95`, one step
  // beyond the outermost cell *centre* at `+/-1.5`, which alone understates the extent).
  // The bound is loose (1.5x): the scaffold should clearly enclose the cells, but not by
  // anything like the old overshoot (~1.75x on this metric; the corrected scaffold sits
  // within ~1.06x, just outside the outermost patch).
  const PlaySurface surface = PlaySurface::build(v);
  REQUIRE(surface.patches().size() == v.dims.cellCount());
  view::Vec3 boardCentroid{};
  std::size_t boardPoints = 0;
  for (const SurfacePatch& patch : surface.patches()) {
    for (const view::Vec3& p : patch.pos) {
      boardCentroid = boardCentroid + p;
      ++boardPoints;
    }
  }
  boardCentroid = boardCentroid * (1.0f / static_cast<float>(boardPoints));
  float patchMax = 0.0f;
  for (const SurfacePatch& patch : surface.patches()) {
    for (const view::Vec3& p : patch.pos) {
      patchMax = std::max(patchMax, dist(p, boardCentroid));
    }
  }
  float scaffoldMax = 0.0f;
  for (const view::Vec3& c : corners) {
    scaffoldMax = std::max(scaffoldMax, dist(c, boardCentroid));
  }
  CAPTURE(scaffoldMax, patchMax, scaffoldMax / patchMax);
  CHECK(scaffoldMax <= patchMax * 1.5f);

  const std::vector<ScaffoldEdge> edges = hypercubeScaffoldEdges(v, rest);
  REQUIRE(edges.size() == 32);
  // XOR adjacency: every vertex has degree exactly 4 (a 4-cube's own).
  std::array<int, 16> degree{};
  for (const ScaffoldEdge& e : edges) {
    int ia = -1;
    int ib = -1;
    for (int i = 0; i < 16; ++i) {
      if (dist(e.a, corners[static_cast<std::size_t>(i)]) < 1e-4f) ia = i;
      if (dist(e.b, corners[static_cast<std::size_t>(i)]) < 1e-4f) ib = i;
    }
    REQUIRE(ia >= 0);
    REQUIRE(ib >= 0);
    ++degree[static_cast<std::size_t>(ia)];
    ++degree[static_cast<std::size_t>(ib)];
  }
  for (int i = 0; i < 16; ++i) CHECK(degree[static_cast<std::size_t>(i)] == 4);
  // Each bar's own frame turns its local +X onto the edge it spans, so the renderer can
  // lay one thin bar along an arbitrary 3-D direction (unlike the axis-aligned rails).
  for (const ScaffoldEdge& e : edges) {
    const view::Vec3 along = byQuat(e.quat, view::Vec3{1.0f, 0.0f, 0.0f});
    CHECK_THAT(dist(along, view::normalize(e.b - e.a)), WithinAbs(0.0f, 1e-4f));
  }

  // Continuity, matching part B's: a small turn of either 4-D plane moves every corner by
  // well under its own spacing, so the scaffold turns rather than jumping.
  for (const float d : {0.01f, -0.01f}) {
    SurfacePose pu;
    pu.hyperSpinU = rest.hyperSpinU + d;
    SurfacePose pv;
    pv.hyperSpinV = d;
    const std::vector<view::Vec3> cu = hypercubeScaffoldCorners(v, pu);
    const std::vector<view::Vec3> cv = hypercubeScaffoldCorners(v, pv);
    REQUIRE(cu.size() == 16);
    REQUIRE(cv.size() == 16);
    for (int i = 0; i < 16; ++i) {
      CAPTURE(i, d);
      CHECK(dist(cu[static_cast<std::size_t>(i)], corners[static_cast<std::size_t>(i)]) <
            1.0f);
      CHECK(dist(cv[static_cast<std::size_t>(i)], corners[static_cast<std::size_t>(i)]) <
            1.0f);
    }
  }
}

TEST_CASE("invert swaps the side on a stacked shape without moving it", "[render]") {
  // M17.22. The same revised invert the 2-D board uses (M17.7): the geometry is
  // identical and the outward normal is reversed, so a piece stands on the other face
  // while the squares stay exactly where they were. It is pure in `evert`, so a capture
  // reproduces.
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    for (int i = 0; i <= 10; ++i) {
      SurfacePose p;
      p.evert = static_cast<float>(i) / 10.0f;
      const PlaySurface a = PlaySurface::build(v, p);
      const PlaySurface b = PlaySurface::build(v, p);
      REQUIRE(a.seats().size() == rest.seats().size());
      for (std::size_t c = 0; c < a.seats().size(); ++c) {
        // Pure in the parameter...
        CHECK_THAT(dist(a.seats()[c].centre, b.seats()[c].centre),
                   WithinAbs(0.0f, 1e-6f));
        CHECK_THAT(view::dot(a.seats()[c].normal, b.seats()[c].normal),
                   WithinAbs(1.0f, 1e-6f));
        // ...and the squares never move, at any point of the turn.
        CHECK_THAT(dist(a.seats()[c].centre, rest.seats()[c].centre),
                   WithinAbs(0.0f, 1e-6f));
      }
    }
    SurfacePose full;
    full.evert = 1.0f;
    const PlaySurface turned = PlaySurface::build(v, full);
    REQUIRE(turned.seats().size() == rest.seats().size());
    for (std::size_t c = 0; c < rest.seats().size(); ++c) {
      // Opposite sides of the same local tangent plane: the seats coincide and the
      // normals are exactly reversed.
      CHECK_THAT(dist(turned.seats()[c].centre, rest.seats()[c].centre),
                 WithinAbs(0.0f, 1e-6f));
      CHECK_THAT(view::dot(turned.seats()[c].normal, rest.seats()[c].normal),
                 WithinAbs(-1.0f, 1e-4f));
    }
  }
}

TEST_CASE("ghosted picking reaches a hidden tile on a stacked shape", "[render]") {
  // M17.22. A `D >= 3` shape is self-intersecting, so an outer sheet hides the cell
  // behind it and nearest-hit picking can never reach it. With the board ghosted
  // (`geometryGhost` below the visibility threshold) the pick walks *all* the
  // intersections, accumulates opacity toward the eye and returns the first tile at which
  // the stacked board reads as more opaque than transparent - so a click through a
  // ghosted outer shell reaches the one inside it. At full opacity the same ray still
  // picks the nearest tile, exactly as before.
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const float w = 1000.0f;
    const float h = 760.0f;
    const view::OrbitCamera cam = view::OrbitCamera::frame(s.bounds(), w / h, 0.0f);
    const view::Vec3 eye = cam.eye();
    int reachedDeeper = 0;
    for (const SurfaceSeat& t : s.seats()) {
      const view::OrbitCamera::ScreenPoint sp = cam.project(t.centre, w / h, w, h);
      if (!sp.visible) continue;
      const CellId near = s.pick(cam, w, h, sp.x, sp.y, 1.0f);
      const CellId deep = s.pick(cam, w, h, sp.x, sp.y, 0.35f);
      if (near == kInvalidCell || deep == kInvalidCell || near == deep) continue;
      const SurfaceSeat* np = seatOfCell(s, near);
      const SurfaceSeat* dp = seatOfCell(s, deep);
      REQUIRE(np != nullptr);
      REQUIRE(dp != nullptr);
      CAPTURE(t.cell, near, deep);
      // The ghosted answer is genuinely behind the opaque one along the ray.
      if (dist(dp->centre, eye) > dist(np->centre, eye) + 0.05f) ++reachedDeeper;
    }
    CHECK(reachedDeeper > 0);
    // The default pick is the opaque one, so existing callers are unchanged.
    for (const SurfaceSeat& t : s.seats()) {
      const view::OrbitCamera::ScreenPoint sp = cam.project(t.centre, w / h, w, h);
      if (!sp.visible) continue;
      CHECK(s.pick(cam, w, h, sp.x, sp.y) == s.pick(cam, w, h, sp.x, sp.y, 1.0f));
    }
  }
}

TEST_CASE("the shape view picks a piece standing off its own tile", "[render]") {
  // M17.27. A stacked shape's pieces stand along their seat's normal, which on
  // `torus3d`'s nested shells can point almost anywhere relative to the camera, so the
  // visible body projects clear of the bare, zero-height tile patch. A ray aimed at the
  // body used to test the tile and miss. The piece-aware pick tests a volume along the
  // seat's normal and resolves the click to the cell the piece stands on. Only seats
  // whose tile-only pick is a clean miss are asserted, so a body hidden behind a nearer
  // shell (correctly not selected) cannot make this flap.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus3d"));
  const PlaySurface s = PlaySurface::build(v);
  const float w = 1000.0f;
  const float h = 760.0f;
  const view::OrbitCamera cam = view::OrbitCamera::frame(s.bounds(), w / h, 0.0f);
  const PieceTypeId rook = v.findPiece("rook");
  const float pieceH = heightFor(v.pieces[rook]);

  int demonstrated = 0;
  for (const SurfaceSeat& seat : s.seats()) {
    Position p = Position::startPosition(v);
    p.clear();
    p.place(seat.cell, rook, Color::White);
    const view::PositionView pv = view::PositionView::capture(p);
    const float fit = std::min(std::sqrt(seat.stepU * seat.stepV), 1.0f);
    const float lift = PlaySurface::kThickness * 0.5f;
    // A point on the piece's own rendered axis, above the tile plane - not the tile
    // centre, which is the whole point.
    const view::Vec3 body =
        seat.centre + seat.normal * (lift + 0.5f * 0.8f * fit * pieceH);
    const view::OrbitCamera::ScreenPoint sp = cam.project(body, w / h, w, h);
    if (!sp.visible) continue;
    const CellId tileOnly = s.pick(cam, w, h, sp.x, sp.y);
    const CellId withPiece = s.pick(cam, w, h, sp.x, sp.y, 1.0f, &pv);
    if (tileOnly == kInvalidCell) {
      CAPTURE(seat.cell, sp.x, sp.y);
      CHECK(withPiece == seat.cell);
      ++demonstrated;
    }
  }
  CHECK(demonstrated > 0);
}

TEST_CASE("a ghosted tile lets the shape view reach the piece behind it", "[render]") {
  // M17.27, the report's own ask: "if ghost mode is enabled, the choice can bypass
  // through the squares". A piece behind a ghosted tile is still reached when the
  // accumulated opacity of the tiles in front has not crossed the visibility threshold;
  // with the board opaque the same ray still stops at the near tile (M17.22, unchanged).
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus3d"));
  const PlaySurface s = PlaySurface::build(v);
  const float w = 1000.0f;
  const float h = 760.0f;
  const view::OrbitCamera cam = view::OrbitCamera::frame(s.bounds(), w / h, 0.0f);
  const PieceTypeId rook = v.findPiece("rook");
  const float pieceH = heightFor(v.pieces[rook]);

  int reached = 0;
  for (const SurfaceSeat& seat : s.seats()) {
    Position p = Position::startPosition(v);
    p.clear();
    p.place(seat.cell, rook, Color::White);
    const view::PositionView pv = view::PositionView::capture(p);
    const float fit = std::min(std::sqrt(seat.stepU * seat.stepV), 1.0f);
    const float lift = PlaySurface::kThickness * 0.5f;
    const view::Vec3 body =
        seat.centre + seat.normal * (lift + 0.5f * 0.8f * fit * pieceH);
    const view::OrbitCamera::ScreenPoint sp = cam.project(body, w / h, w, h);
    if (!sp.visible) continue;
    const CellId opaque = s.pick(cam, w, h, sp.x, sp.y, 1.0f, &pv);
    const CellId tileGhost = s.pick(cam, w, h, sp.x, sp.y, 0.35f);
    const CellId pieceGhost = s.pick(cam, w, h, sp.x, sp.y, 0.35f, &pv);
    // Opaque, the near tile wins. Ghosted, the bare tiles do not reach this body point
    // (the ray is off the tile), but the piece behind the translucent sheet does.
    if (opaque != kInvalidCell && opaque != seat.cell && tileGhost != seat.cell &&
        pieceGhost == seat.cell) {
      CAPTURE(seat.cell, opaque, tileGhost, sp.x, sp.y);
      ++reached;
    }
  }
  CAPTURE(reached);
  CHECK(reached > 0);
}

TEST_CASE("the stacked shape's adjacency is pinned, not asserted in prose", "[render]") {
  // M17.22. `torus3d`'s level axis and `hyper4`'s aeon axis are not Euclidean nearness:
  // a level-adjacent pair sits on *different nested shells at the same angular position*
  // around the core circle. Pinning it means a later change cannot silently "fix" it or
  // break it further without the suite noticing.
  {
    const VariantSpec& v = *new VariantSpec(test::loadVariant("torus3d"));
    const PlaySurface s = PlaySurface::build(v);
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    const int nl = static_cast<int>(v.dims.extent(2));
    // `shellTube` at the fully-rolled point puts the big loop (the rank revolve) in the
    // board's X-Y plane with its core circle on the Z axis, so a cell's angular position
    // around that loop is `atan2(y, x)` and its shell radius is `hypot(x, y)` - the
    // distance out to the nested tube the level axis chose.
    const auto angle = [](const view::Vec3& c) { return std::atan2(c.y, c.x); };
    const auto radius = [](const view::Vec3& c) { return std::hypot(c.x, c.y); };
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        const SurfaceSeat& here = seatAtCoord(s, v, Coord::of({f, r, 0}));
        const float step = std::max(here.stepU, here.stepV);
        // File/rank neighbours are ordinary cell steps on the same shell.
        if (f + 1 < nx) {
          const SurfaceSeat& n = seatAtCoord(s, v, Coord::of({f + 1, r, 0}));
          CHECK(dist(here.centre, n.centre) < 2.0f * step);
        }
        if (r + 1 < nz) {
          const SurfaceSeat& n = seatAtCoord(s, v, Coord::of({f, r + 1, 0}));
          CHECK(dist(here.centre, n.centre) < 2.0f * step);
          // A rank step rotates about the core, so the shell radius is unchanged.
          CHECK_THAT(radius(n.centre), WithinAbs(radius(here.centre), 1e-4f));
        }
        // Level neighbours sit on index-adjacent nested shells, at the same angular
        // position around the core - a radial move, not a step along the surface.
        for (int L = 0; L + 1 < nl; ++L) {
          const SurfaceSeat& a = seatAtCoord(s, v, Coord::of({f, r, L}));
          const SurfaceSeat& b = seatAtCoord(s, v, Coord::of({f, r, L + 1}));
          CAPTURE(f, r, L, angle(a.centre), angle(b.centre), radius(a.centre),
                  radius(b.centre));
          CHECK(std::abs(angle(a.centre) - angle(b.centre)) < 0.02f);
          CHECK(std::abs(radius(a.centre) - radius(b.centre)) > 0.05f);
          CHECK(dist(a.centre, b.centre) > 0.05f);
        }
      }
    }
  }
  {
    const VariantSpec& v = *new VariantSpec(test::loadVariant("hyper4"));
    const PlaySurface s = PlaySurface::build(v);
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    const int nl = static_cast<int>(v.dims.extent(2));
    const int na = static_cast<int>(v.dims.extent(3));
    for (int f = 0; f + 1 < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        const SurfaceSeat& here = seatAtCoord(s, v, Coord::of({f, r, 0, 0}));
        const SurfaceSeat& n = seatAtCoord(s, v, Coord::of({f + 1, r, 0, 0}));
        CHECK(dist(here.centre, n.centre) < 2.0f * std::max(here.stepU, here.stepV));
      }
    }
    for (int r = 0; r + 1 < nz; ++r) {
      const SurfaceSeat& here = seatAtCoord(s, v, Coord::of({0, r, 0, 0}));
      const SurfaceSeat& n = seatAtCoord(s, v, Coord::of({0, r + 1, 0, 0}));
      CHECK(dist(here.centre, n.centre) < 2.0f * std::max(here.stepU, here.stepV));
    }
    // The aeon axis is the nesting: adjacent aeons are distinct tiles on different nested
    // cubes, not a step along the spatial surface. The projection's spin means their
    // drawn offset is not a fixed cell step, so the claim is only that each is a real,
    // separate tile and the four aeons project to four distinct places.
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        for (int L = 0; L < nl; ++L) {
          for (int A = 0; A + 1 < na; ++A) {
            const SurfaceSeat& a = seatAtCoord(s, v, Coord::of({f, r, L, A}));
            const SurfaceSeat& b = seatAtCoord(s, v, Coord::of({f, r, L, A + 1}));
            CAPTURE(f, r, L, A, dist(a.centre, b.centre));
            CHECK(dist(a.centre, b.centre) > 1e-3f);
          }
        }
      }
    }
  }
}

TEST_CASE("the morph's flat end is the flat board, reflected in rank", "[render]") {
  // M18.6 deviation, pinned so it cannot drift silently. `formed = 0` is the flat board -
  // every cell at the ordinary one-cell spacing - but embedded with the opposite
  // handedness to the ordinary 3D board: files match, ranks reverse. That is the real
  // reason the SHAPE morph cannot land exactly on the lattice board: joining two flat
  // embeddings of opposite handedness needs a rank collapse (or changing the shipped
  // shape, which the spec forbids), so the exact-landing sub-part is deferred. This test
  // records the relationship rather than leaving it to an eyeball check.
  for (const std::string& name : kShapes) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    SurfacePose pose;
    pose.formed = 0.0f;
    const PlaySurface s = PlaySurface::build(v, pose);
    REQUIRE_FALSE(s.empty());
    const std::vector<view::Placement> flat =
        view::layout(v.dims, view::ViewConfig::forBoard(v.dims));
    REQUIRE(flat.size() == s.seats().size());

    const auto seatFor = [&](CellId c) -> const SurfaceSeat* {
      for (const SurfaceSeat& t : s.seats()) {
        if (t.cell == c) return &t;
      }
      return nullptr;
    };
    view::Vec3 sc{0, 0, 0};
    for (const SurfaceSeat& t : s.seats()) sc = sc + t.centre;
    sc = sc * (1.0f / static_cast<float>(s.seats().size()));
    view::Vec3 fc{0, 0, 0};
    for (const view::Placement& p : flat) fc = fc + view::Vec3{p.x, p.y, p.z};
    fc = fc * (1.0f / static_cast<float>(flat.size()));

    // The surface's own per-cell steps; the layout's are 1. Normalising by them makes the
    // comparison about the *arrangement* rather than about the Moebius ribbon's stretch,
    // which deliberately spends area on the twist.
    const SurfaceSeat* s00 = seatFor(v.dims.toCell(Coord::of({0, 0})));
    const SurfaceSeat* s10 = seatFor(v.dims.toCell(Coord::of({1, 0})));
    const SurfaceSeat* s01 = seatFor(v.dims.toCell(Coord::of({0, 1})));
    REQUIRE(s00 != nullptr);
    REQUIRE(s10 != nullptr);
    REQUIRE(s01 != nullptr);
    const float sx = s10->centre.x - s00->centre.x;
    const float sy = s01->centre.y - s00->centre.y;
    REQUIRE(std::abs(sx) > 1e-3f);
    REQUIRE(std::abs(sy) > 1e-3f);
    // The ordinary board runs files +X and ranks +Y; the surface's flat embedding keeps
    // the files and reverses the ranks. That sign is the whole story.
    CHECK(sx > 0.0f);
    CHECK(sy < 0.0f);

    for (const view::Placement& p : flat) {
      const SurfaceSeat* t = seatFor(p.cell);
      REQUIRE(t != nullptr);
      const view::Vec3 want{p.x - fc.x, p.y - fc.y, p.z - fc.z};
      const view::Vec3 got = t->centre - sc;
      CAPTURE(p.cell, want.x, want.y, want.z, got.x, got.y, got.z, sx, sy);
      // Files and ranks land on the same lattice points once the surface's own steps are
      // divided out, and the board is flat. The rank sign above is what a continuous
      // morph cannot reproduce against the ordinary board.
      CHECK_THAT(got.x / sx, WithinAbs(want.x, 1e-3f));
      CHECK_THAT(got.y / sy, WithinAbs(want.y, 1e-3f));
      CHECK_THAT(got.z, WithinAbs(want.z, 1e-3f));
    }
  }
}

TEST_CASE("formed one is the shipped surface, bit for bit", "[render]") {
  // M18.6 backward compatibility: the new `formed` defaults to 1, so every existing
  // caller and every golden capture must be reproduced exactly. Any value that is not
  // the formed surface is the new behaviour; 1.0f is the one that is not allowed to move.
  for (const std::string& name : kShapes) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface a = PlaySurface::build(v);  // default pose: formed == 1
    SurfacePose one;
    one.formed = 1.0f;
    const PlaySurface b = PlaySurface::build(v, one);
    REQUIRE(a.seats().size() == b.seats().size());
    for (std::size_t i = 0; i < a.seats().size(); ++i) {
      CHECK(dist(a.seats()[i].centre, b.seats()[i].centre) == 0.0f);
      CHECK(dist(a.seats()[i].normal, b.seats()[i].normal) == 0.0f);
    }
  }
}

TEST_CASE("the flat-to-shape morph is continuous and never degenerate", "[render]") {
  // M18.6: `--formed 0.5` must be a recognisable mid-shape, not a half-built mesh. A
  // fine sweep must move every seat by a bounded step and keep every seat a real, sized
  // square at every point of the transition.
  for (const char* name : {"torus", "klein", "mobius"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    PlaySurface prev;
    constexpr int kSteps = 100;
    for (int i = 0; i <= kSteps; ++i) {
      SurfacePose pose;
      pose.formed = static_cast<float>(i) / static_cast<float>(kSteps);
      const PlaySurface s = PlaySurface::build(v, pose);
      REQUIRE(s.seats().size() == static_cast<std::size_t>(v.dims.cellCount()));
      for (const SurfaceSeat& t : s.seats()) {
        CHECK(std::isfinite(t.centre.x));
        CHECK(std::isfinite(t.centre.y));
        CHECK(std::isfinite(t.centre.z));
        CHECK(t.stepU > 0.01f);
        CHECK(t.stepV > 0.01f);
      }
      if (i > 0) {
        for (std::size_t c = 0; c < s.seats().size(); ++c) {
          CAPTURE(i, c, pose.formed);
          // A hundredth of the morph moves a seat well under a cell: the transition is a
          // continuous roll, not a snap to some other board.
          CHECK(dist(s.seats()[c].centre, prev.seats()[c].centre) < 1.5f);
        }
      }
      prev = s;
    }
  }
}

TEST_CASE("the Mobius ribbon stretches before it curls", "[render]") {
  // M18 revision, item 2. The bug, from actual play: the ribbon went from the square
  // board to the fully-stretched strip instantly and only the curl animated, because
  // `PlaySurface::build` pinned `pose.stretch` at its play value on frame 0. The morph
  // now spends `kRibbonStretchWindow` of `formed` stretching with the ring angle still
  // zero, then curls. Measured the way the flat-end test measures: from the seats' own
  // steps.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("mobius"));
  constexpr int kSamples = 200;
  float prevRatio = 0.0f;
  bool ringCurled = false;
  for (int i = 0; i <= kSamples; ++i) {
    const float form = static_cast<float>(i) / static_cast<float>(kSamples);
    SurfacePose pose;
    pose.formed = form;
    const PlaySurface s = PlaySurface::build(v, pose);
    const SurfaceSeat& s00 = seatAt(s, v, 0, 0);
    const SurfaceSeat& s10 = seatAt(s, v, 1, 0);
    const SurfaceSeat& s01 = seatAt(s, v, 0, 1);
    const float ratio = dist(s10.centre, s00.centre) / dist(s01.centre, s00.centre);
    float maxZ = 0.0f;
    for (const SurfaceSeat& t : s.seats()) maxZ = std::max(maxZ, std::abs(t.centre.z));
    CAPTURE(form, ratio, maxZ);
    if (form <= kRibbonStretchWindow + 1e-6f) {
      // Still flat, and elongating: the ratio starts at 1:1 and grows.
      CHECK_THAT(maxZ, WithinAbs(0.0f, 1e-4f));
      if (i == 0) {
        CHECK_THAT(ratio, WithinAbs(1.0f, 1e-3f));
      } else {
        CHECK(ratio > prevRatio);
      }
      if (form >= kRibbonStretchWindow - 1e-6f) CHECK(ratio > 3.0f);
    } else if (maxZ > 1.0f) {
      ringCurled = true;
    }
    prevRatio = ratio;
  }
  // The ring does curl once the stretch is spent - the test has teeth.
  CHECK(ringCurled);
}

TEST_CASE("the Klein morph is three legible stages", "[render]") {
  // M18 revision, item 3. The bug, from actual play: the ring, the figure-eight fold and
  // the twist all ran off the same `form`, while the twist was applied at full strength
  // from frame 0 - a twist on a half-folded, self-intersecting cross-section inside a
  // still-forming ring, which read as winding through the inside of a torus. The morph is
  // now three sequential windows: ring, fold, twist.
  constexpr float kTau = 6.283185307179586f;
  constexpr float kTwist = 1.0f;
  constexpr int kSamples = 1000;
  {
    const KleinMorph a = kleinMorph(0.0f, kTwist);
    CHECK(a.th == 0.0f);
    CHECK(a.pinch == 0.0f);
    CHECK(a.ph == 0.0f);
    CHECK(a.tau == 0.0f);
    const KleinMorph b = kleinMorph(1.0f, kTwist);
    CHECK(b.th == kTau);
    CHECK(b.pinch == 1.0f);
    CHECK(b.ph == kTau);
    CHECK(b.tau == kTwist);
  }
  // Window boundaries, discovered from the staged parameters themselves, so retuning them
  // does not break the test.
  float ringEnd = -1.0f;
  float foldEnd = -1.0f;
  for (int i = 0; i <= kSamples; ++i) {
    const float form = static_cast<float>(i) / static_cast<float>(kSamples);
    const KleinMorph k = kleinMorph(form, kTwist);
    if (ringEnd < 0.0f && k.pinch > 0.0f) ringEnd = form;
    if (foldEnd < 0.0f && k.tau > 0.0f) foldEnd = form;
  }
  REQUIRE(ringEnd > 0.0f);
  REQUIRE(foldEnd > ringEnd);

  float prevTh = -1.0f;
  float prevPinch = -1.0f;
  float prevTau = -1.0f;
  for (int i = 0; i <= kSamples; ++i) {
    const float form = static_cast<float>(i) / static_cast<float>(kSamples);
    const KleinMorph k = kleinMorph(form, kTwist);
    CAPTURE(form, k.th, k.pinch, k.ph, k.tau);
    CHECK(k.th == k.ph);
    if (form < ringEnd) {
      // Window 1: the ring only, and a plain circular cross-section (no fold, no twist).
      CHECK(k.th >= prevTh);
      if (form > 0.0f) CHECK(k.th > prevTh);
      CHECK(k.pinch == 0.0f);
      CHECK(k.tau == 0.0f);
    } else if (form < foldEnd) {
      // Window 2: the fold, the ring held, still no twist.
      CHECK(k.th == kTau);
      CHECK(k.tau == 0.0f);
      CHECK(k.pinch > prevPinch);
    } else {
      // Window 3: the twist, the fold held.
      CHECK(k.pinch == 1.0f);
      CHECK(k.tau > prevTau);
    }
    prevTh = k.th;
    prevPinch = k.pinch;
    prevTau = k.tau;
  }
}

TEST_CASE(
    "a Klein seat's low corner is nearest its true partner's high corner, under "
    "any shift and slide",
    "[render]") {
  // Tighter than "the surface closes the seams the variant declares" above: that test
  // compares seat CENTRES against a tolerance of `1.1 * widestV`, the single worst
  // adjacent-rank gap anywhere on the board - which on a Klein bottle's own pinch can be
  // large enough to make the tolerance nearly meaningless. This compares the actual
  // wrap-boundary CORNERS (the real seam locus) against every other file's corner, by
  // exhaustive nearest-neighbour search, so a real mismatch cannot hide behind a loose
  // bound. Ground truth for "which file is file f's true rank-seam partner" is read
  // straight from `SeamMap` (the topology engine), not assumed.
  //
  // The key subtlety a from-scratch version of this check gets wrong easily (this one
  // did, before being corrected): cell f's LOW file corner (i = 0) is the shared point
  // with cell (f - 1)'s HIGH file corner, not with cell f's own partner's low corner - a
  // reflection reverses orientation within each cell. Comparing low-to-low instead of
  // low-to-high gives a meaningless ~1.3-1.6 world-unit "mismatch" even at the default,
  // already-correct pose, which is exactly the false alarm that happened while deriving
  // this test.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::SeamMap seams = view::SeamMap::build(v, cfg, view::Theme{});
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));

  std::map<int, int> truePartner;
  for (int f = 0; f < nx; ++f) {
    const CellId c = v.dims.toCell(Coord::of({f, 0}));
    for (const view::SeamFace& face : seams.at(c)) {
      if (face.side == Side::Min && face.screenAxis == 1) {
        truePartner[f] = v.dims.toCoord(face.partner)[0];
      }
    }
  }
  REQUIRE(static_cast<int>(truePartner.size()) == nx);

  for (const float shift : {0.0f, 1.0f, 3.0f}) {
    for (const float lapsSlide : {0.0f, 1.0f, 1.75f}) {
      CAPTURE(shift, lapsSlide);
      SurfacePose pose;
      pose.collapsePhase = shift / static_cast<float>(nx);
      pose.slideV = lapsSlide * static_cast<float>(nz);
      const PlaySurface s = PlaySurface::build(v, pose);
      for (int f = 0; f < nx; ++f) {
        CAPTURE(f);
        const view::Vec3 a = cornerOf(patchAt(s, v, f, 0), 0, 0);
        float best = 1e9f;
        int bestF = -1;
        for (int f2 = 0; f2 < nx; ++f2) {
          const float d = dist(
              a, cornerOf(patchAt(s, v, f2, nz - 1), kSurfaceSubdiv, kSurfaceSubdiv));
          if (d < best) {
            best = d;
            bestF = f2;
          }
        }
        CHECK(bestF == truePartner[f]);
        // Generous: the deliberate kCoverage gap near the pinch's own curvature, not a
        // tight bound - the nearest-neighbour check above is the real assertion.
        CHECK(best < 0.5f);
      }
    }
  }
}

TEST_CASE("the Klein morph never jumps more than a cell", "[render]") {
  // M18 revision, item 3. The three stages must not make a seat disappear and reappear:
  // between adjacent samples of `formed`, no seat moves further than its own neighbour
  // spacing (its `stepU`/`stepV`), which is the seat's own notion of "one square".
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  constexpr int kSamples = 200;
  PlaySurface prev;
  for (int i = 0; i <= kSamples; ++i) {
    SurfacePose pose;
    pose.formed = static_cast<float>(i) / static_cast<float>(kSamples);
    const PlaySurface s = PlaySurface::build(v, pose);
    if (i > 0) {
      REQUIRE(s.seats().size() == prev.seats().size());
      for (std::size_t c = 0; c < s.seats().size(); ++c) {
        const float moved = dist(s.seats()[c].centre, prev.seats()[c].centre);
        const float spacing = std::max(s.seats()[c].stepU, s.seats()[c].stepV);
        CAPTURE(i, c, pose.formed, moved, spacing);
        CHECK(moved < spacing);
      }
    }
    prev = s;
  }
}

TEST_CASE("the morph keeps the camera on the shape as its bounds move", "[render]") {
  // M17.24. `frameBoard` commits the camera's frame once, at the instant SHAPE flips -
  // which is the *flat* end of the morph. The shape's own bounds move as it rolls up, so
  // without re-framing the board drifts off-centre. This drives the interactive loop's
  // own path: `geometryFormed` climbs by `dt * 2` a frame and the camera eases toward the
  // current surface's frame with the same blend the GUI uses.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  auto created = app::Session::create(test::loadVariant("torus"));
  REQUIRE(created.has_value());
  app::Session& session = **created;
  BoardOptions opts;
  opts.surface = true;

  const auto centreOf = [](const view::Bounds& b) {
    return view::Vec3{b.centerX(), b.centerY(), b.centerZ()};
  };
  SurfacePose flat;
  flat.formed = 0.0f;
  SurfacePose one;
  one.formed = 1.0f;
  const view::Vec3 flatCentre = centreOf(PlaySurface::build(v, flat).bounds());
  const view::Vec3 formedCentre = centreOf(PlaySurface::build(v, one).bounds());

  // The toggle instant: the one-shot frame, on the flat end of the morph.
  frameGeometryCamera(session, opts, flat);

  constexpr float kDt = 1.0f / 60.0f;
  float formed = 0.0f;
  float worstTrack = 0.0f;
  float shapeExcursion = 0.0f;
  view::Vec3 prev = session.playerCamera().target;
  for (int i = 0; i < 400 && formed < 1.0f; ++i) {
    formed = std::min(1.0f, formed + kDt * 2.0f);
    SurfacePose pose;
    pose.formed = formed;
    const bool settling = formed >= 1.0f;
    const bool moved =
        easeMorphFraming(session, opts, pose, settling ? 1.0f : morphFollowBlend(kDt));
    REQUIRE(moved);
    const view::Vec3 want = centreOf(PlaySurface::build(v, pose).bounds());
    const view::Vec3 got = session.playerCamera().target;
    INFO("formed = " << formed);
    // The camera's look-at is on the shape's own centre throughout, not stuck at the flat
    // end's frame while the shape rolls away from it (the one-shot design's failure).
    CHECK(dist(got, want) < 0.3f);
    // And it is continuous: no frame teleports the aim.
    CHECK(dist(got, prev) < 0.35f);
    worstTrack = std::max(worstTrack, dist(got, want));
    shapeExcursion = std::max(shapeExcursion, dist(want, flatCentre));
    prev = got;
  }
  INFO("worstTrack = " << worstTrack << " shapeExcursion = " << shapeExcursion);
  CHECK(formed >= 1.0f);
  // The test has teeth: the shape's own centre really does wander, so a camera left on
  // the flat frame would be visibly off by the end of the roll. The torus morph is now
  // two real sequential stages, roll then close (M13.9 revision), not one combined ramp,
  // and the bounds wander less across the sweep as a result (measured ~0.35, was ~0.5) -
  // a real, expected consequence of the new staging, not a tracking regression: the
  // camera's own worst-case miss (`worstTrack`, checked above every step) is still small.
  CHECK(shapeExcursion > 0.3f);
  // The morph completes exactly on the fully-formed shape's frame.
  CHECK(dist(prev, formedCentre) < 1e-4f);

  // The endpoints do not move: re-running the follow at a settled pose is a no-op.
  const view::Vec3 before = session.playerCamera().target;
  CHECK(easeMorphFraming(session, opts, one, 1.0f));
  CHECK(dist(session.playerCamera().target, before) < 1e-5f);
}

TEST_CASE("the morph speed scales the flat/shape roll, and 1.0 is the shipped pace",
          "[render]") {
  // M18.6 revision. The interactive loop eases `geometryFormed` toward its target on a
  // linear `dt * 2` ramp; `Settings::geometryFormSpeed` multiplies that rate. This pins
  // the *relationship* (double the speed, half the frames) rather than one magic number,
  // and that the default 1.0 is exactly the pre-setting ramp, so a player who never
  // touches the slider sees no change at all.
  constexpr float kDt = 0.25f;

  // Frames to cross from `from` to `to` one frame at a time, at a given speed. Clean
  // quarter-second frames make the counts exact; the relationship is what matters, not
  // the wall-clock value of dt.
  const auto framesToForm = [](float speed, float from, float to) {
    float formed = from;
    int frames = 0;
    while (formed != to) {
      formed = advanceFormMorph(formed, to, kDt, speed);
      ++frames;
      REQUIRE(frames < 100000);
    }
    return frames;
  };

  const int base = framesToForm(1.0f, 0.0f, 1.0f);
  CHECK(base == 2);
  CHECK(framesToForm(2.0f, 0.0f, 1.0f) == 1);     // twice the speed, half the frames
  CHECK(framesToForm(0.5f, 0.0f, 1.0f) == 4);     // half the speed, twice the frames
  CHECK(framesToForm(4.0f, 0.0f, 1.0f) == 1);     // the ramp is clamped on the target
  CHECK(framesToForm(1.0f, 1.0f, 0.0f) == base);  // the way back paces the same

  // The per-frame step scales proportionally with the speed. A short frame keeps every
  // step below the target so no clamp is in play, isolating the rate itself.
  constexpr float kStepDt = 0.05f;
  for (float speed : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f}) {
    CAPTURE(speed);
    const float step = advanceFormMorph(0.0f, 1.0f, kStepDt, speed);
    CHECK(step == kStepDt * kFormMorphRate * speed);
  }

  // At 1.0 it is the old inline math (`dt * 2`, snapped on the target) frame for frame.
  constexpr float kRealDt = 1.0f / 60.0f;
  float got = 0.0f;
  float want = 0.0f;
  for (int i = 0; i < 200; ++i) {
    got = advanceFormMorph(got, 1.0f, kRealDt, 1.0f);
    want = std::min(1.0f, want + kRealDt * 2.0f);
    REQUIRE(got == want);
  }

  // Settled stays settled, in either direction.
  CHECK(advanceFormMorph(1.0f, 1.0f, kRealDt, 1.0f) == 1.0f);
  CHECK(advanceFormMorph(0.0f, 0.0f, kRealDt, 1.0f) == 0.0f);
}

#endif  // CB_HAVE_IMGUI

TEST_CASE("swapping layer colours flips every second layer and nothing else",
          "[render][layers]") {
  using cb::Coord;
  const std::uint8_t plane[2] = {0, 1};
  for (int z = 0; z < 4; ++z) {
    const Coord c = Coord::of({2, 3, z});
    const int plain = cb::render::squareParity(c, plane, 2, false);
    const int swapped = cb::render::squareParity(c, plane, 2, true);
    CAPTURE(z);
    CHECK((plain % 2 + 2) % 2 == 1);                  // layer-independent without it
    CHECK(((swapped - plain) % 2 + 2) % 2 == z % 2);  // odd z (2nd, 4th layer) flips
  }
  // A 2-D board is untouched.
  const Coord flat = Coord::of({2, 3});
  CHECK(cb::render::squareParity(flat, plane, 2, true) ==
        cb::render::squareParity(flat, plane, 2, false));
  // An axis the view already draws is not counted twice.
  const std::uint8_t cube[3] = {0, 1, 2};
  const Coord c = Coord::of({1, 1, 1});
  CHECK(cb::render::squareParity(c, cube, 3, true) ==
        cb::render::squareParity(c, cube, 3, false));
}

TEST_CASE(
    "a straight step's boundary waypoint lies between the two squares, not beside them",
    "[render][move]") {
  // Seat centres sit at (file + 0.5, rank + 0.5) in lattice units. The waypoint a glide
  // passes through between two seats must be taken from those same coordinates: sampling
  // from (file, rank) put it half a cell off the line, so a rook went to a corner and
  // then the middle of the edge.
  for (const char* name : {"cylinder", "torus", "klein", "mobius"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const auto seat = [&](int f, int r) -> const SurfaceSeat* {
      for (const SurfaceSeat& st : s.seats()) {
        if (st.file == f && st.rank == r) return &st;
      }
      return nullptr;
    };
    const SurfaceSeat* a = seat(2, 3);
    REQUIRE(a != nullptr);
    for (const auto& d : {std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) {
      const SurfaceSeat* b = seat(2 + d.first, 3 + d.second);
      REQUIRE(b != nullptr);
      cb::view::Vec3 w{};
      REQUIRE(s.nearestBoundary(*a, *b, w));
      // The waypoint is the surface point half-way between the two squares *in lattice
      // coordinates*, the same coordinates the seat centres are sampled at.
      cb::view::Vec3 want{};
      REQUIRE(s.pointAt(
          static_cast<float>(a->file) + 0.5f + 0.5f * static_cast<float>(d.first),
          static_cast<float>(a->rank) + 0.5f + 0.5f * static_cast<float>(d.second),
          want));
      CHECK(cb::view::length(w - want) < 1e-3f);
      cb::view::Vec3 centre{};
      REQUIRE(s.pointAt(static_cast<float>(a->file) + 0.5f,
                        static_cast<float>(a->rank) + 0.5f, centre));
      CHECK(cb::view::length(centre - a->centre) < 1e-3f);
    }
  }
}

TEST_CASE(
    "the 3-D torus's collapsed shells are the plain torus, and spread to the shipped "
    "shells",
    "[render][shells]") {
  // The trailer cuts from the torus Klein folded back into onto this shape. At
  // `shellSpread` 0 it must be that torus - not a ring of roughly its size - or the cut
  // jumps; at 1 it must be the shipped shells to the bit.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus3d_8x8"));
  const float nx = static_cast<float>(v.dims.extent(0));
  const float nz = static_cast<float>(v.dims.extent(1));
  for (int level = 0; level < 4; ++level) {
    for (const auto& cell : {std::pair{0, 0}, std::pair{3, 5}, std::pair{7, 2}}) {
      std::array<float, kMaxDims> axis{};
      axis[0] = static_cast<float>(cell.first) + 0.5f;
      axis[1] = static_cast<float>(cell.second) + 0.5f;
      axis[2] = static_cast<float>(level) + 0.5f;
      SurfacePose flat;
      flat.formed = 1.0f;
      flat.shellSpread = 0.0f;
      OvVec3 got{};
      REQUIRE(playShapeAt(v, axis, got, flat));
      const OvVec3 torus =
          derivedSurfaceAt(app::SurfaceKind::Torus, axis[0] / nx, axis[1] / nz, flat);
      CAPTURE(level, cell.first, cell.second);
      CHECK(std::abs(got.x - torus.x) < 1e-4f);
      CHECK(std::abs(got.y - torus.y) < 1e-4f);
      CHECK(std::abs(got.z - torus.z) < 1e-4f);
    }
  }
  // Collapsed, every level is the same surface; spread apart, they are not.
  std::array<float, kMaxDims> a{};
  a[0] = 2.5f;
  a[1] = 2.5f;
  OvVec3 l0{}, l3{};
  SurfacePose collapsed;
  collapsed.shellSpread = 0.0f;
  a[2] = 0.5f;
  REQUIRE(playShapeAt(v, a, l0, collapsed));
  a[2] = 3.5f;
  REQUIRE(playShapeAt(v, a, l3, collapsed));
  CHECK(std::abs(l0.y - l3.y) < 1e-4f);
  SurfacePose spread;
  spread.shellSpread = 1.0f;
  a[2] = 0.5f;
  REQUIRE(playShapeAt(v, a, l0, spread));
  a[2] = 3.5f;
  REQUIRE(playShapeAt(v, a, l3, spread));
  CHECK(std::abs(l0.y - l3.y) > 1.0f);
}

namespace {
// Where the first seat sits and how wide the shape is, at a given pose.
std::pair<view::Vec3, view::Vec3> firstSeatAndSpan(const char* name, SurfacePose pose) {
  const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
  const PlaySurface s = PlaySurface::build(v, pose);
  const view::Bounds b = s.bounds();
  return {s.seats()[0].centre, {b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ}};
}
float gap(const view::Vec3& a, const view::Vec3& b) {
  return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}
}  // namespace

TEST_CASE(
    "the morph is continuous from the flat board for any thickness, width and slide",
    "[render][morph]") {
  // The trailer's cuts hide behind this: the flat board ignores the player's thickness,
  // width and rank slide, so a shape that applied them on its first rolled frame jumped.
  SurfacePose flat;
  flat.formed = 0.0f;
  flat.thickness = 1.81f;
  flat.openness = 3.0f;
  flat.slideU = -27.0f;
  flat.slideV = 125.5f;
  SurfacePose nudged = flat;
  nudged.formed = 0.004f;
  SurfacePose rolled = flat;
  rolled.formed = view::kTorusRollEnd;
  SurfacePose tubeEnd = flat;
  tubeEnd.formed = 1.0f;
  // The cylinder's flat start does not jump on the first rolled frame (the slide is the
  // trailer's to ease in, `Shell::applySplineAt`, so it is left out here) ...
  {
    SurfacePose a0 = flat;
    SurfacePose b0 = nudged;
    a0.slideU = a0.slideV = b0.slideU = b0.slideV = 0.0f;
    const auto a = firstSeatAndSpan("cylinder", a0);
    const auto b = firstSeatAndSpan("cylinder", b0);
    CHECK(gap(a.second, b.second) < 0.25f);
  }
  // ... and the torus, at its open-tube waypoint, is the cylinder at its own end.
  {
    const auto cyl = firstSeatAndSpan("cylinder", tubeEnd);
    const auto tor = firstSeatAndSpan("torus", rolled);
    CHECK(gap(cyl.first, tor.first) < 1e-3f);
    CHECK(gap(cyl.second, tor.second) < 1e-3f);
  }
  // The Klein ring is the torus at its end.
  {
    SurfacePose torusEnd = flat;
    torusEnd.formed = 1.0f;
    SurfacePose ring = flat;
    ring.formed = view::kKleinRingEnd;
    const auto tor = firstSeatAndSpan("torus", torusEnd);
    const auto kl = firstSeatAndSpan("klein", ring);
    CHECK(gap(tor.second, kl.second) < 1e-3f);
  }
}

TEST_CASE("a long rank slide bends the torus the short way", "[render]") {
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  for (const float formed : {0.55f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f}) {
    CAPTURE(formed);
    SurfacePose a, b;
    a.formed = b.formed = formed;
    a.slideV = 125.521f;
    b.slideV = -2.479f;
    const PlaySurface sa = PlaySurface::build(v, a);
    const PlaySurface sb = PlaySurface::build(v, b);
    REQUIRE(sa.seats().size() == sb.seats().size());
    for (std::size_t i = 0; i < sa.seats().size(); ++i) {
      CHECK(dist(sa.seats()[i].centre, sb.seats()[i].centre) < 1e-3f);
    }
  }
}

TEST_CASE("seam rail weights follow the morph stages", "[render]") {
  using app::SurfaceKind;
  for (const SurfaceKind k :
       {SurfaceKind::Tube, SurfaceKind::Torus, SurfaceKind::Klein, SurfaceKind::Band}) {
    for (const int axis : {0, 1}) {
      CAPTURE(static_cast<int>(k), axis);
      float prev = seamRailWeight(k, axis, 0.0f);
      const bool staged =
          !(axis == 1 && (k == SurfaceKind::Tube || k == SurfaceKind::Band));
      if (staged) CHECK(prev == 0.0f);
      for (int i = 1; i <= 100; ++i) {
        const float w = seamRailWeight(k, axis, static_cast<float>(i) / 100.0f);
        CHECK(w >= prev - 1e-6f);
        prev = w;
      }
      CHECK(prev == 1.0f);
    }
  }
  CHECK(seamRailWeight(SurfaceKind::Torus, 0, view::kTorusRollEnd) == 1.0f);
  CHECK(seamRailWeight(SurfaceKind::Torus, 1, view::kTorusRollEnd) == 0.0f);
  CHECK(seamRailWeight(SurfaceKind::Klein, 0, view::kKleinRingEnd) == 1.0f);
  CHECK(seamRailWeight(SurfaceKind::Klein, 1, view::kKleinRingEnd) == 1.0f);
}

TEST_CASE("the plinth fades out early in the morph and is gone after", "[render]") {
  using app::SurfaceKind;
  CHECK(plinthOpacity(SurfaceKind::Tube, 0.0f) == 1.0f);
  CHECK(plinthOpacity(SurfaceKind::Tube, 0.1f) < 1.0f);
  CHECK(plinthOpacity(SurfaceKind::Tube, 0.1f) > 0.0f);
  CHECK(plinthOpacity(SurfaceKind::Tube, 0.25f) == 0.0f);
  CHECK(plinthOpacity(SurfaceKind::Tube, 1.0f) == 0.0f);
  CHECK(plinthOpacity(SurfaceKind::FlatGrid, 0.0f) == 0.0f);
}

TEST_CASE("a torus3d slide of one lap is the identity", "[render]") {
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus3d_8x8"));
  const float lapU = app::slideLap(v, 0);
  const float lapV = app::slideLap(v, 1);
  CAPTURE(lapU, lapV);
  const auto maxDelta = [&](float u0, float v0, float u1, float v1) {
    SurfacePose a, b;
    a.slideU = u0;
    a.slideV = v0;
    b.slideU = u1;
    b.slideV = v1;
    const PlaySurface sa = PlaySurface::build(v, a);
    const PlaySurface sb = PlaySurface::build(v, b);
    float worst = 0.0f;
    for (std::size_t i = 0; i < sa.seats().size(); ++i) {
      worst = std::max(worst, dist(sa.seats()[i].centre, sb.seats()[i].centre));
    }
    return worst;
  };
  // The slide must actually do something here, or the identity below proves nothing.
  CHECK(maxDelta(0.0f, 0.0f, 3.0f, 0.0f) > 0.1f);
  CHECK(maxDelta(1.5f, 0.0f, 1.5f + lapU, 0.0f) < 1e-3f);
  CHECK(maxDelta(0.0f, 1.5f, 0.0f, 1.5f + lapV) < 1e-3f);
}
