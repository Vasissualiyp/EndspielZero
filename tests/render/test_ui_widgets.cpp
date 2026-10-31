// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI
#include <algorithm>

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
#endif
