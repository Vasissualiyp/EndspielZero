// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace cb::render {
inline constexpr float kPostShoulder = .8f;
inline constexpr float kPostVignette = .45f;
inline constexpr std::uint32_t kMaxBloomLevels = 6;

[[nodiscard]] float toneMap(float x) noexcept;
[[nodiscard]] std::uint32_t bloomLevelCount(std::uint32_t width,
                                           std::uint32_t height) noexcept;
[[nodiscard]] std::uint32_t postFrameSeed(double seconds) noexcept;
[[nodiscard]] float postDither(std::uint32_t x, std::uint32_t y,
                                std::uint32_t seed) noexcept;
[[nodiscard]] float postVignette(float u, float v) noexcept;
}  // namespace cb::render
