// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "app/debug_console.hpp"
#include "app/shell.hpp"
#include "io/variant_toml.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {
std::unique_ptr<Shell> makeShell() {
  auto shell = Shell::create({"standard"}, {});
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  shell->dismissWelcome();
  return shell;
}
bool has(const std::string& s, const char* t) { return s.find(t) != std::string::npos; }
}  // namespace

TEST_CASE("the debug console reaches what the interface does not", "[unit][app][debug]") {
  auto shell = makeShell();
  DebugConsole c;
  CHECK(has(c.run(*shell, "moves"), "no game is running"));
  CHECK(has(c.run(*shell, "load standard"), "started"));
  CHECK(has(c.run(*shell, "moves"), "20 legal moves"));
  CHECK(c.run(*shell, "perft 3") == "8902");
  CHECK(has(c.run(*shell, "hash"), "ok"));
  CHECK(has(c.run(*shell, "play e2e4"), "played"));
  CHECK(has(c.run(*shell, "unmoved"), "unmoved"));
  CHECK(has(c.run(*shell, "attacked e4"), "attacked"));
  CHECK(has(c.run(*shell, "bogus"), "unknown command"));
  CHECK(has(c.run(*shell, "undo"), "ok"));
  CHECK(!c.log().empty());
  c.run(*shell, "clear");
  CHECK(c.log().empty());
}
