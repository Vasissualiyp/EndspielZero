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
  for (std::size_t i = 0; i < scene.doc_.parts.size(); ++i) {
    const auto& p = scene.doc_.parts[i];
    std::array<float, 3> lo{-.5f, -.5f, -.5f}, hi{.5f, .5f, .5f};
    if (scene.partFiles_[i] != kNoFile) {
      lo = {1e30f, 1e30f, 1e30f};
      hi = {-1e30f, -1e30f, -1e30f};
      for (const auto& v : scene.fileMeshes_[scene.partFiles_[i]].positions)
        for (std::size_t a = 0; a < 3; ++a) {
          lo[a] = std::min(lo[a], v[a]);
          hi[a] = std::max(hi[a], v[a]);
        }
    }
    const auto q =
        quatFromYawPitchRoll(radians(p.rot[0]), radians(p.rot[1]), radians(p.rot[2]));
    const view::Vec3 axis{q[0], q[1], q[2]};
    const auto origin = scene.toWorld(p.pos);
    view::Bounds b{1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
    for (int c = 0; c < 8; ++c) {
      const view::Vec3 raw{(c & 1 ? hi[0] : lo[0]) * p.size[0] * kCellsPerMetre,
                           (c & 2 ? hi[1] : lo[1]) * p.size[1] * kCellsPerMetre,
                           (c & 4 ? hi[2] : lo[2]) * p.size[2] * kCellsPerMetre};
      const auto t = view::cross(axis, raw) * 2;
      const auto w = origin + raw + t * q[3] + view::cross(axis, t);
      b = {std::min(b.minX, w.x), std::min(b.minY, w.y), std::min(b.minZ, w.z),
           std::max(b.maxX, w.x), std::max(b.maxY, w.y), std::max(b.maxZ, w.z)};
    }
    scene.partBounds_.push_back(b);
  }
  return scene;
}
const io::LabPart* LabScene::part(std::string_view name) const noexcept {
  for (const auto& p : doc_.parts)
    if (p.name == name) return &p;
  return nullptr;
}
view::Bounds LabScene::partBounds(std::string_view name) const {
  for (std::size_t i = 0; i < doc_.parts.size(); ++i)
    if (doc_.parts[i].name == name) return partBounds_[i];
  return {};
}
view::Bounds LabScene::roomBounds() const {
  view::Bounds room = partBounds("floor");
  const auto has = [&](std::string_view n) { return part(n) != nullptr; };
  if (has("floor")) room.minZ = partBounds("floor").maxZ;
  if (has("wall.left")) room.minX = partBounds("wall.left").maxX;
  if (has("wall.right")) room.maxX = partBounds("wall.right").minX;
  if (has("wall.front.l")) room.minY = partBounds("wall.front.l").maxY;
  if (has("wall.back")) room.maxY = partBounds("wall.back").minY;
  if (has("ceiling.l")) room.maxZ = partBounds("ceiling.l").minZ;
  return room;
}
void setLabFade(Instance& i, float fade) noexcept {
  if (fade < 1) i.panelPrev[3] = -2 - (1 - std::clamp(fade, 0.f, 1.f));
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
Instance LabScene::instanceOf(const io::LabPart& p,
                              const std::array<float, 8>& slabSlides) const {
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
        out * (slabSlides[static_cast<std::size_t>(p.name.back() - '0')] * kCellsPerMetre);
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
  // AO remains authored metadata: the LCD response contract requires this payload
  // to be inert on non-panel materials (M24.1 Step 3's permitted fallback).
  return i;
}
void LabScene::appendTo(InstanceSet& set, const view::Theme& theme,
                        const std::function<float(std::string_view)>& salience,
                        const std::array<float, 8>& slabSlides) const {
  appendTo(set, theme, salience, slabSlides, LabDraw{});
}
void LabScene::appendTo(InstanceSet& set, const view::Theme& theme,
                        const std::function<float(std::string_view)>& salience,
                        const view::RigXform& xform) const {
  LabDraw draw;
  draw.xform = &xform;
  appendTo(set, theme, salience, {}, draw);
}
void LabScene::appendTo(InstanceSet& set, const view::Theme& theme,
                        const std::function<float(std::string_view)>& salience,
                        const std::array<float, 8>& slabSlides,
                        const LabDraw& draw) const {
  (void)theme;  // Authored part colours/materials are independent of the theme.
  set.lab.clear();
  set.labBatches = {};
  set.labFileBatches.assign(fileRanges_.empty()
                                ? 0
                                : static_cast<std::size_t>(*std::max_element(
                                      fileRanges_.begin(), fileRanges_.end())) +
                                      1,
                            {});
  const auto instance = [&](std::size_t index) {
    const auto& p = doc_.parts[index];
    auto i = instanceOf(p, slabSlides);
    if (salience && !p.focus.empty())
      i.panelSignal[2] = std::clamp(salience(p.focus), 0.f, 1.f);
    view::Bounds bounds = partBounds_[index];
    if (draw.xform) {
      const auto c = draw.xform->toBoard({i.center[0], i.center[1], i.center[2]});
      i.center[0] = c.x;
      i.center[1] = c.y;
      i.center[2] = c.z;
      for (float& s : i.scale) s *= draw.xform->labScale;
      const auto lo = draw.xform->toBoard({bounds.minX, bounds.minY, bounds.minZ});
      const auto hi = draw.xform->toBoard({bounds.maxX, bounds.maxY, bounds.maxZ});
      bounds = {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z};
    }
    if (draw.fade) setLabFade(i, draw.fade(p, bounds));
    return i;
  };
  const auto drawn = [&](std::size_t index) {
    const auto& p = doc_.parts[index];
    return p.group != "template" && !(draw.hidden && draw.hidden(p));
  };
  for (std::size_t s = 0; s < set.labBatches.size(); ++s) {
    auto& batch = set.labBatches[s];
    batch.first = static_cast<std::uint32_t>(set.lab.size());
    for (std::size_t p = 0; p < doc_.parts.size(); ++p)
      if (static_cast<std::size_t>(doc_.parts[p].mesh) == s && drawn(p)) {
        set.lab.push_back(instance(p));
        ++batch.count;
      }
  }
  for (std::size_t s = 0; s < set.labFileBatches.size(); ++s) {
    auto& batch = set.labFileBatches[s];
    batch.first = static_cast<std::uint32_t>(set.lab.size());
    for (std::size_t p = 0; p < doc_.parts.size(); ++p)
      if (partFiles_[p] != kNoFile && fileRanges_[partFiles_[p]] == s && drawn(p)) {
        set.lab.push_back(instance(p));
        ++batch.count;
      }
  }
}
void LabScene::insert(InstanceSet& set, std::size_t index, const Instance& i) const {
  const auto& p = doc_.parts[index];
  InstanceSet::Batch* batch = nullptr;
  if (partFiles_[index] != kNoFile) {
    const auto range = fileRanges_.empty() ? std::uint32_t{0} : fileRanges_[partFiles_[index]];
    if (set.labFileBatches.size() <= range) {
      const auto end = static_cast<std::uint32_t>(set.lab.size());
      set.labFileBatches.resize(range + 1, {end, 0});
    }
    batch = &set.labFileBatches[range];
  } else {
    batch = &set.labBatches[static_cast<std::size_t>(p.mesh)];
  }
  const auto at = batch->first + batch->count;
  set.lab.insert(set.lab.begin() + at, i);
  // Batches are contiguous in this order: shapes, then files. Every batch after the
  // grown one moves up by the inserted instance.
  std::vector<InstanceSet::Batch*> order;
  for (auto& b : set.labBatches) order.push_back(&b);
  for (auto& b : set.labFileBatches) order.push_back(&b);
  bool after = false;
  for (auto* b : order) {
    if (after) ++b->first;
    if (b == batch) after = true;
  }
  ++batch->count;
}
void LabScene::appendCopy(InstanceSet& set, std::string_view partName,
                          const std::array<float, 3>& posMetres,
                          const std::array<float, 4>& quat,
                          const std::array<float, 3>& sizeMul, float alpha) const {
  appendCopyAt(set, partName, toWorld(posMetres), quat, sizeMul, alpha);
}
void LabScene::appendCopyAt(InstanceSet& set, std::string_view partName,
                            const view::Vec3& centre, const std::array<float, 4>& quat,
                            const std::array<float, 3>& sizeMul, float alpha) const {
  for (std::size_t index = 0; index < doc_.parts.size(); ++index) {
    const auto& p = doc_.parts[index];
    if (p.name != partName) continue;
    auto i = instanceOf(p, {});
    i.center[0] = centre.x;
    i.center[1] = centre.y;
    i.center[2] = centre.z;
    for (std::size_t a = 0; a < 3; ++a) i.scale[a] *= sizeMul[a];
    std::copy(quat.begin(), quat.end(), i.quat);
    // A glow part's light is its colour; alpha scales it (the opaque pass has no blend).
    if (p.material == io::LabMaterial::Glow)
      for (int c = 0; c < 3; ++c) i.color[c] *= alpha;
    i.color[3] *= alpha;
    insert(set, index, i);
    return;
  }
}
}  // namespace cb::render
