// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include "base/result.hpp"
namespace cb::io {
/// OBJ lab parts use metres, Z up and the model's authored origin. Lab pos places
/// that origin; size multiplies the mesh (1,1,1 preserves its authored dimensions).
struct ObjMesh {
  std::vector<std::array<float, 3>> positions;
  std::vector<std::array<float, 3>> normals;
  std::vector<std::uint32_t> indices;
};
[[nodiscard]] Result<ObjMesh> parseObj(std::string_view text, std::string_view name);
}  // namespace cb::io
