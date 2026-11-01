// SPDX-License-Identifier: GPL-3.0-or-later
//
// Board boot (M23.10 section 3): the pure model of how a board powers on, rank by rank,
// and the cell-order and graph-distance helpers it and the later effects share.
#include <algorithm>
#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "io/variant_toml.hpp"
#include "variant/standard.hpp"
#include "view/board_boot.hpp"
#include "view/cell_distances.hpp"

using namespace cb;
using namespace cb::view;
using Catch::Approx;

namespace {
VariantSpec loadNamed(const char* name) {
  auto v = loadVariantFile(std::string(CB_SOURCE_DIR) + "/variants/" + name + ".toml");
  REQUIRE(v.has_value());
  return std::move(*v);
}
}  // namespace

TEST_CASE("boot: cells power on in order of their distance from the near edge",
          "[unit][view][boot]") {
  float last = -1;
  for (float d = 0; d <= 1.0f; d += 0.05f) {
    const float on = kBootCellOnStart + kBootCellOnSpan * d;
    CHECK(bootState(on - 0.01f, d, 1.0f).onAge < 0);
    CHECK(bootState(on + 0.01f, d, 1.0f).onAge > 0);
    CHECK(on > last);
    last = on;
  }
  // Before its time a cell is dark, at 8 % of its colour.
  CHECK(bootState(0.0f, 1.0f, 1.0f).power == Approx(kBootDarkLevel));
  // A near cell is on while a far one is still dark.
  CHECK(bootState(0.3f, 0.0f, 1.0f).onAge > 0);
  CHECK(bootState(0.3f, 1.0f, 1.0f).onAge < 0);
}

TEST_CASE("boot: a settled board is fully lit and revealed at any intensity",
          "[unit][view][boot]") {
  for (float intensity : {0.0f, 0.3f, 1.0f}) {
    for (float d : {0.0f, 0.37f, 1.0f}) {
      for (float age : {kBootSeconds, kBootSeconds + 5.0f}) {
        const BootState s = bootState(age, d, intensity, 17u);
        CHECK(s.power == Approx(1.0f));
        CHECK(s.flicker == Approx(1.0f));
        CHECK(s.reveal >= 1.0f);
        CHECK(s.heat == Approx(0.0f));
        CHECK(s.bezel == Approx(1.0f));
        CHECK(s.backdrop == Approx(1.0f));
        CHECK(s.onAge < 0);  // no transition left running
      }
    }
  }
}

TEST_CASE("boot: pieces are hidden, then revealed bottom to top, in rank order",
          "[unit][view][boot]") {
  CHECK(bootState(0.5f, 0.0f, 1.0f).reveal == Approx(0.0f));
  const float start = kBootPieceStart + kBootPieceSpan * 0.5f;
  CHECK(bootState(start - 0.01f, 0.5f, 1.0f).reveal == Approx(0.0f));
  const float mid = bootState(start + 0.5f * kBootRevealSeconds, 0.5f, 1.0f).reveal;
  CHECK(mid > 0.1f);
  CHECK(mid < 0.9f);
  CHECK(bootState(start + kBootRevealSeconds + 0.01f, 0.5f, 1.0f).reveal >= 1.0f);
  // The footprint heats up before the reveal begins.
  CHECK(bootState(start - 0.05f, 0.5f, 1.0f).heat > 0.0f);
  // Rank order: a nearer piece is at least as far along as a farther one.
  for (float t = 0.5f; t < kBootSeconds; t += 0.05f)
    CHECK(bootState(t, 0.2f, 1.0f).reveal >= bootState(t, 0.8f, 1.0f).reveal);
}

TEST_CASE("boot: calm has no overshoot, flicker, sweep or transition",
          "[unit][view][boot]") {
  for (float t = 0; t <= kBootSeconds; t += 0.02f) {
    for (float d : {0.0f, 0.5f, 1.0f}) {
      const BootState s = bootState(t, d, 0.0f, 99u);
      CHECK(s.power <= 1.0f);
      CHECK(s.flicker == Approx(1.0f));
      CHECK(s.onAge < 0);
      // Cells and pieces come up together, whatever the rank.
      CHECK(s.reveal == Approx(bootState(t, 0.0f, 0.0f, 99u).reveal));
    }
  }
  CHECK(bootState(kBootCalmSeconds, 0.5f, 0.0f).power == Approx(1.0f));
  CHECK(bootState(kBootCalmSeconds, 0.5f, 0.0f).reveal >= 1.0f);
}

TEST_CASE("boot: the full sequence overshoots and flickers", "[unit][view][boot]") {
  bool overshoot = false, dipped = false;
  const float on = kBootCellOnStart + kBootCellOnSpan * 0.5f;
  for (float a = 0; a < 0.4f; a += 0.002f) {
    const BootState s = bootState(on + a, 0.5f, 1.0f, 5u);
    overshoot = overshoot || s.power > 1.3f;
    dipped = dipped || s.flicker < 0.6f;
    CHECK(s.flicker >= kBootFlickerFloor - 1e-4f);
    CHECK(s.power <= kBootOvershoot + 1e-4f);
  }
  CHECK(overshoot);
  CHECK(dipped);
}

