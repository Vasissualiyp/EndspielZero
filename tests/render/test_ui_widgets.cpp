// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <vector>

#include <imgui.h>

#include "render/ui_widgets.hpp"

using namespace cb;

namespace {
struct WidgetFrame {
  ImGuiContext* context{ImGui::CreateContext()};
  explicit WidgetFrame(float scale) {
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(640, 360);
    io.IniFilename = nullptr;
    io.FontGlobalScale = scale;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
  }
  ~WidgetFrame() {
    ImGui::EndFrame();
    ImGui::DestroyContext(context);
  }
};

bool covers(const ImDrawList& dl, ImVec2 p) {
  const auto cross = [](ImVec2 a, ImVec2 b, ImVec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  };
  for (int i = 0; i + 2 < dl.IdxBuffer.Size; i += 3) {
    const auto& a = dl.VtxBuffer[dl.IdxBuffer[i]];
    const auto& b = dl.VtxBuffer[dl.IdxBuffer[i + 1]];
    const auto& c = dl.VtxBuffer[dl.IdxBuffer[i + 2]];
    if (((a.col | b.col | c.col) & IM_COL32_A_MASK) == 0) continue;
    if (cross(a.pos, b.pos, c.pos) == 0.0f) continue;
    const float x = cross(a.pos, b.pos, p);
    const float y = cross(b.pos, c.pos, p);
    const float z = cross(c.pos, a.pos, p);
    if ((x >= 0.0f && y >= 0.0f && z >= 0.0f) || (x <= 0.0f && y <= 0.0f && z <= 0.0f))
      return true;
  }
  return false;
}
}  // namespace

TEST_CASE("manifold panels leave their centres transparent", "[render]") {
  for (const bool manifold : {false, true}) {
    for (const float scale : {1.0f, 3.0f}) {
      const auto theme = manifold ? view::Theme::manifold() : view::Theme::console();
      WidgetFrame frame(scale);
      render::widgets::beginPlate("panel", theme, ImVec2(300, 200));
      const auto min = ImGui::GetWindowPos();
      const auto size = ImGui::GetWindowSize();
      const auto& dl = *ImGui::GetWindowDrawList();
      REQUIRE(dl.VtxBuffer.Size > 0);
      const bool filled =
          covers(dl, ImVec2(min.x + size.x * 0.5f, min.y + size.y * 0.5f));
      render::widgets::endPlate();
      CHECK(filled == !manifold);
    }
  }
}
namespace {
struct ControlVertex {
  float x, y, u, v;
  ImU32 colour;
  friend bool operator==(const ControlVertex&, const ControlVertex&) = default;
};
struct ControlSample {
  std::array<float, 4> rect{};
  std::vector<ControlVertex> vertices;
  std::vector<std::array<float, 4>> glyphs;
  std::vector<std::array<float, 4>> clips;
  bool pressed{false}, focused{false}, held{false};
};
struct ControlHarness {
  ImGuiContext* previous{ImGui::GetCurrentContext()};
  ImGuiContext* context{ImGui::CreateContext()};
  float scale;
  int kind;
  ControlHarness(float s, int k) : scale(s), kind(k) {
    ImGui::SetCurrentContext(context);
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1920, 1080);
    io.IniFilename = nullptr;
    io.FontGlobalScale = s;
    ImFontConfig font;
    font.SizePixels = 15;
    io.Fonts->AddFontDefault(&font);
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    io.AddMousePosEvent(-100, -100);
    (void)read(view::Theme::manifold(), 0);
    (void)read(view::Theme::manifold(), 0);
  }
  ~ControlHarness() {
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(previous);
  }
  ControlSample read(const view::Theme& theme, std::optional<double> time = {},
                     bool enabled = true, bool clipped = false) {
    ImGui::SetCurrentContext(context);
    std::optional<render::widgets::ScopedVisualTime> clock;
    if (time) clock.emplace(*time);
    ImGui::NewFrame();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(1920, 1080));
    ImGui::Begin("controls", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
    ImGui::SetCursorScreenPos(ImVec2(40, 40));
    auto* dl = ImGui::GetWindowDrawList();
    // Keep stroke UVs distinct from glyph UVs for text-position measurements.
    dl->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    if (clipped)
      dl->PushClipRect(ImVec2(44, 42), ImVec2(40 + 220 * scale, 40 + 35 * scale), true);
    const int start = dl->VtxBuffer.Size;
    if (!enabled) ImGui::BeginDisabled();
    bool pressed = false;
    if (kind == 0)
      pressed = render::widgets::menuEntry("Play", 1, theme, nullptr, 240 * scale,
                                           enabled, scale);
    if (kind == 1)
      pressed =
          render::widgets::button("Play", theme, 240 * scale, false, false, enabled);
    if (kind == 2)
      pressed =
          render::widgets::cycleButton("Mode", "Standard", theme, scale, 240 * scale);
    ControlSample out;
    out.pressed = pressed;
    out.focused = ImGui::IsItemFocused();
    out.held = ImGui::IsItemActive();
    const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    out.rect = {min.x, min.y, max.x, max.y};
    if (!enabled) ImGui::EndDisabled();
    const auto white = ImGui::GetFontTexUvWhitePixel();
    for (int i = start; i < dl->VtxBuffer.Size; ++i) {
      const auto& v = dl->VtxBuffer[i];
      out.vertices.push_back({v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.col});
      if (v.uv.x != white.x || v.uv.y != white.y)
        out.glyphs.push_back({v.pos.x, v.pos.y, v.uv.x, v.uv.y});
    }
    for (const auto& cmd : dl->CmdBuffer)
      if (cmd.ElemCount > 0)
        out.clips.push_back(
            {cmd.ClipRect.x, cmd.ClipRect.y, cmd.ClipRect.z, cmd.ClipRect.w});
    if (clipped) dl->PopClipRect();
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::EndFrame();
    return out;
  }
  void mouse(bool down, bool inside = true) {
    ImGui::SetCurrentContext(context);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(inside ? 80.0f : -100.0f, inside ? 60.0f : -100.0f);
    io.AddMouseButtonEvent(0, down);
  }
};
bool sameDrawing(const ControlSample& a, const ControlSample& b) {
  return a.vertices == b.vertices;
}
}  // namespace

