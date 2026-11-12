// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "io/lab_doc.hpp"
using namespace cb;
namespace {
const std::string minimal = R"(version = 1
[anchors]
"gantry.centre" = [0, 0.6, 1.05]
[[part]]
name = "bench"
mesh = "box"
pos = [1, 2, 3]
size = [1, 1, 1]
[[station]]
name = "menu"
target = [0, 1, 2]
distance = 2
yaw = 12
pitch = 14
fov = 50
)";
}
TEST_CASE("lab document fields and defaults", "[unit][io][lab]") {
  auto d = io::parseLabDoc(minimal, "lab.toml");
  REQUIRE(d.has_value());
  REQUIRE(d->parts.size() == 1);
  CHECK(d->parts[0].name == "bench");
  CHECK(d->parts[0].mesh == io::LabMesh::Box);
  CHECK(d->parts[0].pos == std::array<float, 3>{1, 2, 3});
  CHECK(d->parts[0].size == std::array<float, 3>{1, 1, 1});
  CHECK(d->parts[0].rot == std::array<float, 3>{0, 0, 0});
  CHECK(d->parts[0].material == io::LabMaterial::Matte);
  CHECK(d->parts[0].group == "room");
  CHECK(d->parts[0].ao == 0);
  CHECK(d->parts[0].color.r == view::Rgba::hex(0x808080).r);
  REQUIRE(d->anchor("gantry.centre").has_value());
  CHECK(d->station("menu")->distance == 2);
  CHECK(d->station("absent") == nullptr);
}
TEST_CASE("lab document preserves authored transforms materials and linear colours",
          "[unit][io][lab]") {
  auto text = minimal;
  text.insert(text.find("[[station]]"),
              "rot = [90, -8, 20]\nmaterial = \"metal\"\ncolor = [0.1, 0.2, 0.3]\n"
              "ao = 0.4\ngroup = \"bench\"\n");
  const auto d = io::parseLabDoc(text, "authored.toml");
  REQUIRE(d.has_value());
  const auto& p = d->parts.front();
  CHECK(p.rot == std::array<float, 3>{90, -8, 20});
  CHECK(p.material == io::LabMaterial::Metal);
  CHECK(p.color.array() == std::array<float, 4>{.1f, .2f, .3f, 1});
  CHECK(p.ao == .4f);
  CHECK(p.group == "bench");
}
TEST_CASE("lab document diagnoses authoring errors", "[unit][io][lab]") {
  for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
           {"version = 1", "version = 2"},
           {"mesh = \"box\"", "mesh = \"sphere\""},
           {"mesh = \"box\"", "mesh = \"file:missing.obj\""},
           {"size = [1, 1, 1]", "size = [1, 0, 1]"},
           {"pos = [1, 2, 3]", "colour = [1, 2, 3]"},
           {"fov = 50", "fov = 5"},
           {"pitch = 14", "pitch = 90"},
           {"distance = 2", "distance = nan"},
           {"size = [1, 1, 1]", "size = [1, 1, 1]\nao = 2"},
           {"pos = [1, 2, 3]", "rot = [0, 0, 0]"},
           {"[[station]]",
            "[[part]]\nname = \"bench\"\nmesh = \"box\"\npos = [0,0,0]\nsize = "
            "[1,1,1]\n[[station]]"}}) {
    auto text = minimal;
    text.replace(text.find(from), from.size(), to);
    CAPTURE(to);
    const auto d = io::parseLabDoc(text, "bad.toml");
    REQUIRE_FALSE(d.has_value());
    CHECK(d.error().line > 0);
    const auto name =
        from == "version = 1"                                                  ? "version"
        : from == "fov = 50" || from == "pitch = 14" || from == "distance = 2" ? "menu"
                                                                               : "bench";
    CHECK(d.error().message.find(name) != std::string::npos);
  }
  auto text = minimal;
  text.replace(text.find("mesh = \"box\""), 12,
               "mesh = \"file:missing.obj\"\nfallback = \"box\"");
  auto fallback = io::parseLabDoc(text, "lab.toml");
  REQUIRE(fallback.has_value());
  CHECK(fallback->parts[0].mesh == io::LabMesh::Box);
}
TEST_CASE("lab colours use the theme's hex convention", "[unit][io][lab]") {
  for (const auto hex : {"#808080", "#FF0000"}) {
    auto d = io::parseLabDoc(minimal + "\n", "lab.toml");
    auto text = minimal;
    text.insert(text.find("[[station]]"), std::string("color = \"") + hex + "\"\n");
    d = io::parseLabDoc(text, "lab.toml");
    REQUIRE(d.has_value());
    CHECK(d->parts[0].color.r ==
          (std::string(hex) == "#FF0000" ? 1.0f : view::Rgba::hex(0x808080).r));
  }
}
TEST_CASE("the shipped lab has all stations and anchors", "[unit][io][lab]") {
  const auto d =
      io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(d.has_value());
  for (auto n : {"menu", "library", "watch", "tutorial", "settings", "quit"})
    CHECK(d->station(n));
  for (auto n : {"gantry.centre", "projector.lens", "magazine.front", "door.centre"})
    CHECK(d->anchor(n).has_value());
}

TEST_CASE("lab file parts resolve beside the document and diagnose broken OBJ",
          "[unit][io][lab]") {
  const auto dir = std::filesystem::path(CB_BINARY_DIR) / "lab-io-fixture";
  std::filesystem::create_directories(dir / "models");
  const auto file = dir / "models" / "part.obj";
  {
    std::ofstream out(file);
    out << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }
  auto text = minimal;
  text.replace(text.find("mesh = \"box\""), 12, "mesh = \"file:models/part.obj\"");
  const auto d = io::parseLabDoc(text, (dir / "lab.toml").string());
  REQUIRE(d.has_value());
  CHECK(d->parts[0].mesh == io::LabMesh::File);
  CHECK(d->parts[0].file == std::filesystem::absolute(file));
  {
    std::ofstream out(file);
    out << "# broken\nunsupported 0\n";
  }
  const auto bad = io::parseLabDoc(text, (dir / "lab.toml").string());
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error().message.find("bench") != std::string::npos);
  CHECK(bad.error().message.find("part.obj") != std::string::npos);
  CHECK(bad.error().message.find("line 2") != std::string::npos);
  std::filesystem::remove_all(dir);
}
