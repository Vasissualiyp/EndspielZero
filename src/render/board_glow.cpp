// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/board_glow.hpp"

#include <algorithm>

namespace cb::render {

BoardGlow projectBoardGlow(std::span<const std::array<float, 3>> points,
                           const view::Mat4& vp, GlowRect board, float targetW,
                           float targetH) {
  BoardGlow out;
  if (targetW <= 0.0f || targetH <= 0.0f) return out;
  if (board.width <= 0.0f || board.height <= 0.0f) board = {0, 0, targetW, targetH};
  float lo[2]{1e30f, 1e30f};
  float hi[2]{-1e30f, -1e30f};
  bool any = false;
  for (const auto& p : points) {
    const float cx = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12];
    const float cy = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13];
    const float cw = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15];
    if (cw <= 1e-4f) continue;
    const float px = board.x + (cx / cw * 0.5f + 0.5f) * board.width;
    const float py = board.y + (cy / cw * 0.5f + 0.5f) * board.height;
    const float u = px / targetW;
    const float v = py / targetH;
    lo[0] = std::min(lo[0], u);
    hi[0] = std::max(hi[0], u);
    lo[1] = std::min(lo[1], v);
    hi[1] = std::max(hi[1], v);
    any = true;
  }
  if (!any) return out;
  out.valid = true;
  out.cx = 0.5f * (lo[0] + hi[0]);
  out.cy = 0.5f * (lo[1] + hi[1]);
  out.hx = std::max(0.5f * (hi[0] - lo[0]), 0.02f);
  out.hy = std::max(0.5f * (hi[1] - lo[1]), 0.02f);
  return out;
}

}  // namespace cb::render
