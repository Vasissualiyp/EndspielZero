// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell's screen change is a camera move, and these are the two properties of it
// that are worth pinning: a decoration at alpha 0 must leave no geometry behind, and
// zooming it must scale it rather than move it. Both are assertions about a draw list,
// so no GPU is involved.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI

#include <array>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

#include <imgui.h>

#include "app/shell.hpp"
#include "render/deco.hpp"

using namespace cb;
using namespace cb::render;

namespace {

/// A draw list with an ImGui context behind it, so `drawDeco` can be run headlessly.
struct DecoFrame {
  ImGuiContext* ctx{nullptr};
  ImDrawList dl;
  DecoFrame() : ctx(ImGui::CreateContext()), dl(ImGui::GetDrawListSharedData()) {
    dl._ResetForNewFrame();
  }
  ~DecoFrame() {
    if (ctx != nullptr) ImGui::DestroyContext(ctx);
  }
};

std::pair<float, float> extent(const ImDrawList& dl) {
  if (dl.VtxBuffer.Size == 0) return {0.0f, 0.0f};
  float minX = dl.VtxBuffer[0].pos.x;
  float maxX = minX;
  float minY = dl.VtxBuffer[0].pos.y;
  float maxY = minY;
  for (int i = 1; i < dl.VtxBuffer.Size; ++i) {
    minX = std::min(minX, dl.VtxBuffer[i].pos.x);
    maxX = std::max(maxX, dl.VtxBuffer[i].pos.x);
    minY = std::min(minY, dl.VtxBuffer[i].pos.y);
    maxY = std::max(maxY, dl.VtxBuffer[i].pos.y);
  }
  return {maxX - minX, maxY - minY};
}

}  // namespace

TEST_CASE("a decoration at alpha zero emits nothing", "[render]") {
  DecoFrame f;
  drawDeco(&f.dl, Deco::Manifold, ImVec2(0, 0), ImVec2(400, 300), view::Theme::manifold(),
           IconStyle::Faceted, 0.0f, nullptr, 1.0f, 0.0f);
  CHECK(f.dl.VtxBuffer.Size == 0);

  // And a visible one does emit something, so the test above is not vacuous.
  DecoFrame g;
  drawDeco(&g.dl, Deco::Manifold, ImVec2(0, 0), ImVec2(400, 300), view::Theme::manifold(),
           IconStyle::Faceted, 0.0f, nullptr, 1.0f, 1.0f);
  CHECK(g.dl.VtxBuffer.Size > 0);
}

TEST_CASE("the main menu wears the quintic manifold object", "[render]") {
  // The original, dedicated centrepiece: the Calabi-Yau cross-section itself, not the
  // quintic reduced to one faint wireframe among the clutter. The first M18.4 pass
  // retired it from these screens; the correction restores it, and this pins the mapping
  // so a later background edit cannot quietly drop it again.
  CHECK(decoForScreen(static_cast<int>(app::Screen::MainMenu)) == Deco::Manifold);
  CHECK(decoForScreen(static_cast<int>(app::Screen::Welcome)) == Deco::Manifold);

  // When a live board is being drawn behind the menu - the attract sequence (M13.9) - the
  // board replaces the manifold rather than sharing the screen with it. Every other
  // screen's object is unchanged either way, so this can never leak into the library, the
  // pause section or the quit prompts.
  CHECK(decoForScreen(static_cast<int>(app::Screen::MainMenu), true) == Deco::None);
  for (const app::Screen s :
       {app::Screen::Welcome, app::Screen::NewGame, app::Screen::Settings,
        app::Screen::Editor, app::Screen::Paused, app::Screen::GameInfo,
        app::Screen::PieceMoves, app::Screen::QuitConfirm,
        app::Screen::PauseQuitConfirm}) {
    CHECK(decoForScreen(static_cast<int>(s)) == decoForScreen(static_cast<int>(s), true));
  }
}

