// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/neural_vr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>

#include "render/overture_scene.hpp"
#include "render/slab_skin.hpp"
#include "view/neural_vr.hpp"

namespace cb::render {
namespace {
/// The slab's half thickness, back to cell top (`lab_rig.cpp`'s body), and how far above
/// the cell tops the code's modules stand.
constexpr float kFaceHalf = 0.185f;
constexpr float kModuleLift = 0.03f;
constexpr float kModuleDepth = 0.02f;
constexpr float kModuleFill = 0.78f;  ///< of the module pitch: a visible LED grid

view::Vec3 rotate(const std::array<float, 4>& q, const view::Vec3& v) noexcept {
  const view::Vec3 u{q[0], q[1], q[2]};
  const view::Vec3 t = view::cross(u, v) * 2.0f;
  return v + t * q[3] + view::cross(u, t);
}

/// Insert `items` at the end of the lab batch `shape`, shifting the batches after it
/// (as `insertLabCylinders` does for cylinders).
std::size_t insertLab(InstanceSet& set, LabShape shape,
                      const std::vector<Instance>& items) {
  if (items.empty()) return 0;
  auto& batch = set.labBatches[static_cast<std::size_t>(shape)];
  const std::uint32_t at = batch.first + batch.count;
  const auto n = static_cast<std::uint32_t>(items.size());
  set.lab.insert(set.lab.begin() + at, items.begin(), items.end());
  bool after = false;
  for (auto& b : set.labBatches) {
    if (after) b.first += n;
    if (&b == &batch) after = true;
  }
  for (auto& b : set.labFileBatches)
    if (b.first >= at) b.first += n;
  batch.count += n;
  return items.size();
}

std::int64_t gridKey(float x, float y) noexcept {
  const auto ix = static_cast<std::int64_t>(std::lround(x)) + (1 << 20);
  const auto iy = static_cast<std::int64_t>(std::lround(y)) + (1 << 20);
  return (ix << 22) | iy;
}
}  // namespace

int rackBayCount(const io::LabDoc& doc) {
  if (!doc.deploy) return 0;
  const std::string& prefix = doc.deploy->slabBay;
  int bays = 0;
  for (const auto& part : doc.parts) {
    const std::string_view name = part.name;
    if (name.size() < prefix.size() + 2 || name.substr(0, prefix.size()) != prefix ||
        name[prefix.size()] != '.')
      continue;
    const std::string_view rest = name.substr(prefix.size() + 1);
    if (std::all_of(rest.begin(), rest.end(),
                    [](char c) { return c >= '0' && c <= '9'; }))
      ++bays;
  }
  return bays;
}

const std::vector<view::Placement>& vrStandIn() {
  static const std::vector<view::Placement> standIn = [] {
    std::vector<view::Placement> out;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x)
        out.push_back({static_cast<CellId>(y * 8 + x), static_cast<float>(x),
                       static_cast<float>(y), 0.0f, 0});
    return out;
  }();
  return standIn;
}

view::OrbitCamera vrPictureCamera(const view::Bounds& bounds, float aspect) {
  auto camera = view::OrbitCamera::frame(bounds, aspect, 0);
  camera.orthographic = true;
  camera.pitch = 1.5707963f;
  camera.yaw = 0;
  camera.target = {(bounds.minX + bounds.maxX) * .5f, (bounds.minY + bounds.maxY) * .5f,
                   0};
  const float halfHeight =
      .5f *
      std::max(bounds.maxY - bounds.minY + 1, (bounds.maxX - bounds.minX + 1) / aspect) /
      .85f;
  camera.distance = halfHeight / std::tan(camera.fovY * .5f);
  return camera;
}

std::size_t appendNeuralCode(InstanceSet& set, const RigLayout& layout,
                             const std::vector<view::SlabPose>& poses, float strength,
                             float seconds, const view::Theme& theme) {
  strength = std::clamp(strength, 0.0f, 1.0f);
  if (strength <= 0.0f) return 0;
  const int n = view::kNeuralCodeModules;
  std::vector<Instance> modules;
  const view::Rgba led = theme.ember;
  for (std::size_t s = 0; s < layout.slabs.size() && s < poses.size(); ++s) {
    const auto& pose = poses[s];
    if (!pose.clamped) continue;
    const auto& slab = layout.slabs[s];
    const float side =
        std::min(slab.maxX - slab.minX, slab.maxY - slab.minY) - 2.0f * kBezelMargin;
    const float pitch = side / static_cast<float>(n);
    for (int j = 0; j < n; ++j)
      for (int i = 0; i < n; ++i) {
        if (!view::neuralCodeModule(i, j, n, seconds, static_cast<std::uint32_t>(s)))
          continue;
        const view::Vec3 local{-side * .5f + (static_cast<float>(i) + .5f) * pitch,
                               side * .5f - (static_cast<float>(j) + .5f) * pitch,
                               kFaceHalf + kModuleLift};
        const view::Vec3 at = pose.centre + rotate(pose.quat, local);
        Instance m{};
        m.center[0] = at.x;
        m.center[1] = at.y;
        m.center[2] = at.z;
        m.scale[0] = m.scale[1] = kModuleFill * pitch;
        m.scale[2] = kModuleDepth;
        std::copy(pose.quat.begin(), pose.quat.end(), m.quat);
        m.color[0] = led.r * strength;
        m.color[1] = led.g * strength;
        m.color[2] = led.b * strength;
        m.color[3] = 1.0f;
        m.material = shaderMaterial(view::Material::Glow, Cut::Plank, 0.0f);
        modules.push_back(m);
      }
  }
  return insertLab(set, LabShape::Box, modules);
}

