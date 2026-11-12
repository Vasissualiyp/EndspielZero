// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
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

TEST_CASE("the shipped lab uses the authored OBJ assemblies at their base origins",
          "[unit][io][lab]") {
  const auto d =
      io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(d.has_value());
  const auto part = [&](std::string_view name) -> const io::LabPart& {
    const auto it = std::find_if(d->parts.begin(), d->parts.end(),
                                 [&](const auto& p) { return p.name == name; });
    REQUIRE(it != d->parts.end());
    return *it;
  };
  struct Model {
    const char* name;
    const char* file;
    io::LabMaterial material;
  };
  for (const auto& model :
       {Model{"projector.body", "projector_body.obj", io::LabMaterial::Matte},
        Model{"projector.lens", "projector_lens.obj", io::LabMaterial::Glass},
        Model{"projector.leds", "projector_leds.obj", io::LabMaterial::Glow},
        Model{"lamp.body", "lamp.obj", io::LabMaterial::Metal},
        Model{"lamp.head.emitter", "lamp_head_emitter.obj", io::LabMaterial::Glow},
        Model{"magazine.frame", "magazine.obj", io::LabMaterial::Metal},
        Model{"magazine.bayleds", "magazine_leds.obj", io::LabMaterial::Glow},
        Model{"gantry.carriage.0", "gantry_carriage.obj", io::LabMaterial::Metal},
        Model{"door.panel", "door_roller.obj", io::LabMaterial::Metal},
        Model{"screen.old", "bench_monitor.obj", io::LabMaterial::Matte},
        Model{"tools.mug", "clutter_mug.obj", io::LabMaterial::Matte},
        Model{"tools.toolbox", "clutter_toolbox.obj", io::LabMaterial::Matte},
        Model{"tools.spool", "clutter_spool.obj", io::LabMaterial::Matte},
        Model{"tools.multimeter", "clutter_multimeter.obj", io::LabMaterial::Matte}}) {
    CAPTURE(model.name);
    const auto& p = part(model.name);
    CHECK(p.mesh == io::LabMesh::File);
    CHECK(p.file.filename() == model.file);
    CHECK(p.material == model.material);
    CHECK(p.size == std::array<float, 3>{1, 1, 1});
    CHECK(p.rot == std::array<float, 3>{0, 0, 0});
  }
  const auto& body = part("projector.body");
  CHECK(part("projector.lens").pos == body.pos);
  CHECK(part("projector.leds").pos == body.pos);
  REQUIRE(d->anchor("projector.lens").has_value());
  const auto lens = *d->anchor("projector.lens");
  CHECK(lens == std::array<float, 3>{-.6f, 2.62f, 1.015f});
  CHECK(body.pos[2] + .075f == Catch::Approx(lens[2]));
  const auto& pad = part("projector.base");
  CHECK(pad.pos[2] + pad.size[2] / 2 == Catch::Approx(body.pos[2]));
  const auto& bench = part("bench.top");
  const auto top = bench.pos[2] + bench.size[2] / 2;
  CHECK(pad.pos[2] - pad.size[2] / 2 == Catch::Approx(top));
  for (auto name : {"lamp.body", "screen.old", "tools.mug", "tools.toolbox",
                    "tools.spool", "tools.multimeter"}) {
    CAPTURE(name);
    CHECK(part(name).pos[2] == Catch::Approx(top));
  }
  CHECK(part("lamp.body").pos == part("lamp.head.emitter").pos);
  CHECK(part("magazine.frame").pos == part("magazine.bayleds").pos);
  CHECK(part("magazine.frame").pos[0] == Catch::Approx(2.5f));
  CHECK(part("door.panel").pos[2] == 0);
  const auto& carriage = part("gantry.carriage.0");
  // The model's bearing wheels reach 84 mm above its bottom-face origin.
  // The parked Y bridges touch those wheels and meet the perimeter X rails.
  for (auto name : {"gantry.bridge.a", "gantry.bridge.b"}) {
    const auto& bridge = part(name);
    CHECK(std::abs(bridge.pos[0] - carriage.pos[0]) == Catch::Approx(.112f));
    CHECK(bridge.pos[1] == carriage.pos[1]);
    CHECK(bridge.size[1] == Catch::Approx(2.4f));
    CHECK(bridge.pos[2] - bridge.size[2] / 2 == Catch::Approx(carriage.pos[2] + .084f));
  }
  for (const auto& p : d->parts) {
    CHECK_FALSE(p.name.starts_with("door.slats."));
    CHECK_FALSE(p.name.starts_with("projector.leds."));
    CHECK_FALSE(p.name.starts_with("magazine.bayleds."));
    CHECK_FALSE(p.name.starts_with("magazine.frame."));
  }
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

TEST_CASE("lab point and spot lights parse with validated metre fields",
          "[unit][io][lab][light]") {
  const std::string point = R"(
[[light]]
name = "ceiling"
kind = "point"
pos = [0, 0.3, 2.62]
color = "#C8D2E6"
intensity = 0.35
radius = 6
)";
  const std::string spot = R"(
[[light]]
name = "lamp"
kind = "spot"
pos = [0.7, 2.6, 1.45]
dir = [0, 0.1, -1]
cone = 35
soft = 12
color = [1, 0.5, 0.2]
intensity = 1.2
radius = 2
group = "machine"
)";
  auto d = io::parseLabDoc(minimal + point + spot, "lights.toml");
  REQUIRE(d.has_value());
  REQUIRE(d->lights.size() == 2);
  CHECK(d->lights[0].kind == io::LabLight::Kind::Point);
  CHECK(d->lights[0].pos == std::array<float, 3>{0, .3f, 2.62f});
  CHECK(d->lights[0].color.r == Catch::Approx(.5775805f));
  CHECK(d->lights[0].intensity == .35f);
  CHECK(d->lights[0].radius == 6);
  CHECK(d->lights[0].group == "room");
  CHECK(d->lights[1].kind == io::LabLight::Kind::Spot);
  CHECK(d->lights[1].dir == std::array<float, 3>{0, .1f, -1});
  CHECK(d->lights[1].cone == 35);
  CHECK(d->lights[1].soft == 12);
  CHECK(d->lights[1].color.g == .5f);
  CHECK(d->lights[1].group == "machine");
  for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
           {"radius = 2", "radius = 0"},
           {"intensity = 1.2", "intensity = -1"},
           {"dir = [0, 0.1, -1]", ""},
           {"dir = [0, 0.1, -1]", "dir = [0,0,0]"},
           {"cone = 35", "cone = 0"},
           {"cone = 35", "cone = 90"},
           {"soft = 12", "soft = -1"},
           {"soft = 12", "soft = 46"},
           {"group = \"machine\"", "group = \"invalid\""},
           {"kind = \"spot\"", "kind = \"invalid\""}}) {
    auto bad = spot;
    bad.replace(bad.find(from), from.size(), to);
    const auto result = io::parseLabDoc(minimal + bad, "bad-light.toml");
    CAPTURE(to);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().line > 0);
  }
  CHECK_FALSE(io::parseLabDoc(minimal + spot + spot, "duplicate.toml"));
  auto nine = minimal;
  for (int i = 0; i < 9; ++i) {
    auto next = point;
    next.replace(next.find("ceiling"), 7, "light" + std::to_string(i));
    nine += next;
  }
  const auto invalid = io::parseLabDoc(nine, "nine.toml");
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().line > 0);
}
TEST_CASE("the shipped lab has five to eight practical lights",
          "[unit][io][lab][light]") {
  const auto d =
      io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(d);
  CHECK(d->lights.size() >= 5);
  CHECK(d->lights.size() <= 8);
}

TEST_CASE("light hex colours decode sRGB while linear triples pass through",
          "[unit][io][lab][light]") {
  const auto d = io::parseLabDoc(R"(version=1
[[light]]
name="hex"
kind="point"
pos=[0,0,0]
radius=1
color="#808008"
[[light]]
name="linear"
kind="point"
pos=[0,0,0]
radius=1
color=[0.5,0.25,0.1]
)",
                                 "colour.toml");
  REQUIRE(d);
  CHECK(d->lights[0].color.r == Catch::Approx(.2158605f));
  CHECK(d->lights[0].color.b == Catch::Approx(.002428216f));
  CHECK(d->lights[1].color.r == .5f);
  CHECK(d->lights[1].color.g == .25f);
}

TEST_CASE("lab focus is optional free text", "[unit][io][rig]") {
  auto text = minimal;
  auto d = io::parseLabDoc(text, "lab.toml");
  REQUIRE(d);
  CHECK(d->parts[0].focus.empty());
  text.insert(text.find("[[station]]"), "focus = \"any object\"\n");
  d = io::parseLabDoc(text, "lab.toml");
  REQUIRE(d);
  CHECK(d->parts[0].focus == "any object");
}
