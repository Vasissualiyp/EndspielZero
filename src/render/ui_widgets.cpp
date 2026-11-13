// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ui_widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include <imgui_internal.h>

#include "audio/audio.hpp"

namespace cb::render::widgets {
float trackedText(ImDrawList* dl, ImVec2 pos, const char* text, float tracking) {
  const float start = pos.x;
  ImFont* font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  const ImU32 colour = ImGui::GetColorU32(ImGuiCol_Text);
  for (const char* p = text; *p;) {
    unsigned int codepoint = 0;
    const int bytes = ImTextCharFromUtf8(&codepoint, p, nullptr);
    if (bytes <= 0) break;
    dl->AddText(font, size, pos, colour, p, p + bytes);
    const ImFontGlyph* glyph = font->FindGlyph(static_cast<ImWchar>(codepoint));
    pos.x += (glyph ? glyph->AdvanceX : font->FallbackAdvanceX) * size / font->FontSize;
    p += bytes;
    if (*p) pos.x += tracking;
  }
  return pos.x - start;
}

namespace {

struct VisualInput {
  ImGuiContext* context{nullptr};
  double seconds{0};
};
thread_local VisualInput visualInput;

/// Widget chrome is sized from the current font, which the interface already scales for
/// the interface-scale option. A control therefore keeps its proportions at any scale
/// without every call site having to thread yet another number through.
float uiScale() {
  return std::max(0.5f, ImGui::GetFontSize() / 15.0f);
}

/// A vertical two-tone fill with rounded corners. Dear ImGui's rounded fill takes one
/// colour and its multi-colour fill takes no rounding, so a card needs both: a rounded
/// base, then a rounded-topped cap over the upper part.
void gradientRect(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 top,
                  ImU32 bottom, float r) {
  dl->AddRectFilled(a, b, bottom, r);
  const float mid = a.y + (b.y - a.y) * 0.55f;
  dl->AddRectFilled(a, ImVec2(b.x, mid), top, r, ImDrawFlags_RoundCornersTop);
}

/// One card. Drawn rather than styled, because Dear ImGui will not give a window both a
/// thick dark outline and a bright inner rim.
void drawPlate(ImDrawList* dl, const ImVec2& pmin, const ImVec2& pmax,
               const view::Theme& t, float s) {
  if (t.panelStyle == view::PanelStyle::Brackets) {
    panelCorners(dl, pmin, pmax, t, s);
    return;
  }
  const float r = 10.0f * s;
  gradientRect(dl, pmin, pmax, u32(t.panelHi), u32(t.panel), r);
  // A deep outline first, then a warm rim just inside it: the brass edging a game card
  // has, and the reason a plain ImGui window reads as a spreadsheet by comparison.
  dl->AddRect(pmin, pmax, u32(t.ink), r, 0, 3.0f * s);
  dl->AddRect(ImVec2(pmin.x + 2.5f * s, pmin.y + 2.5f * s),
              ImVec2(pmax.x - 2.5f * s, pmax.y - 2.5f * s), u32(t.emberDeep, 0.55f),
              r * 0.8f, 0, 1.4f * s);
  dl->AddLine(ImVec2(pmin.x + r * 0.9f, pmin.y + 4.0f * s),
              ImVec2(pmax.x - r * 0.9f, pmin.y + 4.0f * s), u32(t.bone, 0.10f), 1.0f * s);

  // Corner rivets set into the margin, so the plate looks fastened down.
  const float rr = 2.0f * s;
  const float in = 10.0f * s;
  const ImVec2 rivets[4] = {{pmin.x + in, pmin.y + in},
                            {pmax.x - in, pmin.y + in},
                            {pmin.x + in, pmax.y - in},
                            {pmax.x - in, pmax.y - in}};
  for (const ImVec2& c : rivets) dl->AddCircleFilled(c, rr, u32(t.ember, 0.7f));
}

}  // namespace

bool& framelessControls() {
  static bool on = false;
  return on;
}

ScopedVisualTime::ScopedVisualTime(double seconds) noexcept
    : previousContext_(visualInput.context), previousSeconds_(visualInput.seconds) {
  visualInput = {ImGui::GetCurrentContext(),
                 std::isfinite(seconds) && seconds >= 0 ? seconds : 0};
}
ScopedVisualTime::~ScopedVisualTime() {
  visualInput = {previousContext_, previousSeconds_};
}

void panelCorners(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                  float scale) {
  const float width = max.x - min.x;
  const float height = max.y - min.y;
  if (width <= 0.0f || height <= 0.0f) return;
  const float length = std::min({14.0f * scale, width * 0.2f, height * 0.2f});
  const ImU32 colour = u32(theme.ember, 0.40f);
  for (const float x : {min.x, max.x}) {
    for (const float y : {min.y, max.y}) {
      const float dx = x == min.x ? length : -length;
      const float dy = y == min.y ? length : -length;
      dl->AddLine(ImVec2(x + dx, y), ImVec2(x, y), colour, scale);
      dl->AddLine(ImVec2(x, y), ImVec2(x, y + dy), colour, scale);
    }
  }
}

void hudPanel(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
              float scale, float draw, float level, float corner) {
  const float width = max.x - min.x;
  const float height = max.y - min.y;
  if (width <= 0.0f || height <= 0.0f || draw <= 0.0f) return;
  // The implant is light, not a display: no fill, frame, bracket or raster (BCI brief 2).
  if (theme.implantFrameless) return;
  const bool led = theme.panelStyle == view::PanelStyle::Brackets;
  const float cut = std::min({6.0f * scale * corner, width * 0.25f, height * 0.25f});
  const ImVec2 outline[8] = {{min.x + cut, min.y}, {max.x - cut, min.y},
                             {max.x, min.y + cut}, {max.x, max.y - cut},
                             {max.x - cut, max.y}, {min.x + cut, max.y},
                             {min.x, max.y - cut}, {min.x, min.y + cut}};
  // Dark glass over the stage in the LED theme; the plate theme's own panel colour.
  const view::Rgba fill = led ? theme.ink : theme.panel;
  dl->AddConvexPolyFilled(outline, 8, u32(fill, (led ? 0.55f : 0.80f) * level * draw));
  const view::Rgba edge = led ? theme.ember : theme.emberDeep;
  const ImU32 edgeColour = u32(edge, (led ? 0.35f : 0.80f) * level);
  if (draw >= 1.0f) {
    dl->AddPolyline(outline, 8, edgeColour, ImDrawFlags_Closed, std::max(1.0f, scale));
  } else {
    // Trace the perimeter on, from the top-left corner, clockwise.
    float total = 0.0f;
    float lengths[8];
    for (int i = 0; i < 8; ++i) {
      const ImVec2 a = outline[i], b = outline[(i + 1) % 8];
      lengths[i] = std::hypot(b.x - a.x, b.y - a.y);
      total += lengths[i];
    }
    float left = draw * total;
    for (int i = 0; i < 8 && left > 0.0f; ++i) {
      const ImVec2 a = outline[i], b = outline[(i + 1) % 8];
      const float f = std::min(1.0f, left / std::max(lengths[i], 1e-3f));
      dl->AddLine(a, ImVec2(a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f), edgeColour,
                  std::max(1.0f, scale));
      left -= lengths[i];
    }
  }
  if (led && draw >= 1.0f) {
    // Faint raster inside the glass: one pixel line every three.
    const float step = std::max(3.0f, 3.0f * scale);
    const ImU32 line = u32(theme.ember, 0.04f * level);
    for (float y = min.y + cut; y < max.y - cut; y += step) {
      dl->AddLine(ImVec2(min.x + 1.0f, y), ImVec2(max.x - 1.0f, y), line, 1.0f);
    }
  }
  if (draw >= 1.0f) {
    const float arm = std::min({10.0f * scale * corner, width * 0.3f, height * 0.3f});
    const ImU32 bracket = u32(led ? theme.ember : theme.emberDeep, level);
    for (const float x : {min.x, max.x}) {
      for (const float y : {min.y, max.y}) {
        const float dx = x == min.x ? arm : -arm;
        const float dy = y == min.y ? arm : -arm;
        dl->AddLine(ImVec2(x + dx, y), ImVec2(x, y), bracket, std::max(1.0f, scale));
        dl->AddLine(ImVec2(x, y), ImVec2(x, y + dy), bracket, std::max(1.0f, scale));
      }
    }
  }
}

view::Rgba mix(const view::Rgba& a, const view::Rgba& b, float t) {
  return view::Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                    a.a + (b.a - a.a) * t};
}

