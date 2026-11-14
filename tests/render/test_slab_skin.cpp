// SPDX-License-Identifier: GPL-3.0-or-later
//
// The slab skin (M24.5b), as geometry and explicit time. No GPU: the stretch is a ratio
// of surface lengths, a rib is a polyline behind the seats, a latch is a step function of
// its age, and the Klein light band is the set of cells whose patches would pass through
// another part of the sheet.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#ifdef CB_HAVE_IMGUI

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "app/fx_timeline.hpp"
#include "render/play_surface.hpp"
#include "render/slab_skin.hpp"
#include "support/variants.hpp"
#include "view/morph.hpp"

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

PlaySurface at(const std::string& name, float formed, bool skin = false) {
  SurfacePose pose;
  pose.formed = formed;
  pose.skin = skin;
  return PlaySurface::build(variant(name.c_str()), pose);
}

std::vector<CellStretch> stretchAt(const std::string& name, float formed) {
  return cellStretch(at(name, formed), at(name, 0.0f));
}

const SurfaceSeat& nearestSeat(const PlaySurface& s, const view::Vec3& p) {
  const SurfaceSeat* best = &s.seats().front();
  float bestD = 1e30f;
  for (const SurfaceSeat& seat : s.seats()) {
    const float d = view::length(p - seat.centre);
    if (d < bestD) {
      bestD = d;
      best = &seat;
    }
  }
  return *best;
}

}  // namespace

TEST_CASE("a flat board is unstretched", "[render][skin]") {
  for (const char* name : {"cylinder", "torus", "mobius", "klein"}) {
    const auto s = stretchAt(name, 0.0f);
    REQUIRE(s.size() == 64);
    for (const CellStretch& c : s) {
      CHECK_THAT(c.u, WithinAbs(1.0, 1e-4));
      CHECK_THAT(c.v, WithinAbs(1.0, 1e-4));
    }
  }
}

TEST_CASE("a formed cylinder is developable: the rolled axis keeps its length",
          "[render][skin]") {
  const auto s = stretchAt("cylinder", 1.0f);
  REQUIRE_FALSE(s.empty());
  for (const CellStretch& c : s) {
    CHECK_THAT(c.u, WithinAbs(1.0, 0.02));
    CHECK_THAT(c.v, WithinAbs(1.0, 0.02));
  }
}

TEST_CASE("a formed torus stretches its outer equator far more than its inner one",
          "[render][skin]") {
  // The shipped torus's ring is about 2.2 times the flat board's length, so even its
  // inner equator is stretched (about 1.27): the spec's "< 0.9 on the inner" premise does
  // not hold for the shipped shape, whose maths this slice must not change. What holds is
  // the gradient: the inner equator is the least stretched, under half the outer.
  const auto s = stretchAt("torus", 1.0f);
  float hi = 0.0f, lo = 1e9f;
  for (const CellStretch& c : s) {
    hi = std::max(hi, c.v);
    lo = std::min(lo, c.v);
  }
  CHECK(hi > 1.15f);
  CHECK(lo < 0.5f * hi);
  // Compression is real elsewhere: the Moebius ribbon trades its ranks for its files.
  float band = 1e9f;
  for (const CellStretch& c : stretchAt("mobius", 1.0f)) band = std::min(band, c.v);
  CHECK(band < 0.9f);
}

TEST_CASE("the stretch is continuous in formed", "[render][skin]") {
  // The Klein bottle's ring stage lengthens its ranks about 0.1 per 0.01 of formed (the
  // ring forms in the first 0.4 of the morph), so its bound per 0.01 is 0.11; a step ten
  // times finer moves it ten times less, which is the continuity that matters.
  const auto worstOver = [](const char* name, float step, int steps) {
    std::vector<CellStretch> prev = stretchAt(name, 0.0f);
    float worst = 0.0f;
    for (int k = 1; k <= steps; ++k) {
      const auto now = stretchAt(name, static_cast<float>(k) * step);
      REQUIRE(now.size() == prev.size());
      for (std::size_t i = 0; i < now.size(); ++i) {
        worst = std::max(worst, std::abs(now[i].u - prev[i].u));
        worst = std::max(worst, std::abs(now[i].v - prev[i].v));
      }
      prev = now;
    }
    return worst;
  };
  for (const char* name : {"cylinder", "torus", "mobius"}) {
    INFO(name);
    CHECK(worstOver(name, 0.01f, 100) < 0.05f);
  }
  CHECK(worstOver("klein", 0.01f, 100) < 0.11f);
  CHECK(worstOver("klein", 0.001f, 120) < 0.011f);
}

