// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "io/obj_mesh.hpp"
using namespace cb;
TEST_CASE("OBJ quads normals and relative indices", "[unit][io][lab]") {
  const std::string vertices = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";
  for (auto face : {"f 1//1 2//1 3//1 4//1", "f 1/8/1 2/9/1 3/2/1 4/1/1",
                    "f -4//-1 -3//-1 -2//-1 -1//-1", "f 1 2 3 4"}) {
    auto m = io::parseObj(vertices + "vn 0 0 2\n" + face, "quad");
    REQUIRE(m.has_value());
    CHECK(m->indices.size() == 6);
    CHECK(m->positions.size() == 4);
    for (const auto& n : m->normals) CHECK(n[2] == Catch::Approx(1));
  }
}
TEST_CASE("OBJ errors carry source and line", "[unit][io][lab]") {
  auto m = io::parseObj("# comment\nwat 1 2 3", "broken");
  REQUIRE_FALSE(m.has_value());
  CHECK(m.error().line == 2);
  CHECK(m.error().message.find("broken") != std::string::npos);
  for (auto text : {"v nan 0 0", "f 0 1 2", "v 0 0 0\nf 1 1 1", "vn 0 0 0"})
    CHECK_FALSE(io::parseObj(text, "bad").has_value());
}
TEST_CASE("OBJ splits hard edges and deduplicates identical position normal pairs",
          "[unit][io][lab]") {
  const std::string vertices = "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n";
  auto m = io::parseObj(vertices + "f 1 2 3\nf 1 4 2\nf 1 2 3\n", "edges");
  REQUIRE(m.has_value());
  CHECK(m->positions.size() == 6);
  CHECK(m->indices.size() == 9);
  CHECK(m->indices[0] == m->indices[6]);
  CHECK(m->normals[m->indices[0]] == std::array<float, 3>{0, 0, 1});
  CHECK(m->normals[m->indices[3]] == std::array<float, 3>{0, 1, 0});
  // Any missing corner normal makes the whole face flat, regardless of vn data.
  m = io::parseObj(vertices + "vn 1 0 0\nf 1//1 2 3//1\n", "mixed");
  REQUIRE(m.has_value());
  for (const auto& n : m->normals) CHECK(n == std::array<float, 3>{0, 0, 1});
}
TEST_CASE("OBJ output vertex budget asks for decimation", "[unit][io][lab]") {
  std::string text;
  for (int i = 0; i < 70000; ++i) text += "v " + std::to_string(i) + " 0 0\n";
  text += "vn 0 0 1\n";
  for (int i = 1; i + 2 <= 70000; i += 3)
    text += "f " + std::to_string(i) + "//1 " + std::to_string(i + 1) + "//1 " +
            std::to_string(i + 2) + "//1\n";
  auto m = io::parseObj(text, "large");
  REQUIRE_FALSE(m.has_value());
  CHECK(m.error().message.find("decimate") != std::string::npos);
}
