// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI
#include <algorithm>
#include <array>
#include <cmath>
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
  float available{1200};
  float requested{240};
  std::string caption{"Play"};
  std::string value{"Standard"};
  bool pane{false};
  bool autoResize{false};
  float paneScale{1};
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
    ImGui::SetNextWindowSize(autoResize ? ImVec2(0, 0)
                                        : ImVec2(40 + available * scale, 1080));
    if (pane)
      render::widgets::beginPane("transition-controls", ImVec2(0, 0),
                                 ImVec2(40 + available * scale, 1080), paneScale, 1, 0,
                                 false);
    else
      ImGui::Begin(autoResize ? "auto-controls" : "controls", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                       (autoResize ? ImGuiWindowFlags_AlwaysAutoResize : 0));
    ImGui::SetCursorScreenPos(ImVec2(40, 40));
    ImGui::PushItemWidth(available * scale);
    auto* dl = ImGui::GetWindowDrawList();
    // Keep stroke UVs distinct from glyph UVs for text-position measurements.
    dl->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
    if (clipped)
      dl->PushClipRect(ImVec2(44, 42), ImVec2(40 + 220 * scale, 40 + 35 * scale), true);
    const int start = dl->VtxBuffer.Size;
    if (!enabled) ImGui::BeginDisabled();
    bool pressed = false;
    if (kind == 0)
      pressed = render::widgets::menuEntry(caption.c_str(), 1, theme, nullptr,
                                           requested * scale, enabled, scale);
    if (kind == 1)
      pressed = render::widgets::button(caption.c_str(), theme, requested * scale, false,
                                        false, enabled);
    if (kind == 2)
      pressed = render::widgets::cycleButton(caption.c_str(), value, theme, scale,
                                             requested * scale);
    ControlSample out;
    out.pressed = pressed;
    out.focused = ImGui::IsItemFocused();
    out.held = ImGui::IsItemActive();
    const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    out.rect = {min.x, min.y, max.x, max.y};
    if (!enabled) ImGui::EndDisabled();
    if (clipped) dl->PopClipRect();
    ImGui::PopItemWidth();
    if (pane)
      render::widgets::endPane();
    else
      ImGui::End();
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

TEST_CASE("manifold menu plates have finite hit targets and measured wrapped captions",
          "[render][control-layout]") {
  for (float scale : {1.0f, 3.0f}) {
    for (float width : {600.0f, 260.0f, 110.0f}) {
      CAPTURE(scale, width);
      ControlHarness h(scale, 0);
      h.available = width;
      h.requested = width;
      h.caption = "Show me all the boards";
      const auto theme = view::Theme::manifold();
      const auto a = h.read(theme, 0.3);
      const float expected =
          std::min(320.0f, width - (width >= 200 ? 24 : std::min(8.0f, width * 0.04f))) *
          scale;
      CHECK(std::abs(a.rect[2] - a.rect[0] - expected) < 0.001f);
      CHECK(a.rect[3] - a.rect[1] >= 42 * scale);
      REQUIRE_FALSE(a.glyphs.empty());
      for (const auto& v : a.glyphs) {
        CHECK(v[0] >= a.rect[0]);
        CHECK(v[0] <= a.rect[2]);
        CHECK(v[1] >= a.rect[1]);
        CHECK(v[1] <= a.rect[3]);
      }
      auto& io = ImGui::GetIO();
      io.AddMousePosEvent(a.rect[2] + 8 * scale, (a.rect[1] + a.rect[3]) * 0.5f);
      io.AddMouseButtonEvent(0, true);
      CHECK_FALSE(h.read(theme, 0.3).pressed);
      io.AddMouseButtonEvent(0, false);
      CHECK_FALSE(h.read(theme, 0.3).pressed);
      CHECK(h.read(theme, 0.3).rect == a.rect);
      if (width == 110) CHECK(a.rect[3] - a.rect[1] > 42 * scale);
      const auto disabled = h.read(theme, 0.3, false);
      CHECK(disabled.rect[2] == a.rect[2]);
      CHECK(disabled.rect[3] >= a.rect[3]);
    }
  }
}

TEST_CASE("manifold values and automatic actions measure visible text",
          "[render][control-layout]") {
  for (float scale : {1.0f, 3.0f}) {
    ControlHarness h(scale, 2);
    h.caption = "Mode";
    h.value = "A deliberately long setting value";
    h.available = 150;
    h.requested = 150;
    const auto a = h.read(view::Theme::manifold(), 0.3);
    CHECK(a.rect[3] - a.rect[1] > 42 * scale);
    for (const auto& v : a.glyphs) CHECK(v[0] <= a.rect[2]);
    h.available = 600;
    h.requested = 0;
    const auto automatic = h.read(view::Theme::manifold(), 0.3);
    CHECK(automatic.rect[2] - automatic.rect[0] > 200 * scale);
    CHECK(automatic.rect[2] - automatic.rect[0] <= 600 * scale);
  }
}

TEST_CASE("auto-fit HUD controls retain their explicit width from the first frame",
          "[render][control-layout]") {
  ControlHarness h(1, 1);
  h.autoResize = true;
  h.requested = 78;
  h.caption = "UNDO";
  const auto first = h.read(view::Theme::manifold(), 0.3);
  CHECK(first.rect[2] - first.rect[0] == 78);
  for (int i = 0; i < 4; ++i) {
    const auto next = h.read(view::Theme::manifold(), 0.3);
    CHECK(next.rect[2] - next.rect[0] == 78);
    CHECK(next.rect[3] - next.rect[1] == first.rect[3] - first.rect[1]);
  }
}

