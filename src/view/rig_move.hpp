// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "view/camera.hpp"
namespace cb::view {
inline constexpr float kRigLift = 0.05f;
inline constexpr float kRigSettle = 0.018f;
inline constexpr float kRigMinSeconds = 0.6f;
inline constexpr float kRigMaxSeconds = 1.1f;
/// Station camera at eased progress, with geometric distance, lift and shortest yaw.
/// Lens clipping, projection and roll belong to the destination.
[[nodiscard]] OrbitCamera blendCamera(const OrbitCamera& from, const OrbitCamera& to,
                                      float s);
/// Cubic ease-in-out with a small settle; endpoints are held exactly.
[[nodiscard]] float rigEase(float x) noexcept;
[[nodiscard]] float rigMoveSeconds(const OrbitCamera& from, const OrbitCamera& to,
                                   float metresPerUnit) noexcept;
/// Angular speed in rad/s, sampled across +/-5 ms, zero outside the move.
[[nodiscard]] float rigAngularSpeed(const OrbitCamera& from, const OrbitCamera& to,
                                    float age, float seconds) noexcept;
/// Flat two-dimensional sections of the existing layout, capped to the eight bays.
[[nodiscard]] int rackSlabCount(const DimSpec& dims);
/// Selected rack slots slide 60 mm over 250 ms; calm snaps to the target.
[[nodiscard]] float slabSlide(int slot, int count, float ageSinceChange,
                              float intensity) noexcept;
}  // namespace cb::view
