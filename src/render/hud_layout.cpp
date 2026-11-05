// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/hud_layout.hpp"

#include <algorithm>
#include <cmath>

namespace cb::render {
float autoUiFactor(int framebufferHeight) noexcept {
  return std::clamp(std::round(static_cast<float>(framebufferHeight) / 180.0f) * 0.25f,
                    1.0f, 3.0f);
}
float effectiveUiScale(float multiplier, bool automatic, int height, bool capture,
                       bool captureAuto, float exact) noexcept {
  if (exact > 0) return exact;
  return multiplier * ((capture ? captureAuto : automatic) ? autoUiFactor(height) : 1);
}
HudRect stageRect(HudRect vp, std::span<const StagePanel> panels, float gutter) noexcept {
  float left = vp.x, right = vp.x + vp.width, top = vp.y, bottom = vp.y + vp.height;
  for (const auto& p : panels) {
    const auto& r = p.rect;
    if (r.width <= 0 || r.height <= 0) continue;
    switch (p.edge) {
      case StageEdge::Left:
        left = std::max(left, r.x + r.width + gutter);
        break;
      case StageEdge::Right:
        right = std::min(right, r.x - gutter);
        break;
      case StageEdge::Top:
        top = std::max(top, r.y + r.height + gutter);
        break;
      case StageEdge::Bottom:
        bottom = std::min(bottom, r.y - gutter);
        break;
    }
  }
  return {left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top)};
}
DockLayout dockLayout(std::span<const float> widths, std::size_t viewStart,
                      float available, float height, float gap, float groupGap) {
  DockLayout l;
  available = std::max(1.0f, available);
  float x = 0, y = 0;
  for (std::size_t i = 0; i < widths.size(); ++i) {
    const float w = std::min(widths[i], available);
    const bool boundary = i > 0 && (i == viewStart || i + 1 == widths.size());
    const float spacing = i == 0 ? 0 : boundary ? groupGap + 1 : gap;
    if (x + spacing + w > available + .01f) {
      x = 0;
      y += height + gap;
      if (boundary) l.rules.push_back({0, y - gap * .5f, 0, 1});
    } else {
      if (boundary) l.rules.push_back({x + groupGap * .5f, y, 1, height});
      x += spacing;
    }
    l.buttons.push_back({x, y, w, height});
    x += w;
    l.width = std::max(l.width, x);
  }
  for (auto& rule : l.rules)
    if (rule.width == 0) rule.width = l.width;
  l.height = widths.empty() ? 0 : y + height;
  return l;
}
std::vector<CapturedGroup> groupCaptured(std::span<const Piece> pieces) {
  std::vector<CapturedGroup> out;
  for (const Piece p : pieces) {
    auto it = std::find_if(out.begin(), out.end(), [&](const auto& g) {
      return g.piece.type == p.type && g.piece.colorOf() == p.colorOf();
    });
    if (it == out.end())
      out.push_back({p, 1});
    else
      ++it->count;
  }
  return out;
}
float captureReveal(float age, float intensity) noexcept {
  return intensity < .5f ? 1.0f : std::clamp(age / .35f, 0.0f, 1.0f);
}
}  // namespace cb::render
