// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/post_params.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace cb::render {
namespace {
std::uint32_t hash(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}
float unit(std::uint32_t x) { return static_cast<float>(x >> 8) / 16777216.0f; }
}

float toneMap(float x) noexcept {
  if (x <= kPostShoulder) return x;
  return kPostShoulder + (1 - kPostShoulder) *
                            (1 - std::exp(-(x - kPostShoulder) / (1 - kPostShoulder)));
}

std::uint32_t bloomLevelCount(std::uint32_t width, std::uint32_t height) noexcept {
  const int log = static_cast<int>(std::bit_width(std::min(width, height))) - 1;
  return static_cast<std::uint32_t>(std::clamp(log - 3, 3, 6));
}

std::uint32_t postFrameSeed(double seconds) noexcept {
  if (!std::isfinite(seconds) || seconds < 0) return 0;
  // Integer 24-Hz ticks, reduced before conversion; safe even on very long captures.
  return static_cast<std::uint32_t>(std::floor(std::fmod(seconds, 16777216.0) * 24));
}

float postDither(std::uint32_t x, std::uint32_t y, std::uint32_t seed) noexcept {
  const auto h = hash(x * 0x1f3d5b79u ^ y * 0x9e3779b1u ^ seed * 0x85ebca6bu);
  return (unit(hash(h ^ 0x68bc21ebu)) - unit(hash(h ^ 0x31415926u))) / 255;
}

float postVignette(float u, float v) noexcept {
  const float edge = std::max(std::abs(u - .5f), std::abs(v - .5f)) * 2;
  return std::clamp(1 - kPostVignette * std::pow(edge, 2.6f), 0.0f, 1.0f);
}
}  // namespace cb::render
