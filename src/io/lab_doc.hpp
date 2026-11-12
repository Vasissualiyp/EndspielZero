// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "base/result.hpp"
#include "view/theme.hpp"
namespace cb::io {
enum class LabMesh : std::uint8_t { Box, Bevel, Cylinder, Slab, File };
enum class LabMaterial : std::uint8_t { Matte, Metal, Glass, Glow, Hologram, Led, Sky };
struct LabPart {
  std::string name;
  LabMesh mesh{LabMesh::Box};
  std::filesystem::path file;
  std::optional<LabMesh> fallback;
  std::array<float, 3> pos{}, size{}, rot{};
  LabMaterial material{LabMaterial::Matte};
  view::Rgba color{view::Rgba::hex(0x808080)};
  float ao{0};
  std::string group{"room"};
};
struct LabLight {
  std::string name;
  enum class Kind : std::uint8_t { Point, Spot } kind{Kind::Point};
  std::array<float, 3> pos{}, dir{0, 0, -1};
  float cone{30}, soft{10};
  view::Rgba color{};
  float intensity{1}, radius{1};
  std::string group{"room"};
};
struct LabStation {
  std::string name;
  std::array<float, 3> target{};
  float distance{1}, yaw{0}, pitch{0}, fov{50};
};
struct LabDoc {
  std::vector<LabPart> parts;
  std::vector<LabLight> lights;
  std::vector<LabStation> stations;
  std::vector<std::pair<std::string, std::array<float, 3>>> anchors;
  [[nodiscard]] const LabStation* station(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<std::array<float, 3>> anchor(std::string_view name) const;
};
[[nodiscard]] Result<LabDoc> parseLabDoc(std::string_view text,
                                         std::string_view sourceName);
[[nodiscard]] Result<LabDoc> loadLabDoc(const std::filesystem::path& file);
}  // namespace cb::io