TEST_CASE("live controls use explicit time without moving text or hit rectangles",
          "[render][live-controls]") {
  for (float scale : {1.0f, 3.0f}) {
    for (int kind : {0, 1, 2}) {
      CAPTURE(scale, kind);
      ControlHarness h(scale, kind);
      const auto theme = view::Theme::manifold();
      const auto a = h.read(theme, 0.3), b = h.read(theme, 2.3),
                 again = h.read(theme, 0.3);
      CHECK_FALSE(sameDrawing(a, b));
      CHECK(sameDrawing(a, again));
      CHECK(a.rect == b.rect);
      CHECK(bool(a.glyphs == b.glyphs));
      h.mouse(false);
      const auto hover = h.read(theme, 0.3);
      CHECK(hover.rect == a.rect);
      CHECK(bool(hover.glyphs == a.glyphs));
      CHECK_FALSE(sameDrawing(a, hover));
      h.mouse(false, false);
      (void)h.read(theme, 0.3);
      const auto console = view::Theme::console();
      CHECK(sameDrawing(h.read(console, 0.3), h.read(console, 2.3)));
      CHECK(sameDrawing(h.read(theme, 0.3), a));  // immediate live theme round trip
      const auto clip = h.read(theme, 0.3, true, true);
      CHECK(std::any_of(clip.clips.begin(), clip.clips.end(), [&](const auto& c) {
        return c[0] >= 44 && c[1] >= 42 && c[2] <= 40 + 220 * scale &&
               c[3] <= 40 + 35 * scale;
      }));
    }
  }
}

TEST_CASE("live controls retain one activation and a visible stationary keyboard focus",
          "[render][live-controls]") {
  for (float scale : {1.0f, 3.0f}) {
    for (int kind : {0, 1, 2}) {
      CAPTURE(scale, kind);
      ControlHarness h(scale, kind);
      const auto theme = view::Theme::manifold();
      const auto idle = h.read(theme, 0.3);
      h.mouse(false);
      (void)h.read(theme, 0.3);
      h.mouse(true);
      const auto held = h.read(theme, 0.3);
      h.mouse(false);
      const auto release = h.read(theme, 0.3);
      CHECK(held.held);
      CHECK_FALSE(held.pressed);
      CHECK(release.pressed);
      CHECK_FALSE(h.read(theme, 0.3).pressed);
      CHECK(held.rect == idle.rect);
      CHECK(bool(held.glyphs == idle.glyphs));
      h.mouse(false, false);
      auto& io = ImGui::GetIO();
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      io.AddKeyEvent(ImGuiKey_Tab, true);
      (void)h.read(theme, 0.3);
      io.AddKeyEvent(ImGuiKey_Tab, false);
      const auto focused = h.read(theme, 0.3);
      CAPTURE(io.NavVisible, io.NavActive, idle.focused);
      REQUIRE(focused.focused);
      CHECK_FALSE(sameDrawing(idle, focused));
      CHECK(focused.rect == idle.rect);
      CHECK(bool(focused.glyphs == idle.glyphs));
      const auto disabled = h.read(theme, 0.3, false);
      CHECK(sameDrawing(disabled, h.read(theme, 2.3, false)));
      h.mouse(true);
      CHECK_FALSE(h.read(theme, 0.3, false).pressed);
      h.mouse(false);
      CHECK_FALSE(h.read(theme, 0.3, false).pressed);
    }
  }
}

TEST_CASE("scoped widget time restores nesting and is local to its ImGui context",
          "[render][live-controls]") {
  ControlHarness h(1, 1);
  const auto theme = view::Theme::manifold();
  const auto zero = h.read(theme, 0), one = h.read(theme, 1);
  CHECK(sameDrawing(zero, h.read(theme, -1)));
  CHECK(sameDrawing(zero, h.read(theme, std::numeric_limits<double>::quiet_NaN())));
  {
    render::widgets::ScopedVisualTime outer(1);
    CHECK(sameDrawing(one, h.read(theme)));
    {
      render::widgets::ScopedVisualTime inner(2);
      CHECK_FALSE(sameDrawing(one, h.read(theme)));
    }
    CHECK(sameDrawing(one, h.read(theme)));
    {
      ControlHarness other(1, 1);
      CHECK(sameDrawing(other.read(theme), other.read(theme, 0)));
    }
    CHECK(sameDrawing(one, h.read(theme)));
  }
  CHECK(sameDrawing(zero, h.read(theme)));
}
#endif