TEST_CASE("manifold's ambient field is a perspective floor", "[render]") {
  DepthField field;
  field.advance(1.0f);
  DecoFrame f;
  field.draw(&f.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
             IconStyle::Faceted, true, true);
  REQUIRE(f.dl.VtxBuffer.Size > 0);
  for (const auto& v : f.dl.VtxBuffer) {
    // The old field scatters solids across the frame, including above the horizon.
    CHECK(v.pos.y >= 0.40f * 360.0f);
    CHECK(std::isfinite(v.pos.x));
    CHECK(std::isfinite(v.pos.y));
  }
}

TEST_CASE("the grid floor follows elapsed time rather than body simulation", "[render]") {
  const auto picture = [](const DepthField& field, float opacity = 1.0f,
                          bool shapes = true) {
    DecoFrame f;
    field.draw(&f.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
               IconStyle::Faceted, true, shapes, opacity);
    std::vector<std::tuple<float, float, ImU32>> out;
    for (const auto& v : f.dl.VtxBuffer) out.emplace_back(v.pos.x, v.pos.y, v.col);
    return out;
  };
  DepthField a;
  a.advance(1.0f);
  DepthField b;
  b.push(-3.1f);
  for (int i = 0; i < 4; ++i) b.advance(0.25f);
  CHECK(picture(a) == picture(b));
  CHECK(picture(a, 0.0f).empty());
  CHECK(picture(a, 1.0f, false).empty());
  CHECK(picture(a, 0.5f) != picture(a));
  const auto before = picture(a);
  a.advance(2.0f);
  CHECK(picture(a) != before);
}

TEST_CASE("the grid floor handles tiny and empty rectangles", "[render]") {
  DepthField field;
  for (const ImVec2 size : {ImVec2(1, 1), ImVec2(0, 0), ImVec2(-1, -1)}) {
    DecoFrame f;
    field.draw(&f.dl, ImVec2(0, 0), size, view::Theme::manifold(), IconStyle::Faceted,
               true, true);
    if (size.x <= 0.0f) CHECK(f.dl.VtxBuffer.Size == 0);
    for (const auto& v : f.dl.VtxBuffer) {
      CHECK(std::isfinite(v.pos.x));
      CHECK(std::isfinite(v.pos.y));
    }
  }
}

TEST_CASE("the field's wire vocabulary is five shapes and reaches no sixth", "[render]") {
  // The quintic survives only as the main menu's dedicated `Deco::Manifold`; it is not a
  // clutter body. The selection is a modulo over the shape count, so the test states the
  // bound directly and then samples many respawns to confirm no body ever holds a value
  // at or past it (which is what a stray `% 6` over a five-value enum would produce).
  REQUIRE(static_cast<int>(WireShape::Count) == 5);
  DepthField field;
  for (int step = 0; step < 2000; ++step) {
    field.advance(0.5f);
    for (const BodyState& b : field.bodies()) {
      if (b.piece) continue;
      CHECK(static_cast<int>(b.wire) < static_cast<int>(WireShape::Count));
    }
  }
}

TEST_CASE("both themes keep the drifting field below the interface", "[render]") {
  // Neither theme is a light page now. Their ornaments must remain muted rather than
  // taking attention away from the board and the interface.
  DepthField field;
  field.advance(1.0f);
  const auto meanLuminance = [&](const view::Theme& th) {
    DecoFrame f;
    field.draw(&f.dl, ImVec2(0, 0), ImVec2(400, 300), th, IconStyle::Faceted, true, true);
    float sum = 0.0f;
    int n = 0;
    for (int i = 0; i < f.dl.VtxBuffer.Size; ++i) {
      const ImU32 c = f.dl.VtxBuffer[i].col;
      const float r = static_cast<float>(c & 0xFF) / 255.0f;
      const float g = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
      const float b = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
      sum += 0.2126f * r + 0.7152f * g + 0.0722f * b;
      ++n;
    }
    return n == 0 ? 0.0f : sum / static_cast<float>(n);
  };
  const float manifold = meanLuminance(view::Theme::manifold());
  const float console = meanLuminance(view::Theme::console());
  REQUIRE(manifold > 0.0f);
  REQUIRE(console > 0.0f);
  CHECK(manifold < 0.35f);
  CHECK(console < 0.35f);
}

