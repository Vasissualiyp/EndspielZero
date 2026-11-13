// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/rig_move.hpp"
#include <numbers>
namespace cb::view {
namespace {
constexpr float pi = std::numbers::pi_v<float>;
float yawDelta(const OrbitCamera& a, const OrbitCamera& b) {
  return std::remainder(b.yaw - a.yaw, 2 * pi);
}
}  // namespace
OrbitCamera blendCamera(const OrbitCamera& from, const OrbitCamera& to, float s) {
  auto out = to;
  out.target = from.target + (to.target - from.target) * s;
  out.distance = std::exp(std::lerp(std::log(from.distance), std::log(to.distance), s)) +
                 kRigLift * .5f * (from.distance + to.distance) *
                     std::sin(pi * std::clamp(s, 0.f, 1.f));
  out.yaw = from.yaw + yawDelta(from, to) * s;
  out.pitch = std::lerp(from.pitch, to.pitch, s);
  out.fovY = std::lerp(from.fovY, to.fovY, s);
  // Avoid a residual sine/log roundoff at the held endpoints.
  if (s == 0) out.distance = from.distance;
  if (s == 1) out.distance = to.distance;
  return out;
}
float rigEase(float x) noexcept {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  const float v = -2 * x + 2;
  const float c = x < .5f ? 4 * x * x * x : 1 - v * v * v / 2;
  return c + kRigSettle * std::sin(pi * std::clamp((x - .72f) / .28f, 0.f, 1.f));
}
float rigMoveSeconds(const OrbitCamera& from, const OrbitCamera& to,
                     float metresPerUnit) noexcept {
  const float ang = std::abs(yawDelta(from, to)) + std::abs(to.pitch - from.pitch);
  const float far = length(to.target - from.target) * metresPerUnit;
  return std::clamp(.55f + .25f * ang + .08f * far, kRigMinSeconds, kRigMaxSeconds);
}
float rigAngularSpeed(const OrbitCamera& from, const OrbitCamera& to, float age,
                      float seconds) noexcept {
  if (!(seconds > 0) || age < 0 || age >= seconds ||
      (yawDelta(from, to) == 0 && from.pitch == to.pitch))
    return 0;
  const auto direction = [&](float t) {
    const auto cam =
        blendCamera(from, to, rigEase(std::clamp(t, 0.f, seconds) / seconds));
    return normalize(cam.target - cam.eye());
  };
  return std::acos(
             std::clamp(dot(direction(age - .005f), direction(age + .005f)), -1.f, 1.f)) /
         .01f;
}
int rackSlabCount(const DimSpec& dims) {
  const auto cfg = ViewConfig::forBoard(dims);
  int count = 1;
  for (std::size_t i = 2; i < cfg.screenAxes.size(); ++i)
    count = std::min(8, count * dims.extent(cfg.screenAxes[i]));
  for (const auto axis : cfg.gridAxes) count = std::min(8, count * dims.extent(axis));
  return count;
}
float slabSlide(int slot, int count, float ageSinceChange, float intensity) noexcept {
  if (slot < 0 || slot >= count) return 0;
  return .06f * (intensity < .5f ? 1.f : rigEase(ageSinceChange / .25f));
}
}  // namespace cb::view