TEST_CASE("boot: pure in its arguments", "[unit][view][boot]") {
  const BootState a = bootState(0.77f, 0.4f, 1.0f, 12u);
  const BootState b = bootState(0.77f, 0.4f, 1.0f, 12u);
  CHECK(a.power == b.power);
  CHECK(a.flicker == b.flicker);
  CHECK(a.reveal == b.reveal);
}

TEST_CASE("bootCellOrder: standard is rank order, torus goes through the wrap, a box "
          "is layer then rank",
          "[unit][view][boot]") {
  {
    const auto v = makeStandardChess();
    REQUIRE(v.has_value());
    const auto d = bootCellOrder(*v);
    REQUIRE(d.size() == 64);
    for (int rank = 0; rank < 8; ++rank)
      for (int file = 0; file < 8; ++file) {
        Coord c(2);
        c.c[0] = static_cast<std::int16_t>(file);
        c.c[1] = static_cast<std::int16_t>(rank);
        CHECK(d[v->dims.toCell(c)] == Approx(static_cast<float>(rank) / 7.0f));
      }
  }
  {
    const VariantSpec v = loadNamed("torus");
    const auto d = bootCellOrder(v);
    REQUIRE(d.size() == v.dims.cellCount());
    float lo = 2, hi = -1;
    for (float x : d) {
      CHECK(x >= 0.0f);
      CHECK(x <= 1.0f);
      lo = std::min(lo, x);
      hi = std::max(hi, x);
    }
    CHECK(lo == Approx(0.0f));
    CHECK(hi == Approx(1.0f));
    // The first rank is the source.
    for (int file = 0; file < v.dims.extent(0); ++file) {
      Coord c(v.dims.dims());
      c.c[0] = static_cast<std::int16_t>(file);
      CHECK(d[v.dims.toCell(c)] == Approx(0.0f));
    }
    // The last rank wraps next to the first, so it is not the farthest row.
    Coord last(v.dims.dims());
    last.c[1] = static_cast<std::int16_t>(v.dims.extent(1) - 1);
    CHECK(d[v.dims.toCell(last)] < 0.5f);
  }
  {
    const VariantSpec v = loadNamed("cube5");
    const auto d = bootCellOrder(v);
    REQUIRE(d.size() == v.dims.cellCount());
    // Layer dominates rank: the top rank of layer 0 is before the bottom of layer 1.
    Coord a(v.dims.dims()), b(v.dims.dims());
    a.c[1] = static_cast<std::int16_t>(v.dims.extent(1) - 1);
    b.c[2] = 1;
    CHECK(d[v.dims.toCell(a)] < d[v.dims.toCell(b)]);
    CHECK(*std::max_element(d.begin(), d.end()) == Approx(1.0f));
  }
}

TEST_CASE("cellDistances: Chebyshev on a plain board, wrapped on a torus, twisted on "
          "a Klein bottle",
          "[unit][view][boot]") {
  const auto cell = [](const VariantSpec& v, int x, int y) {
    Coord c(v.dims.dims());
    c.c[0] = static_cast<std::int16_t>(x);
    c.c[1] = static_cast<std::int16_t>(y);
    return v.dims.toCell(c);
  };
  {
    const auto v = makeStandardChess();
    const CellId src[] = {cell(*v, 0, 0)};
    const auto d = cellDistances(*v, src);
    CHECK(d[cell(*v, 0, 0)] == 0);
    CHECK(d[cell(*v, 7, 0)] == 7);
    CHECK(d[cell(*v, 7, 7)] == 7);
    CHECK(d[cell(*v, 3, 5)] == 5);
  }
  {
    const VariantSpec v = loadNamed("torus");
    const CellId src[] = {cell(v, 0, 0)};
    const auto d = cellDistances(v, src);
    CHECK(d[cell(v, 7, 0)] == 1);  // a1 to h1 wraps
    CHECK(d[cell(v, 0, 7)] == 1);
    CHECK(d[cell(v, 4, 4)] == 4);
  }
  {
    const VariantSpec v = loadNamed("klein");
    const CellId src[] = {cell(v, 0, 0)};
    const auto d = cellDistances(v, src);
    // The twist glues the rank seam with a flipped file: a1's rank-neighbour on the far
    // side is h8-to-a1 mirrored, so the cell diagonally below a1 across the seam is h1.
    CHECK(d[cell(v, 7, 7)] == 1);
    for (int x = 0; x < 8; ++x)
      for (int y = 0; y < 8; ++y) CHECK(d[cell(v, x, y)] >= 0);
  }
}