TEST_CASE("each wireframe body is a real drawing", "[render]") {
  // The field's vocabulary is five geometries, and each has to project to an actual
  // figure rather than an empty path or a degenerate line: a shape function that divides
  // by a radius or forgets an angle can silently emit nothing, and a background of
  // nothing looks exactly like a feature that was never wired up.
  const int count = static_cast<int>(WireShape::Count);
  REQUIRE(count == 5);
  for (int s = 0; s < count; ++s) {
    for (const float t : {0.0f, 0.5f, 2.0f}) {
      DecoFrame f;
      drawWireShape(&f.dl, static_cast<WireShape>(s), ImVec2(200.0f, 150.0f), 80.0f, t,
                    t * 0.5f, view::Theme::manifold(), 0.3f, 0.4f);
      REQUIRE(f.dl.VtxBuffer.Size > 0);
      const auto [w, h] = extent(f.dl);
      CHECK(w > 10.0f);
      CHECK(h > 10.0f);
    }
  }
}

TEST_CASE("a body's two orientation rates are independent", "[render]") {
  // Regression for the old single-`spin` coupling: yaw and pitch were both derived from
  // one value (`pitch == 0.55 * yaw`), so a body traced one fixed path through
  // orientation space rather than genuinely tumbling. `pitch - 0.55 * yaw` was then a
  // constant (zero) for every body at every time; with two independent rates it drifts.
  DepthField field;
  field.advance(0.25f);
  const std::vector<BodyState> before = field.bodies();
  REQUIRE(!before.empty());

  const auto residual = [](const BodyState& b) { return b.pitch - 0.55f * b.yaw; };

  // Find two wireframe bodies of the same shape, to make the divergence claim concrete.
  std::size_t i0 = 0, i1 = 0;
  bool found = false;
  for (std::size_t i = 0; i < before.size() && !found; ++i) {
    if (before[i].piece) continue;
    for (std::size_t j = i + 1; j < before.size(); ++j) {
      if (before[j].piece || before[j].wire != before[i].wire) continue;
      i0 = i;
      i1 = j;
      found = true;
      break;
    }
  }
  REQUIRE(found);

  field.advance(9.0f);
  const std::vector<BodyState> after = field.bodies();
  REQUIRE(after.size() == before.size());

  // The two same-shape bodies have each wandered off the old coupling line, and by
  // different amounts: they are not moving together along one shared path.
  const float moved0 = std::abs(residual(after[i0]) - residual(before[i0]));
  const float moved1 = std::abs(residual(after[i1]) - residual(before[i1]));
  CHECK((moved0 > 1e-3f || moved1 > 1e-3f));
  CHECK(std::abs(moved0 - moved1) > 1e-4f);
}

TEST_CASE("a drifting body never breathes into the centre", "[render]") {
  // The spawn radius is only the body's *average* distance from centre; the sinusoidal
  // breathe rides on top of it, so without a clamp a body would swing inside the floor it
  // was moved out of. Sample a whole cycle (the slowest breathe is ~314 s) and pin the
  // invariant for both the menu field and the quieter board field, whose margin is
  // tighter.
  for (const bool quiet : {false, true}) {
    DepthField field{quiet};
    float closest = 1e9f;
    for (int step = 0; step < 3600; ++step) {
      field.advance(0.2f);
      for (const BodyState& b : field.bodies()) {
        closest = std::min(closest, b.centreDistance);
      }
    }
    CHECK(closest >= kFieldMinRadius - 1e-4f);
  }
}

