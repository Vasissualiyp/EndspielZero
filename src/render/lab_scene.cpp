// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/lab_scene.hpp"
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <sstream>
namespace cb::render {
namespace {
constexpr float radians(float d) {
  return d * std::numbers::pi_v<float> / 180;
}
constexpr auto kNoFile = std::numeric_limits<std::uint32_t>::max();
}  // namespace
std::array<float, 4> quatFromYawPitchRoll(float yaw, float pitch, float roll) {
  const float cy = std::cos(yaw / 2), sy = std::sin(yaw / 2), cp = std::cos(pitch / 2),
              sp = std::sin(pitch / 2), cr = std::cos(roll / 2), sr = std::sin(roll / 2);
  return {cy * cp * sr - sy * sp * cr, cy * sp * cr + sy * cp * sr,
          sy * cp * cr - cy * sp * sr, cy * cp * cr + sy * sp * sr};
}
Result<LabScene> LabScene::create(io::LabDoc doc) {
  LabScene scene;
  auto centre = doc.anchor("gantry.centre");
  if (!centre)
    return fail(ErrorCode::ValidationError, "lab needs anchor 'gantry.centre'");
  scene.deployCentre_ = {(*centre)[0], (*centre)[1], (*centre)[2]};
  std::map<std::filesystem::path, std::uint32_t> files;
  for (const auto& part : doc.parts) {
    auto index = kNoFile;
    if (part.mesh == io::LabMesh::File) {
      auto it = files.find(part.file);
      if (it == files.end()) {
        std::ifstream file(part.file);
        if (!file)
          return fail(ErrorCode::ParseError,
                      "part '" + part.name + "': cannot open " + part.file.string());
        std::ostringstream text;
        text << file.rdbuf();
        auto mesh = io::parseObj(text.str(), part.file.string());
        if (!mesh)
          return fail(mesh.error().code,
                      "part '" + part.name + "': " + mesh.error().message,
                      mesh.error().line);
        index = static_cast<std::uint32_t>(scene.fileMeshes_.size());
        files.emplace(part.file, index);
        scene.fileMeshes_.push_back(std::move(*mesh));
      } else
        index = it->second;
    }
    scene.partFiles_.push_back(index);
  }
  scene.doc_ = std::move(doc);
  return scene;
}
void LabScene::setFileRanges(std::vector<std::uint32_t> ranges) {
  fileRanges_ = std::move(ranges);
}
view::Vec3 LabScene::toWorld(std::array<float, 3> m) const noexcept {
  return {(m[0] - deployCentre_.x) * kCellsPerMetre,
          (m[1] - deployCentre_.y) * kCellsPerMetre,
          (m[2] - deployCentre_.z) * kCellsPerMetre};
}
std::optional<view::OrbitCamera> LabScene::station(std::string_view name) const {
  const auto* s = doc_.station(name);
  if (!s) return {};
  view::OrbitCamera c;
  c.target = toWorld(s->target);
  c.distance = s->distance * kCellsPerMetre;
  c.yaw = radians(s->yaw);
  c.pitch = radians(s->pitch);
  c.fovY = radians(s->fov);
  c.nearZ = .05f * kCellsPerMetre;
  c.farZ = 30 * kCellsPerMetre;
  return c;
}
void LabScene::appendTo(InstanceSet& set, const view::Theme& theme,
                        const std::function<float(std::string_view)>& salience,
                        const std::array<float, 8>& slabSlides) const {
  (void)theme;  // Authored part colours/materials are independent of the theme.
  set.lab.clear();
  set.labBatches = {};
  set.labFileBatches.assign(fileRanges_.empty()
                                ? 0
                                : static_cast<std::size_t>(*std::max_element(
                                      fileRanges_.begin(), fileRanges_.end())) +
                                      1,
                            {});
  const auto instance = [&](const io::LabPart& p) {
    Instance i;
    const auto world = toWorld(p.pos);
    i.center[0] = world.x;
    i.center[1] = world.y;
    i.center[2] = world.z;
    const auto half = BoardRenderer::cellHalfExtent();
    const std::array<float, 3> h{half.x, half.y, half.z};
    for (std::size_t a = 0; a < 3; ++a)
      i.scale[a] =
          p.size[a] * kCellsPerMetre / (p.mesh == io::LabMesh::Slab ? 2 * h[a] : 1);
    const auto q =
        quatFromYawPitchRoll(radians(p.rot[0]), radians(p.rot[1]), radians(p.rot[2]));
    std::copy(q.begin(), q.end(), i.quat);
    if (p.name.starts_with("magazine.slab.") && p.name.size() == 15 &&
        p.name.back() >= '0' && p.name.back() <= '7') {
      // Local +Y points out of the authored slot (world -X at the rack pose).
      const view::Vec3 axis{q[0], q[1], q[2]}, local{0, 1, 0};
      const auto t = view::cross(axis, local) * 2;
      const auto out = local + t * q[3] + view::cross(axis, t);
      const auto offset =
          out *
          (slabSlides[static_cast<std::size_t>(p.name.back() - '0')] * kCellsPerMetre);
      i.center[0] += offset.x;
      i.center[1] += offset.y;
      i.center[2] += offset.z;
    }
    const auto color = p.color.array();
    std::copy(color.begin(), color.end(), i.color);
    constexpr std::array<view::Material, 7> materials{
        view::Material::Matte, view::Material::Metal,    view::Material::Glass,
        view::Material::Glow,  view::Material::Hologram, view::Material::Led,
        view::Material::Sky};
    i.material =
        shaderMaterial(materials[static_cast<std::size_t>(p.material)], Cut::Plank, 0);
    if (salience && !p.focus.empty())
      i.panelSignal[2] = std::clamp(salience(p.focus), 0.f, 1.f);
    // AO remains authored metadata: the LCD response contract requires this payload
    // to be inert on non-panel materials (M24.1 Step 3's permitted fallback).
    return i;
  };
  for (std::size_t s = 0; s < set.labBatches.size(); ++s) {
    auto& batch = set.labBatches[s];
    batch.first = static_cast<std::uint32_t>(set.lab.size());
    for (const auto& p : doc_.parts)
      if (static_cast<std::size_t>(p.mesh) == s) {
        set.lab.push_back(instance(p));
        ++batch.count;
      }
  }
  for (std::size_t s = 0; s < set.labFileBatches.size(); ++s) {
    auto& batch = set.labFileBatches[s];
    batch.first = static_cast<std::uint32_t>(set.lab.size());
    for (std::size_t p = 0; p < doc_.parts.size(); ++p)
      if (partFiles_[p] != kNoFile && fileRanges_[partFiles_[p]] == s) {
        set.lab.push_back(instance(doc_.parts[p]));
        ++batch.count;
      }
  }
}
}  // namespace cb::render