TEST_CASE("skin tiles keep their flat size; the membrane is the unshrunk sheet behind",
          "[render][skin]") {
  const PlaySurface plain = at("torus", 1.0f);
  const PlaySurface skin = at("torus", 1.0f, true);
  // The skin is opt-in: without it nothing is added and the tiles are as before.
  CHECK(plain.membranePatches().empty());
  CHECK(plain.stretch().empty());
  REQUIRE(skin.patches().size() == plain.patches().size());
  REQUIRE(skin.stretch().size() == skin.seats().size());
  REQUIRE(skin.membranePatches().size() == skin.patches().size());
  constexpr int kSide = kSurfaceSubdiv + 1;
  for (std::size_t c = 0; c < skin.patches().size(); ++c) {
    const auto& tile = skin.patches()[c];
    const auto& old = plain.patches()[c];
    const auto& st = skin.stretch()[c];
    REQUIRE(st.cell == tile.cell);
    // Span of the tile's middle row along the ranks: a stretched cell's tile is no
    // larger than the old (coverage-only) tile, and shrinks by 1/stretch.
    const auto span = [&](const SurfacePatch& p) {
      float len = 0.0f;
      for (int j = 0; j + 1 < kSide; ++j)
        len += view::length(p.pos[static_cast<std::size_t>(2 * kSide + j + 1)] -
                            p.pos[static_cast<std::size_t>(2 * kSide + j)]);
      return len;
    };
    if (st.v > 1.0f) CHECK(span(tile) < span(old) / st.v * 1.03f);
    if (st.v <= 1.0f) CHECK_THAT(span(tile), WithinAbs(span(old), 1e-4));
    // The membrane is the cell's whole footprint (no slit to the next cell), pushed
    // back along its normals.
    const auto& m = skin.membranePatches()[c];
    REQUIRE(m.cell == tile.cell);
    const SurfaceSeat& seat = skin.seats()[c];
    for (int i = 0; i < kSide; ++i) {
      for (int j = 0; j < kSide; ++j) {
        view::Vec3 on{};
        REQUIRE(skin.pointAt(static_cast<float>(seat.file) + static_cast<float>(i) / 4.0f,
                             static_cast<float>(seat.rank) + static_cast<float>(j) / 4.0f,
                             on));
        const std::size_t k = static_cast<std::size_t>(i * kSide + j);
        CHECK_THAT(view::dot(m.pos[k] - on, m.normal[k]),
                   WithinAbs(-kMembraneOffset, 1e-3));
        CHECK(view::length(m.pos[k] - on) < kMembraneOffset + 1e-3f);
      }
    }
  }
}

TEST_CASE("with the skin on, a click in a gap picks the cell it belongs to",
          "[render][skin]") {
  const PlaySurface skin = at("torus", 1.0f, true);
  // The most stretched cell along the ranks: its tile leaves a wide gap at its edge.
  const auto& st = skin.stretch();
  const auto it = std::max_element(
      st.begin(), st.end(), [](const auto& a, const auto& b) { return a.v < b.v; });
  REQUIRE(it->v > 1.15f);
  const SurfaceSeat& seat = skin.seats()[static_cast<std::size_t>(it - st.begin())];
  // 0.42 of a cell from the centre: inside the cell, outside its shrunk tile.
  view::Vec3 gap{};
  REQUIRE(skin.pointAt(static_cast<float>(seat.file) + 0.5f,
                       static_cast<float>(seat.rank) + 0.92f, gap));
  view::OrbitCamera cam;
  cam.target = gap;
  cam.distance = 6.0f;
  const view::Vec3 back = seat.normal;
  cam.pitch = std::asin(std::clamp(back.z, -1.0f, 1.0f));
  cam.yaw = std::atan2(back.x, -back.y);
  CHECK(skin.pick(cam, 400.0f, 400.0f, 200.0f, 200.0f) == seat.cell);
}

