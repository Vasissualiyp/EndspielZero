// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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
// Calm eyes (M24.4d): the camera never follows the gaze; after kIdleBeforeWander seconds
// without input the viewport defocuses and a small sharp spot drifts between fixations.
inline constexpr float kIdleBeforeWander = 10.f;
inline constexpr float kFixationMin = 2.f, kFixationMax = 5.f;
inline constexpr float kDaydreamRiseSeconds = 1.5f, kDaydreamWakeSeconds = .3f;
inline constexpr float kDaydreamBlurPixels = 6.f;  ///< circle of confusion at 1080p
inline constexpr float kDaydreamUiSoft = .8f;      ///< how far the panel mixes to uiSoft
inline constexpr float kSpotRadius = .12f, kSpotFeather = .06f;  ///< fractions of the height
inline constexpr float kSpotBlurFraction = .25f;
inline constexpr float kRandomFixationChance = .35f;
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
  bool input{false};  ///< a click or key press this frame (mouse motion is `mouseMoved`)
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
  int lastTarget{-1};  ///< -1 after a made-up point rather than a target
  float daydream{0}, daydreamT{0};  ///< eased amount 0..1, and its linear progress
};
[[nodiscard]] GazeState stepGaze(const GazeState& s, const GazeInput& in) noexcept;
/// Mirrors the room pass and the panel mix: the uniform daydream circle of confusion at
/// `px`, falling to kSpotBlurFraction of it inside the focus spot at `spot`.
[[nodiscard]] float daydreamBlurPixels(float daydream, Vec2f px, Vec2f spot, float height,
                                       float pixelScale) noexcept;
[[nodiscard]] float circleOfConfusion(float z, float focus, float cocScale) noexcept;
[[nodiscard]] float cocScaleFor(float cellsPerMetre) noexcept;
[[nodiscard]] float linearDepth(float depth, float nearZ, float farZ) noexcept;
}  // namespace cb::view
