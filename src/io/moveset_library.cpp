// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/moveset_library.hpp"

#include <fstream>
#include <sstream>

namespace cb {
namespace {

std::vector<std::filesystem::path>& configured() {
  static std::vector<std::filesystem::path> dirs;
  return dirs;
}

bool isBareName(std::string_view name) {
  if (name.empty()) return false;
  for (const char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_';
    if (!ok) return false;
  }
  return true;
}

}  // namespace

void setMovesetDirs(std::vector<std::filesystem::path> dirs) {
  configured() = std::move(dirs);
}

std::vector<std::filesystem::path> movesetDirs() {
  if (!configured().empty()) return configured();
  std::vector<std::filesystem::path> found;
  for (const char* base : {"movesets", "../movesets", "../../movesets", "share/chessbox/movesets",
                           "/usr/share/chessbox/movesets"}) {
    if (std::filesystem::is_directory(base)) {
      found.emplace_back(base);
      break;
    }
  }
  return found;
}

std::optional<std::string> libraryMoveset(std::string_view name) {
  if (!isBareName(name)) return std::nullopt;
  for (const auto& dir : movesetDirs()) {
    std::ifstream in(dir / (std::string(name) + ".moveset"));
    if (!in) continue;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
  return std::nullopt;
}

MovesetContext movesetContextFor(const toml::table& root) {
  MovesetContext ctx;
  std::string orient = root["orientation_axis"].value_or(std::string{});
  if (const auto* axes = root["axis"].as_array()) {
    int i = 0;
    for (const auto& node : *axes) {
      const auto* t = node.as_table();
      const std::string name = t != nullptr ? (*t)["name"].value_or(std::string{}) : std::string{};
      ctx.axisNames.push_back(name);
      if (!orient.empty() && name == orient) ctx.orientAxis = i;
      ++i;
    }
  }
  ctx.dims = static_cast<std::uint8_t>(ctx.axisNames.size());
  if (const auto* tags = root["mark"].as_array()) {
    for (const auto& node : *tags) {
      const auto* t = node.as_table();
      const std::string n = t != nullptr ? (*t)["name"].value_or(std::string{}) : std::string{};
      if (!n.empty() && n != "ep") ctx.markTags.push_back(n);
    }
  }
  // Names: the variant's own table first, then the shared library.
  const toml::table* own = root["movesets"].as_table();
  ctx.lookup = [own](std::string_view name) -> std::optional<std::string> {
    if (own != nullptr) {
      if (const auto text = (*own)[name].value<std::string>()) return *text;
    }
    return libraryMoveset(name);
  };
  return ctx;
}

}  // namespace cb
