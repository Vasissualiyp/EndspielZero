#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "support/variants.hpp"
TEST_CASE("DUMPIDS", "[dumpids]") {
  std::ofstream out(std::getenv("CB_DUMP") ? std::getenv("CB_DUMP") : "/dev/null");
  for (const auto& e : std::filesystem::directory_iterator(cb::test::repoRoot() / "variants")) {
    const auto v = cb::loadVariantFile(e.path());
    out << e.path().stem().string() << " " << (v.has_value() ? std::to_string(v->variantId()) : std::string("ERR")) << "\n";
  }
}