TEST_CASE("ribs: none on the flat board, twelve plus two spokes on a torus",
          "[render][skin]") {
  const view::Vec3 yoke{0.0f, 6.0f, -3.0f};
  CHECK(slabRibs(at("torus", 0.0f), kRibCount, yoke, 0.0f).empty());
  const PlaySurface torus = at("torus", 1.0f);
  const auto ribs = slabRibs(torus, kRibCount, yoke, 1.0f);
  REQUIRE(ribs.size() == 14);
  CHECK(ribs[12].points.size() == 2);
  CHECK(ribs[13].points.size() == 2);
  CHECK_THAT(view::length(ribs[12].points.front() - yoke), WithinAbs(0.0, 1e-5));
  // A cylinder rolls one axis: six ribs, two spokes.
  CHECK(slabRibs(at("cylinder", 1.0f), kRibCount, yoke, 1.0f).size() == 8);
  CHECK(slabRibs(at("mobius", 1.0f), kRibCount, yoke, 1.0f).size() == 8);
}

TEST_CASE("ribs sit behind the skin, never on the playing side", "[render][skin]") {
  const view::Vec3 yoke{0.0f, 6.0f, -3.0f};
  for (const char* name : {"cylinder", "torus", "mobius"}) {
    INFO(name);
    const PlaySurface s = at(name, 1.0f);
    const auto ribs = slabRibs(s, kRibCount, yoke, 1.0f);
    REQUIRE(ribs.size() >= 2);
    for (std::size_t r = 0; r + 2 < ribs.size(); ++r) {
      for (const view::Vec3& p : ribs[r].points) {
        const SurfaceSeat& seat = nearestSeat(s, p);
        const float behind = view::dot(p - seat.centre, seat.normal);
        CHECK(behind < 0.0f);
        CHECK_THAT(behind, WithinAbs(-kRibBehindCells, 0.05));
      }
    }
  }
}

TEST_CASE("ribs grow from their middle as the shape forms", "[render][skin]") {
  const view::Vec3 yoke{0.0f, 6.0f, -3.0f};
  const auto length = [](const Rib& r) {
    float l = 0.0f;
    for (std::size_t i = 1; i < r.points.size(); ++i)
      l += view::length(r.points[i] - r.points[i - 1]);
    return l;
  };
  const auto early = slabRibs(at("cylinder", 0.1f), kRibCount, yoke, 0.1f);
  const auto late = slabRibs(at("cylinder", 0.5f), kRibCount, yoke, 0.5f);
  REQUIRE(early.size() == late.size());
  CHECK(length(early[0]) < length(late[0]));
}

TEST_CASE("each wrap axis closes at its own stage boundary", "[render][skin]") {
  const auto isNan = [](float f) { return std::isnan(f); };
  const auto cyl = wrapCloseAt(variant("cylinder"));
  CHECK(cyl[0] == 1.0f);
  CHECK(isNan(cyl[1]));
  const auto torus = wrapCloseAt(variant("torus"));
  CHECK(torus[0] == view::kTorusRollEnd);
  CHECK(torus[1] == 1.0f);
  const auto klein = wrapCloseAt(variant("klein"));
  CHECK(klein[0] == view::kKleinRingEnd);
  CHECK(klein[1] == view::kKleinRingEnd);
  const auto band = wrapCloseAt(variant("mobius"));
  CHECK(band[0] == 1.0f);
  CHECK(isNan(band[1]));
  const auto flat = wrapCloseAt(variant("standard"));
  CHECK(isNan(flat[0]));
  CHECK(isNan(flat[1]));
}

TEST_CASE("the latch flash is three hard steps, and none at calm", "[render][skin]") {
  CHECK(latchBoost(0.03f, 1.0f) == 2.5f);
  CHECK(latchBoost(0.1f, 1.0f) == 1.6f);
  CHECK(latchBoost(0.2f, 1.0f) == 1.2f);
  CHECK(latchBoost(0.3f, 1.0f) == 1.0f);
  CHECK(latchBoost(0.03f, 0.3f) == 1.0f);
  CHECK(latchBoost(-0.01f, 1.0f) == 1.0f);
}

