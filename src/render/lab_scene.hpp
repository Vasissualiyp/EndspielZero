// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "io/lab_doc.hpp"
#include "io/obj_mesh.hpp"
#include "render/board_renderer.hpp"
namespace cb::render {
inline constexpr float kCellsPerMetre = 20.0f;
/// Right-handed yaw Z, pitch Y, roll X; composition Z*Y*X, in radians.
[[nodiscard]] std::array<float, 4> quatFromYawPitchRoll(float yaw, float pitch,
                                                        float roll);
class LabScene {
 public:
  static Result<LabScene> create(io::LabDoc doc);
  [[nodiscard]] view::Vec3 toWorld(std::array<float, 3> metres) const noexcept;
  [[nodiscard]] std::optional<view::OrbitCamera> station(std::string_view name) const;
  void appendTo(InstanceSet& set, const view::Theme& theme) const;
  [[nodiscard]] const io::LabDoc& doc() const noexcept { return doc_; }
  [[nodiscard]] const std::vector<io::ObjMesh>& fileMeshes() const noexcept {
    return fileMeshes_;
  }
  void setFileRanges(std::vector<std::uint32_t> ranges);

 private:
  io::LabDoc doc_;
  view::Vec3 deployCentre_{};
  std::vector<io::ObjMesh> fileMeshes_;
  std::vector<std::uint32_t> partFiles_, fileRanges_;
};
}  // namespace cb::render