view::Rgba shade(const view::Rgba& c, float factor) {
  return view::Rgba{c.r * factor, c.g * factor, c.b * factor, c.a};
}

void heading(const char* text, const view::Theme& theme, ImFont* font) {
  if (font != nullptr) ImGui::PushFont(font);
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.ember));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  if (font != nullptr) ImGui::PopFont();

  const ImVec2 max = ImGui::GetItemRectMax();
  const ImVec2 min = ImGui::GetItemRectMin();
  const float y = (min.y + max.y) * 0.5f;
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  if (right > max.x + 8.0f) {
    ImGui::GetWindowDrawList()->AddLine(ImVec2(max.x + 8.0f, y), ImVec2(right, y),
                                        u32(theme.emberDeep, 0.5f), 1.5f);
  }
  ImGui::Dummy(ImVec2(0, 2));
}

void keyValue(const char* key, const std::string& value, const view::Theme& theme,
              bool cold) {
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneDim));
  ImGui::TextUnformatted(key);
  ImGui::PopStyleColor();
  ImGui::SameLine();
  const float avail = ImGui::GetContentRegionAvail().x;
  const float textWidth = ImGui::CalcTextSize(value.c_str()).x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, avail - textWidth));
  ImGui::PushStyleColor(ImGuiCol_Text, col(cold ? theme.rift : theme.bone));
  ImGui::TextUnformatted(value.c_str());
  ImGui::PopStyleColor();
}

