// SPDX-License-Identifier: GPL-3.0-or-later
// Architecture tests: the invariants in AGENTS.md that are about the source tree
// itself rather than about behaviour. Cheap to run, and they catch the drift that
// code review reliably misses.
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

namespace {

const fs::path kRoot{CB_SOURCE_DIR};

std::vector<fs::path> sourcesUnder(const fs::path& dir) {
  std::vector<fs::path> out;
  if (!fs::exists(dir)) return out;
  for (const auto& e : fs::recursive_directory_iterator(dir)) {
    if (!e.is_regular_file()) continue;
    const std::string ext = e.path().extension().string();
    if (ext == ".cpp" || ext == ".hpp") out.push_back(e.path());
  }
  return out;
}

std::string readFile(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

/// Blank out comments and string literals so a scan sees only real code.
std::string stripNonCode(const std::string& src) {
  std::string out;
  out.reserve(src.size());
  enum class S { Code, Line, Block, Str, Chr } s = S::Code;
  for (std::size_t i = 0; i < src.size(); ++i) {
    const char c = src[i];
    const char n = i + 1 < src.size() ? src[i + 1] : '\0';
    switch (s) {
      case S::Code:
        if (c == '/' && n == '/') {
          s = S::Line;
          ++i;
        } else if (c == '/' && n == '*') {
          s = S::Block;
          ++i;
        } else if (c == '"') {
          s = S::Str;
        } else if (c == '\'' &&
                   !(!out.empty() &&
                     (std::isalnum(static_cast<unsigned char>(out.back())) != 0 ||
                      out.back() == '_'))) {
          // A quote directly after an alphanumeric is a digit separator
          // (0xFFFF'FFFFu), not the start of a character literal. Treating it as
          // one desynchronizes the scanner and makes later comment text look like
          // code - which is exactly how this test produced a false positive.
          s = S::Chr;
        } else {
          out += c;
        }
        break;
      case S::Line:
        if (c == '\n') {
          s = S::Code;
          out += '\n';
        }
        break;
      case S::Block:
        if (c == '*' && n == '/') {
          s = S::Code;
          ++i;
        } else if (c == '\n') {
          out += '\n';
        }
        break;
      case S::Str:
        if (c == '\\')
          ++i;
        else if (c == '"')
          s = S::Code;
        break;
      case S::Chr:
        if (c == '\\')
          ++i;
        else if (c == '\'')
          s = S::Code;
        break;
    }
  }
  return out;
}

bool containsWord(const std::string& hay, const std::string& word) {
  std::size_t pos = 0;
  while ((pos = hay.find(word, pos)) != std::string::npos) {
    const bool leftOk =
        pos == 0 ||
        (!std::isalnum(static_cast<unsigned char>(hay[pos - 1])) && hay[pos - 1] != '_');
    const std::size_t end = pos + word.size();
    const bool rightOk =
        end >= hay.size() ||
        (!std::isalnum(static_cast<unsigned char>(hay[end])) && hay[end] != '_');
    if (leftOk && rightOk) return true;
    pos = end;
  }
  return false;
}

}  // namespace

TEST_CASE("the engine core contains no floating-point arithmetic", "[arch]") {
  // Determinism is a hard requirement: multiplayer desync detection, the replay
  // corpus and later self-play all compare hashes bit for bit. Floats are the
  // classic way to lose that silently (ARCH section 12, AGENTS.md rule 5).
  const std::vector<std::string> coreDirs{"base",     "diag",     "space",   "geometry",
                                          "position", "pieces",   "variant", "movegen",
                                          "rules",    "temporal", "game"};
  std::vector<std::string> offenders;
  for (const std::string& d : coreDirs) {
    for (const fs::path& f : sourcesUnder(kRoot / "src" / d)) {
      const std::string code = stripNonCode(readFile(f));
      if (containsWord(code, "float") || containsWord(code, "double") ||
          containsWord(code, "long double")) {
        offenders.push_back(fs::relative(f, kRoot).string());
      }
    }
  }
  CAPTURE(offenders);
  REQUIRE(offenders.empty());
}

TEST_CASE("every source file carries an SPDX header", "[arch]") {
  std::vector<std::string> missing;
  for (const std::string_view d : {"src", "tests", "bench"}) {
    for (const fs::path& f : sourcesUnder(kRoot / d)) {
      const std::string head = readFile(f).substr(0, 200);
      if (head.find("SPDX-License-Identifier: GPL-3.0-or-later") == std::string::npos) {
        missing.push_back(fs::relative(f, kRoot).string());
      }
    }
  }
  CAPTURE(missing);
  REQUIRE(missing.empty());
}

TEST_CASE("kMaxDims is defined in exactly one place", "[arch]") {
  // AGENTS.md promises that raising the dimension budget is a one-line change.
  int definitions = 0;
  for (const fs::path& f : sourcesUnder(kRoot / "src")) {
    const std::string code = stripNonCode(readFile(f));
    if (code.find("constexpr int kMaxDims") != std::string::npos) ++definitions;
  }
  REQUIRE(definitions == 1);
}

TEST_CASE("the declared layer graph is acyclic and bottom-up", "[arch]") {
  // cb_layer() already rejects an upward dependency at configure time; this
  // asserts the manifest it emits is well-formed and that levels are unique
  // enough to give a total order.
  const fs::path manifest = fs::path(CB_BINARY_DIR) / "layers.manifest";
  REQUIRE(fs::exists(manifest));
  std::ifstream in(manifest);
  std::string name;
  int level = 0;
  std::set<int> levels;
  int count = 0;
  while (in >> name >> level) {
    REQUIRE(name.starts_with("endspielzero_"));
    REQUIRE(levels.insert(level).second);  // one layer per level
    ++count;
  }
  REQUIRE(count >= 4);
}

TEST_CASE("every test source is registered under a CTest label", "[arch]") {
  // A test file in a directory we do not glob would silently never run.
  for (const fs::path& f : sourcesUnder(kRoot / "tests")) {
    const std::string rel = fs::relative(f, kRoot / "tests").string();
    const std::string top = rel.substr(0, rel.find(fs::path::preferred_separator));
    const std::set<std::string> known{"unit", "property", "golden", "perft",
                                      "arch", "support",  "oracle", "render"};
    CAPTURE(rel);
    REQUIRE(known.contains(top));
  }
}
