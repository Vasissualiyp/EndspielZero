// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include "io/lab_doc.hpp"
#include "io/obj_mesh.hpp"
#include "render/board_renderer.hpp"
#include "view/rig_deploy.hpp"
namespace cb::render {
inline constexpr float kCellsPerMetre = 20.0f;
/// Right-handed yaw Z, pitch Y, roll X; composition Z*Y*X, in radians.
[[nodiscard]] std::array<float, 4> quatFromYawPitchRoll(float yaw, float pitch,
                                                        float roll);
/// How the lab is drawn around a game (M24.5): placed by `xform`, some parts hidden
/// (the parked carriage while copies fly, slabs out on a deploy), room parts faded by
/// the screen door. All optional; the default is the menu lab.
struct LabDraw {
  const view::RigXform* xform{nullptr};
  std::function<bool(const io::LabPart&)> hidden;
  /// Fade (0..1, 1 drawn) of a part from its bounds in the drawn space.
  std::function<float(const io::LabPart&, const view::Bounds&)> fade;
};
/// Encode a screen-door fade on a lab instance: `panelPrev[3]` below -1.5 (lab parts
/// have no colour transition, so the settled marker carries it; board.frag discards).
void setLabFade(Instance& i, float fade) noexcept;
class LabScene {
 public:
  static Result<LabScene> create(io::LabDoc doc);
  [[nodiscard]] view::Vec3 toWorld(std::array<float, 3> metres) const noexcept;
  [[nodiscard]] std::optional<view::OrbitCamera> station(std::string_view name) const;
  void appendTo(InstanceSet& set, const view::Theme& theme,
                const std::function<float(std::string_view)>& salience = {},
                const std::array<float, 8>& slabSlides = {}) const;
  /// The lab placed around a board: every centre through `toBoard`, scales x labScale.
  void appendTo(InstanceSet& set, const view::Theme& theme,
                const std::function<float(std::string_view)>& salience,
                const view::RigXform& xform) const;
  void appendTo(InstanceSet& set, const view::Theme& theme,
                const std::function<float(std::string_view)>& salience,
                const std::array<float, 8>& slabSlides, const LabDraw& draw) const;
  /// One more instance of an authored part (mesh, material, colour, group and focus),
  /// at `posMetres` in the lab's metres, inserted into its own batch.
  void appendCopy(InstanceSet& set, std::string_view partName,
                  const std::array<float, 3>& posMetres, const std::array<float, 4>& quat,
                  const std::array<float, 3>& sizeMul, float alpha = 1) const;
  /// The same, centred at `centre` in the drawn space (board space during a game). The
  /// size stays the part's own: rig hardware is built at the board's scale.
  void appendCopyAt(InstanceSet& set, std::string_view partName, const view::Vec3& centre,
                    const std::array<float, 4>& quat, const std::array<float, 3>& sizeMul,
                    float alpha = 1) const;
  [[nodiscard]] const io::LabPart* part(std::string_view name) const noexcept;
  /// Lab-world bounds of a part as authored, after its rotation.
  [[nodiscard]] view::Bounds partBounds(std::string_view name) const;
  /// The room's inner box (walls, floor and ceiling faces), lab world.
  [[nodiscard]] view::Bounds roomBounds() const;
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
  std::vector<view::Bounds> partBounds_;
  [[nodiscard]] Instance instanceOf(const io::LabPart& p,
                                    const std::array<float, 8>& slabSlides) const;
  void insert(InstanceSet& set, std::size_t partIndex, const Instance& i) const;
};
}  // namespace cb::render
