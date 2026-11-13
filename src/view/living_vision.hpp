// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include "view/camera.hpp"

namespace cb::view {
// There is no 2-D vector in view; keep this pure API independent of ImGui.
using Vec2f = std::array<float, 2>;
inline constexpr float kLensWarpK = .030f;
inline constexpr float kTremorPixels = .80f;
inline constexpr float kPeripheryStart = .28f, kPeripheryFull = .95f;
inline constexpr float kPeripheryBlurPixels = 1.4f;
inline constexpr float kPeripheryDesaturate = .25f;
inline constexpr float kPeripheryFringePixels = .8f;
inline constexpr float kWorldDefocusPixels = 2.4f, kUiDefocusPixels = 1.2f;
inline constexpr float kUiAwayAlpha = .75f;
inline constexpr float kAccommodationSeconds = .22f, kSaccadeSeconds = .045f;
inline constexpr float kIdleBeforeWander = 2.5f;
inline constexpr float kFixationMin = 1.2f, kFixationMax = 3.2f;
inline constexpr float kEyeShiftFraction = .10f, kEyeShiftMaxRadians = .014f;
inline constexpr float kDriftRadians = .0009f;
inline constexpr float kMenuFocusDistanceMetres = .45f;
[[nodiscard]] Vec2f lensWarpSource(Vec2f px, Vec2f size, float k) noexcept;
[[nodiscard]] Vec2f tremorOffset(double seconds, float intensity, std::uint32_t seed) noexcept;
[[nodiscard]] float eccentricity(Vec2f px, Vec2f gazePx, Vec2f size) noexcept;
[[nodiscard]] float peripheryAmount(float eccentricity) noexcept;
struct GazeTarget {
  std::string_view key;
  Vec3 world{};
  float weight{0};
};
struct GazeInput {
  float dt{0};
  double time{0};
  Vec2f mousePx{};
  bool mouseMoved{false}, mouseOverPanel{false}, dragging{false};
  bool active{false}, wander{true};
  float intensity{1};
  Vec2f viewport{}, panelCentrePx{};
  std::span<const GazeTarget> targets;
  std::function<std::optional<std::pair<Vec2f, float>>(Vec3)> project;
  float defaultDistance{1}, fovY{.9f};
  std::uint32_t seed{0};
};
struct GazeState {
  Vec2f pointPx{}, fromPx{}, toPx{};
  float saccadeT{1}, uiFocus{1}, uiFocusTarget{1};
  float focusDistance{1}, focusDistanceTarget{1};
  float idle{0}, fixationLeft{0}, outsidePanel{0};
  std::uint32_t fixationCount{0};
  int lastTarget{-1};
  Vec2f eyeShift{}, eyeShiftTarget{}, eyeShiftFrom{};
};
[[nodiscard]] GazeState stepGaze(const GazeState& s, const GazeInput& in) noexcept;
/// The yaw/pitch (radians) the camera shifts when the eye looks at pixel `px`: a tenth of
/// the angle from the viewport centre, capped at kEyeShiftMaxRadians.
[[nodiscard]] Vec2f eyeShiftToward(Vec2f px, Vec2f viewport, float fovY) noexcept;
[[nodiscard]] float circleOfConfusion(float z, float focus, float cocScale) noexcept;
[[nodiscard]] float cocScaleFor(float cellsPerMetre) noexcept;
[[nodiscard]] float linearDepth(float depth, float nearZ, float farZ) noexcept;
}  // namespace cb::view
