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
HudPoint resultPanelPos(HudRect stage, std::optional<HudPoint> king,
                        HudPoint size) noexcept {
  const float right = stage.x + std::max(0.0f, stage.width - size.x);
  const float bottom = stage.y + std::max(0.0f, stage.height - size.y);
  const HudPoint centre{(stage.x + right) * .5f, (stage.y + bottom) * .5f};
  if (!king) return centre;
  const auto clear = [&](HudPoint p) {
    return king->x < p.x - 24 || king->x > p.x + size.x + 24 || king->y < p.y - 24 ||
           king->y > p.y + size.y + 24;
  };
  if (clear(centre)) return centre;
  HudPoint best = centre;
  float distance = 1e30f;
  const HudPoint candidates[] = {
      {centre.x, std::clamp(king->y - size.y - 25, stage.y, bottom)},
      {centre.x, std::clamp(king->y + 25, stage.y, bottom)},
      {std::clamp(king->x - size.x - 25, stage.x, right), centre.y},
      {std::clamp(king->x + 25, stage.x, right), centre.y}};
  for (const auto p : candidates) {
    const float d = std::hypot(p.x - centre.x, p.y - centre.y);
    if (clear(p) && d < distance) {
      best = p;
      distance = d;
    }
  }
  return best;
}
ResultPanelFx resultPanelFx(float age, view::EndKind kind, int maxDistance,
                            float intensity) noexcept {
  const bool calm = intensity < .5f;
  const float duration = calm ? view::kCalmFadeSeconds
                         : kind == view::EndKind::Draw
                             ? view::kDrawDimSeconds
                             : static_cast<float>(std::max(maxDistance, 0)) /
                                       view::kPowerDownCellsPerSecond +
                                   view::kPowerDownFallSeconds;
  const float t = age - view::kEndDelaySeconds - duration;
  if (t < 0) return {};
  const float content = std::clamp((t - (calm ? 0 : .4f)) / .4f, 0.0f, 1.0f);
  return {calm ? 1 : std::clamp(t / .4f, 0.0f, 1.0f),
          content * content * (3 - 2 * content),
          calm ? content * content * (3 - 2 * content) : 1};
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
