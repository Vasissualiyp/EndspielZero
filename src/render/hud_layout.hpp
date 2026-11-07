// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "position/piece.hpp"
#include "view/game_end_fx.hpp"

namespace cb::render {

[[nodiscard]] float autoUiFactor(int framebufferHeight) noexcept;
/// Captures opt in to automatic sizing; an explicit scale is always exact.
[[nodiscard]] float effectiveUiScale(float multiplier, bool automatic, int height,
                                     bool capture, bool captureAuto,
                                     float exact = -1) noexcept;

struct HudRect {
  float x{}, y{}, width{}, height{};
};
struct HudPoint {
  float x{}, y{};
};
/// Centre where possible; choose the nearest clear position with a king-cell margin.
[[nodiscard]] HudPoint resultPanelPos(HudRect stage, std::optional<HudPoint> king,
                                      HudPoint size) noexcept;
/// As above, but first looks for room beside the board: outside `board` (its projected
/// bounds), inside the stage, vertically centred, on the side nearest the stage centre
/// (above or below as a last try). Falls back to the clear-of-king placement when the
/// panel cannot fit anywhere clear of the board.
[[nodiscard]] HudPoint resultPanelPos(HudRect stage, std::optional<HudRect> board,
                                      std::optional<HudPoint> king, HudPoint size,
                                      float gap) noexcept;
struct ResultPanelFx {
  float draw{}, content{}, level{1};
};
[[nodiscard]] ResultPanelFx resultPanelFx(float age, view::EndKind kind, int maxDistance,
                                          float intensity) noexcept;
enum class StageEdge { Left, Right, Top, Bottom };
struct StagePanel {
  HudRect rect;
  StageEdge edge;
};
/// Insets only the indicated edge; the full-screen render viewport is unchanged.
[[nodiscard]] HudRect stageRect(HudRect viewport, std::span<const StagePanel> panels,
                                float gutter = 0) noexcept;

struct DockLayout {
  std::vector<HudRect> buttons;
  std::vector<HudRect> rules;
  float width{}, height{};
};
/// Sequential game/view/menu groups; wide groups wrap, MENU follows the last view
/// control.
[[nodiscard]] DockLayout dockLayout(std::span<const float> widths, std::size_t viewStart,
                                    float available, float height, float gap,
                                    float groupGap);

struct CapturedGroup {
  Piece piece;
  int count{};
  [[nodiscard]] int icons() const { return count <= 4 ? count : 1; }
  [[nodiscard]] std::string label() const {
    return count > 4 ? "x" + std::to_string(count) : std::string{};
  }
};
[[nodiscard]] std::vector<CapturedGroup> groupCaptured(std::span<const Piece> pieces);
[[nodiscard]] float captureReveal(float age, float intensity) noexcept;

/// A small vector arrow for the glyphs the HUD font lacks (M23.11 section 7): a shaft and
/// two head strokes inside a `size` square centred on `centre`. Y grows downward, as on
/// the screen, so Up points to smaller y.
enum class ArrowDir { Right, Left, Up };
struct ArrowStroke {
  HudPoint a, b;
};
[[nodiscard]] std::array<ArrowStroke, 3> arrowStrokes(HudPoint centre, float size,
                                                      ArrowDir dir) noexcept;

}  // namespace cb::render