namespace {

struct ControlLayout {
  ImFont* font{};
  std::string caption, tail;
  float width{}, height{}, fontSize{}, tailSize{}, wrap{}, tailWrap{};
  ImVec2 label{}, value{}, number{};
  bool showNumber{}, showTail{};
};

ControlLayout measureControl(const char* label, const char* tail, ImFont* font,
                             float width, float height, float s, int index,
                             bool compact = false) {
  ControlLayout l;
  l.font = font ? font : ImGui::GetFont();
  l.fontSize = font ? font->FontSize : ImGui::GetFontSize();
  l.caption = upper(label);
  l.tail = tail ? upper(tail) : "";
  l.tailSize = index >= 0 ? l.fontSize * 0.58f : l.fontSize;
  const auto measure = [&](const std::string& text, float size, float wrap = 0) {
    return l.font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
  };
  const float textWidth = measure(l.caption, l.fontSize).x;
  const float valueWidth = measure(l.tail, l.tailSize).x;
  // Auto-fit windows measure their contents; their previous width cannot constrain
  // those same contents without a frame-history feedback loop (notably the HUD).
  const auto* window = ImGui::GetCurrentWindow();
  const auto* viewport = ImGui::GetMainViewport();
  const float room =
      (window->Flags & ImGuiWindowFlags_AlwaysAutoResize)
          ? viewport->WorkPos.x + viewport->WorkSize.x - ImGui::GetCursorScreenPos().x
          : ImGui::GetContentRegionAvail().x;
  const float available = std::max(1.0f, room);
  const float natural = textWidth + 24 * s + (tail ? valueWidth + 12 * s : 0);
  l.width = std::max(1.0f, std::min(width > 0 ? width : natural, available));
  l.showNumber = index >= 0;
  l.showTail = tail != nullptr;
  float left = l.showNumber ? 36 * s : 12 * s;
  if (index >= 0 && textWidth + left + 12 * s + valueWidth + 12 * s > l.width) {
    l.showNumber = false;
    left = 12 * s;
    if (l.tail == ">") l.showTail = false;
  }
  const bool below = l.showTail && textWidth + left + 24 * s + valueWidth > l.width;
  l.wrap = std::max(
      1.0f, l.width - left - 12 * s - (l.showTail && !below ? valueWidth + 12 * s : 0));
  const ImVec2 labelSize = measure(l.caption, l.fontSize, l.wrap);
  l.tailWrap = std::max(1.0f, l.width - 24 * s);
  const ImVec2 tailSize = measure(l.tail, l.tailSize, l.tailWrap);
  const float contentH = labelSize.y + (below ? 4 * s + tailSize.y : 0);
  l.height = std::ceil(std::max(height, contentH + (compact ? 4 : 16) * s));
  l.label = ImVec2(left, (l.height - contentH) * 0.5f);
  l.number = ImVec2(6 * s, (l.height - l.tailSize) * 0.5f);
  l.value = below ? ImVec2(12 * s, l.label.y + labelSize.y + 4 * s)
                  : ImVec2(l.width - 12 * s - valueWidth, (l.height - l.tailSize) * 0.5f);
  return l;
}

std::uint32_t controlHash(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}

void drawControlGlitch(ImDrawList* dl, ImVec2 min, ImVec2 max, const ControlLayout& l,
                       const view::Theme& theme, float s) {
  const ImGuiID id = ImGui::GetItemID();
  const double seconds = visualInput.context == ImGui::GetCurrentContext()
                             ? std::fmod(visualInput.seconds, 120.0)
                             : 0;
  const double offset = static_cast<double>(controlHash(id)) / 4294967296.0 * 1.2;
  const double phase = std::fmod(seconds + offset, 1.2);
  const auto smooth = [](double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
  };
  const auto pulse = [&](double start, double end) {
    return smooth((phase - start) / 0.025) * smooth((end - phase) / 0.025);
  };
  const float burst = static_cast<float>(std::max(pulse(0, 0.16), pulse(0.30, 0.38)));
  const auto step = static_cast<std::uint32_t>(std::floor((seconds + offset) * 8));
  const auto seed = controlHash(id ^ controlHash(step));
  const auto tint = mix(theme.bone, theme.ember, 0.20f);
  const ImVec2 text(min.x + l.label.x, min.y + l.label.y);
  const float labelH =
      l.font->CalcTextSizeA(l.fontSize, FLT_MAX, l.wrap, l.caption.c_str()).y;
  const auto lines = std::max(1u, static_cast<unsigned>(std::round(labelH / l.fontSize)));
  const float lineY = text.y + static_cast<float>(seed % lines) * l.fontSize;
  const float thickness = std::min(2 * s, l.fontSize * 0.12f);
  for (unsigned i = 0; i < 2; ++i) {
    const auto noise = controlHash(seed + i * 0x9e3779b9u);
    const float direction = (noise & 1u) ? 1.0f : -1.0f;
    const float dx =
        direction * (1 + burst * static_cast<float>(1u + (noise >> 1) % 2u)) * s;
    const float y = lineY + (i == 0 ? 0.30f : 0.70f) * l.fontSize;
    dl->PushClipRect(ImVec2(text.x - 3 * s, y),
                     ImVec2(text.x + l.wrap + 3 * s, y + thickness), true);
    const ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
    const ImVec4 clip(clipMin.x, clipMin.y, clipMax.x, clipMax.y);
    dl->AddText(l.font, l.fontSize, ImVec2(text.x + dx, text.y),
                u32(i == 0 ? tint : theme.boneDim,
                    (0.16f + 0.22f * burst) * (i == 0 ? 1.0f : 0.65f)),
                l.caption.c_str(), nullptr, l.wrap, &clip);
    dl->PopClipRect();
    const float length =
        std::min((8 + static_cast<float>((noise >> 4) % 15u)) * s, l.width * 0.25f);
    const float x = i == 0 ? min.x + 8 * s : max.x - length - 8 * s;
    const float edgeY = i == 0 ? min.y + 4 * s : max.y - 4 * s;
    dl->AddRectFilled(ImVec2(x + dx, edgeY), ImVec2(x + dx + length, edgeY + s),
                      u32(tint, 0.25f + 0.45f * burst));
    dl->AddLine(ImVec2(x - dx, edgeY + 2 * s),
                ImVec2(x - dx + length * 0.45f, edgeY + 2 * s),
                u32(theme.boneDim, 0.20f + 0.35f * burst), s);
  }
}

void drawLiveControl(ImDrawList* dl, ImVec2 min, ImVec2 max, const ControlLayout& l,
                     const view::Theme& theme, float s, int index, bool usable,
                     bool primary, bool cold, bool hovered, bool focused, bool held) {
  const bool hot = hovered || focused;
  const auto accent = mix(theme.bone, theme.ember, 0.20f);
  const auto edge = hot ? theme.boneDim : theme.interfaceRule;
  if (theme.implantFrameless) {
    // Selection is brightness plus a soft wash: nested translucent rounded rects stand in
    // for a 10 px feather, so no edge is visible. The core is full `implant`, the rest dim.
    const float level = held ? 1.0f : hot ? 0.9f : 0.0f;
    if (level > 0.0f) {
      constexpr int layers = 5;
      for (int i = 0; i < layers; ++i) {
        const float grow = static_cast<float>(layers - 1 - i) * 2.5f * s;
        dl->AddRectFilled(ImVec2(min.x - grow, min.y - grow), ImVec2(max.x + grow, max.y + grow),
                          u32(theme.implantDeep, .30f * level / static_cast<float>(layers)), 12 * s + grow);
      }
    }
    const ImVec2 text(min.x + l.label.x, min.y + l.label.y);
    if (l.showNumber) {
      char number[4]{};
      std::snprintf(number, sizeof(number), "%02d", std::clamp(index, 0, 99));
      dl->AddText(l.font, l.tailSize, ImVec2(min.x + l.number.x, min.y + l.number.y),
                  u32(theme.implantFaint), number);
    }
    const auto labelColour = !usable ? theme.implantFaint
                             : cold  ? theme.rift
                             : (hot || primary || held) ? theme.implant
                                                         : theme.implantDim;
    dl->AddText(l.font, l.fontSize, text, u32(labelColour), l.caption.c_str(), nullptr, l.wrap);
    if (l.showTail)
      dl->AddText(l.font, l.tailSize, ImVec2(min.x + l.value.x, min.y + l.value.y),
                  u32(usable ? theme.implantDim : theme.implantFaint), l.tail.c_str(), nullptr,
                  l.tailWrap);
    return;
  }
  dl->PushClipRect(min, max, true);
  const float cut = std::min({6 * s, (max.y - min.y) * 0.15f, (max.x - min.x) * 0.15f});
  const ImVec2 plate[] = {min, ImVec2(max.x - cut, min.y), ImVec2(max.x, min.y + cut),
                          max, ImVec2(min.x + cut, max.y), ImVec2(min.x, max.y - cut)};
  dl->AddConvexPolyFilled(plate, 6,
                          u32(hot || held ? theme.panelHi : theme.panel, held  ? 0.96f
                                                                         : hot ? 0.86f
                                                                               : 0.68f));
  // Broken corners and side rails give the plate a definite right end.
  dl->AddLine(ImVec2(min.x + 1 * s, min.y + 8 * s),
              ImVec2(min.x + 1 * s, max.y - cut - 2 * s), u32(edge, 0.6f), s);
  dl->AddLine(ImVec2(max.x - 1 * s, min.y + cut + 2 * s),
              ImVec2(max.x - 1 * s, max.y - 8 * s), u32(edge, hot ? 0.9f : 0.6f), s);
  const float corner = std::min(18 * s, l.width * 0.2f);
  dl->AddLine(min, ImVec2(min.x + corner, min.y), u32(hot ? accent : edge, 0.8f), s);
  dl->AddLine(ImVec2(max.x - corner, max.y - s), ImVec2(max.x, max.y - s),
              u32(hot ? accent : edge, 0.8f), s);
  if (focused || held) {
    const float x = min.x + 3 * s;
    dl->AddLine(ImVec2(x, min.y + 8 * s), ImVec2(x, max.y - 8 * s), u32(accent, 0.9f),
                (held ? 3 : 2) * s);
  }
  if (usable && (hot || held)) drawControlGlitch(dl, min, max, l, theme, s);
  const ImVec2 clipMin = dl->GetClipRectMin(), clipMax = dl->GetClipRectMax();
  const ImVec4 textClip(clipMin.x, clipMin.y, clipMax.x, clipMax.y);
  if (l.showNumber) {
    char number[4]{};
    std::snprintf(number, sizeof(number), "%02d", std::clamp(index, 0, 99));
    dl->AddText(l.font, l.tailSize, ImVec2(min.x + l.number.x, min.y + l.number.y),
                u32(theme.boneFaint), number, nullptr, 0, &textClip);
  }
  const ImU32 labelColour = u32(!usable   ? theme.boneFaint
                                : primary ? theme.ember
                                : cold    ? theme.rift
                                          : theme.bone);
  dl->AddText(l.font, l.fontSize, ImVec2(min.x + l.label.x, min.y + l.label.y),
              labelColour, l.caption.c_str(), nullptr, l.wrap, &textClip);
  if (l.showTail)
    dl->AddText(l.font, l.tailSize, ImVec2(min.x + l.value.x, min.y + l.value.y),
                u32(usable ? theme.boneDim : theme.boneFaint), l.tail.c_str(), nullptr,
                l.tailWrap, &textClip);
  dl->PopClipRect();
}

/// The shell's one interactive row. A menu entry and a Start/Back button are the same
/// control: manifold uses a signal plate, console a hairline and pointer wash.
/// A list entry adds a number and a tail; a button adds neither. One drawing here
/// is what keeps the two from drifting apart again.
bool rowControl(const char* id, const char* label, const view::Theme& theme, ImFont* font,
                float width, float height, float scale, bool enabled, bool primary,
                bool cold, int index, const char* tail, bool compact = false) {
  const float s = scale;
  const auto layout = theme.liveControls ? measureControl(label, tail, font, width,
                                                          height, s, index, compact)
                                         : ControlLayout{};
  // The stock button is only an input target: every pixel is drawn after it.
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  // The stock label is hidden; every pixel is drawn here instead.
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
  if (!enabled) ImGui::BeginDisabled();
  const bool pressed =
      ImGui::Button(id, theme.liveControls ? ImVec2(layout.width, layout.height)
                                           : ImVec2(width, height));
  const bool usable =
      theme.liveControls
          ? enabled && (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) == 0
          : enabled;
  if (!enabled) ImGui::EndDisabled();
  const bool hovered = usable && ImGui::IsItemHovered();
  const bool focused =
      theme.liveControls && usable && ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
  const bool held = theme.liveControls && usable && ImGui::IsItemActive();
  const bool hot = hovered || focused;
  ImGui::PopStyleColor(5);

  // Everything is positioned from the item's *actual* rectangle rather than from the
  // cursor read before it: they are not always the same.
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  if (theme.liveControls) {
    drawLiveControl(dl, min, max, layout, theme, s, index, usable, primary, cold, hovered,
                    focused, held);
  } else {
    const view::Rgba wash = cold ? theme.rift : theme.ember;
    if (hot) {
      dl->AddRectFilledMultiColor(min, max, u32(wash, 0.16f), u32(wash, 0.0f),
                                  u32(wash, 0.0f), u32(wash, 0.16f));
    }
    dl->AddLine(min, ImVec2(max.x, min.y), u32(theme.interfaceRule, 0.8f), 1.0f);

    const float labelHeight = font != nullptr ? font->FontSize : ImGui::GetFontSize();
    const float top = min.y + (max.y - min.y - labelHeight) * 0.5f;
    const float slide = hovered ? 12.0f * s : 0.0f;
    const float inset = index >= 0 ? 36.0f * s : 12.0f * s;
    const float textX = min.x + inset + slide;

    const ImU32 labelColor = u32(!usable   ? theme.boneFaint
                                 : primary ? theme.ember
                                 : cold    ? theme.rift
                                 : hot     ? theme.ember
                                           : theme.bone);
    const std::string caption = upper(label);
    if (font != nullptr) {
      const float small = font->FontSize * 0.58f;
      if (index >= 0) {
        char idx[4]{};
        std::snprintf(idx, sizeof(idx), "%02d", std::clamp(index, 0, 99));
        dl->AddText(font, small,
                    ImVec2(min.x + 6.0f * s, min.y + (max.y - min.y - small) * 0.5f),
                    u32(theme.boneFaint), idx);
      }
      dl->AddText(font, labelHeight, ImVec2(textX, top), labelColor, caption.c_str());
      if (tail != nullptr) {
        const ImVec2 w = font->CalcTextSizeA(small, FLT_MAX, 0.0f, tail);
        dl->AddText(
            font, small,
            ImVec2(max.x - 8.0f * s - w.x, min.y + (max.y - min.y - small) * 0.5f),
            u32(usable ? (hot ? theme.ember : theme.rule) : theme.boneFaint), tail);
      }
    } else {
      dl->AddText(ImVec2(textX, top), labelColor, caption.c_str());
      // The far-end value is drawn here too, so a control without a display font - the
      // settings rows - still shows what it is set to.
      if (tail != nullptr) {
        const ImVec2 w = ImGui::CalcTextSize(upper(tail).c_str());
        dl->AddText(ImVec2(max.x - 10.0f * s - w.x, min.y + (max.y - min.y - w.y) * 0.5f),
                    u32(usable ? (hot ? theme.ember : theme.boneDim) : theme.boneFaint),
                    upper(tail).c_str());
      }
    }
  }
  // One chokepoint for every control the shell draws: a menu row, a button and a cycling
  // setting all pass through here, so this is the single place a navigation click is
  // announced (M18.5). A disabled control reports no press, so it stays silent.
  if (pressed && enabled) audio::play(audio::Sound::Click);
  return pressed && enabled;
}

}  // namespace

