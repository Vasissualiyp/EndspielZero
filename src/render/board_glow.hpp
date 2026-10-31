// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "view/camera.hpp"

namespace cb::render {

/// Where the board lands on screen, in uv of the whole render target (origin top left):
/// the centre and half-size of its projected bounding box. The backdrop's light is
/// centred on this and sized by it, so the room is lit by the board (M23.9 B). Pure.
struct BoardGlow {
  bool valid{false};
  float cx{0.5f}, cy{0.45f};
  float hx{0.3f}, hy{0.3f};
};

/// The sub-rectangle of the target the board is drawn into, in pixels; zero width = all.
struct GlowRect {
  float x{0}, y{0}, width{0}, height{0};
};

/// Project world points through `viewProj` and fit them into the board's rectangle of a
/// `targetW` x `targetH` target. Points behind the camera are skipped; with none in front
/// the result is invalid. A strided subsample is enough, so the caller may pass up to a
/// few thousand points regardless of board size.
[[nodiscard]] BoardGlow projectBoardGlow(std::span<const std::array<float, 3>> points,
                                         const view::Mat4& viewProj, GlowRect board,
                                         float targetW, float targetH);

}  // namespace cb::render
