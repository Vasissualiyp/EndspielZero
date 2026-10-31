// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ui_widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <imgui_internal.h>

#include "audio/audio.hpp"

namespace cb::render::widgets {
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
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneFaint));
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

/// The shell's one interactive row. A menu entry and a Start/Back button are the same
/// control: type on a hairline with an accent wash under the pointer, never a filled
/// key. A list entry adds a number and a tail; a button adds neither. One drawing here
/// is what keeps the two from drifting apart again.
bool rowControl(const char* id, const char* label, const view::Theme& theme, ImFont* font,
                float width, float height, float scale, bool enabled, bool primary,
                bool cold, int index, const char* tail) {
  const float s = scale;
  // The stock button is only an input target: every pixel is drawn after it.
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  // The stock label is hidden; every pixel is drawn here instead.
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
  if (!enabled) ImGui::BeginDisabled();
  const bool pressed = ImGui::Button(id, ImVec2(width, height));
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

  // A wash that sweeps in from the left rather than a filled row: the row is type on a
  // page, and a solid block would turn the list back into a stack of buttons.
  const view::Rgba wash = cold ? theme.rift : theme.ember;
  if (hot) {
    dl->AddRectFilledMultiColor(min, max, u32(wash, 0.16f), u32(wash, 0.0f),
                                u32(wash, 0.0f), u32(wash, 0.16f));
  }
  if (theme.liveControls) dl->PushClipRect(min, max, true);
  dl->AddLine(min, ImVec2(max.x, min.y),
              u32(theme.liveControls && hot ? wash : theme.interfaceRule, 0.8f),
              theme.liveControls ? s : 1.0f);
  if (theme.liveControls && usable) {
    const float widthInside = max.x - min.x - 4.0f * s;
    const float length = std::min(28.0f * s, (max.x - min.x) * 0.16f);
    if (widthInside > length && max.y > min.y) {
      const double seconds =
          visualInput.context == ImGui::GetCurrentContext() ? visualInput.seconds : 0;
      const double phase = static_cast<double>(ImGui::GetItemID() & 0xFFFFu) / 65536.0;
      const double cycle = std::fmod(seconds, 6.0) / 6.0 + phase;
      const float t = static_cast<float>(cycle - std::floor(cycle));
      const auto ease = [](float x) {
        x = std::clamp(x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
      };
      const float envelope = ease(t / 0.1f) * ease((1.0f - t) / 0.1f);
      const float x = min.x + 2.0f * s + (widthInside - length) * t;
      const float y = min.y + 1.5f * s;
      dl->AddLine(ImVec2(x, y), ImVec2(x + length, y),
                  u32(wash, envelope * (hot ? 0.22f : 0.08f)), 3.0f * s);
      dl->AddLine(ImVec2(x, y), ImVec2(x + length, y),
                  u32(wash, envelope * (hot ? 0.85f : 0.35f)), s);
    }
    const float mid = (min.y + max.y) * 0.5f;
    if (focused) {
      dl->AddLine(ImVec2(min.x + 3.0f * s, mid - 6.0f * s),
                  ImVec2(min.x + 3.0f * s, mid + 6.0f * s), u32(wash, 0.9f), 2.0f * s);
    }
    if (held) {
      const float x = min.x + 6.0f * s;
      dl->AddLine(ImVec2(x + 4.0f * s, mid - 5.0f * s), ImVec2(x, mid - 5.0f * s),
                  u32(wash), s);
      dl->AddLine(ImVec2(x, mid - 5.0f * s), ImVec2(x, mid + 5.0f * s), u32(wash), s);
      dl->AddLine(ImVec2(x, mid + 5.0f * s), ImVec2(x + 4.0f * s, mid + 5.0f * s),
                  u32(wash), s);
    }
  }
  if (theme.liveControls) dl->PopClipRect();

  const float labelHeight = font != nullptr ? font->FontSize : ImGui::GetFontSize();
  const float top = min.y + (max.y - min.y - labelHeight) * 0.5f;
  // Live labels stay fixed; console retains its existing pointer step.
  const float slide = hovered && !theme.liveControls ? 12.0f * s : 0.0f;
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
      dl->AddText(font, small,
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
            bool cold, bool enabled, ImFont* font) {
  const float s = uiScale();
  return rowControl(label, label, theme, font, width, controlHeight(), s, enabled,
                    primary, cold, -1, nullptr);
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
  const std::string id = "##" + std::string(label);
  // What is coming stays in the list and says so, in two words at the far end of the
  // row. It does not need a paragraph underneath it every time the pointer goes past.
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
               float pad, bool remember) {
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
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
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