namespace {

float twiceArea(const ImVec2* p, int n) {
  float a = 0.0f;
  for (int i = 0; i < n; ++i) {
    const ImVec2& u = p[i];
    const ImVec2& v = p[(i + 1) % n];
    a += u.x * v.y - v.x * u.y;
  }
  return a;
}

float cross2(const ImVec2& o, const ImVec2& a, const ImVec2& b) {
  return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool inTriangle(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& p) {
  const float d1 = cross2(a, b, p);
  const float d2 = cross2(b, c, p);
  const float d3 = cross2(c, a, p);
  const bool neg = d1 < 0 || d2 < 0 || d3 < 0;
  const bool pos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(neg && pos);
}

}  // namespace

void triangulate(const ImVec2* pts, int n, std::vector<int>& out) {
  out.clear();
  if (pts == nullptr || n < 3) return;
  std::vector<int> poly(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) poly[static_cast<std::size_t>(i)] = i;
  // Work anticlockwise, whichever way the author wound it: an outline is a shape, not a
  // direction, and both windings have to fill the same.
  if (twiceArea(pts, n) < 0.0f) std::reverse(poly.begin(), poly.end());

  out.reserve(static_cast<std::size_t>(n - 2) * 3);
  int guard = n * n;  // a self-intersecting outline has no ear; stop rather than spin
  while (poly.size() > 3 && guard-- > 0) {
    bool clipped = false;
    const int m = static_cast<int>(poly.size());
    for (int i = 0; i < m; ++i) {
      const int ia = poly[static_cast<std::size_t>((i + m - 1) % m)];
      const int ib = poly[static_cast<std::size_t>(i)];
      const int ic = poly[static_cast<std::size_t>((i + 1) % m)];
      const ImVec2& a = pts[ia];
      const ImVec2& b = pts[ib];
      const ImVec2& c = pts[ic];
      // Reflex corners are not ears - this is the test that keeps a valley a valley.
      if (cross2(a, b, c) <= 0.0f) continue;
      bool contains = false;
      for (const int j : poly) {
        if (j == ia || j == ib || j == ic) continue;
        if (inTriangle(a, b, c, pts[j])) {
          contains = true;
          break;
        }
      }
      if (contains) continue;
      out.push_back(ia);
      out.push_back(ib);
      out.push_back(ic);
      poly.erase(poly.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped) {
      out.clear();  // no ear anywhere: not a simple polygon
      return;
    }
  }
  if (poly.size() == 3) {
    out.push_back(poly[0]);
    out.push_back(poly[1]);
    out.push_back(poly[2]);
  }
}

void fillPolygon(ImDrawList* dl, const ImVec2* pts, int n, ImU32 col) {
  if (pts == nullptr || n < 3) return;
  // Copied first, and this is not defensive tidiness: callers pass `dl->_Path`, and
  // every triangle drawn below rewrites `_Path` on its way through PathFillConvex. The
  // points would change under the loop that is reading them, which shows up as vertices
  // in the 1e33 range rather than as anything that looks like a drawing bug.
  const std::vector<ImVec2> p(pts, pts + n);
  // And cleared: `AddTriangleFilled` *appends* to the pending path, so with the caller's
  // polygon still in it the first triangle was filled as the convex hull of the whole
  // outline - which is what closed a rook's notches over and drew wedges across the
  // designer's profile. Callers that keep their points save a copy first.
  dl->PathClear();
  std::vector<int> idx;
  triangulate(p.data(), n, idx);
  if (idx.empty()) return;
  // Anti-aliased fill is turned off across the triangles: each one would otherwise
  // feather its own edges, and the shared edges between them show as pale hairlines
  // through what is meant to be one solid shape.
  const ImDrawListFlags saved = dl->Flags;
  dl->Flags &= ~static_cast<ImDrawListFlags>(ImDrawListFlags_AntiAliasedFill);
  for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
    dl->AddTriangleFilled(p[static_cast<std::size_t>(idx[i])],
                          p[static_cast<std::size_t>(idx[i + 1])],
                          p[static_cast<std::size_t>(idx[i + 2])], col);
  }
  dl->Flags = saved;
}

void sectionHead(const char* label, const view::Theme& theme, void* displayFont,
                 float scale) {
  ImGui::Dummy(ImVec2(0, 6.0f * scale));
  if (displayFont != nullptr) ImGui::PushFont(static_cast<ImFont*>(displayFont));
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneFaint));
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  if (displayFont != nullptr) ImGui::PopFont();
  const ImVec2 lo = ImGui::GetItemRectMin();
  const ImVec2 hi = ImGui::GetItemRectMax();
  const float avail = ImGui::GetContentRegionAvail().x;
  // Drawn from the item's own rectangle, never from a cursor position captured before
  // it - those are not always the same, and the rule ends up floating.
  ImGui::GetWindowDrawList()->AddLine(ImVec2(hi.x + 8.0f * scale, (lo.y + hi.y) * 0.5f),
                                      ImVec2(lo.x + avail, (lo.y + hi.y) * 0.5f),
                                      u32(theme.rule, 0.8f), 1.0f);
  ImGui::Dummy(ImVec2(0, 3.0f * scale));
}

