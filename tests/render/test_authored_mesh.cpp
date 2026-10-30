// SPDX-License-Identifier: GPL-3.0-or-later
//
// M7.3.4/M7.3.6: an authored body is swept into a mesh the renderer draws in a slot of
// its own. Pinned without a GPU: determinism, closure, symmetry replication, and that a
// piece with no model keeps its archetype.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <tuple>

#include "render/piece_mesh.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::render;

namespace {

using Key = std::tuple<long, long, long>;

Key keyOf(const MeshVertex& v) {
  // Quantised so the duplicated vertices of a faceted mesh meet.
  const auto q = [](float x) { return std::lround(x * 1e4f); };
  return {q(v.pos[0]), q(v.pos[1]), q(v.pos[2])};
}

/// Every edge of positive length is shared by an even number of triangles - two, or four
/// where a mirror pair meets itself on the axis: the surface has no boundary, so the
/// sweep closed.
bool closed(const MeshLibrary& m, const MeshRange& r) {
  std::map<std::pair<Key, Key>, int> edges;
  for (std::uint32_t i = 0; i < r.indexCount; i += 3) {
    Key k[3];
    for (int j = 0; j < 3; ++j) {
      const auto vi = static_cast<std::size_t>(r.vertexOffset) +
                      m.indices[r.firstIndex + i + static_cast<std::uint32_t>(j)];
      k[j] = keyOf(m.vertices[vi]);
    }
    for (int j = 0; j < 3; ++j) {
      Key a = k[j], b = k[(j + 1) % 3];
      if (a == b) continue;  // a degenerate edge where the profile meets the axis
      if (b < a) std::swap(a, b);
      ++edges[{a, b}];
    }
  }
  for (const auto& [e, n] : edges) {
    if (n % 2 != 0) return false;
  }
  return !edges.empty();
}

}  // namespace

TEST_CASE("an authored body sweeps into a closed, deterministic mesh", "[render]") {
  for (const char* name :
       {"dome", "tower", "spire", "crown", "monolith", "wedge", "horn"}) {
    CAPTURE(name);
    const assets::PieceModel model = assets::archetypeModel(name);
    MeshLibrary a;
    MeshLibrary b;
    const MeshRange ra = a.appendModel(model);
    const MeshRange rb = b.appendModel(model);
    REQUIRE(ra.indexCount > 0);
    CHECK(ra.indexCount == rb.indexCount);
    CHECK(a.vertices.size() == b.vertices.size());
    CHECK(closed(a, ra));
    // It stands on the board and fits the foot it replaces.
    float lo = 1e9f, hi = -1e9f, rmax = 0.0f;
    for (const MeshVertex& v : a.vertices) {
      lo = std::min(lo, v.pos[2]);
      hi = std::max(hi, v.pos[2]);
      rmax = std::max(rmax, std::hypot(v.pos[0], v.pos[1]));
    }
    CHECK(lo == 0.0f);
    CHECK(hi > 0.3f);
    CHECK(rmax < 0.5f);
  }
}

TEST_CASE("the symmetry group places one copy of an element per member", "[render]") {
  assets::PieceModel m = assets::archetypeModel("crown");  // k-fold, one point element
  const auto elementVerts = [&](std::uint8_t k) {
    m.symmetry.k = k;
    MeshLibrary lib;
    (void)lib.appendModel(m);
    assets::PieceModel bare = m;
    bare.elements.clear();
    MeshLibrary plain;
    (void)plain.appendModel(bare);
    return lib.vertices.size() - plain.vertices.size();
  };
  const std::size_t four = elementVerts(4);
  const std::size_t five = elementVerts(5);
  REQUIRE(four > 0);
  CHECK(four % 4 == 0);
  CHECK(five == four / 4 * 5);

  m.symmetry.kind = assets::SymmetryKind::Mirror;
  CHECK(elementVerts(4) == four / 4 * 2);
}

TEST_CASE("an authored piece draws in a slot of its own; the rest keep their archetype",
          "[render]") {
  VariantSpec v = test::loadVariant("standard");
  const PieceTypeId queen = v.findPiece("queen");
  const PieceTypeId rook = v.findPiece("rook");
  const std::vector<std::size_t> before = shapeSlots(v);
  CHECK(authoredKey(v) == 0);
  CHECK(before[rook] == static_cast<std::size_t>(archetypeFor(v.pieces[rook])));

  v.pieces[queen].model = assets::archetypeModel("crown");
  const std::vector<std::size_t> after = shapeSlots(v);
  CHECK(after[queen] == static_cast<std::size_t>(Archetype::Count));
  CHECK(after[rook] == before[rook]);
  CHECK(authoredKey(v) != 0);

  const MeshLibrary lib = MeshLibrary::build(&v);
  CHECK(lib.ranges[after[queen]].indexCount > 0);
  // Its token puck in the shape view is its archetype's.
  const auto fallback = static_cast<std::size_t>(archetypeFor(v.pieces[queen]));
  CHECK(lib.cylinderRanges[after[queen]].firstIndex ==
        lib.cylinderRanges[fallback].firstIndex);

  // A different model is a different key: the GPU library is rebuilt on a change.
  const std::uint64_t k1 = authoredKey(v);
  v.pieces[queen].model->segments = 7;
  CHECK(authoredKey(v) != k1);
}
