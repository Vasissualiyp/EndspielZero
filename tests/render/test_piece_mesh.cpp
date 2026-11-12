// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_VULKAN
#include <cmath>
#include "render/piece_mesh.hpp"
using namespace cb::render;
TEST_CASE("the lab kit has bounded unit geometry and unit normals",
          "[unit][render][lab]") {
  const auto m = MeshLibrary::build();
  for (std::size_t s = 0; s < static_cast<std::size_t>(LabShape::Count); ++s) {
    const auto& r = m.labRanges[s];
    REQUIRE(r.indexCount > 0);
    CHECK(r.indexCount % 3 == 0);
    REQUIRE(r.firstIndex + r.indexCount <= m.indices.size());
    for (std::uint32_t i = 0; i < r.indexCount; ++i) {
      const auto at =
          static_cast<std::size_t>(r.vertexOffset) + m.indices[r.firstIndex + i];
      REQUIRE(at < m.vertices.size());
      const auto& v = m.vertices[at];
      CHECK(std::abs(std::hypot(v.normal[0], v.normal[1], v.normal[2]) - 1) < 1e-5f);
      for (auto x : v.pos) CHECK(std::abs(x) <= .500001f);
      CHECK(v.height >= 0);
      CHECK(v.height <= 1);
      CHECK(v.panelPrev[3] == -1);
      if (s == static_cast<std::size_t>(LabShape::Cylinder) &&
          std::abs(v.normal[2]) < .1f)
        CHECK(std::abs(std::hypot(v.pos[0], v.pos[1]) - .5f) < 1e-5f);
    }
  }
  CHECK(m.labRanges[0].indexCount == 36);
}
#endif