bool tabRow(std::initializer_list<const char*> labels, int& selected,
            const view::Theme& theme, void* displayFont, float scale) {
  bool changed = false;
  ImGui::Dummy(ImVec2(0, 8.0f * scale));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  int i = 0;
  const float startY = ImGui::GetCursorScreenPos().y;
  for (const char* label : labels) {
    if (i != 0) ImGui::SameLine(0.0f, 22.0f * scale);
    const bool live = i == selected;
    if (displayFont != nullptr) ImGui::PushFont(static_cast<ImFont*>(displayFont));
    ImGui::PushStyleColor(ImGuiCol_Text, col(live ? theme.bone : theme.boneFaint));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    if (ImGui::Button(label)) {
      if (selected != i) changed = true;
      selected = i;
    }
    ImGui::PopStyleColor(4);
    if (displayFont != nullptr) ImGui::PopFont();
    const ImVec2 lo = ImGui::GetItemRectMin();
    const ImVec2 hi = ImGui::GetItemRectMax();
    if (live) {
      dl->AddLine(ImVec2(lo.x, hi.y + 1.0f), ImVec2(hi.x, hi.y + 1.0f), u32(theme.ember),
                  2.0f * scale);
    }
    ++i;
  }
  const float endY = ImGui::GetItemRectMax().y;
  // One hairline under the whole row, so the tabs read as a strip rather than as a
  // handful of words that happen to be in a line.
  dl->AddLine(
      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x, endY + 1.0f),
      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x, endY + 1.0f),
      u32(theme.rule, 0.7f), 1.0f);
  (void)startY;
  ImGui::Dummy(ImVec2(0, 10.0f * scale));
  return changed;
}

