// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include "app/debug_console.hpp"
#include "app/shell.hpp"
#include "io/lab_doc.hpp"
using namespace cb;
TEST_CASE("lab station and salience console pins validate and clear",
          "[unit][app][rig]") {
  auto shell = app::Shell::create({}, {});
  REQUIRE(shell);
  app::DebugConsole console;
  CHECK(console.run(*shell, "station menu") == "lab is off");
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  console.setLabDoc(&*doc);
  CHECK(console.run(*shell, "station menu") == "station menu");
  CHECK(console.stationPin() == "menu");
  CHECK(console.run(*shell, "station missing").find("unknown") != std::string::npos);
  CHECK(console.stationPin() == "menu");
  CHECK(console.run(*shell, "salience projector") == "salience projector");
  CHECK(console.saliencePin() == "projector");
  CHECK(console.run(*shell, "salience off") == "salience off");
  CHECK(console.saliencePin().empty());
  CHECK(console.run(*shell, "station off") == "station off");
  CHECK(console.stationPin().empty());
}

TEST_CASE("lab transition console reaches every menu station", "[unit][app][rig]") {
  auto shell = app::Shell::create({}, {});
  REQUIRE(shell);
  app::DebugConsole console;
  for (const std::string to : {"settings", "quit", "welcome", "info", "pieces"}) {
    CHECK(console.run(*shell, "transition menu " + to) == "transition menu -> " + to);
  }
}