TEST_CASE("a closing axis latches exactly once, and opening never does",
          "[render][skin]") {
  const float close = view::kTorusRollEnd;
  int fired = 0;
  float prev = 0.0f;
  for (int k = 1; k <= 120; ++k) {
    const float f = std::min(1.0f, static_cast<float>(k) * 0.013f);
    if (latchCrossed(prev, f, close)) ++fired;
    prev = f;
  }
  CHECK(fired == 1);
  CHECK_FALSE(latchCrossed(1.0f, 0.2f, close));
  CHECK_FALSE(latchCrossed(close, close, close));
  CHECK_FALSE(latchCrossed(0.1f, 0.9f, std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("the fx timeline holds the last latch and its age", "[render][skin]") {
  app::FxTimeline fx;
  CHECK_FALSE(fx.latchAge(1.0).has_value());
  fx.latch(1, 2.0);
  const auto a = fx.latchAge(2.1);
  REQUIRE(a.has_value());
  CHECK(a->axis == 1);
  CHECK_THAT(a->age, WithinAbs(0.1, 1e-5));
  CHECK_FALSE(fx.latchAge(3.0).has_value());  // long over
  CHECK_FALSE(fx.latchAge(1.9).has_value());  // not yet
}

TEST_CASE("only the Klein bottle passes through itself", "[render][skin]") {
  for (const char* name : {"cylinder", "torus", "mobius"}) {
    for (int k = 0; k <= 10; ++k) {
      const float f = static_cast<float>(k) * 0.1f;
      INFO(name << " formed " << f);
      CHECK(passThroughCells(at(name, f), kPassThroughGap).empty());
    }
  }
  CHECK(passThroughCells(at("klein", 0.0f), kPassThroughGap).empty());
}

TEST_CASE("the Klein light band is a sorted, connected band", "[render][skin]") {
  const VariantSpec& v = variant("klein");
  const PlaySurface s = at("klein", 1.0f);
  const auto band = passThroughCells(s, kPassThroughGap);
  REQUIRE_FALSE(band.empty());
  CHECK(std::is_sorted(band.begin(), band.end()));
  CHECK(std::adjacent_find(band.begin(), band.end()) == band.end());
  const std::set<CellId> in(band.begin(), band.end());
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  for (const CellId c : band) {
    const Coord co = v.dims.toCoord(c);
    bool neighbour = false;
    for (int df = -1; df <= 1; ++df) {
      for (int dr = -1; dr <= 1; ++dr) {
        if (df == 0 && dr == 0) continue;
        int f = co.c[0] + df;
        int r = co.c[1] + dr;
        if (r < 0 || r >= nz) {  // across the rank seam, the file flips
          r = (r + nz) % nz;
          f = nx - 1 - f;
        }
        f = (f + nx) % nx;
        if (in.contains(v.dims.toCell(Coord::of({f, r})))) neighbour = true;
      }
    }
    CHECK(neighbour);
  }
  // The skin build marks the same cells, and leaves them without membrane.
  const PlaySurface skin = at("klein", 1.0f, true);
  CHECK(skin.passThrough() == band);
  CHECK(skin.membranePatches().size() == skin.patches().size() - band.size());
  // The figure-eight's lobes are thinner than the rib's offset: no rib point, and no
  // segment's midpoint, ends up on the playing side of the sheet.
  {
    const auto all = slabRibs(skin, kRibCount, view::Vec3{0.0f, 6.0f, -3.0f}, 1.0f);
    REQUIRE(all.size() > 2);
    for (std::size_t r = 0; r + 2 < all.size(); ++r) {
      const auto& pts = all[r].points;
      for (std::size_t i = 0; i < pts.size(); ++i) {
        const SurfaceSeat& seat = nearestSeat(skin, pts[i]);
        CHECK(view::dot(pts[i] - seat.centre, seat.normal) < 0.0f);
        if (i == 0) continue;
        const view::Vec3 mid = (pts[i] + pts[i - 1]) * 0.5f;
        const SurfaceSeat& m = nearestSeat(skin, mid);
        CHECK(view::dot(mid - m.centre, m.normal) < 0.0f);
      }
    }
  }
  // No rib point lands on a band cell.
  const auto ribs =
      slabRibs(skin, kRibCount, view::Vec3{0.0f, 6.0f, -3.0f}, 1.0f, &skin.passThrough());
  for (std::size_t r = 0; r + 2 < ribs.size(); ++r)
    for (const view::Vec3& p : ribs[r].points)
      CHECK_FALSE(in.contains(nearestSeat(skin, p).cell));
}

#endif