TEST_CASE("manifold footer pairs compact then stack without losing either action",
          "[render][control-layout]") {
  for (float scale : {1.0f, 3.0f}) {
    for (float available : {700.0f, 300.0f, 150.0f}) {
      CAPTURE(scale, available);
      WidgetFrame frame(scale);
      ImGui::SetNextWindowSize(ImVec2(available * scale + 32, 1000));
      ImGui::Begin("pair");
      const auto pair = render::widgets::actionPairLayout(
          "BACK", "RESET TO DEFAULTS", view::Theme::manifold(), 120 * scale, 200 * scale,
          nullptr, available * scale);
      CHECK(pair.first.x > 0);
      CHECK(pair.second.x > 0);
      CHECK(pair.first.x <= available * scale);
      CHECK(pair.second.x <= available * scale);
      if (available == 150) REQUIRE(pair.stacked);
      render::widgets::button("BACK", view::Theme::manifold(), pair.first.x);
      const auto firstMin = ImGui::GetItemRectMin(), firstMax = ImGui::GetItemRectMax();
      if (!pair.stacked) ImGui::SameLine();
      render::widgets::button("RESET TO DEFAULTS", view::Theme::manifold(),
                              pair.second.x);
      const auto secondMin = ImGui::GetItemRectMin(), secondMax = ImGui::GetItemRectMax();
      CHECK(std::abs(firstMax.y - firstMin.y - pair.first.y) < 0.001f);
      CHECK(std::abs(secondMax.y - secondMin.y - pair.second.y) < 0.001f);
      CHECK(std::abs(secondMax.y - firstMin.y - pair.height) < 0.01f);
      if (pair.stacked)
        CHECK(secondMin.y >= firstMax.y);
      else
        CHECK(secondMin.x >= firstMax.x);
      ImGui::End();
    }
  }
}

TEST_CASE("menu transition scales control and glitch clips with their vertices",
          "[render][live-controls]") {
  ControlHarness h(1, 0);
  h.available = 1880;
  h.pane = true;
  h.mouse(false);
  const auto theme = view::Theme::manifold();
  (void)h.read(theme, 0.3);
  (void)h.read(theme, 0.3);
  const auto base = h.read(theme, 0.3);
  for (float k : {0.8f, 1.1f}) {
    h.paneScale = k;
    const auto sample = h.read(theme, 0.3);
    CHECK(sample.rect == base.rect);
    REQUIRE(sample.clips.size() == base.clips.size());
    REQUIRE(sample.glyphs.size() == base.glyphs.size());
    for (std::size_t i = 0; i < base.clips.size(); ++i) {
      const auto& a = base.clips[i];
      if (a == std::array<float, 4>{0, 0, 1920, 1080}) continue;
      const std::array<float, 4> want{std::clamp(960 + (a[0] - 960) * k, 0.0f, 1920.0f),
                                      std::clamp(540 + (a[1] - 540) * k, 0.0f, 1080.0f),
                                      std::clamp(960 + (a[2] - 960) * k, 0.0f, 1920.0f),
                                      std::clamp(540 + (a[3] - 540) * k, 0.0f, 1080.0f)};
      for (std::size_t j = 0; j < 4; ++j)
        CHECK(std::abs(sample.clips[i][j] - want[j]) < 0.01f);
    }
    for (std::size_t i = 0; i < base.glyphs.size(); ++i) {
      CHECK(std::abs(sample.glyphs[i][0] - (960 + (base.glyphs[i][0] - 960) * k)) <
            0.01f);
      CHECK(std::abs(sample.glyphs[i][1] - (540 + (base.glyphs[i][1] - 540) * k)) <
            0.01f);
    }
  }
}

TEST_CASE("hover glitches use explicit time behind intact text and fixed hit rectangles",
          "[render][live-controls]") {
  for (float scale : {1.0f, 3.0f}) {
    for (int kind : {0, 1, 2}) {
      CAPTURE(scale, kind);
      ControlHarness h(scale, kind);
      const auto theme = view::Theme::manifold();
      const auto a = h.read(theme, 0.3), b = h.read(theme, 2.3),
                 again = h.read(theme, 0.3);
      CHECK(sameDrawing(a, b));
      CHECK(sameDrawing(a, again));
      CHECK(a.rect == b.rect);
      CHECK(bool(a.glyphs == b.glyphs));
      h.mouse(false);
      const auto hover = h.read(theme, 0.3);
      CHECK(hover.rect == a.rect);
      REQUIRE(hover.glyphs.size() > a.glyphs.size());
      CHECK(std::equal(a.glyphs.rbegin(), a.glyphs.rend(), hover.glyphs.rbegin()));
      std::vector<ControlSample> phases;
      for (double t : {0.3, 0.425, 0.55, 0.675}) {
        phases.push_back(h.read(theme, t));
        REQUIRE(phases.back().glyphs.size() >= a.glyphs.size());
        CHECK(std::equal(a.glyphs.rbegin(), a.glyphs.rend(),
                         phases.back().glyphs.rbegin()));
        CHECK(phases.back().rect == a.rect);
      }
      CHECK(std::any_of(phases.begin() + 1, phases.end(), [&](const auto& sample) {
        return !sameDrawing(phases.front(), sample);
      }));
      CHECK(sameDrawing(hover, h.read(theme, 0.3)));
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
      REQUIRE(held.glyphs.size() >= idle.glyphs.size());
      CHECK(std::equal(idle.glyphs.rbegin(), idle.glyphs.rend(), held.glyphs.rbegin()));
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
      REQUIRE(focused.glyphs.size() >= idle.glyphs.size());
      CHECK(
          std::equal(idle.glyphs.rbegin(), idle.glyphs.rend(), focused.glyphs.rbegin()));
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
  h.mouse(false);
  const auto zero = h.read(theme, 0), one = h.read(theme, 1);
  CHECK_FALSE(sameDrawing(zero, one));
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