bool stepper(const char* id, int& value, int lo, int hi, const view::Theme& theme,
             float scale, float width) {
  bool changed = false;
  const float w = width > 0.0f ? width : 34.0f * scale;
  ImGui::PushID(id);
  ImGui::BeginGroup();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneDim));
  if (ImGui::Button("+", ImVec2(w, 0.0f)) && value < hi) {
    ++value;
    changed = true;
  }
  ImGui::PopStyleColor(4);

  // The number is a button: clicking it opens a box to type in, which beats eight
  // clicks to get from 1 to 9.
  char text[16];
  std::snprintf(text, sizeof(text), "%d", value);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.bone));
  if (ImGui::Button(text, ImVec2(w, 0.0f))) ImGui::OpenPopup("##type");
  ImGui::PopStyleColor(4);

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneDim));
  if (ImGui::Button("-", ImVec2(w, 0.0f)) && value > lo) {
    --value;
    changed = true;
  }
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar();

  if (ImGui::BeginPopup("##type")) {
    int typed = value;
    ImGui::SetNextItemWidth(90.0f * scale);
    ImGui::SetKeyboardFocusHere();
    if (ImGui::InputInt(
            "##v", &typed, 1, 1,
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
      const int want = typed < lo ? lo : (typed > hi ? hi : typed);
      if (want != value) {
        value = want;
        changed = true;
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::EndGroup();
  // A cell drawn round the whole stack, so a run of axes reads as a run of cells rather
  // than as one long strip of signs.
  const ImVec2 hi2 = ImGui::GetItemRectMax();
  dl->AddRect(ImVec2(origin.x - 2.0f, origin.y - 2.0f),
              ImVec2(hi2.x + 2.0f, hi2.y + 2.0f), u32(theme.rule, 0.85f), 2.0f * scale, 0,
              1.0f);
  ImGui::PopID();
  return changed;
}

bool button(const char* label, const view::Theme& theme, float width, bool primary,
            bool cold, bool enabled, ImFont* font, bool compact) {
  const float s = uiScale();
  return rowControl(label, label, theme, font, width, compact ? 20 * s : controlHeight(),
                    s, enabled, primary, cold, -1, nullptr, compact);
}

bool cycleButton(const char* label, const std::string& value, const view::Theme& theme,
                 float scale, float width) {
  // The row draws the name on the left and the value on the right, so the control states
  // its setting without being opened. The value is upper-cased the same way the name is.
  const std::string tail = upper(value);
  const float s = scale;
  return rowControl(label, label, theme, nullptr, width, controlHeight(), s, true, false,
                    true, -1, tail.c_str());
}

ActionPairLayout actionPairLayout(const char* first, const char* second,
                                  const view::Theme& theme, float firstWidth,
                                  float secondWidth, ImFont* font, float available) {
  const float s = uiScale();
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  if (!theme.liveControls)
    return {{firstWidth, controlHeight()},
            {secondWidth, controlHeight()},
            controlHeight(),
            false};
  if (available <= 0) available = ImGui::GetContentRegionAvail().x;
  const float room = std::max(1.0f, available - std::min(8 * s, available * 0.04f));
  auto a = measureControl(first, nullptr, font, firstWidth, controlHeight(), s, -1);
  auto b = measureControl(second, nullptr, font, secondWidth, controlHeight(), s, -1);
  bool stacked = false;
  if (firstWidth + spacing + secondWidth > room) {
    a = measureControl(first, nullptr, font, 0, controlHeight(), s, -1);
    b = measureControl(second, nullptr, font, 0, controlHeight(), s, -1);
    if (a.width + spacing + b.width > room) {
      stacked = true;
      a = measureControl(first, nullptr, font, room, controlHeight(), s, -1);
      b = measureControl(second, nullptr, font, room, controlHeight(), s, -1);
    }
  }
  return {{a.width, a.height},
          {b.width, b.height},
          stacked ? a.height + ImGui::GetStyle().ItemSpacing.y + b.height
                  : std::max(a.height, b.height),
          stacked};
}

float controlHeight() {
  return 42.0f * uiScale();
}

void eyebrow(const char* text, const view::Theme& theme, ImFont* monoFont, float scale) {
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float size = (monoFont != nullptr ? monoFont->FontSize : ImGui::GetFontSize());
  const std::string caption = upper(text);
  float textW = 0;
  if (monoFont != nullptr) {
    textW = monoFont->CalcTextSizeA(size, FLT_MAX, 0.0f, caption.c_str()).x;
    dl->AddText(monoFont, size, at, u32(theme.ember), caption.c_str());
  } else {
    textW = ImGui::CalcTextSize(caption.c_str()).x;
    dl->AddText(at, u32(theme.ember), caption.c_str());
  }
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  const float y = at.y + size * 0.5f;
  if (right > at.x + textW + 10.0f * scale) {
    dl->AddLine(ImVec2(at.x + textW + 10.0f * scale, y), ImVec2(right, y),
                u32(theme.rule), 1.0f);
  }
  ImGui::Dummy(ImVec2(0, size + 6.0f * scale));
}

void screenTitle(const char* text, const view::Theme& theme, ImFont* displayFont,
                 float scale, float sizeMul) {
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::string caption = upper(text);
  const float base =
      displayFont != nullptr ? displayFont->FontSize : ImGui::GetFontSize();
  const float size = base * sizeMul;
  if (displayFont != nullptr) {
    dl->AddText(displayFont, size, at, u32(theme.bone), caption.c_str());
  } else {
    dl->AddText(at, u32(theme.bone), caption.c_str());
  }
  ImGui::Dummy(ImVec2(0, size + 4.0f * scale));
}

bool menuEntry(const char* label, int index, const view::Theme& theme, ImFont* labelFont,
               float width, bool enabled, float scale) {
  if (theme.liveControls) {
    const float available = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    const float room = std::min(width > 0 ? width : available, available);
    const float gutter =
        room >= 200 * scale ? 24 * scale : std::min(8 * scale, room * 0.04f);
    width = std::max(1.0f, std::min(320 * scale, room - gutter));
  }
  const std::string id = "##" + std::string(label);
  // Developer mode can expose unfinished rows; mark them at the far end of the row.
  return rowControl(id.c_str(), label, theme, labelFont, width, 42.0f * scale, scale,
                    enabled, false, false, index, enabled ? ">" : "NOT BUILT");
}

namespace {

/// How tall each pane's content was last frame, so it can be centred this frame.
///
/// Measured rather than declared: the content is laid out by ImGui and depends on the
/// interface scale, the fonts and the text itself, so the only honest height is the one
/// it actually came out at. One frame of lag is invisible; a menu whose last row falls
/// off the bottom at a large interface scale is not.
std::map<std::string, float>& paneHeights() {
  static std::map<std::string, float> heights;
  return heights;
}
std::string& currentPane() {
  static std::string id;
  return id;
}
/// Where this pane's content actually began. Subtracted again when the height is
/// recorded, or centring would feed back into its own measurement and creep down the
/// screen a little further every frame.
float& currentPaneTop() {
  static float top = 0;
  return top;
}
/// Whether this pane's height may be remembered. False for a departing ghost.
bool& currentPaneRemember() {
  static bool remember = true;
  return remember;
}
/// Whether ImGui actually laid the pane out this frame. It does not while the pane's
/// alpha is still zero at the very start of a transition, and a zero height recorded
/// then would fling the pane to the wrong place on the next frame.
bool& currentPaneBuilt() {
  static bool built = true;
  return built;
}

/// The transition scale a pane's finished drawing is transformed by at endPane, and the
/// point it is transformed about. One pane is built at a time (a departing ghost first,
/// then the arriving screen), so a single slot is enough.
float& paneFinishedAlpha() {
  static float alpha = -1;
  return alpha;
}
float& paneTransitionScale() {
  static float k = 1.0f;
  return k;
}
ImVec2& paneTransitionCentre() {
  static ImVec2 c{0, 0};
  return c;
}

}  // namespace

void beginPane(const char* id, ImVec2 min, ImVec2 max, float scale, float alpha,
               float pad, bool remember, bool fadeFinished) {
  const float k = std::max(0.05f, scale);
  const float a = std::clamp(alpha, 0.0f, 1.0f);
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  // The pane is always laid out at its settled size and position. The transition scale is
  // applied at endPane to the *finished* drawing, about this centre, so the whole pane -
  // card, rows and type together - zooms. Laying it out small and growing it instead
  // anchors the type to the top-left corner, which reads as a slide rather than a zoom.
  const ImVec2 size(max.x - min.x - pad * 2.0f, max.y - min.y - pad * 2.0f);

  // Centre the content by moving the whole window, never the cursor inside it. A cursor
  // offset would change how much room a scrolling body has left, so its measured height
  // would depend on where the pane had been centred - and the two would chase each other,
  // creeping for a few frames or jittering. Offsetting the window leaves the measurement
  // alone, so the pane is in its final place from the second frame, under the fade.
  const auto it = paneHeights().find(id);
  const float prevH = it != paneHeights().end() ? it->second : size.y;
  const float top = std::max(0.0f, (size.y - prevH) * 0.5f);
  ImGui::SetNextWindowPos(ImVec2(centre.x, centre.y + top), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size);
  // Stock text respects style alpha, custom chrome does not. Station panes fade
  // their complete drawing once at endPane, while still measuring at alpha zero.
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fadeFinished ? 1.f : a);
  paneFinishedAlpha() = fadeFinished ? a : -1.f;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  // The pane window has no border of its own (WindowBorderSize is 0), so the colour only
  // reaches its frames. The frameless implant keeps their faint outline: a checkbox ring,
  // a slider track, an input field (M24.4c step 3.2).
  ImGui::PushStyleColor(ImGuiCol_Border, framelessControls()
                                             ? ImGui::GetStyle().Colors[ImGuiCol_Border]
                                             : ImVec4(0, 0, 0, 0));
  const bool began =
      ImGui::Begin(id, nullptr,
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
  currentPaneBuilt() = began;
  // Draw without the window's own clip, so a pane scaled up past its settled edge is not
  // cut off there.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::PushClipRect(
      vp->WorkPos, ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y),
      false);
  paneTransitionScale() = k;
  paneTransitionCentre() = centre;

  currentPane() = id;
  currentPaneRemember() = remember;
  currentPaneTop() = ImGui::GetCursorPosY();
}

std::array<float, 4> paneBounds(float scale, ImVec2 limitMin, ImVec2 limitMax,
                                bool measured) {
  if (!currentPaneBuilt()) return {};
  const auto it = paneHeights().find(currentPane());
  if (!measured && it == paneHeights().end()) return {};
  const float height = measured ? ImGui::GetCursorPosY() - currentPaneTop() : it->second;
  const ImVec2 wp = ImGui::GetWindowPos();
  const ImVec2 ws = ImGui::GetWindowSize();
  const float mx = 20.0f * scale, my = 16.0f * scale, edge = 8.0f * scale;
  return {std::max(wp.x - mx, limitMin.x + edge), std::max(wp.y - my, limitMin.y + edge),
          std::min(wp.x + ws.x + mx, limitMax.x - edge),
          std::min(wp.y + height + my, limitMax.y - edge)};
}

void paneFrame(const view::Theme& theme, float scale, ImVec2 limitMin, ImVec2 limitMax,
               float draw, float level) {
  const auto rect = paneBounds(scale, limitMin, limitMax, false);
  if (rect[2] <= rect[0] || rect[3] <= rect[1]) return;
  hudPanel(ImGui::GetWindowDrawList(), {rect[0], rect[1]}, {rect[2], rect[3]}, theme,
           scale, draw, level, kScreenPanelCorner);
}

void endPane() {
  if (currentPaneRemember() && currentPaneBuilt()) {
    paneHeights()[currentPane()] = ImGui::GetCursorPosY() - currentPaneTop();
  }
  // Scale the pane's finished drawing about its centre. The clip pushed at beginPane is
  // the whole viewport, so a pane that grows past its settled edge stays drawn.
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float k = paneTransitionScale();
  if (k != 1.0f) {
    const ImVec2 c = paneTransitionCentre();
    for (int i = 0; i < dl->VtxBuffer.Size; ++i) {
      ImVec2& p = dl->VtxBuffer[i].pos;
      p.x = c.x + (p.x - c.x) * k;
      p.y = c.y + (p.y - c.y) * k;
    }
    // Item and glyph-slice scissors travel with their vertices. The viewport itself
    // remains the fixed outer limit, including when the departing pane grows.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec4 viewport(vp->WorkPos.x, vp->WorkPos.y, vp->WorkPos.x + vp->WorkSize.x,
                          vp->WorkPos.y + vp->WorkSize.y);
    for (auto& cmd : dl->CmdBuffer) {
      auto& r = cmd.ClipRect;
      if (cmd.ElemCount == 0 || (r.x == viewport.x && r.y == viewport.y &&
                                 r.z == viewport.z && r.w == viewport.w))
        continue;
      r.x = std::clamp(c.x + (r.x - c.x) * k, viewport.x, viewport.z);
      r.y = std::clamp(c.y + (r.y - c.y) * k, viewport.y, viewport.w);
      r.z = std::clamp(c.x + (r.z - c.x) * k, viewport.x, viewport.z);
      r.w = std::clamp(c.y + (r.w - c.y) * k, viewport.y, viewport.w);
    }
  }
  const float alpha = paneFinishedAlpha();
  if (alpha >= 0 && alpha < 1) {
    const auto fade = [&](auto&& self, ImGuiWindow* window) -> void {
      for (auto& vertex : window->DrawList->VtxBuffer) {
        const auto oldAlpha = (vertex.col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
        const auto newAlpha =
            static_cast<ImU32>(static_cast<float>(oldAlpha) * alpha + .5f);
        vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (newAlpha << IM_COL32_A_SHIFT);
      }
      for (auto* child : window->DC.ChildWindows)
        if (child->LastFrameActive == ImGui::GetFrameCount()) self(self, child);
    };
    fade(fade, ImGui::GetCurrentWindow());
  }
  ImGui::PopClipRect();
  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

void beginPlate(const char* id, const view::Theme& theme, ImVec2 size, float yBias) {
  const float s = uiScale();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                 vp->WorkPos.y + vp->WorkSize.y * yBias),
                          ImGuiCond_Always, ImVec2(0.5f, yBias));
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * s, 22.0f * s));
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
  // The card is drawn first so content lands on top of it. For an auto-height plate the
  // size is the previous frame's, which settles on the frame after a screen opens.
  const ImVec2 p = ImGui::GetWindowPos();
  const ImVec2 sz = ImGui::GetWindowSize();
  drawPlate(ImGui::GetWindowDrawList(), p, ImVec2(p.x + sz.x, p.y + sz.y), theme, s);
}

void endPlate() {
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

}  // namespace cb::render::widgets
