// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <string_view>
#include "view/theme.hpp"
namespace cb::io {
struct LabDoc;
}
namespace cb::render {
class LabScene;
inline constexpr float kLabKey = .12f;
/// Three vec4s, matching the shader light record. Radius stays in metres.
struct GpuLight {
  float posRadius[4];
  float colorIntensity[4];
  float dirCone[4];
};
static_assert(sizeof(GpuLight) == 48);
/// std140 uniform block: softCos is packed as vec4[2] in GLSL.
struct GpuLightBlock {
  float header[4];  ///< count, key gain, metres per world unit, unused
  float softCos[8];
  GpuLight lights[8];
  float implant[4];
};
static_assert(sizeof(GpuLightBlock) == 16 + 32 + 8 * 48 + 16);
static_assert(offsetof(GpuLightBlock, lights) == 48);
static_assert(offsetof(GpuLightBlock, implant) == 432);
/// Finite inverse-square falloff, exactly zero at and beyond radius (metres).
[[nodiscard]] float lightFalloff(float dm, float radius) noexcept;
/// Smooth cone edge; equal bounds give a hard cut-off (soft = 0).
[[nodiscard]] float spotFactor(float cosAngle, float cosCone, float cosSoft) noexcept;
[[nodiscard]] GpuLightBlock labLightBlock(
    const io::LabDoc& doc, const LabScene& scene, float roomLight, float keyFactor,
    std::string_view lightName = {}, float intensityScale = 1,
    const view::Theme& theme = view::Theme::manifold());
[[nodiscard]] GpuLightBlock neutralLightBlock() noexcept;
}  // namespace cb::render