TEST_CASE("the drifting field is the same picture from the same state", "[render]") {
  // The shell draws the same frame twice in a headless capture, so the field has to be a
  // pure function of its own accumulated state. Two fields advanced by the same clock
  // must emit byte-identical geometry; a colour or position that reaches for a global
  // random source shows up here as a mismatch.
  DepthField a;
  DepthField b;
  for (int i = 0; i < 5; ++i) {
    a.advance(0.016f);
    b.advance(0.016f);
  }
  DecoFrame fa;
  DecoFrame fb;
  a.draw(&fa.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
         IconStyle::Faceted, true, true);
  b.draw(&fb.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
         IconStyle::Faceted, true, true);
  REQUIRE(fa.dl.VtxBuffer.Size > 0);
  REQUIRE(fa.dl.VtxBuffer.Size == fb.dl.VtxBuffer.Size);
  for (int i = 0; i < fa.dl.VtxBuffer.Size; ++i) {
    CHECK(fa.dl.VtxBuffer[i].pos.x == fb.dl.VtxBuffer[i].pos.x);
    CHECK(fa.dl.VtxBuffer[i].pos.y == fb.dl.VtxBuffer[i].pos.y);
    CHECK(fa.dl.VtxBuffer[i].col == fb.dl.VtxBuffer[i].col);
  }
}

TEST_CASE("the board screen's field is wireframes and no pieces", "[render]") {
  // Behind a real game there must be no piece icons: the board already has actual pieces,
  // and a second set drifting behind them competes with the thing being played. The quiet
  // field is therefore wireframes only, whatever flags its caller passes.
  DepthField quiet{true};
  quiet.advance(1.0f);
  DecoFrame wires;
  quiet.draw(&wires.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
             IconStyle::Faceted, false, true);
  CHECK(wires.dl.VtxBuffer.Size > 0);
  DecoFrame pieces;
  quiet.draw(&pieces.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
             IconStyle::Faceted, true, false);
  CHECK(pieces.dl.VtxBuffer.Size == 0);
}

TEST_CASE("a decoration answers a turn", "[render]") {
  // The menu objects can be dragged. The failure this guards against is the quiet one:
  // a turn parameter that is threaded through the call but never reaches the camera,
  // which looks exactly like an object that simply does not rotate.
  const auto vertices = [](Deco what, float yaw, float elev) {
    DecoFrame f;
    drawDeco(&f.dl, what, ImVec2(0, 0), ImVec2(400, 300), view::Theme::manifold(),
             IconStyle::Faceted, 0.0f, nullptr, 1.0f, 1.0f, yaw, elev);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(f.dl.VtxBuffer.Size) * 2);
    for (int i = 0; i < f.dl.VtxBuffer.Size; ++i) {
      out.push_back(f.dl.VtxBuffer[i].pos.x);
      out.push_back(f.dl.VtxBuffer[i].pos.y);
    }
    return out;
  };
  for (const Deco what : {Deco::Manifold, Deco::Lattice, Deco::Tesseract}) {
    const std::vector<float> still = vertices(what, 0.0f, 0.0f);
    REQUIRE(!still.empty());
    const std::vector<float> turned = vertices(what, 0.7f, 0.0f);
    const std::vector<float> tipped = vertices(what, 0.0f, 0.5f);
    REQUIRE(turned.size() == still.size());
    REQUIRE(tipped.size() == still.size());
    CHECK(turned != still);
    CHECK(tipped != still);
  }
}

TEST_CASE("zooming a decoration scales it about its centre", "[render]") {
  DecoFrame one;
  drawDeco(&one.dl, Deco::Tesseract, ImVec2(0, 0), ImVec2(400, 300),
           view::Theme::manifold(), IconStyle::Faceted, 0.0f, nullptr, 1.0f, 1.0f);
  DecoFrame two;
  drawDeco(&two.dl, Deco::Tesseract, ImVec2(0, 0), ImVec2(400, 300),
           view::Theme::manifold(), IconStyle::Faceted, 0.0f, nullptr, 2.0f, 1.0f);

  const auto [w1, h1] = extent(one.dl);
  const auto [w2, h2] = extent(two.dl);
  REQUIRE(w1 > 0.0f);
  REQUIRE(h1 > 0.0f);
  CHECK(w2 > w1 * 1.8f);
  CHECK(h2 > h1 * 1.8f);
}

#endif
