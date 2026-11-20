// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <span>
#include <string_view>
#include "view/rig_deploy.hpp"
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
/// At most 12 lights: the eight authored ones plus the game's slab glows (M24.5).
inline constexpr std::size_t kMaxLabLights = 12;
/// std140 uniform block: softCos is packed as vec4[3] in GLSL.
struct GpuLightBlock {
  float header[4];  ///< count, key gain, metres per world unit, unused
  float softCos[kMaxLabLights];
  GpuLight lights[kMaxLabLights];
  float implant[4];
};
static_assert(sizeof(GpuLightBlock) == 16 + 48 + 12 * 48 + 16);
static_assert(offsetof(GpuLightBlock, lights) == 64);
static_assert(offsetof(GpuLightBlock, implant) == 640);
/// Finite inverse-square falloff, exactly zero at and beyond radius (metres).
[[nodiscard]] float lightFalloff(float dm, float radius) noexcept;
/// Smooth cone edge; equal bounds give a hard cut-off (soft = 0).
[[nodiscard]] float spotFactor(float cosAngle, float cosCone, float cosSoft) noexcept;
[[nodiscard]] GpuLightBlock labLightBlock(
    const io::LabDoc& doc, const LabScene& scene, float roomLight, float keyFactor,
    std::string_view lightName = {}, float intensityScale = 1,
    const view::Theme& theme = view::Theme::manifold(), float wallProjectorScale = 1,
    float roomGroupScale = 1, const view::RigXform* xform = nullptr,
    std::span<const GpuLight> extra = {});
[[nodiscard]] GpuLightBlock neutralLightBlock() noexcept;
}  // namespace cb::render
