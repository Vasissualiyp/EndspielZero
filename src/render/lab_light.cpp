// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/lab_light.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include "render/lab_scene.hpp"
namespace cb::render {
float lightFalloff(float dm, float radius) noexcept {
  const float x = dm / radius;
  const float win = std::clamp(1 - x * x * x * x, 0.f, 1.f);
  return win * win / (dm * dm + .25f);
}
float spotFactor(float cosAngle, float cosCone, float cosSoft) noexcept {
  if (cosCone == cosSoft) return cosAngle >= cosCone ? 1.f : 0.f;
  const float t = std::clamp((cosAngle - cosSoft) / (cosCone - cosSoft), 0.f, 1.f);
  return t * t * (3 - 2 * t);
}
GpuLightBlock neutralLightBlock() noexcept {
  GpuLightBlock b{};
  b.header[1] = 1;
  b.header[2] = 1 / kCellsPerMetre;
  return b;
}
GpuLightBlock labLightBlock(const io::LabDoc& doc, const LabScene& scene, float roomLight,
                            float keyFactor, std::string_view lightName,
                            float intensityScale, const view::Theme& theme) {
  auto b = neutralLightBlock();
  const auto count = std::min<std::size_t>(doc.lights.size(), 8);
  const auto implant = theme.implant.array();
  std::copy(implant.begin(), implant.end(), b.implant);
  b.header[0] = static_cast<float>(count);
  b.header[1] = keyFactor;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& l = doc.lights[i];
    auto& out = b.lights[i];
    const auto p = scene.toWorld(l.pos);
    out.posRadius[0] = p.x;
    out.posRadius[1] = p.y;
    out.posRadius[2] = p.z;
    out.posRadius[3] = l.radius;
    out.colorIntensity[0] = l.color.r;
    out.colorIntensity[1] = l.color.g;
    out.colorIntensity[2] = l.color.b;
    out.colorIntensity[3] = l.intensity * (l.group == "room" ? roomLight : 1) *
                            (l.name == lightName ? intensityScale : 1);
    out.dirCone[3] = -2;
    if (l.kind == io::LabLight::Kind::Spot) {
      const float length = std::hypot(l.dir[0], l.dir[1], l.dir[2]);
      for (std::size_t a = 0; a < 3; ++a) out.dirCone[a] = l.dir[a] / length;
      constexpr float rad = std::numbers::pi_v<float> / 180;
      out.dirCone[3] = std::cos(l.cone * rad);
      b.softCos[i] = std::cos((l.cone + l.soft) * rad);
    }
  }
  return b;
}
}  // namespace cb::render