void carryVr(InstanceSet& set, const std::vector<view::Placement>& placements,
             const std::vector<view::Vec3>& offset) {
  std::unordered_map<std::int64_t, std::vector<std::size_t>> grid;
  for (std::size_t k = 0; k < placements.size(); ++k)
    grid[gridKey(placements[k].x, placements[k].y)].push_back(k);
  const auto nearest = [&](const float* c) -> long {
    long best = -1;
    float bestD = std::numeric_limits<float>::max();
    const auto ix = std::lround(c[0]), iy = std::lround(c[1]);
    for (long dy = -1; dy <= 1; ++dy)
      for (long dx = -1; dx <= 1; ++dx) {
        const auto it =
            grid.find(gridKey(static_cast<float>(ix + dx), static_cast<float>(iy + dy)));
        if (it == grid.end()) continue;
        for (const std::size_t k : it->second) {
          const auto& p = placements[k];
          const float d = (p.x - c[0]) * (p.x - c[0]) + (p.y - c[1]) * (p.y - c[1]) +
                          (p.z - c[2]) * (p.z - c[2]);
          if (d < bestD) {
            bestD = d;
            best = static_cast<long>(p.cell);
          }
        }
      }
    return best;
  };
  const auto move = [&](Instance& i) {
    const long cell =
        i.fxCell > 0 ? static_cast<long>(i.fxCell) / 2 - 1 : nearest(i.center);
    if (cell < 0 || static_cast<std::size_t>(cell) >= offset.size()) return;
    const view::Vec3& d = offset[static_cast<std::size_t>(cell)];
    i.center[0] += d.x;
    i.center[1] += d.y;
    i.center[2] += d.z;
  };
  // The surface mesh's own identity instances are not a cell's (`eachBoardInstance`).
  std::vector<bool> surface;
  if (!set.surfaceVertices.empty()) {
    surface.assign(set.instances.size(), false);
    const auto mark = [&](std::uint32_t i) {
      if (i < surface.size()) surface[i] = true;
    };
    mark(set.surfaceInstance);
    for (const auto& d : set.surfaceDraws) mark(d.instance);
  }
  for (std::size_t k = 0; k < set.instances.size(); ++k)
    if (surface.empty() || !surface[k]) move(set.instances[k]);
  for (auto* list : {&set.shadows, &set.flourish})
    for (auto& i : *list) move(i);
  if (set.standbyLed) move(*set.standbyLed);
  // Surface vertices encode their own cell in the signed panel tag. Carry the mesh
  // once, leaving its identity draw instances fixed (as carryRelayout does).
  for (auto& v : set.surfaceVertices) {
    const float tag = std::abs(v.panelCoord[2]);
    if (tag < 3.0f) continue;
    const auto cell = static_cast<std::size_t>((tag - 1.0f) * 0.5f) - 1;
    if (cell >= offset.size()) continue;
    v.pos[0] += offset[cell].x;
    v.pos[1] += offset[cell].y;
    v.pos[2] += offset[cell].z;
  }
}

bool vrCarriesLayout(const BoardOptions& options, const VariantSpec& v) {
  return !(options.surface && !options.flat && hasPlaySurface(v));
}

std::size_t appendVrBridges(InstanceSet& set,
                            const std::vector<view::LightBridge>& bridges, float power,
                            const view::Theme& theme,
                            const std::optional<view::Vec3>& eye) {
  power = std::clamp(power, 0.0f, 1.0f);
  if (power <= 0.0f) return 0;
  const view::Rgba tile = vrLightColor(theme.boardDark, theme);
  const float cap = kVrBridgeShare * std::max({tile.r, tile.g, tile.b});
  std::vector<Instance> streaks;
  streaks.reserve(bridges.size() * kBridgeSegments);
  std::vector<float> depth;
  for (const auto& b : bridges) {
    const float peak = std::max({b.color.r, b.color.g, b.color.b, 1e-6f});
    for (int i = 0; i < kBridgeSegments; ++i) {
      const auto p = view::bridgePoint(b, static_cast<float>(i) / kBridgeSegments);
      const auto q = view::bridgePoint(b, static_cast<float>(i + 1) / kBridgeSegments);
      auto streak =
          cylinderBetween(p, q, kVrBridgeWidth * kBridgeCoreMetres * kCellsPerMetre);
      streak.color[0] = b.color.r / peak * cap * power;
      streak.color[1] = b.color.g / peak * cap * power;
      streak.color[2] = b.color.b / peak * cap * power;
      streak.color[3] = 1;
      streak.material = shaderMaterial(view::Material::Glow, Cut::Plank, 0);
      if (eye) depth.push_back(view::length((p + q) * 0.5f - *eye));
      streaks.push_back(streak);
    }
  }
  // Fading with depth, nearest streak to farthest.
  if (!depth.empty()) {
    const auto [lo, hi] = std::minmax_element(depth.begin(), depth.end());
    const float span = std::max(*hi - *lo, 1e-6f);
    for (std::size_t k = 0; k < streaks.size(); ++k) {
      const float f = 1.0f - (1.0f - kVrBridgeFar) * (depth[k] - *lo) / span;
      for (int c = 0; c < 3; ++c) streaks[k].color[c] *= f;
    }
  }
  return insertLab(set, LabShape::Box, streaks);
}

}  // namespace cb::render
