// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ui.hpp"
#include "render/lab_scene.hpp"
#include "view/implant_fx.hpp"
#include "view/rig_move.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

#include "io/notation.hpp"
#include "pieces/atom.hpp"
#include "render/deco.hpp"
#include "render/hud_layout.hpp"
#include "render/offscreen_target.hpp"
#include "render/overture_scene.hpp"
#include "render/piece_icon.hpp"
#include "render/piece_mesh.hpp"
#include "render/play_surface.hpp"
#include "render/post_process.hpp"
#include "render/ui_widgets.hpp"
#include "shaders.hpp"
#include "view/hud_fx.hpp"
#include "view/piece_tint.hpp"
#include "view/seams.hpp"

namespace cb::render {
using namespace widgets;

bool coordinateLabelsShown(const app::Settings& settings,
                           const app::Session& session) noexcept {
  // The flat 2-D projection is the only one the camera-projected text reads in; the solid
  // board and the shape both drop it (M17.21 label revision).
  return settings.showCoordinates && session.variant().dims.dims() == 2 &&
         session.flatView();
}

ViewModeButton viewModeButton(const app::Settings& settings, bool flatView,
                              bool surfaceAvailable) noexcept {
  // The shape view is a board that actually is its surface: a variant with one, SHAPE
  // lit, and not overridden by the flat view. Only there does the old board meaning die
  // and the piece-style meaning take its place, live in the same 2D/3D words - "2D" is
  // the flat icon puck, "3D" the sculptural mesh.
  if (settings.geometryView && surfaceAvailable && !flatView) {
    return {settings.shapePieceCylinders ? "2D" : "3D", true, true};
  }
  // Everywhere else it is exactly the board's own flat/solid toggle it always was. Its
  // enabled state greys out exactly where a shape is actually standing in for the flat
  // board (`geometryView` on a variant that *has* a surface) - not merely wherever
  // `geometryView` happens to be left on, which can be true on a variant with no surface
  // at all (SHAPE has no way to turn itself off switching to e.g. standard or cube5):
  // there is nothing for it to have replaced there, so disabling the toggle would only
  // break the ordinary flat/solid board for no shape to show in its place (reported
  // directly: "your edits have disabled the normal 2D mode... on cube5 or standard").
  // Greyed only where a shape is standing in for the board. The flat view is never
  // greyed: it is the way back. (With `geometryView` left on from a glued variant, a
  // player who had switched to 2D on a variant with no shape would otherwise find both
  // this and SHAPE greyed on the next glued board, with no way out.)
  return {flatView ? "2D" : "3D",
          flatView || !(settings.geometryView && surfaceAvailable), false};
}

namespace {

// Trailer and hot-seat keys must not also navigate the HUD. Menus and pending
// move/promotion choices retain navigation; active controls and text still capture.
void updateKeyboardNavigation(const app::Shell& shell) {
  ImGuiIO& io = ImGui::GetIO();
  const auto* session = shell.session();
  const bool boardKeys = shell.screen() == app::Screen::Game && session &&
                         session->hotSeat() && !session->pendingPromotion().active &&
                         !session->pendingMove().active;
  if (shell.attracting() || boardKeys) {
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  } else {
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  }
}

struct HudTextRow {
  std::string text;
  ImFont* font;
  view::Rgba colour;
  std::function<void()> clicked{};
  // Live feedback (M23.11 section 4). Any of these draws the row through the draw list.
  std::string from{};       ///< cross-fade from this text...
  view::Rgba fromColour{};  ///< ...in this colour
  float mix{1.0f};          ///< 0 shows `from`, 1 shows `text`
  float reveal{1.0f};       ///< left-to-right reveal with a bright leading edge
  float shift{0.0f};        ///< fraction of a row to ease up from, in a scrolled list
  float alpha{1.0f};        ///< row opacity
  float trailing{0.0f};     ///< extra width reserved after the text
  /// Draws beside the text: (draw list, the text's top-left, row size, panel content
  /// left).
  std::function<void(ImDrawList*, ImVec2, ImVec2, float)> after{};
  [[nodiscard]] bool animated() const {
    return mix < 1.0f || reveal < 1.0f || shift != 0.0f || alpha != 1.0f || bool(after);
  }
};

/// How a block of HUD text is framed (M23.10 section 7): an instrument panel behind it.
struct HudPanelSpec {
  ImVec2 size{};  ///< optional measured panel width/height
  const view::Theme* theme{nullptr};
  float scale{1.0f};
  float draw{1.0f};     ///< edge trace-in, 0..1
  float content{1.0f};  ///< text alpha
  float level{1.0f};    ///< panel dimming
  bool scrollRows{false};
  bool leftAlign{false};
  const char* header{nullptr};  ///< small accent label above the rows
  ImFont* headerFont{nullptr};
  const view::Rgba* led{nullptr};  ///< a lit dot before the first row, in this colour
  float ledLevel{1.0f};            ///< its brightness (the turn flip's flicker)
  int focusRow{-1};  ///< scrolling rows: centre this row when it changes (review cursor)
};

/// An animated HUD row (M23.11 section 4): cross-fade, left-to-right reveal and an eased
/// shift, drawn through the window's draw list. Reserves exactly the row's extent.
void drawAnimatedRow(const HudTextRow& row, ImVec2 extent, float spacing, float wrapWidth,
                     bool scrolled, const view::Theme* theme, float s, bool leftAlign,
                     float contentLeft) {
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::Dummy(extent);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImFont* font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  const float styleAlpha = ImGui::GetStyle().Alpha;
  const auto colourOf = [&](const view::Rgba& c, float a) {
    return ImGui::ColorConvertFloat4ToU32(col(c, a * row.alpha * styleAlpha));
  };
  ImVec2 at = p;
  if (scrolled) at.y += row.shift * (extent.y + spacing);
  const float textW = extent.x - row.trailing;
  if (row.mix < 1.0f && !row.from.empty()) {
    const float fromW = font->CalcTextSizeA(size, FLT_MAX, 0.0f, row.from.c_str()).x;
    const float dx = leftAlign ? 0.0f : textW - fromW;
    dl->AddText(font, size, ImVec2(at.x + dx, at.y),
                colourOf(row.fromColour, 1.0f - row.mix), row.from.c_str());
  }
  const float a = row.mix < 1.0f ? row.mix : 1.0f;
  if (row.reveal < 1.0f) {
    dl->PushClipRect(at, ImVec2(at.x + extent.x * row.reveal, at.y + extent.y), true);
    dl->AddText(font, size, at, colourOf(row.colour, a), row.text.c_str(), nullptr,
                wrapWidth);
    dl->PopClipRect();
    if (theme != nullptr && row.reveal > 0.0f) {
      const float edge = at.x + extent.x * row.reveal;
      dl->AddRectFilled(ImVec2(edge - std::max(1.0f, s), at.y),
                        ImVec2(edge, at.y + extent.y),
                        colourOf(theme->bone, 1.0f - row.reveal * 0.5f));
    }
  } else {
    dl->AddText(font, size, at, colourOf(row.colour, a), row.text.c_str(), nullptr,
                wrapWidth);
  }
  if (row.after) row.after(dl, at, ImVec2(textW, extent.y), contentLeft);
}

// Right/bottom pivots must use this frame's measured content, including a window's
// first appearance. Auto-fit otherwise anchors against the previous frame's size.
HudRect drawHudText(const char* id, std::span<const HudTextRow> rows, ImVec2 anchor,
                    ImVec2 pivot, ImGuiWindowFlags flags, float spacing,
                    const HudPanelSpec* panel = nullptr) {
  if (rows.empty()) return {};
  std::vector<ImVec2> extents;
  extents.reserve(rows.size());
  float width = 0, height = 0;
  const float s = panel ? panel->scale : 1.0f;
  const float pad = panel ? 10.0f * s : 0.0f;
  const float wrapWidth =
      panel && panel->scrollRows
          ? std::max(1.0f, panel->size.x - 2 * pad - ImGui::GetStyle().ScrollbarSize)
          : 0;
  for (const auto& row : rows) {
    if (row.font != nullptr) ImGui::PushFont(row.font);
    ImVec2 size = ImGui::CalcTextSize(row.text.c_str(), nullptr, false, wrapWidth);
    size.x += row.trailing;
    size.y = std::max(size.y, ImGui::GetTextLineHeight());
    if (row.font != nullptr) ImGui::PopFont();
    extents.push_back(size);
    width = std::max(width, size.x);
    height += size.y;
  }
  const float ledW = panel && panel->led ? 14.0f * s : 0.0f;
  ImVec2 headerSize(0, 0);
  if (panel && panel->header) {
    if (panel->headerFont) ImGui::PushFont(panel->headerFont);
    headerSize = ImGui::CalcTextSize(panel->header);
    headerSize.x +=
        static_cast<float>(std::char_traits<char>::length(panel->header) - 1) *
        ImGui::GetFontSize() * .12f;
    if (panel->headerFont) ImGui::PopFont();
    width = std::max(width, headerSize.x);
    height += headerSize.y + spacing;
  }
  width = std::max(1.0f, std::ceil(width + ledW));
  height =
      std::max(1.0f, std::ceil(height + spacing * static_cast<float>(rows.size() - 1)));
  const float rowsHeight = height - (panel && panel->header ? headerSize.y + spacing : 0);
  if (panel) {
    width = panel->scrollRows ? panel->size.x - 2 * pad
                              : std::max(width, panel->size.x - 2 * pad);
    height = panel->size.y > 0 ? std::min(height, std::max(1.0f, panel->size.y - 2 * pad))
                               : height;
  }
  ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, pivot);
  ImGui::SetNextWindowSize(ImVec2(width + 2 * pad, height + 2 * pad), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, spacing));
  ImGui::Begin(id, nullptr, flags & ~ImGuiWindowFlags_AlwaysAutoResize);
  if (panel) {
    const ImVec2 wp = ImGui::GetWindowPos();
    widgets::hudPanel(ImGui::GetWindowDrawList(), wp,
                      ImVec2(wp.x + width + 2 * pad, wp.y + height + 2 * pad),
                      *panel->theme, s, panel->draw, panel->level);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::clamp(panel->content, 0.0f, 1.0f));
  }
  float left = ImGui::GetCursorPosX();
  if (panel && panel->header) {
    if (panel->headerFont) ImGui::PushFont(panel->headerFont);
    ImGui::PushStyleColor(ImGuiCol_Text, col(panel->theme->ember, 0.8f));
    ImGui::SetCursorPosX(panel->leftAlign ? left : left + width - headerSize.x);
    widgets::trackedText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
                         panel->header, ImGui::GetFontSize() * .12f);
    ImGui::Dummy(headerSize);
    ImGui::PopStyleColor();
    if (panel->headerFont) ImGui::PopFont();
  }
  const bool scroll = panel && panel->scrollRows;
  if (scroll) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID countId = ImGui::GetID("##rowCount");
    const int count = static_cast<int>(rows.size());
    ImGui::SetNextWindowContentSize(ImVec2(0, rowsHeight));
    if (storage->GetInt(countId, -1) != count) {
      ImGui::SetNextWindowScroll(ImVec2(-1, rowsHeight));
      storage->SetInt(countId, count);
    }
    // A row to keep in view (the review cursor): centred when it moves, and the newest
    // row again when the focus is dropped.
    const ImGuiID focusId = ImGui::GetID("##focusRow");
    if (storage->GetInt(focusId, -1) != panel->focusRow) {
      storage->SetInt(focusId, panel->focusRow);
      if (panel->focusRow < 0) {
        ImGui::SetNextWindowScroll(ImVec2(-1, rowsHeight));
      } else {
        float top = 0;
        for (int j = 0; j < panel->focusRow && j < count; ++j)
          top += extents[static_cast<std::size_t>(j)].y + spacing;
        const float view = std::max(1.0f, height - headerSize.y - spacing);
        const float rowH =
            extents[static_cast<std::size_t>(std::min(panel->focusRow, count - 1))].y;
        ImGui::SetNextWindowScroll(ImVec2(-1, std::max(0.0f, top - (view - rowH) * .5f)));
      }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##rows",
                      ImVec2(width, std::max(1.0f, height - headerSize.y - spacing)), 0,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    left = ImGui::GetCursorPosX();
  }
  const bool scrolled =
      scroll && rowsHeight > std::max(1.0f, height - headerSize.y - spacing) + 0.5f;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto& row = rows[i];
    if (row.font != nullptr) ImGui::PushFont(row.font);
    const float x = panel && panel->leftAlign ? left : left + width - extents[i].x;
    ImGui::SetCursorPosX(x);
    if (i == 0 && panel && panel->led) {
      const ImVec2 c = ImGui::GetCursorScreenPos();
      const float r = 4.0f * s;
      const ImVec2 centre(c.x - 8.0f * s, c.y + extents[i].y * 0.5f);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const float lv = panel->ledLevel;
      dl->AddCircleFilled(centre, r * 1.8f, u32(*panel->led, 0.18f * lv), 16);
      dl->AddCircleFilled(centre, r, u32(*panel->led, lv), 16);
      dl->AddCircle(centre, r, u32(panel->theme->boneFaint, 0.8f), 16, std::max(1.0f, s));
    }
    if (row.animated() && !row.clicked) {
      drawAnimatedRow(row, extents[i], spacing, wrapWidth, scrolled,
                      panel ? panel->theme : nullptr, s, panel && panel->leftAlign,
                      ImGui::GetWindowPos().x + left);
      if (row.font != nullptr) ImGui::PopFont();
      continue;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, col(row.colour));
    if (scroll) ImGui::PushTextWrapPos(left + wrapWidth);
    if (row.clicked) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(row.text.c_str(), false, 0, extents[i])) row.clicked();
      ImGui::PopID();
    } else {
      ImGui::TextUnformatted(row.text.c_str());
    }
    if (scroll) ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (row.font != nullptr) ImGui::PopFont();
  }
  if (scroll) ImGui::EndChild();
  const ImVec2 pos = ImGui::GetWindowPos();
  const HudRect rect{pos.x, pos.y, width + 2 * pad, height + 2 * pad};
  if (panel) ImGui::PopStyleVar();
  ImGui::End();
  ImGui::PopStyleVar(3);
  return rect;
}

std::filesystem::path findFont(const char* name) {
  // Relative to the *binary*, not the working directory: the game must look the same
  // whether it is launched from the build tree, from an install, or from a menu entry
  // with no sensible cwd at all.
  std::vector<std::filesystem::path> roots;
  if (const char* base = SDL_GetBasePath(); base != nullptr) {
    const std::filesystem::path exe(base);
    roots.push_back(exe / "fonts");
    roots.push_back(exe / ".." / "fonts");
    roots.push_back(exe / ".." / ".." / "fonts");
    roots.push_back(exe / ".." / "share" / "endspielzero" / "fonts");
  }
  for (const char* rel : {"fonts", "../fonts", "build/dev/fonts",
                          "share/endspielzero/fonts", "/usr/share/endspielzero/fonts"}) {
    roots.emplace_back(rel);
  }
  for (const auto& root : roots) {
    std::error_code ec;
    const std::filesystem::path p = root / name;
    if (std::filesystem::exists(p, ec)) return p;
  }
  return {};
}

Result<VkShaderModule> makeShader(const VulkanContext& ctx,
                                  std::span<const std::uint8_t> spv) {
  VkShaderModuleCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  sci.codeSize = spv.size();
  sci.pCode = reinterpret_cast<const std::uint32_t*>(spv.data());
  VkShaderModule m = VK_NULL_HANDLE;
  if (const VkResult r = vkCreateShaderModule(ctx.device(), &sci, nullptr, &m);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the field shader: " + describe(r));
  }
  return m;
}

/// A vector arrow centred in the item rectangle `min`..`max` (see `arrowStrokes`).
void drawArrow(ImDrawList* dl, ImVec2 min, ImVec2 max, ArrowDir dir, ImU32 colour,
               float thickness, float fill = 0.8f) {
  const float size = std::min(max.x - min.x, max.y - min.y);
  const HudPoint centre{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
  for (const ArrowStroke& s : arrowStrokes(centre, std::max(size, 4.0f) * fill, dir))
    dl->AddLine(ImVec2(s.a.x, s.a.y), ImVec2(s.b.x, s.b.y), colour,
                std::max(1.0f, thickness));
}

/// The shell's own screens - the main menu and the panels it opens into. They move as
/// panes and a departing one is worth drawing again; the board screens are covered by
/// the board's own pull-back instead.
bool isMenuScreen(app::Screen s) noexcept {
  switch (s) {
    case app::Screen::MainMenu:
    case app::Screen::NewGame:
    case app::Screen::Watch:
    case app::Screen::Editor:
    case app::Screen::Settings:
    case app::Screen::QuitConfirm:
    case app::Screen::PauseQuitConfirm:
    case app::Screen::Paused:
    case app::Screen::GameInfo:
    case app::Screen::PieceMoves:
      return true;
    default:
      return false;
  }
}

/// How far in the pause branch a screen is: the game is in front (0), pause is a step
/// back (1), and the reference and quit panels are another step (2). Screens outside the
/// branch return -1.
int pauseRank(app::Screen s) noexcept {
  switch (s) {
    case app::Screen::Game:
      return 0;
    case app::Screen::Paused:
      return 1;
    case app::Screen::GameInfo:
    case app::Screen::PieceMoves:
    case app::Screen::PauseQuitConfirm:
      return 2;
    default:
      return -1;
  }
}

/// Which way the camera travels when moving from one screen to another.
///
/// On the shell's own menus, deeper is "a greater screen depth": opening a panel moves
/// forward into it. The pause branch is the reverse - the game is in front, pressing
/// pause steps back, and opening a reference panel steps back again - so there it is the
/// *lower* rank that is forward. The main-menu quit prompt is the other exception: it
/// sits outside the menu, so opening it backs out too.
bool goesDeeper(app::Screen from, app::Screen to) noexcept {
  if (to == app::Screen::QuitConfirm) return false;
  if (from == app::Screen::QuitConfirm) return true;
  const int fromRank = pauseRank(from);
  const int toRank = pauseRank(to);
  if (fromRank >= 0 && toRank >= 0) return toRank < fromRank;
  return app::screenDepth(to) >= app::screenDepth(from);
}

/// The transition in two halves, so the screens do not cross-fade. The departing screen
/// is finished and gone by `kOutEnd`; the arriving one does not begin until `kInStart`.
/// The gap is the point: with both moving at once the eye reads one screen turning into
/// another, where a short pause reads as passing through the first into the second.
constexpr float kOutEnd = 0.40f;
constexpr float kInStart = 0.55f;

float smoothstep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

/// A heading with a rule running to the right of it, the way a plate is labelled.

/// A piece drawn as a flat icon, for the top-down view where a model would be a blob.
///
/// The outlines come from a table rather than from a switch full of drawing calls, so
/// the two styles differ only in which table is read, a variant could eventually ship
/// its own, and the shapes themselves are covered by tests that need no GPU.
///
/// `behind` is what the icon is drawn on: the cut-outs are painted in it, which is how
/// a knight gets an eye out of a single-colour silhouette.
void drawIconTable(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 behind,
                   const PieceIcon& icon, bool upsideDown = false) {
  // The box is 100 wide; r is the icon's half-size on screen. Upside down is how a piece
  // whose forward a seam has reversed shows it (Piece::flipped).
  const float k = r / 50.0f;
  const float ky = upsideDown ? -k : k;
  const auto trace = [&](const IconPoly& poly, ImU32 fill) {
    if (poly.size() < 3) return;
    dl->PathClear();
    for (const IconPoint& p : poly) {
      dl->PathLineTo(ImVec2(c.x + (p.x - 50.0f) * k, c.y + (p.y - 50.0f) * ky));
    }
    // Concave, and properly so: a rook's crenellations, a queen's coronet and any
    // valley an author draws are not convex hulls, and a fill that treats them as one
    // closes the notches over.
    fillPolygon(dl, dl->_Path.Data, dl->_Path.Size, fill);
    dl->PathClear();
  };
  for (const IconPoly& poly : icon.fills) trace(poly, col);
  for (const IconPoly& poly : icon.cuts) trace(poly, behind);
}

void drawPieceGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 behind,
                    Archetype shape, IconStyle style, bool upsideDown = false) {
  drawIconTable(dl, c, r, col, behind, pieceIcon(style, shape), upsideDown);
}

/// A piece's authored icon as a drawable table, converted once and kept: the conversion
/// owns the points the table's spans refer to. A piece with no icon of its own keeps the
/// shipped one for its archetype. Checked by value, so an edit made in the designer is
/// picked up the next frame.
const PieceIcon* authoredIcon(const PieceTypeDef& def) {
  if (!def.icon.has_value()) return nullptr;
  struct Cached {
    assets::IconModel model;
    std::unique_ptr<OwnedIcon> table;
  };
  static std::unordered_map<const PieceTypeDef*, std::pair<Cached, PieceIcon>> cache;
  const assets::IconModel& want = *def.icon;
  auto& slot = cache[&def];
  if (!slot.first.table || !(slot.first.model == want)) {
    slot.first.model = want;
    slot.first.table = std::make_unique<OwnedIcon>(want);
    slot.second = slot.first.table->view();
  }
  return &slot.second;
}

void drawPieceOf(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 behind,
                 const PieceTypeDef& def, IconStyle style, bool upsideDown = false) {
  if (const PieceIcon* own = authoredIcon(def)) {
    drawIconTable(dl, c, r, col, behind, *own, upsideDown);
  } else {
    drawPieceGlyph(dl, c, r, col, behind, archetypeFor(def), style, upsideDown);
  }
}

}  // namespace

Result<std::unique_ptr<Ui>> Ui::create(const VulkanContext& ctx, SDL_Window* window,
                                       const view::Theme& theme, VkFormat colorFormat) {
  auto ui = std::unique_ptr<Ui>(new Ui());
  ui->ctx_ = &ctx;
  ui->window_ = window;
  ui->theme_ = theme;

  // ImGui needs its own descriptor pool; one combined-image-sampler set per font atlas
  // and per user texture is plenty for an interface with no images in it.
  const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8}};
  VkDescriptorPoolCreateInfo dpci{};
  dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  dpci.maxSets = 8;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = sizes;
  if (const VkResult r = vkCreateDescriptorPool(ctx.device(), &dpci, nullptr, &ui->pool_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the interface's descriptor pool: " + describe(r));
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // no stray imgui.ini beside the binary
  // Keyboard navigation is on: the whole menu tree is reachable without a mouse, and the
  // focus ring is drawn in the accent so it matches the rest (see applyStyle).
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // Idle navigation focus must leave app shortcuts available. Active widgets, text
  // fields and modals still capture keys; the SDL backend receives every event.
  io.ConfigNavCaptureKeyboard = false;

  if (auto r = ui->loadFonts(); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  ui->applyStyle();

  if (!ImGui_ImplSDL3_InitForVulkan(window)) {
    return fail(ErrorCode::Unsupported, "cannot attach the interface to the window");
  }

  const VkFormat targetFormat = colorFormat;
  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &targetFormat;
  rendering.depthAttachmentFormat = OffscreenTarget::kDepthFormat;

  ImGui_ImplVulkan_InitInfo info{};
  info.Instance = ctx.instance();
  info.PhysicalDevice = ctx.physicalDevice();
  info.Device = ctx.device();
  info.QueueFamily = ctx.queueFamily();
  info.Queue = ctx.queue();
  info.DescriptorPool = ui->pool_;
  info.MinImageCount = 2;
  // Four, not two: the field and the overlay are two RenderDrawData calls per frame, and
  // the backend's vertex/index buffers round-robin over ImageCount. Two frames in flight
  // times two passes needs four slots for a reused buffer to be a frame old, not one.
  info.ImageCount = 4;
  info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  // The interface is drawn into the same pass as the board, so it declares the same
  // attachment formats. One render path, not two.
  info.UseDynamicRendering = true;
  info.PipelineRenderingCreateInfo = rendering;
  if (!ImGui_ImplVulkan_Init(&info)) {
    return fail(ErrorCode::Unsupported, "cannot initialise the interface renderer");
  }
  if (auto r = ui->buildBoardFieldPipeline(targetFormat); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  ui->initialised_ = true;
  return ui;
}

Result<void> Ui::buildBoardFieldPipeline(VkFormat colorFormat) {
  const auto vs = makeShader(*ctx_, spv::imgui_field_vert_spv_span());
  if (!vs.has_value()) return fail(vs.error().code, vs.error().message);
  boardFieldVert_ = *vs;
  const auto fs = makeShader(*ctx_, spv::imgui_field_frag_spv_span());
  if (!fs.has_value()) return fail(fs.error().code, fs.error().message);
  boardFieldFrag_ = *fs;

  // The interface's frame-wide background list is filled with the field plus coordinate
  // labels and every menu, so it cannot be submitted early; the field gets its own list
  // and this is the pipeline that draws it, matching ImGui's own vertex layout and blend
  // state but at the board pass's sample count.
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dlci{};
  dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dlci.bindingCount = 1;
  dlci.pBindings = &binding;
  if (const VkResult r = vkCreateDescriptorSetLayout(ctx_->device(), &dlci, nullptr,
                                                     &boardFieldSetLayout_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the field set layout: " + describe(r));
  }

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push.offset = 0;
  push.size = sizeof(float) * 4;
  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &boardFieldSetLayout_;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &push;
  if (const VkResult r =
          vkCreatePipelineLayout(ctx_->device(), &plci, nullptr, &boardFieldLayout_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the field layout: " + describe(r));
  }

  // ImDrawVert: pos, uv, colour, matching the backend's own vertex input exactly.
  VkVertexInputBindingDescription vb{};
  vb.binding = 0;
  vb.stride = sizeof(ImDrawVert);
  vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attrs[3]{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, pos)};
  attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(ImDrawVert, uv)};
  attrs[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)};
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &vb;
  vi.vertexAttributeDescriptionCount = 3;
  vi.pVertexAttributeDescriptions = attrs;

  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = OffscreenTarget::kSampleCount;
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  VkPipelineColorBlendAttachmentState blend{};
  blend.blendEnable = VK_TRUE;
  blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.colorBlendOp = VK_BLEND_OP_ADD;
  blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend.alphaBlendOp = VK_BLEND_OP_ADD;
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &blend;
  const VkDynamicState dynamics[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{};
  dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dynamics;

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = boardFieldVert_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = boardFieldFrag_;
  stages[1].pName = "main";

  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = OffscreenTarget::kDepthFormat;
  VkGraphicsPipelineCreateInfo gpi{};
  gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gpi.pNext = &rendering;
  gpi.stageCount = 2;
  gpi.pStages = stages;
  gpi.pVertexInputState = &vi;
  gpi.pInputAssemblyState = &ia;
  gpi.pViewportState = &vp;
  gpi.pRasterizationState = &rs;
  gpi.pMultisampleState = &ms;
  gpi.pDepthStencilState = &ds;
  gpi.pColorBlendState = &cb;
  gpi.pDynamicState = &dy;
  gpi.layout = boardFieldLayout_;
  if (const VkResult r = vkCreateGraphicsPipelines(ctx_->device(), VK_NULL_HANDLE, 1,
                                                   &gpi, nullptr, &boardFieldPipeline_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the field pipeline: " + describe(r));
  }
  if (!PostProcess::supported(*ctx_)) return {};
  const VkFormat hdrFormat = PostProcess::kSceneFormat;
  rendering.pColorAttachmentFormats = &hdrFormat;
  if (const VkResult r = vkCreateGraphicsPipelines(
          ctx_->device(), VK_NULL_HANDLE, 1, &gpi, nullptr, &boardFieldHdrPipeline_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the HDR field pipeline: " + describe(r));
  }
  return {};
}

Result<void> Ui::loadFonts() {
  ImGuiIO& io = ImGui::GetIO();
  // Three voices, three jobs: Chakra Petch names things, Public Sans explains them, and
  // JetBrains Mono is reserved for anything the engine computed. That last rule is what
  // makes a number look like a measurement rather than like prose.
  const auto display = findFont("ChakraPetch-Bold.ttf");
  const auto body = findFont("PublicSans-Regular.ttf");
  const auto light = findFont("PublicSans-Light.ttf");
  const auto mono = findFont("JetBrainsMono-Regular.ttf");

  const auto load = [&](const std::filesystem::path& path, float size,
                        float tracking = 0.0f) -> void* {
    if (path.empty()) return nullptr;
    ImFontConfig cfg;
    cfg.GlyphExtraSpacing.x = tracking * scale_;
    return io.Fonts->AddFontFromFileTTF(path.string().c_str(), size * scale_, &cfg);
  };

  fontBody_ = load(body, 15.5f);
  if (fontBody_ == nullptr) fontBody_ = io.Fonts->AddFontDefault();
  fontSmall_ = load(light.empty() ? body : light, 12.5f);
  if (fontSmall_ == nullptr) fontSmall_ = fontBody_;
  fontMono_ = load(mono, 12.0f, 0.2f);
  if (fontMono_ == nullptr) fontMono_ = fontSmall_;
  // Tracked out a little: Chakra Petch is set in caps here, and caps need the air.
  fontDisplay_ = load(display, 17.0f, 0.9f);
  // A missing typeface must not stop the game starting; it just looks plainer.
  if (fontDisplay_ == nullptr) fontDisplay_ = fontBody_;
  fontAction_ = load(mono, 17.0f, 0.3f);
  if (fontAction_ == nullptr) fontAction_ = fontDisplay_;
  fontLabel_ = load(mono, 15.5f);
  if (fontLabel_ == nullptr) fontLabel_ = fontBody_;

  io.FontDefault = static_cast<ImFont*>(fontBody_);
  return {};
}

ImFont* Ui::actionFont() const noexcept {
  return static_cast<ImFont*>(theme_.monoLabels ? fontAction_ : fontDisplay_);
}

void Ui::applyStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  // Start from stock so re-applying after a rescale is idempotent rather than cumulative.
  s = ImGuiStyle{};
  // Machined edges. Rounding is the single thing that makes ImGui look like a debug
  // overlay, so the panels get none and the controls get just enough to look milled.
  s.WindowRounding = 0.0f;
  s.ChildRounding = 6.0f;
  s.FrameRounding = 2.0f;
  s.PopupRounding = 0.0f;
  s.ScrollbarRounding = 0.0f;
  s.GrabRounding = 2.0f;
  // The plate draws its own thick outline; letting ImGui add a second, thin one on top
  // of it just muddies the edge.
  s.WindowBorderSize = 0.0f;
  s.ChildBorderSize = 1.0f;
  s.FrameBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.WindowPadding = ImVec2(14, 14);
  s.FramePadding = ImVec2(9, 5);
  s.ItemSpacing = ImVec2(8, 8);
  s.ItemInnerSpacing = ImVec2(6, 5);
  s.IndentSpacing = 14.0f;
  s.ScrollbarSize = 10.0f;

  const view::Theme& t = theme_;
  if (t.panelStyle == view::PanelStyle::Brackets) {
    s.ChildRounding = 0.0f;
    s.FrameRounding = 0.0f;
    s.GrabRounding = 0.0f;
  }
  ImVec4* c = s.Colors;
  c[ImGuiCol_WindowBg] = col(t.soot, t.implantFrameless ? 0.f : .96f);
  c[ImGuiCol_ChildBg] = col(t.panel, 0.0f);
  c[ImGuiCol_PopupBg] = col(t.panel, 0.99f);
  c[ImGuiCol_Border] = col(t.interfaceRule);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = col(t.panel);
  c[ImGuiCol_FrameBgHovered] = col(t.panelHi);
  c[ImGuiCol_FrameBgActive] = col(t.panelHi);
  c[ImGuiCol_TitleBg] = col(t.panel);
  c[ImGuiCol_TitleBgActive] = col(t.panelHi);
  c[ImGuiCol_Text] = col(t.bone);
  c[ImGuiCol_TextDisabled] = col(t.boneFaint);
  c[ImGuiCol_Button] = col(t.panel);
  c[ImGuiCol_ButtonHovered] = col(t.panelHi);
  // A press lands on the accent, so a click feels like it did something.
  c[ImGuiCol_ButtonActive] = col(t.ember);
  c[ImGuiCol_Header] = col(t.panelHi);
  c[ImGuiCol_HeaderHovered] = col(t.panelHi);
  c[ImGuiCol_HeaderActive] = col(t.emberDeep);
  c[ImGuiCol_Separator] = col(t.interfaceRule);
  c[ImGuiCol_ScrollbarBg] = col(t.ink, 0.6f);
  c[ImGuiCol_ScrollbarGrab] = col(t.rule);
  c[ImGuiCol_ScrollbarGrabHovered] = col(t.emberDeep);
  c[ImGuiCol_ScrollbarGrabActive] = col(t.ember);
  c[ImGuiCol_CheckMark] = col(t.ember);
  c[ImGuiCol_SliderGrab] = col(t.ember);
  c[ImGuiCol_SliderGrabActive] = col(t.emberDeep);
  c[ImGuiCol_TextSelectedBg] = col(t.emberDeep, 0.5f);
  c[ImGuiCol_ModalWindowDimBg] = col(t.ink, 0.72f);
  // The keyboard-navigation ring, in the accent, so a nav-driven menu reads like the
  // rest.
  c[ImGuiCol_NavCursor] = col(t.ember);
  c[ImGuiCol_NavWindowingHighlight] = col(t.ember, 0.7f);
  widgets::framelessControls() = t.implantFrameless;
  if (t.implantFrameless) {
    // M24.4c / BCI brief 2: light, not boxes. The frame fills are already clear, so the
    // checkbox ring, slider track and input field keep a faint outline; the slider knob
    // is a soft dim dot rather than the brightest block on screen; a selection is a wash.
    s.ChildBorderSize = 0.0f;
    s.GrabRounding = 8.0f;
    s.GrabMinSize = 8.0f;
    c[ImGuiCol_Border] = col(t.implantFaint, 0.6f);
    c[ImGuiCol_SliderGrab] = col(t.implantDim, 0.9f);
    c[ImGuiCol_SliderGrabActive] = col(t.implant);
    c[ImGuiCol_Header] = col(t.implantDeep, 0.0f);
    c[ImGuiCol_HeaderHovered] = col(t.implantDeep, 0.30f);
    c[ImGuiCol_HeaderActive] = col(t.implantDeep, 0.30f);
    c[ImGuiCol_ScrollbarBg] = col(t.ink, 0.0f);
  }

  // Sizes scale with the interface; colours do not.
  s.ScaleAllSizes(scale_);
}

Result<void> Ui::setScale(float scale) {
  const float clamped = std::clamp(scale, 0.6f, 9.0f);
  if (std::abs(clamped - scale_) < 0.01f) return {};
  scale_ = clamped;

  // Fonts are rebuilt at the new size rather than stretched - a scale option that makes
  // text blurry is not worth having - and that means waiting for the GPU to finish with
  // the old atlas first.
  vkDeviceWaitIdle(ctx_->device());
  ImGui_ImplVulkan_DestroyFontsTexture();
  ImGui::GetIO().Fonts->Clear();
  fontBody_ = nullptr;
  fontSmall_ = nullptr;
  fontDisplay_ = nullptr;
  if (auto r = loadFonts(); !r.has_value()) return r;
  if (!ImGui_ImplVulkan_CreateFontsTexture()) {
    return fail(ErrorCode::Internal, "cannot rebuild the interface font atlas");
  }
  applyStyle();
  return {};
}

bool Ui::processEvent(const SDL_Event& e) {
  ImGui_ImplSDL3_ProcessEvent(&e);
  const ImGuiIO& io = ImGui::GetIO();
  switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
      return io.WantCaptureMouse;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
      return capturesKeyboard();
    default:
      return false;
  }
}

bool Ui::capturesMouse() const {
  return ImGui::GetIO().WantCaptureMouse;
}
bool Ui::capturesKeyboard() const {
  // A modal can open during build, after NewFrame computed IO capture. Its current
  // popup ownership must protect the first visible frame as well as settled frames.
  return ImGui::GetIO().WantCaptureKeyboard || ImGui::GetTopMostPopupModal() != nullptr;
}

void Ui::setTheme(const view::Theme& t) noexcept {
  theme_ = t;
  // ImGui's own text, panel and border colours are copied out of the theme in
  // `applyStyle`, so a palette change has to re-run it. Without this the old theme's
  // colours stay behind - manifold's dark type on console's dark page, which is where a
  // retheme "not applying" was first noticed.
  applyStyle();
  // The cached scene has the old theme's seam and event colours baked into it.
  overtureCacheValid_ = false;
}

void Ui::setVisualTime(double seconds) noexcept {
  visualTime_ = std::isfinite(seconds) && seconds >= 0 ? seconds : 0;
}

void Ui::tick(float dt) {
  lastDt_ = dt;
  clock_ += dt;
  rackAge_ += dt;
  field_.advance(dt);
  boardField_.advance(dt);
  // Ease the arrival rather than run it linearly: a screen that stops dead has not
  // travelled anywhere.
  // The new screen moves (section 6) run on the same clock at their own rate: their
  // length in seconds is the whole clock.
  const float seconds = currentMoveSeconds();
  implantMoveAge_ = pinnedMoveAge_ ? *pinnedMoveAge_ : implantMoveAge_ + dt;
  if (pinnedMoveAge_ && seconds > 0.0f) {
    enter_ = std::clamp(*pinnedMoveAge_ / seconds, 0.0f, 1.0f);
  } else if (enter_ < 1.0f) {
    enter_ = std::min(1.0f, enter_ + dt * (seconds > 0.0f ? 1.0f / seconds : 2.2f));
  }
}

app::MoveFx Ui::moveFx() const noexcept {
  if ((move_ == app::ScreenMove::Deploy || move_ == app::ScreenMove::Stow) &&
      intensity_ >= .5f)
    return app::moveFx(move_, enter_ * rigTotal_, intensity_, rigTotal_, rigLock_);
  return app::moveFx(move_, enter_ * currentMoveSeconds(), intensity_,
                     currentMoveSeconds());
}

std::optional<Ui::RigMove> Ui::rigMove() const noexcept {
  if (move_ != app::ScreenMove::Deploy && move_ != app::ScreenMove::Stow)
    return std::nullopt;
  const float total = currentMoveSeconds();
  const bool calm = intensity_ < .5f;
  return RigMove{move_,
                 enter_ * total,
                 total,
                 calm ? 0.f : rigLock_,
                 calm ? 0.f : rigShutdown_,
                 enter_ >= 1,
                 rigFrom_};
}

float Ui::currentMoveSeconds() const noexcept {
  if (move_ == app::ScreenMove::Station && intensity_ >= .5f) return stationSeconds_;
  if ((move_ == app::ScreenMove::Deploy || move_ == app::ScreenMove::Stow) &&
      intensity_ >= .5f)
    return rigTotal_;
  return app::moveSeconds(move_, intensity_);
}

float Ui::implantPanelAge(bool leaving) const noexcept {
  return pinnedPanelAge_.value_or(implantMoveAge_ -
                                  (leaving ? 0.f : .70f * currentMoveSeconds()));
}

float Ui::saccadicDip() const noexcept {
  if (move_ != app::ScreenMove::Station || enter_ >= 1 || !fromCam_ || !toCam_) return 0;
  return view::saccadicDip(
      view::rigAngularSpeed(*fromCam_, *toCam_, implantMoveAge_, currentMoveSeconds()),
      intensity_);
}

view::Vec2f Ui::implantPanelCentre() const {
  const auto& io = ImGui::GetIO();
  view::Vec2f centre{io.DisplaySize.x * .5f, io.DisplaySize.y * .5f};
  float largest = 0;
  for (const auto& r : implantRecords_) {
    const float area = (r.rect[2] - r.rect[0]) * (r.rect[3] - r.rect[1]);
    if (r.settled && area > largest) {
      largest = area;
      centre = {(r.rect[0] + r.rect[2]) * .5f, (r.rect[1] + r.rect[3]) * .5f};
    }
  }
  return centre;
}
bool Ui::mouseOverImplantPanel() const {
  float x{}, y{};
  SDL_GetMouseState(&x, &y);
  const auto contains = [&](const auto& r, float grow) {
    return x >= r[0] - grow && x < r[2] + grow && y >= r[1] - grow && y < r[3] + grow;
  };
  for (const auto& r : toolRects_)
    if (contains(r, 0)) return false;
  for (const auto& r : implantRecords_)
    if (r.settled && contains(r.rect, 8)) return true;
  return false;
}

void Ui::recordToolRect() {
  const auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
  const auto scale = ImGui::GetIO().DisplayFramebufferScale;
  const auto origin = ImGui::GetMainViewport()->Pos;
  toolRects_.push_back({(pos.x - origin.x) * scale.x, (pos.y - origin.y) * scale.y,
                        (pos.x + size.x - origin.x) * scale.x,
                        (pos.y + size.y - origin.y) * scale.y});
}

bool Ui::paneInteractive() const noexcept {
  if (implantActive_ && move_ == app::ScreenMove::Station &&
      (enter_ < 1 || pinnedPanelAge_))
    return !ghosting_ && implantPanelAge(false) >= (intensity_ < .5f ? .1f : .25f);
  return !ghosting_ && (move_ != app::ScreenMove::Station || moveFx().arriveAlpha > .5f);
}

std::optional<view::OrbitCamera> Ui::labCamera() const {
  if (!labBackdrop_) return std::nullopt;
  if (move_ == app::ScreenMove::Station && intensity_ >= .5f && fromCam_ && toCam_) {
    if (enter_ >= 1) return toCam_;
    return view::blendCamera(*fromCam_, *toCam_, view::rigEase(enter_));
  }
  return toCam_;
}

float Ui::salience(std::string_view focus) const {
  if (!labBackdrop_ || focus.empty()) return 0;
  if (!console_.saliencePin().empty()) return focus == console_.saliencePin() ? 1.f : 0.f;
  if (!focusFor_ || currentScreen_ < 0) return 0;
  const auto arriving = focusFor_(static_cast<app::Screen>(currentScreen_));
  if (move_ != app::ScreenMove::Station || enter_ >= 1)
    return focus == arriving ? 1.f : 0.f;
  const auto leaving = focusFor_(static_cast<app::Screen>(leavingScreen_));
  if (arriving == leaving) return focus == arriving ? 1.f : 0.f;
  const float age = enter_ * currentMoveSeconds();
  float value = 0;
  if (focus == leaving) value = 1 - smoothstep(age / .3f);
  if (focus == arriving) value = smoothstep((age - .5f * currentMoveSeconds()) / .3f);
  return value;
}

std::array<float, 8> Ui::rackSlides() const {
  auto result = rackFrom_;
  const float age = rackScreenChange_ && pinnedMoveAge_ ? *pinnedMoveAge_ : rackAge_;
  const float progress = view::slabSlide(0, 1, age, intensity_) / .06f;
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] =
        std::lerp(rackFrom_[i], static_cast<int>(i) < rackCount_ ? .06f : 0.f, progress);
  return result;
}

HologramRequest Ui::hologramRequest() const {
  auto out = hologramRequest_;
  if (intensity_ >= .5f && enter_ < 1) {
    const float age = enter_ * currentMoveSeconds();
    if (move_ == app::ScreenMove::Deploy) {
      // The bench hologram collapses into its lens as the menu dissolves (M24.5).
      if (age >= app::kDeployMenuOffSeconds) return {};
      out = leavingHologram_;
      out.seconds = clock_;
      out.scale = out.alpha = 1 - smoothstep(age / app::kDeployMenuOffSeconds);
      out.squashZ = out.scale * out.scale;
      return out;
    }
    if (move_ == app::ScreenMove::Stow) {
      // ...and grows out of it once the slabs are home.
      if (age < rigLock_) return {};
      out.scale = out.alpha = smoothstep((age - rigLock_) / app::kStowMenuSeconds);
      out.squashZ = out.scale;
      return out;
    }
  }
  if (move_ != app::ScreenMove::Station || intensity_ < .5f || enter_ >= 1) return out;
  const float radius =
      std::lerp(leavingHologram_.radiusMetres, out.radiusMetres, smoothstep(enter_));
  if (out.projector == leavingHologram_.projector && out.kind == leavingHologram_.kind &&
      (out.kind != HologramRequest::Overture || leavingVariant_ == pickedVariant_))
    return out;
  if (enter_ < .4f) {
    out = leavingHologram_;
    out.seconds = clock_;
    out.scale = out.alpha = 1 - smoothstep(enter_ / .4f);
    out.squashZ = out.scale * out.scale;
  } else if (enter_ < .5f) {
    out.kind = HologramRequest::None;
    out.scale = out.alpha = out.squashZ = 0;
  } else {
    out.scale = out.alpha = smoothstep((enter_ - .5f) / .5f);
    out.squashZ = out.scale;
  }
  out.radiusMetres = radius;
  return out;
}

void Ui::newFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  if (inject_) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(injectPos_.x, injectPos_.y);
    io.AddMouseButtonEvent(injectButton_, injectDown_);
  }
  if (injectKey_ != 0) {
    ImGui::GetIO().AddKeyEvent(static_cast<ImGuiKey>(injectKey_), injectKeyDown_);
    if (!injectKeyDown_) injectKey_ = 0;
  }
  ImGui::NewFrame();
}

void Ui::updateObjectDrag(app::Shell& shell, const ImVec2& min, const ImVec2& max) {
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 m = io.MousePos;
  const bool inside = m.x >= min.x && m.x < max.x && m.y >= min.y && m.y < max.y;
  // The grab has to *start* on the object. Testing "inside" every frame would let a
  // drag that began on a menu row keep turning the object once the pointer crossed the
  // hairline, which feels like the interface grabbing at the cursor.
  // The piece designer's board uses the left button to place pieces, so there the grab is
  // the right or middle button; everywhere else it is the left one.
  const bool placing = shell.screen() == app::Screen::Editor;
  const auto grabbing = [&] {
    return placing ? (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                      ImGui::IsMouseDown(ImGuiMouseButton_Middle))
                   : ImGui::IsMouseDown(ImGuiMouseButton_Left);
  };
  const auto pressed = [&] {
    return placing ? (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                      ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
                   : ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  };
  if (pressed() && inside && !io.WantCaptureMouse) objectDrag_ = true;
  if (!grabbing()) objectDrag_ = false;
  if (!objectDrag_) return;

  const app::Settings& set = shell.settings();
  const float k = 0.007f * set.orbitSensitivity;
  objectYaw_ += io.MouseDelta.x * k;
  // Dragging down lowers the camera, the same way round as orbiting the board, and the
  // same setting inverts both.
  objectElev_ -= io.MouseDelta.y * k * (set.invertOrbitY ? -1.0f : 1.0f);
  // Kept off the poles: an object seen exactly edge-on is a line, and one turned past
  // the vertical is being looked at from underneath - which is the thing that most
  // reliably reads as a bug rather than as a camera.
  objectElev_ = std::clamp(objectElev_, -1.15f, 1.15f);
}

void Ui::refreshOvertureCache(app::Shell& shell) {
  const app::OverturePlayer& p = shell.overtures();
  const app::Overture which = p.current();
  const float progress = p.progress();
  const bool intro = p.intro();
  // A variant with a hand-authored scene uses it; anything else - a Workshop package, a
  // variant added as data - animates with the scene derived from its own spec (M13).
  const VariantSpec* spec = shell.preview(pickedVariant_);
  const bool derived = !app::hasBespokeOverture(pickedVariant_) && spec != nullptr;
  // Rebuild only when the inputs changed. The held dwell pins `t` at 1 for 700 ms, so
  // this is the difference between rebuilding 1600 quads every frame and building them
  // once; a scene is pure, so the cache is exact rather than approximate.
  if (!overtureCacheValid_ || overtureCacheWhich_ != which ||
      overtureCacheT_ != progress || overtureCacheIntro_ != intro ||
      overtureCacheVariant_ != pickedVariant_ || overtureCacheDerived_ != derived) {
    overtureCache_ = derived ? derivedOvertureScene(*spec, progress, theme_)
                             : render::overtureScene(which, progress, intro, theme_);
    overtureCacheWhich_ = which;
    overtureCacheT_ = progress;
    overtureCacheIntro_ = intro;
    overtureCacheVariant_ = pickedVariant_;
    overtureCacheDerived_ = derived;
    overtureCacheValid_ = true;
  }
}

void Ui::drawOvertureObject(app::Shell& shell, const ImVec2& min, const ImVec2& max,
                            float zoom, float alpha) {
  refreshOvertureCache(shell);
  // A copy, because the player's turn below is this frame's camera, not the scene's.
  OvertureScene scene = overtureCache_;
  // The player's turn is added to the overture's own camera, not substituted for it, so
  // a shape taken hold of mid-morph carries on morphing.
  scene.cam.yaw += objectYaw_;
  scene.cam.elev = std::clamp(scene.cam.elev + objectElev_, 0.10f, 1.53f);
  drawOverture(ImGui::GetBackgroundDrawList(), scene, min, max, theme_, iconStyle_, zoom,
               alpha);
}

void Ui::drawShellFrame(app::Shell& shell, ImVec2& menuMin, ImVec2& menuMax) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const ImVec2 min = vp->WorkPos;
  const ImVec2 max =
      ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);

  // The object stays on one side across screens. Alternating it by depth made the
  // decoration jump from one half of the frame to the other as the screen changed, which
  // reads as a teleport; keeping it put lets the one clock zoom it in place, so a deeper
  // screen looks like the camera moving towards the same object rather than a new one.
  const bool decoLeft = true;
  // Most screens split the frame in half. The designer needs a wider pane for its rows
  // of controls, so it takes the larger share and leaves the preview a narrower strip.
  const float menuShare = shell.screen() == app::Screen::Editor ? 0.42f : 0.5f;
  const float split = min.x + (max.x - min.x) * menuShare;
  const ImVec2 decoMin(decoLeft ? min.x : split, min.y);
  const ImVec2 decoMax(decoLeft ? split : max.x, max.y);
  menuMin = ImVec2(decoLeft ? split : min.x, min.y);
  menuMax = ImVec2(decoLeft ? max.x : split, max.y);
  // Remembered so a screen that owns its own object - the piece reference and its move
  // diagrams - can put it in the same place the deco would have gone.
  decoMin_ = decoMin;
  decoMax_ = decoMax;

  // A dragging ghost only needs the rectangle: the ground, the field and the arriving
  // object are the arriving screen's to draw, and drawing them twice would double them.
  if (ghosting_) return;

  // The ground. A light shell has to paint its own, or the board's clear colour shows
  // through where nothing else is drawn.
  if (!labBackdrop_) {
    dl->AddRectFilled(min, max, u32(theme_.ink));
    field_.draw(dl, min, max, theme_, iconStyle_, true, true);
  }
  // Every pane carries its own screen panel now; only the editor, which does not, keeps
  // the open frame.
  if (theme_.panelStyle == view::PanelStyle::Brackets &&
      shell.screen() == app::Screen::Editor) {
    widgets::panelCorners(dl, ImVec2(menuMin.x + px(24), menuMin.y + px(24)),
                          ImVec2(menuMax.x - px(24), menuMax.y - px(24)), theme_, scale_);
  }

  // A screen's object belongs to that screen: the library's lattice is the *selected*
  // variant's, while a board screen shows the running game's. The departing ghost must
  // ask for its own subject rather than inherit the arriving one - otherwise the lattice
  // left behind by "new game" would briefly wear the running game's boards.
  const auto subjectFor = [&](app::Screen s) -> const VariantSpec* {
    if (s == app::Screen::NewGame) return shell.preview(pickedVariant_);
    return shell.hasGame() ? &shell.session()->variant() : nullptr;
  };
  // The departing screen's object leaves first; the arriving screen's only grows in once
  // it is gone. The two halves of the clock are separate for exactly that reason.
  const float inE = inEase();
  const float outE = outEase();
  const float inZoom = deeper_ ? 0.55f + 0.45f * inE : 1.7f - 0.7f * inE;
  if (!labBackdrop_)
    drawDeco(dl, decoForScreen(static_cast<int>(shell.screen()), shell.showsBoard()),
             decoMin, decoMax, theme_, iconStyle_, clock_, subjectFor(shell.screen()),
             inZoom, inE, objectYaw_, objectElev_);
  if (!labBackdrop_ && leaving_ != Deco::None && outE < 1.0f) {
    const float outZoom = deeper_ ? 1.0f + 0.8f * outE : 1.0f - 0.5f * outE;
    const VariantSpec* leavingSubject =
        leavingScreen_ >= 0 ? subjectFor(static_cast<app::Screen>(leavingScreen_))
                            : nullptr;
    drawDeco(dl, leaving_, decoMin, decoMax, theme_, iconStyle_, clock_, leavingSubject,
             outZoom, 1.0f - outE, objectYaw_, objectElev_);
  }

  // The piece designer's object *is* a board: the selected piece at its centre and the
  // cells its atoms reach, in the half the decoration would otherwise occupy.
  if (shell.screen() == app::Screen::Editor && editorPage_ == 1) {
    drawEditorPreview(shell, decoMin, decoMax);
  }
  // Whatever object this screen shows, the player can take hold of it and turn it.
  updateObjectDrag(shell, decoMin, decoMax);
  // And the library's object is the selected variant's overture: the 8x8 board becoming
  // whatever that variant plays on, and back again. It replaces the turning lattice,
  // which said only how many boards there were.
  if (shell.screen() == app::Screen::NewGame) {
    refreshOvertureCache(shell);
    if (!labBackdrop_) drawOvertureObject(shell, decoMin, decoMax, inZoom, inE);
  }

  hologramRequest_.seconds = clock_;
  hologramRequest_.yaw = objectYaw_;
  if (shell.screen() == app::Screen::NewGame) {
    hologramRequest_.kind = HologramRequest::Overture;
    hologramRequest_.projector = HologramRequest::Projector::Wall;
    hologramRequest_.radiusMetres = kWallHologramRadiusMetres;
  } else {
    switch (decoForScreen(static_cast<int>(shell.screen()), shell.showsBoard())) {
      case Deco::Manifold:
        hologramRequest_.kind = HologramRequest::Manifold;
        hologramRequest_.radiusMetres = kMenuHologramRadiusMetres;
        break;
      case Deco::Tesseract:
        hologramRequest_.kind = HologramRequest::Tesseract;
        break;
      default:
        break;
    }
  }

  // A hairline between the object and the menu, and the depth ladder on the far left.
  if (!labBackdrop_)
    dl->AddLine(ImVec2(split, min.y), ImVec2(split, max.y), u32(theme_.rule, 0.6f));

  // The ladder lives on the decoration side's outer edge. The menu side is full of
  // list rows and buttons that reach the margins; the object is centred in its own half
  // and leaves the edge clear.
  const int depth = app::screenDepth(shell.screen());
  const char* rungs[]{"shell", "choose", "play"};
  auto* small = static_cast<ImFont*>(fontMono_);
  const float ladderX = decoLeft ? min.x + px(20.0f) : max.x - px(80.0f);
  for (int i = 0; i < 3; ++i) {
    const float y = max.y - px(96.0f) + static_cast<float>(i) * px(20.0f);
    const bool on = i <= depth;
    const ImVec2 c(ladderX, y);
    const float r = px(3.5f);
    const ImVec2 quad[4]{ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r),
                         ImVec2(c.x - r, c.y)};
    if (on) {
      dl->AddConvexPolyFilled(quad, 4, u32(theme_.ember));
    } else {
      dl->AddPolyline(quad, 4, u32(theme_.rule), ImDrawFlags_Closed, 1.0f);
    }
    if (small != nullptr) {
      dl->AddText(small, small->FontSize, ImVec2(c.x + px(10.0f), y - px(6.0f)),
                  u32(on ? theme_.boneDim : theme_.rule), rungs[i]);
    }
  }
}

void Ui::drawEditorPreview(app::Shell& shell, const ImVec2& min, const ImVec2& max) {
  const app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const std::vector<std::string> names = editor->pieceNames();
  if (names.empty()) return;
  const int index = std::clamp(editorPiece_, 0, static_cast<int>(names.size()) - 1);
  auto atoms = editor->pieceAtoms(names[static_cast<std::size_t>(index)]);
  if (!atoms.has_value()) return;

  // A piece is declared once and played on whatever board a variant declares, so the
  // question "what does this do on a four-dimensional board" is one the designer has to
  // answer. The extent shrinks as the dimension grows for the obvious reason: nine to
  // the fourth is six and a half thousand cells and nothing can be read in it.
  const int dims = std::clamp(previewDims_, 2, 4);
  const int extent = dims == 2 ? 9 : (dims == 3 ? 7 : 5);
  std::vector<AxisDecl> decls;
  static const char* kAxisNames[4]{"file", "rank", "level", "aeon"};
  for (int i = 0; i < dims; ++i) {
    AxisDecl d;
    d.extent = extent;
    d.name = kAxisNames[i];
    decls.push_back(d);
  }
  auto spec = DimSpec::create(decls);
  if (!spec.has_value()) return;
  const DimSpec& ds = *spec;
  const view::ViewConfig cfg = view::ViewConfig::forBoard(ds);
  const std::vector<view::Placement> cells = view::layout(ds, cfg);
  if (cells.empty()) return;

  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const view::Theme& t = theme_;
  dl->AddRectFilled(min, max, u32(t.ink));

  // Where the piece stands: the middle of the board on every axis.
  Coord centre(static_cast<std::uint8_t>(dims));
  for (std::size_t i = 0; i < static_cast<std::size_t>(dims); ++i) {
    centre.c[i] = static_cast<std::int16_t>(extent / 2);
  }
  const CellId origin = ds.toCell(centre);

  std::vector<std::uint8_t> mark(ds.cellCount(), 0);
  std::vector<std::uint8_t>& occupied = previewOcc_[static_cast<std::size_t>(dims)];
  occupied.resize(ds.cellCount(), 0);
  occupied[origin] = 0;
  const int orientIndex = editor->orientationAxis() == 0 ? 0 : 1;
  for (const MoveAtom& a : *atoms) {
    const std::vector<Direction> dirs =
        expandAtomFor(a, static_cast<std::uint8_t>(dims),
                      editor->orientationAxis() >= 0 ? orientIndex : -1, Color::White);
    const StepRange range = stepRange(a);
    const int first = std::max(1, static_cast<int>(range.lo));
    const int last = range.hi == kUnlimited
                         ? extent - 1
                         : std::min(static_cast<int>(range.hi), extent - 1);
    for (const Direction& d : dirs) {
      // A hop must clear exactly one occupied cell, and only lands beyond it.
      bool hurdled = false;
      for (int n = 1; n <= last; ++n) {
        Coord p = centre;
        for (std::uint8_t k = 0; k < d.nsup; ++k) {
          const std::uint8_t ax = d.sup[k];
          p.c[ax] = static_cast<std::int16_t>(p.c[ax] + n * d.v[ax]);
        }
        if (!ds.inRange(p)) break;
        const CellId c = ds.toCell(p);
        const std::uint8_t occ = occupied[c];
        if (a.mode == MoveMode::Hop) {
          if (!hurdled) {
            if (occ != 0) hurdled = true;
            continue;
          }
          if (n < first) {
            if (occ != 0) break;
            continue;
          }
          if (occ != 0) {
            if (occ == 1 && a.capture != CapturePolicy::Cannot) mark[c] = 2;
            break;
          }
          if (a.capture != CapturePolicy::Must && mark[c] == 0) mark[c] = 1;
          continue;
        }
        if (n < first) {
          if (occ != 0 && a.mode != MoveMode::Leap) break;
          continue;
        }
        if (occ != 0) {
          if (occ == 1 && a.capture != CapturePolicy::Cannot) mark[c] = 2;
          if (a.mode != MoveMode::Leap) break;
          continue;
        }
        if (a.capture != CapturePolicy::Must && mark[c] == 0) mark[c] = 1;
      }
    }
  }

  // The camera. A flat board is looked at from almost straight down, as a diagram; a
  // solid one is stepped back so its depth reads. The drag adds to both, so the same
  // gesture that turns the menu's object turns this board.
  const float yaw = (dims == 2 ? 0.0f : 0.62f) + objectYaw_;
  const float elev = std::clamp((dims == 2 ? 1.50f : 0.92f) + objectElev_, 0.12f, 1.55f);
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);
  const float ce = std::cos(elev);
  const float se = std::sin(elev);
  const view::Bounds bounds = view::boundsOf(cells);
  const auto raw = [&](float x, float y, float z) {
    const float dx = x - bounds.centerX();
    const float dy = y - bounds.centerY();
    const float dz = z - bounds.centerZ();
    const float x1 = dx * cy + dy * sy;
    const float y1 = -dx * sy + dy * cy;
    const float up = dz * ce + y1 * se;
    const float depth = -dz * se + y1 * ce;
    return std::array<float, 3>{x1, -up, depth};
  };
  float ext = 0.001f;
  for (const view::Placement& pl : cells) {
    const auto q = raw(pl.x, pl.y, pl.z);
    ext = std::max(ext, std::max(std::abs(q[0]), std::abs(q[1])));
  }
  const float span = std::min(max.x - min.x, max.y - min.y);
  const float scale = span * 0.40f / (ext + 0.8f);
  const ImVec2 mid((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const auto at = [&](float x, float y, float z) {
    const auto q = raw(x, y, z);
    return ImVec2(mid.x + q[0] * scale, mid.y + q[1] * scale);
  };

  struct Tile {
    ImVec2 p[4];
    float depth{0};
    CellId cell{0};
    bool light{false};
  };
  std::vector<Tile> tiles;
  tiles.reserve(cells.size());
  for (const view::Placement& pl : cells) {
    const Coord p = ds.toCoord(pl.cell);
    int parity = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(dims); ++i) {
      parity += p.c[i];
    }
    Tile tile;
    tile.p[0] = at(pl.x - 0.46f, pl.y - 0.46f, pl.z);
    tile.p[1] = at(pl.x + 0.46f, pl.y - 0.46f, pl.z);
    tile.p[2] = at(pl.x + 0.46f, pl.y + 0.46f, pl.z);
    tile.p[3] = at(pl.x - 0.46f, pl.y + 0.46f, pl.z);
    tile.depth = raw(pl.x, pl.y, pl.z)[2];
    tile.cell = pl.cell;
    tile.light = (parity & 1) == 0;
    tiles.push_back(tile);
  }
  // Far to near: a board of boards overlaps itself from every useful angle, and without
  // the sort the far ones paint over the near ones.
  std::sort(tiles.begin(), tiles.end(),
            [](const Tile& a, const Tile& b) { return a.depth > b.depth; });

  const bool solid = dims == 2;
  for (const Tile& tile : tiles) {
    const view::Rgba base = tile.light ? t.boardLight : t.boardDark;
    // Above two dimensions the board is mostly cells the piece cannot reach - a knight
    // on a 5^4 lattice lights 48 of 625 - so the ones it can have to win. The board
    // steps back and the marks stay at full strength.
    const bool lit = mark[tile.cell] != 0;
    const float ground = solid ? 1.0f : (lit ? 0.9f : 0.34f);
    dl->AddQuadFilled(tile.p[0], tile.p[1], tile.p[2], tile.p[3], u32(base, ground));
    if (mark[tile.cell] == 2) {
      dl->AddQuadFilled(tile.p[0], tile.p[1], tile.p[2], tile.p[3], u32(t.blood, 0.7f));
    } else if (mark[tile.cell] == 1) {
      dl->AddQuadFilled(tile.p[0], tile.p[1], tile.p[2], tile.p[3], u32(t.moss, 0.65f));
    }
    if (tile.cell == origin) {
      dl->AddQuad(tile.p[0], tile.p[1], tile.p[2], tile.p[3], u32(t.ember), 2.0f);
    }
  }

  // The pieces: the selected one at the centre, and whatever has been placed, far to near
  // like the tiles so a stack of boards overlaps the right way round.
  PieceTypeDef shapeDef;
  shapeDef.atoms = *atoms;
  const Archetype shape = archetypeFor(shapeDef);
  const float cell =
      std::hypot(at(1, 0, 0).x - at(0, 0, 0).x, at(1, 0, 0).y - at(0, 0, 0).y);
  for (auto it = tiles.begin(); it != tiles.end(); ++it) {
    const std::uint8_t occ = it->cell == origin ? 3 : occupied[it->cell];
    if (occ == 0) continue;
    const ImVec2 c((it->p[0].x + it->p[1].x + it->p[2].x + it->p[3].x) * 0.25f,
                   (it->p[0].y + it->p[1].y + it->p[2].y + it->p[3].y) * 0.25f);
    const float r = cell * (dims == 2 ? 0.36f : 0.30f);
    dl->AddCircleFilled(c, r, u32(t.pieceToken), 18);
    if (occ == 3) {
      drawPieceGlyph(dl, c, r * 0.84f, u32(t.whitePiece), u32(t.pieceToken), shape,
                     iconStyle_);
    } else {
      drawPieceGlyph(dl, c, r * 0.84f, u32(occ == 1 ? t.blackPiece : t.whitePiece),
                     u32(t.pieceToken), Archetype::Dome, iconStyle_);
    }
  }

  // The pointer. Left click places the chosen piece on the cell under it - in any
  // dimension, by hit-testing the drawn quads nearest first - and right or middle drag
  // turns the board (`updateObjectDrag`). Nothing is a window over the board, so neither
  // gesture can be eaten by one.
  ImGuiIO& io = ImGui::GetIO();
  previewHover_ = -1;
  const bool over = !io.WantCaptureMouse && io.MousePos.x >= min.x &&
                    io.MousePos.x < max.x && io.MousePos.y >= min.y &&
                    io.MousePos.y < max.y;
  if (over) {
    const auto inside = [&](const Tile& q) {
      float sign = 0.0f;
      for (int i = 0; i < 4; ++i) {
        const ImVec2& a = q.p[i];
        const ImVec2& b = q.p[(i + 1) % 4];
        const float cr =
            (b.x - a.x) * (io.MousePos.y - a.y) - (b.y - a.y) * (io.MousePos.x - a.x);
        if (cr != 0.0f) {
          if (sign != 0.0f && (cr > 0.0f) != (sign > 0.0f)) return false;
          sign = cr;
        }
      }
      return true;
    };
    for (auto it = tiles.rbegin(); it != tiles.rend(); ++it) {
      if (!inside(*it)) continue;
      previewHover_ = static_cast<std::int64_t>(it->cell);
      dl->AddQuad(it->p[0], it->p[1], it->p[2], it->p[3], u32(t.bone, 0.9f), 2.0f);
      if (it->cell != origin && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        std::uint8_t& slot = occupied[it->cell];
        // Pressing the same piece again takes it off: one gesture to place, one to undo.
        slot = slot == static_cast<std::uint8_t>(previewTool_)
                   ? static_cast<std::uint8_t>(0)
                   : static_cast<std::uint8_t>(previewTool_);
      }
      break;
    }
  }
}

void Ui::pauseFrame(UiRequest& request, ImVec2& menuMin, ImVec2& menuMax) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 vmin = vp->WorkPos;
  const ImVec2 vmax(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
  const float split = vmin.x + (vmax.x - vmin.x) * 0.52f;
  menuMin = ImVec2(split, vmin.y);
  menuMax = ImVec2(vmax.x, vmax.y);
  // The board keeps the left of the frame; a screen that draws its own object - the move
  // diagrams - puts it there.
  decoMin_ = vmin;
  decoMax_ = ImVec2(split, vmax.y);
  // A departing ghost must not lay the scrim a second time.
  if (ghosting_) return;
  ImGui::GetBackgroundDrawList()->AddRectFilled(vmin, vmax, u32(theme_.ink, 0.42f));
  request.boardRect[0] = vmin.x;
  request.boardRect[1] = vmin.y;
  request.boardRect[2] = split - vmin.x;
  request.boardRect[3] = vmax.y - vmin.y;
}

void Ui::drawCheckEdges(app::Shell& shell) {
  if (!shell.showsCheckWarning()) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 min = vp->WorkPos;
  const ImVec2 max(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  // A soft red vignette at the frame's edge: a few nested outlines, strongest at the
  // very edge and fading inward, so the whole screen reads as "in check" without a panel.
  constexpr int kRings = 7;
  const float band = px(9.0f);
  for (int i = 0; i < kRings; ++i) {
    const float inset = static_cast<float>(i) * band;
    const float alpha = 0.30f * (1.0f - static_cast<float>(i) / kRings);
    dl->AddRect(ImVec2(min.x + inset, min.y + inset),
                ImVec2(max.x - inset, max.y - inset), u32(theme_.blood, alpha), 0.0f, 0,
                band);
  }
}

void Ui::drawCinemaLook() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 min = vp->WorkPos;
  const ImVec2 max(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  // The grade, before the vignette. A very low-alpha crush plus a faint wash of the
  // theme's accent is the cheap honest version of a colour curve: a black overlay cannot
  // truly raise contrast, but it does read as a graded frame, and it costs one more 2-D
  // rectangle rather than a second render target and a fullscreen pass. `ember` is the
  // one accent, so the wash is cool on manifold and warm on console without a second,
  // cinema-only colour.
  //
  // The vignette is `drawCheckEdges`'s nested-outline technique, neutral instead of red.
  // It is a dark neutral taken from `shadow`, not `ink`/`soot`: those are the *page*
  // colours and would lighten the light theme, exactly the trap M18.2 hit.
  const view::Rgba dark = theme_.shadow.withAlpha(1.0f);
  dl->AddRectFilled(min, max, u32(dark, 0.06f));
  dl->AddRectFilled(min, max, u32(theme_.ember, 0.04f));
  // Many thin rings rather than the check warning's seven fat ones. The check glow is a
  // shout and reads as one; a cinema vignette has to be soft, and seven steps this size
  // look like a picture frame. The alpha falls off quadratically so the weight sits at
  // the very edge and the innermost rings are almost nothing.
  constexpr int kRings = 80;
  const float band = px(1.5f);
  for (int i = 0; i < kRings; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kRings);
    const float inset = static_cast<float>(i) * band;
    const float alpha = 0.40f * (1.0f - t) * (1.0f - t);
    dl->AddRect(ImVec2(min.x + inset, min.y + inset),
                ImVec2(max.x - inset, max.y - inset), u32(dark, alpha), 0.0f, 0, band);
  }
}

float Ui::outEase() const noexcept {
  if (move_ != app::ScreenMove::Cut)
    return 1.0f;  // the move has its own fx; no ghost deco
  return smoothstep(enter_ / kOutEnd);
}

float Ui::inEase() const noexcept {
  if (move_ != app::ScreenMove::Cut) return moveFx().arriveAlpha;
  return smoothstep((enter_ - kInStart) / (1.0f - kInStart));
}

Ui::PaneMove Ui::paneMove() const noexcept {
  if (implantActive_ && move_ == app::ScreenMove::Station) {
    if (ghosting_) return {1, 1, 1};
    return {1,
            (enter_ >= 1 && !pinnedPanelAge_) || implantPanelAge(false) >= 0 ? 1.f : 0.f,
            1};
  }
  if (move_ != app::ScreenMove::Cut) {
    // A move into or out of the game (section 6): the panel traces on or off in place,
    // with no grow or swell. Calm moves cross-fade with the edge complete.
    const app::MoveFx fx = moveFx();
    return ghosting_ ? PaneMove{1.0f, fx.leaveAlpha, fx.leaveTrace}
                     : PaneMove{1.0f, fx.arriveAlpha, fx.arriveTrace};
  }
  // Pause and the reference panels sit over the board and do not travel as panes; they
  // keep the old grow-in, which rides only the arriving half of the clock.
  const int which = ghosting_ ? leavingScreen_ : currentScreen_;
  if (!isMenuScreen(static_cast<app::Screen>(which))) {
    const float inE = inEase();
    return {0.66f + 0.34f * inE, inE};
  }
  if (ghosting_) {
    // The screen being left swells towards the viewer on the way in, and shrinks away on
    // the way back out. It has faded to nothing by the time the next one starts.
    const float outE = outEase();
    return {deeper_ ? 1.0f + 0.35f * outE : 1.0f - 0.34f * outE, 1.0f - outE};
  }
  // The screen arriving grows from small on the way in; on the way back it returns from
  // the size the outgoing one left it at, so the two moves are reverses.
  const float inE = inEase();
  return {deeper_ ? 0.66f + 0.34f * inE : 1.35f - 0.35f * inE, inE};
}

void Ui::drawGhost(app::Shell& shell) {
  if (leavingScreen_ < 0) return;
  const auto leaving = static_cast<app::Screen>(leavingScreen_);
  if (move_ == app::ScreenMove::Cut) {
    if (outEase() >= 1.0f) return;
    if (!isMenuScreen(leaving) || !isMenuScreen(static_cast<app::Screen>(currentScreen_)))
      return;
  } else {
    // Only the moves that take a panel away leave one to draw: the menu off to the game,
    // the pause panel off to the game.
    const app::MoveFx fx = moveFx();
    if (implantActive_ && move_ == app::ScreenMove::Station &&
        (!view::implantLeaving(implantPanelAge(true), intensity_).visible() ||
         implantMoveAge_ >= implantLeavingUntil_))
      return;
    const bool implantGhost =
        implantActive_ && move_ == app::ScreenMove::Station &&
        implantMoveAge_ < implantLeavingUntil_ &&
        view::implantLeaving(implantPanelAge(true), intensity_).visible();
    if ((!implantGhost && (fx.done || fx.leaveAlpha <= 0.0f)) ||
        (!isMenuScreen(leaving) &&
         !(move_ == app::ScreenMove::Station && leaving == app::Screen::Welcome)))
      return;
  }
  // Disabled while it is drawn: a departing screen must be seen but not touched. Its
  // request is discarded too, so re-running its build can neither start a game nor
  // change a setting.
  ghosting_ = true;
  ImGui::BeginDisabled();
  switch (leaving) {
    case app::Screen::Welcome:
      (void)buildWelcome(shell);
      break;
    case app::Screen::MainMenu:
      (void)buildMainMenu(shell);
      break;
    case app::Screen::NewGame:
      (void)buildNewGame(shell);
      break;
    case app::Screen::Watch:
      (void)buildWatch(shell);
      break;
    case app::Screen::Editor:
      (void)buildEditor(shell);
      break;
    case app::Screen::Settings:
      (void)buildSettings(shell);
      break;
    case app::Screen::QuitConfirm:
      (void)buildQuitConfirm(shell);
      break;
    case app::Screen::PauseQuitConfirm:
      (void)buildPauseQuitConfirm(shell);
      break;
    case app::Screen::Paused:
      (void)buildPause(shell);
      break;
    case app::Screen::GameInfo:
      (void)buildGameInfo(shell);
      break;
    case app::Screen::PieceMoves:
      (void)buildPieceMoves(shell);
      break;
    default:
      break;
  }
  ImGui::EndDisabled();
  ghosting_ = false;
}

void Ui::drawDebugConsole(app::Shell& shell) {
  if (ImGui::IsKeyPressed(ImGuiKey_F12, false) ||
      (!ImGui::GetIO().WantTextInput &&
       ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))) {
    consoleOpen_ = !consoleOpen_;
    consoleFocus_ = consoleOpen_;
  }
  if (!consoleOpen_) return;
  ImGui::SetNextWindowSize(ImVec2(640, 360), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
  // A tool stays legible under the frameless implant theme: it keeps its own backing.
  ImGui::PushStyleColor(ImGuiCol_WindowBg, col(theme_.soot, 0.96f));
  const bool opened = ImGui::Begin("Debug console (F12)", &consoleOpen_);
  ImGui::PopStyleColor();
  recordToolRect();
  if (opened) {
    const float footer = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("scrollback", ImVec2(0, -footer), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& l : console_.log()) ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f || consoleFocus_)
      ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    if (consoleFocus_) {
      ImGui::SetKeyboardFocusHere();
      consoleFocus_ = false;
    }
    ImGui::SetNextItemWidth(-1);
    const auto recall = [](ImGuiInputTextCallbackData* d) -> int {
      auto* self = static_cast<Ui*>(d->UserData);
      const auto& h = self->console_.history();
      if (h.empty()) return 0;
      if (d->EventKey == ImGuiKey_UpArrow)
        self->consoleHistoryPos_ = self->consoleHistoryPos_ < 0
                                       ? static_cast<int>(h.size()) - 1
                                       : std::max(0, self->consoleHistoryPos_ - 1);
      else if (d->EventKey == ImGuiKey_DownArrow)
        self->consoleHistoryPos_ =
            self->consoleHistoryPos_ < 0 ? -1 : self->consoleHistoryPos_ + 1;
      if (self->consoleHistoryPos_ >= static_cast<int>(h.size()))
        self->consoleHistoryPos_ = -1;
      d->DeleteChars(0, d->BufTextLen);
      if (self->consoleHistoryPos_ >= 0)
        d->InsertChars(0, h[static_cast<std::size_t>(self->consoleHistoryPos_)].c_str());
      return 0;
    };
    if (ImGui::InputText(
            "##cmd", consoleInput_.data(), consoleInput_.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
            recall, this)) {
      console_.run(shell, consoleInput_.data());
      consoleInput_[0] = '\0';
      consoleHistoryPos_ = -1;
      consoleFocus_ = true;
    }
  }
  ImGui::End();
}

UiRequest Ui::build(app::Shell& shell, float fps) {
  implantRecords_.clear();
  toolRects_.clear();
  seamHover_.reset();
  const widgets::ScopedVisualTime visualTime(visualTime_);
  updateKeyboardNavigation(shell);
  // Start the frame with an empty field list. Only the board screen fills it; every other
  // screen, and cinema, leaves it empty so `recordBackground` submits nothing.
  resetBoardFieldList();
  // Cinema is the board and the framing look, nothing else: no screen, no rails, no
  // transition. The caller records this pass, so drawing only the look here is what keeps
  // the furniture out while still compositing the grade and vignette over the board.
  if (cinema_) {
    hologramRequest_ = {};
    drawCinemaLook();
    return UiRequest{};
  }
  // A screen change starts the camera moving and shoves the field towards the viewer -
  // or away from it, on the way back out.
  drawDebugConsole(shell);
  const int screen = static_cast<int>(shell.screen());
  currentScreen_ = screen;
  // A console preview names the screen it pretends to come from; a real change comes from
  // the one drawn last.
  const bool previewed = shell.transitionSerial() != lastTransitionSerial_;
  if (screen != lastScreen_ || previewed) {
    const auto currentCamera = labCamera();
    const bool interrupted = move_ == app::ScreenMove::Station && enter_ < 1;
    leavingHologram_ = hologramRequest();
    overtureLeaving_ = overtureCache_;
    leavingVariant_ = pickedVariant_;
    if (previewed) {
      lastTransitionSerial_ = shell.transitionSerial();
      lastScreen_ = static_cast<int>(shell.transitionFrom());
      leavingHologram_ = {};
      leavingHologram_.seconds = clock_;
      leavingHologram_.yaw = objectYaw_;
      if (shell.transitionFrom() == app::Screen::NewGame) {
        refreshOvertureCache(shell);
        overtureLeaving_ = overtureCache_;
        leavingHologram_.kind = HologramRequest::Overture;
        leavingHologram_.projector = HologramRequest::Projector::Wall;
        leavingHologram_.radiusMetres = kWallHologramRadiusMetres;
      } else {
        switch (decoForScreen(lastScreen_, false)) {
          case Deco::Manifold:
            leavingHologram_.kind = HologramRequest::Manifold;
            leavingHologram_.radiusMetres = kMenuHologramRadiusMetres;
            break;
          case Deco::Tesseract:
            leavingHologram_.kind = HologramRequest::Tesseract;
            break;
          default:
            break;
        }
      }
    }
    move_ = lastScreen_ < 0 ? app::ScreenMove::Cut
                            : app::classifyMove(static_cast<app::Screen>(lastScreen_),
                                                shell.screen());
    // The lab's game moves (M24.5): the slabs travel, so the clock runs their length.
    if (lastScreen_ >= 0) rigFrom_ = static_cast<app::Screen>(lastScreen_);
    if (rigEnabled_ && move_ == app::ScreenMove::MenuToGame) {
      move_ = app::ScreenMove::Deploy;
      rigLock_ = rigDeployLock_;
      rigShutdown_ = 0;
      rigTotal_ = std::max(app::kDeploySeconds, rigDeployLock_ + 1.0f);
    } else if (rigEnabled_ && move_ == app::ScreenMove::GameToMenu) {
      move_ = app::ScreenMove::Stow;
      rigShutdown_ = rigStowShutdown_;
      rigLock_ = rigShutdown_ + std::max(rigStowTravel_, app::kStowFlightMinSeconds);
      rigTotal_ = rigLock_ + app::kStowMenuSeconds;
    }
    labLeaving_ = labLast_;
    if (previewed && stationCamera_ && lastScreen_ >= 0) {
      const auto from = static_cast<app::Screen>(lastScreen_);
      labLeaving_ = labBackdrop_ && from != app::Screen::Editor &&
                    from != app::Screen::Game && from != app::Screen::Paused &&
                    from != app::Screen::PauseQuitConfirm &&
                    stationCamera_(from).has_value();
    }
    const bool arrivingLab =
        labBackdrop_ && !shell.showsBoard() && shell.screen() != app::Screen::Editor;
    toCam_ =
        arrivingLab && stationCamera_ ? stationCamera_(shell.screen()) : std::nullopt;
    if (move_ == app::ScreenMove::Cut && labLeaving_ && arrivingLab && stationCamera_ &&
        lastScreen_ >= 0) {
      fromCam_ = interrupted && currentCamera
                     ? currentCamera
                     : stationCamera_(static_cast<app::Screen>(lastScreen_));
      if (fromCam_ && toCam_) {
        move_ = app::ScreenMove::Station;
        stationSeconds_ = view::rigMoveSeconds(*fromCam_, *toCam_, 1 / kCellsPerMetre);
      }
    }
    const bool deeper = lastScreen_ < 0 ||
                        goesDeeper(static_cast<app::Screen>(lastScreen_), shell.screen());
    // Going in, the field rushes towards and past the viewer; coming back it recedes.
    // The impulse is negative for "towards the viewer" - see DepthField::push.
    field_.push(deeper ? -3.1f : 2.6f);
    // Remember what is leaving, and from which screen, so its pane and its decoration can
    // be drawn once more on the way out. The ghost's input is disabled and its request
    // discarded, so re-running its build cannot navigate.
    leaving_ = lastScreen_ >= 0 ? decoForScreen(lastScreen_) : Deco::None;
    leavingScreen_ = lastScreen_;
    deeper_ = deeper;
    enter_ = pinnedMoveAge_ && currentMoveSeconds() > 0
                 ? std::clamp(*pinnedMoveAge_ / currentMoveSeconds(), 0.f, 1.f)
                 : 0.f;
    implantMoveAge_ = pinnedMoveAge_.value_or(0.f);
    implantLeavingUntil_ =
        implantActive_ && move_ == app::ScreenMove::Station
            ? view::kImplantDissolveSeconds + view::kImplantAfterimageSeconds
            : 0.f;
    lastScreen_ = screen;
    // The library always opens on standard. It is the board every overture departs from,
    // so it is the one that makes the next choice legible - and reopening on whatever
    // was picked last time means arriving mid-thought at a shape with no context.
    // Not when a capture has already said what it wants to see: `--shot --screen newgame`
    // pins the overture before the first frame is built, and this hook runs on that very
    // frame - so without the guard every capture would come back showing standard.
    if (shell.screen() == app::Screen::NewGame && !shell.overtures().pinned()) {
      const auto& lib = shell.library();
      if (std::find(lib.begin(), lib.end(), "standard") != lib.end()) {
        pickedVariant_ = "standard";
        shell.overtures().jumpTo(app::Overture::Standard);
      }
    }
  }

  labLast_ = labBackdrop_ && !shell.showsBoard() && shell.screen() != app::Screen::Editor;
  hologramRequest_ = {};
  // The departing menu first, so the arriving one is drawn over it.
  drawGhost(shell);

  // Game to menu: for the first 0.8 s the board powers down and the menu is not there
  // yet. Nothing is built, so nothing can be clicked; any key or click skips to the menu.
  if ((move_ == app::ScreenMove::GameToMenu || move_ == app::ScreenMove::Stow) &&
      moveFx().holdsBoard) {
    UiRequest held;
    held.boardRect[0] = 0;
    held.boardRect[1] = 0;
    held.boardRect[2] = ImGui::GetMainViewport()->Size.x;
    held.boardRect[3] = ImGui::GetMainViewport()->Size.y;
    return held;
  }

  UiRequest request;
  const bool waiting = move_ == app::ScreenMove::Station && !paneInteractive();
  if (waiting) ImGui::BeginDisabled();
  switch (shell.screen()) {
    case app::Screen::Welcome:
      request = buildWelcome(shell);
      break;
    case app::Screen::MainMenu:
      request = buildMainMenu(shell);
      break;
    case app::Screen::NewGame:
      request = buildNewGame(shell);
      break;
    case app::Screen::Watch:
      request = buildWatch(shell);
      break;
    case app::Screen::Editor:
      request = buildEditor(shell);
      break;
    case app::Screen::QuitConfirm:
      request = buildQuitConfirm(shell);
      break;
    case app::Screen::PauseQuitConfirm:
      request = buildPauseQuitConfirm(shell);
      break;
    case app::Screen::Settings:
      request = buildSettings(shell);
      break;
    case app::Screen::GameInfo:
      request = buildGameInfo(shell);
      break;
    case app::Screen::PieceMoves:
      request = buildPieceMoves(shell);
      break;
    case app::Screen::Paused:
      request = buildPause(shell);
      break;
    case app::Screen::Game:
      request = buildGameHud(shell, fps);
      break;
  }
  if (previewed && move_ == app::ScreenMove::Station) {
    // A console/CLI preview starts at its named source, independent of the last
    // real screen. The ghost Library has now resolved its selected variant.
    rackCount_ = 0;
    if (shell.transitionFrom() == app::Screen::NewGame)
      if (const auto* spec = shell.preview(pickedVariant_))
        rackCount_ = view::rackSlabCount(spec->dims);
    for (std::size_t i = 0; i < rackFrom_.size(); ++i)
      rackFrom_[i] = static_cast<int>(i) < rackCount_ ? .06f : 0.f;
    rackAge_ = 0;
    rackScreenChange_ = true;
  }
  int rackCount = 0;
  if (labBackdrop_ && shell.screen() == app::Screen::NewGame)
    if (const auto* spec = shell.preview(pickedVariant_))
      rackCount = view::rackSlabCount(spec->dims);
  if (rackCount != rackCount_) {
    rackFrom_ = rackSlides();
    rackCount_ = rackCount;
    rackAge_ = 0;
    rackScreenChange_ = enter_ < 1;
  }
  if (waiting) ImGui::EndDisabled();
  drawCheckEdges(shell);
  // An action may have closed a modal or changed screens. Prepare navigation before
  // the next NewFrame processes queued Tab/Space events with the new ownership.
  updateKeyboardNavigation(shell);
  return request;
}

void Ui::buildTrailerEditRows(app::Shell& shell, app::Session& session,
                              UiRequest& request, ImFont* small, bool editing) {
  const view::Theme& t = theme_;
  auto note = [&](const char* text) {
    ImGui::SameLine();
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  };
  char text[128];

  if (editing) {
    // Row 1 - where in the trailer the camera is being set up. STEP moves by the grid:
    // a whole move, or a half, quarter or eighth of one, so a camera that has to circle
    // something can be keyed several times inside a single move.
    if (button("< STEP", t, px(92), false, true, true, actionFont())) {
      (void)shell.trailerStepFraction(-1);
    }
    ImGui::SameLine();
    if (button("STEP >", t, px(92), false, true, true, actionFont())) {
      (void)shell.trailerStepFraction(+1);
    }
    ImGui::SameLine();
    char grid[16];
    std::snprintf(grid, sizeof(grid), shell.trailerGrid() == 1 ? "GRID 1" : "GRID 1/%d",
                  shell.trailerGrid());
    if (button(grid, t, px(108), false, true, true, actionFont())) {
      const int g = shell.trailerGrid();
      shell.setTrailerGrid(g == 1 ? 2 : g == 2 ? 4 : g == 4 ? 8 : 1);
    }
    ImGui::SameLine();
    if (button("< BEAT", t, px(92), false, true, true, actionFont())) {
      shell.requestTrailerJumpBeat(-1);
    }
    ImGui::SameLine();
    if (button("BEAT >", t, px(92), false, true, true, actionFont())) {
      shell.requestTrailerJumpBeat(+1);
    }
    std::snprintf(text, sizeof(text), "beat %zu of %zu  -  %s  -  move %zu + %zu/8",
                  shell.attractStep() + 1, shell.attractCount(),
                  shell.currentVariant().c_str(), session.game().moveHistory().size(),
                  shell.trailerEditSub());
    note(text);

    // Row 2 - keyframes. A keyframe is the view exactly as it stands, pinned to this
    // move.
    const app::CameraCue here{shell.currentVariant(), session.game().moveHistory().size(),
                              shell.attractStep() + 1, shell.trailerEditSub()};
    const app::CameraKeyframe* key = shell.cameraTrack().find(here);
    if (button("REC KEY", t, px(100), key != nullptr, true, true, actionFont())) {
      shell.recordCameraKeyframe(shell.trailerKeySpeed());
    }
    ImGui::SameLine();
    if (button("DEL KEY", t, px(100), false, true, key != nullptr, actionFont())) {
      (void)shell.eraseCameraKeyframe();
    }
    std::snprintf(text, sizeof(text), "%zu keys%s",
                  shell.cameraTrack().keyframes().size(),
                  key != nullptr ? "  -  key on this point" : "");
    note(text);
  }

  // Row 3 - the same shape-mode controls the game screen offers. The pose is what a
  // keyframe records; a button here sets the edit pose and the trailer eases to it.
  // Shown on every beat, flat ones too: the choices are the viewer's and carry over to
  // the shape beats that follow, so the opener must not hide them.
  const bool shaped = hasPlaySurface(session.variant());
  {
    app::TrailerPose& pose = shell.editPose();
    const bool formed = pose.formed > 0.5f;
    if (editing && shaped) {
      if (button("SHAPE", t, px(84), formed, true, true, actionFont())) {
        pose.formed = formed ? 0.0f : 1.0f;
      }
      ImGui::SameLine();
    }
    const bool inverted = shell.settings().geometryInvert;
    if (button("INVERT", t, px(92), inverted, true, true, actionFont())) {
      shell.settings().geometryInvert = !inverted;
      request.settingsChanged = true;
    }
    ImGui::SameLine();
    const bool ghost = (editing ? pose.ghost : shell.settings().geometryGhost) < 0.99f;
    if (button("GHOST", t, px(84), ghost, true, true, actionFont())) {
      shell.setTrailerGhost(ghost ? 1.0f : 0.35f);
    }
    ImGui::SameLine();
    const bool flat = shell.settings().shapePieceCylinders;
    if (button("PIECES", t, px(100), flat, true, true, actionFont())) {
      shell.setTrailerFlatPieces(!flat);
      request.settingsChanged = true;
    }
    if (editing && shaped && session.variant().dims.dims() > 2) {
      ImGui::SameLine();
      const bool spread = pose.shellSpread > 0.5f;
      if (button("SPREAD", t, px(92), spread, true, true, actionFont())) {
        pose.shellSpread = spread ? 0.0f : 1.0f;
      }
    }
    if (editing) {
      note(shaped ? "drag to orbit, middle-drag to slide, then REC KEY"
                  : "drag to orbit, then REC KEY");
    }
  }

  // How fast the trailer's shape morph and each piece's move play - the trailer's own
  // multipliers, so the player's game is untouched. Live: they apply as the slider moves.
  {
    app::Settings& st = shell.settings();
    bool changed = false;
    ImGui::SetNextItemWidth(px(150.0f));
    changed |=
        ImGui::SliderFloat("MORPH SPEED", &st.trailerMorphSpeed, 0.25f, 4.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(150.0f));
    changed |=
        ImGui::SliderFloat("MOVE SPEED", &st.trailerMoveSpeed, 0.25f, 4.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(150.0f));
    changed |= ImGui::SliderFloat("NEAR CLIP", &st.trailerNearClip, view::kMinNearClip,
                                  8.0f, "%.2f");
    if (changed) request.settingsChanged = true;
  }
}

float Ui::buildTrailerTimeline(app::Shell& shell) {
  const view::Theme& t = theme_;
  auto* small = static_cast<ImFont*>(fontSmall_);
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float margin = px(22.0f);
  const float height = px(46.0f);
  const float width = std::max(px(200.0f), vp->WorkSize.x - 2.0f * margin);
  const float total = std::max(1.0f, shell.trailerLength());

  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + margin, vp->WorkPos.y + vp->WorkSize.y - margin),
      ImGuiCond_Always, ImVec2(0.0f, 1.0f));
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##timeline", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const auto u32 = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };
  const float labelH = px(15.0f);
  const float y0 = origin.y + labelH;  // the track
  const float y1 = origin.y + height;
  // Padding at both ends: the first and last frames, and a keyframe sitting on either,
  // would otherwise be half off the track and hard to grab.
  const float pad = px(18.0f);
  const float trackW = width - 2.0f * pad;
  const auto xOf = [&](float g) { return origin.x + pad + g / total * trackW; };

  ImGui::InvisibleButton("##track", ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();

  // Beats: one segment each, the current one lit.
  ImGui::PushFont(small);
  for (std::size_t b = 0; b < shell.attractCount(); ++b) {
    const float xa = xOf(shell.beatOffset(b));
    const float xb = xOf(shell.beatOffset(b + 1));
    const bool current = b == shell.attractStep();
    dl->AddRectFilled(
        ImVec2(xa, y0), ImVec2(xb, y1),
        u32(col(t.boneFaint, current ? 0.40f : (b % 2 == 0 ? 0.18f : 0.28f))));
    dl->AddLine(ImVec2(xa, y0), ImVec2(xa, y1), u32(col(t.boneFaint, 0.55f)), 1.0f);
    // One tick per move when they are far enough apart to read.
    const std::size_t moves = shell.attractBeat(b).moves.size();
    if ((xb - xa) / static_cast<float>(moves + 1) >= px(5.0f)) {
      for (std::size_t m = 1; m <= moves; ++m) {
        const float x = xOf(shell.beatOffset(b) + static_cast<float>(m));
        dl->AddLine(ImVec2(x, y1 - px(6.0f)), ImVec2(x, y1), u32(col(t.boneFaint, 0.35f)),
                    1.0f);
      }
    }
    dl->PushClipRect(ImVec2(xa, origin.y), ImVec2(xb, y0), true);
    dl->AddText(ImVec2(xa + px(4.0f), origin.y), u32(col(current ? t.bone : t.boneFaint)),
                shell.attractBeat(b).variant.c_str());
    dl->PopClipRect();
  }
  ImGui::PopFont();

  // Keyframes, as diamonds. The one under the playhead is lit.
  const float here = shell.trailerTime();
  const float r = px(5.5f);
  const float ymid = 0.5f * (y0 + y1);
  for (const float time : shell.trailerKeyTimes()) {
    const float x = xOf(time);
    const bool lit = std::abs(time - here) < 1.0f / 16.0f;
    const ImU32 fill = u32(col(lit ? t.bone : t.ember));
    const ImVec2 pts[4] = {ImVec2(x, ymid - r), ImVec2(x + r, ymid), ImVec2(x, ymid + r),
                           ImVec2(x - r, ymid)};
    dl->AddConvexPolyFilled(pts, 4, fill);
    dl->AddPolyline(pts, 4, u32(col(t.boneFaint, 0.9f)), ImDrawFlags_Closed, 1.0f);
  }

  // Playhead.
  const float xh = xOf(here);
  dl->AddLine(ImVec2(xh, y0 - px(3.0f)), ImVec2(xh, y1), u32(col(t.bone)), px(2.0f));
  dl->AddTriangleFilled(ImVec2(xh - px(5.0f), y0 - px(8.0f)),
                        ImVec2(xh + px(5.0f), y0 - px(8.0f)), ImVec2(xh, y0 - px(2.0f)),
                        u32(col(t.bone)));

  // Scrubbing: edit mode only, and it snaps - to a keyframe when the pointer is near one,
  // otherwise to the grid in use.
  if (hovered || ImGui::IsItemActive()) {
    const float mx = ImGui::GetIO().MousePos.x;
    const float g = std::clamp((mx - origin.x - pad) / trackW * total, 0.0f, total);
    float snapTime = -1.0f;
    for (const float time : shell.trailerKeyTimes()) {
      if (std::abs(xOf(time) - mx) <= px(7.0f)) snapTime = time;
    }
    const app::TrailerCue cue =
        snapTime >= 0.0f ? shell.trailerCueAt(snapTime, 8) : shell.trailerCueAt(g);
    if (ImGui::IsItemActive()) shell.requestTrailerSeek(cue.beat, cue.ply, cue.sub);
    if (hovered) {
      ImGui::BeginTooltip();
      ImGui::Text("%s  -  move %zu + %zu/8%s",
                  shell.attractBeat(cue.beat).variant.c_str(), cue.ply, cue.sub,
                  snapTime >= 0.0f ? "  -  keyframe" : "");
      ImGui::TextUnformatted("click or drag to go there");
      ImGui::EndTooltip();
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();
  return height + px(8.0f);
}

UiRequest Ui::buildGameHud(app::Shell& shell, float fps) {
  UiRequest request;
  const view::Theme& t = theme_;
  app::Session& session = *shell.session();
  pinnedSeam_ = session.seamHoverPin();
  const VariantSpec& v = session.variant();
  const Game& game = session.game();
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  // No rails while playing. The board is the thing; everything else sits in a corner and
  // gets out of the way.
  request.boardRect[0] = 0.0f;
  request.boardRect[1] = 0.0f;
  request.boardRect[2] = vp->Size.x;
  request.boardRect[3] = vp->Size.y;

  // The ambient field, much quieter than the menus': wireframes only, a wider ring so the
  // bodies stay out towards the frame rather than washing over the board, and a low
  // opacity. The board is the subject; this is only the room it sits in (M18.4). It goes
  // to its own list, not ImGui's shared background list, so `recordBackground` can submit
  // it early - inside the board's pass, behind the board (M18 correction). The
  // coordinate labels further down stay on the shared list and the late overlay path.
  // Where the renderer draws the stars instead (an emitting board), the field is left
  // out: the game screen's room is the stars alone (M23.10 section 6).
  if (!view::gameScreenHasStars(theme_)) {
    boardField_.draw(boardFieldList_.get(), ImVec2(0.0f, 0.0f),
                     ImVec2(vp->Size.x, vp->Size.y), t, iconStyle_, false, true, 0.80f);
  }

  // File and rank labels. On the flat board they sit on the near edges rather than on its
  // cells: a label *inside* the edge cell is covered by whatever stands there and reads
  // as graffiti. A closed shape has no near edge, so the surface view labels the same two
  // reference rings the seam rails colour - the file letters along the rank-0 ring, the
  // rank numbers along the file-0 ring - each pushed out along its seat's own normal
  // (M17.21). Both views share the one `showCoordinates` toggle and the machine face, so
  // a coordinate reads as a measurement either way.
  //
  // Projected text only reads in the flat 2-D view, though: in a 3-D perspective a letter
  // skews, overlaps its neighbour, or goes nearly edge-on, so the whole block is gated on
  // the flat view and neither the solid board nor the shape draws it (M17.21 label
  // revision). The seam rails are presentation, not text, and stay.
  // Flat wins over the shape here exactly as it does in `optionsFor`: with both flags on,
  // the renderer draws the flat board, so the flat tokens are the pieces - skipping them
  // left a glued board with no pieces at all.
  const bool surfaceView = shell.settings().geometryView &&
                           hasPlaySurface(session.variant()) && !session.flatView();
  if (coordinateLabelsShown(shell.settings(), session)) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const view::OrbitCamera cam = session.camera();
    const float w = vp->Size.x;
    const float h = vp->Size.y;
    auto* mono = static_cast<ImFont*>(fontMono_ != nullptr ? fontMono_ : fontBody_);
    const float size = mono != nullptr ? mono->FontSize * 0.92f : ImGui::GetFontSize();
    const auto drawLabel = [&](const std::string& text,
                               const view::OrbitCamera::ScreenPoint& at,
                               const view::OrbitCamera::ScreenPoint& inward) {
      if (!at.visible || !inward.visible) return;
      // One cell further out than the edge cell, so the label clears the board.
      const float ox = at.x + (at.x - inward.x) * 0.65f;
      const float oy = at.y + (at.y - inward.y) * 0.65f;
      const ImVec2 sz = mono != nullptr
                            ? mono->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str())
                            : ImGui::CalcTextSize(text.c_str());
      const ImVec2 pos(ox - sz.x * 0.5f, oy - sz.y * 0.5f);
      if (mono != nullptr) {
        dl->AddText(mono, size, pos, u32(t.boneDim), text.c_str());
      } else {
        dl->AddText(pos, u32(t.boneDim), text.c_str());
      }
    };
    if (!surfaceView) {
      std::vector<view::OrbitCamera::ScreenPoint> screenOf(
          session.snapshot().cellCount());
      for (const view::Placement& pl : session.placements()) {
        screenOf[pl.cell] = cam.project(view::Vec3{pl.x, pl.y, pl.z}, w / h, w, h);
      }
      const int nx = static_cast<int>(v.dims.extent(0));
      const int nz = static_cast<int>(v.dims.extent(1));
      for (int f = 0; f < nx && nz > 1; ++f) {
        const CellId edge = v.dims.toCell(Coord::of({f, 0}));
        const CellId in = v.dims.toCell(Coord::of({f, 1}));
        drawLabel(std::string(1, static_cast<char>('a' + f)), screenOf[edge],
                  screenOf[in]);
      }
      for (int r = 0; r < nz && nx > 1; ++r) {
        const CellId edge = v.dims.toCell(Coord::of({0, r}));
        const CellId in = v.dims.toCell(Coord::of({1, r}));
        drawLabel(std::to_string(r + 1), screenOf[edge], screenOf[in]);
      }
    } else {
      const app::Settings& st = shell.settings();
      SurfacePose pose;
      pose.slideU = st.geometrySlideU + st.geometryAlignOffset;
      pose.slideV = st.geometrySlideV + st.geometryAlignOffsetV;
      pose.hyperSpinU = st.geometryHyperSpinU;
      pose.hyperSpinV = st.geometryHyperSpinV;
      pose.evert = st.geometryEvert;
      pose.twist = st.kleinTwist;
      pose.openness = st.geometryWidth;
      const PlaySurface surf = PlaySurface::build(v, pose);
      const view::Vec3 eye = cam.eye();
      for (const SurfaceSeat& seat : surf.seats()) {
        // A label on the far side of the shape would float in front of the near side, so
        // draw one only where its own seat faces the eye.
        if (view::dot(seat.normal, eye - seat.centre) <= 0.0f) continue;
        const float step = seat.rank == 0 ? seat.stepV : seat.stepU;
        const view::Vec3 outward = seat.centre + seat.normal * step;
        // The flat code moves the label away from an inward point; here the inward point
        // is the seat reflected across the outward one, so the label lands just off the
        // surface along the seat's own normal.
        const view::OrbitCamera::ScreenPoint at = cam.project(seat.centre, w / h, w, h);
        const view::OrbitCamera::ScreenPoint inward =
            cam.project(seat.centre * 2.0f - outward, w / h, w, h);
        if (seat.rank == 0) {
          drawLabel(std::string(1, static_cast<char>('a' + seat.file)), at, inward);
        }
        if (seat.file == 0) {
          drawLabel(std::to_string(seat.rank + 1), at, inward);
        }
      }
    }
  }

  const ImGuiWindowFlags overlayFlags =
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

  const float margin = px(16.0f);

  // ---------------------------------------------------------- variant, top left
  const float hudPad = px(8.0f);
  // One frame for every HUD panel (M23.10 section 7): the panel is drawn behind the
  // window's own content, sized from this frame's measured content extent.
  ImVec2 framedSize;
  const auto beginFramed = [&](const char* id, ImVec2 pos, ImVec2 pivot,
                               ImVec2 size = ImVec2(0, 0)) {
    framedSize = size;
    if (size.x > 0) ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(hudPad, hudPad));
    ImGui::Begin(
        id, nullptr,
        size.x > 0 ? overlayFlags & ~ImGuiWindowFlags_AlwaysAutoResize : overlayFlags);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::clamp(hudContent_, 0.0f, 1.0f));
  };
  HudRect lastPanel;
  const auto endFramed = [&](float level) {
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 maxPos = framedSize.x > 0 ? ImVec2(wp.x + framedSize.x - hudPad,
                                                    wp.y + framedSize.y - hudPad)
                                           : ImGui::GetCurrentWindow()->DC.CursorMaxPos;
    dl->ChannelsSetCurrent(0);
    widgets::hudPanel(dl, wp, ImVec2(maxPos.x + hudPad, maxPos.y + hudPad), t, px(1.0f),
                      hudDraw_, level);
    lastPanel = {wp.x, wp.y, maxPos.x + hudPad - wp.x, maxPos.y + hudPad - wp.y};
    dl->ChannelsMerge();
    ImGui::End();
    ImGui::PopStyleVar();
  };
  // The axis names with their arrows. The HUD font has no -> or up arrow, so where it
  // lacks the glyphs each arrow is drawn as three vector strokes (M23.11 section 7).
  struct LegendPart {
    std::string name;
    ArrowDir dir;
  };
  std::vector<LegendPart> axisLegend;
  const bool arrowGlyphs = small->FindGlyphNoFallback(0x2192) != nullptr &&
                           small->FindGlyphNoFallback(0x2191) != nullptr;
  if (shell.settings().showCoordinates) {
    const auto& axes = session.viewConfig().screenAxes;
    for (std::size_t i = 0; i < axes.size(); ++i) {
      axisLegend.push_back(
          {v.dims.name(axes[i]), i == 0 ? ArrowDir::Right : ArrowDir::Up});
    }
  }
  const char* topology = v.geom.isBox()          ? "flat board"
                         : v.geom.isOrientable() ? "glued board"
                                                 : "glued with a twist";
  std::vector<view::SeamLegendEntry> seamKey;
  if (shell.settings().showSeams && shell.settings().showSeamLegend && !v.geom.isBox())
    seamKey = view::seamLegend(session.seams(), session.viewConfig(), v.dims);
  const auto textWidth = [](ImFont* font, const std::string& text) {
    return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0, text.c_str()).x;
  };
  const float arrowSize = std::round(small->FontSize * 0.7f);
  // The old single-string legend separated axes with three spaces; keep that width.
  const float legendGap = textWidth(small, "   ");
  float legendWidth = 0;
  for (const auto& part : axisLegend) {
    const std::string text =
        arrowGlyphs ? part.name + (part.dir == ArrowDir::Right ? " \xE2\x86\x92"
                                                               : " \xE2\x86\x91")
                    : part.name;
    legendWidth += textWidth(small, text) + (arrowGlyphs ? 0 : px(4) + arrowSize) +
                   (legendWidth > 0 ? legendGap : 0);
  }
  const float metaWidth =
      textWidth(small, topology) + (axisLegend.empty() ? 0 : px(8) + legendWidth);
  float seamWidth = 0;
  for (const auto& e : seamKey)
    seamWidth +=
        px(21) + textWidth(small, e.axis + " edges") + (seamWidth > 0 ? px(8) : 0);
  const ImVec2 variantSize(
      std::ceil(std::max({textWidth(display, upper(shell.currentVariant())), metaWidth,
                          seamWidth}) +
                2 * hudPad),
      std::ceil(display->FontSize + px(2) + small->FontSize +
                (seamKey.empty() ? 0 : px(2) + std::max(small->FontSize, px(12))) +
                2 * hudPad));
  beginFramed("##variant", ImVec2(vp->WorkPos.x + margin, vp->WorkPos.y + margin),
              ImVec2(0, 0), variantSize);
  {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(2)));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::PushFont(display);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
    ImGui::TextUnformatted(upper(shell.currentVariant()).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    // A hairline under the name, struck like an engraved plate.
    const ImVec2 nameMax = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x, nameMax.y + px(3)),
                                        ImVec2(nameMax.x, nameMax.y + px(3)),
                                        u32(t.emberDeep), 1.0f);
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(v.geom.isBox() ? t.boneFaint : t.rift));
    ImGui::TextUnformatted(topology);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    // Name the screen axes, so a 3-D or 4-D board says which axis runs which way: a 4-D
    // board otherwise raises "which of these four is up?" as its first question.
    if (shell.settings().showCoordinates) {
      ImGui::SameLine(0, px(8));
      ImGui::PushFont(small);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      for (std::size_t i = 0; i < axisLegend.size(); ++i) {
        const auto& part = axisLegend[i];
        if (i > 0) ImGui::SameLine(0, legendGap);
        if (arrowGlyphs) {
          ImGui::TextUnformatted(
              (part.name +
               (part.dir == ArrowDir::Right ? " \xE2\x86\x92" : " \xE2\x86\x91"))
                  .c_str());
        } else {
          ImGui::TextUnformatted(part.name.c_str());
          ImGui::SameLine(0, px(4));
          ImGui::Dummy(ImVec2(arrowSize, small->FontSize));
          drawArrow(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(),
                    ImGui::GetItemRectMax(), part.dir, u32(t.boneFaint), px(1));
        }
      }
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
    // Name the seam colours, so "the a-file and the h-file are the same edge" is
    // readable off the board rather than something a player has to be told (M14.4). The
    // ramp is per-cell, so one swatch per glued axis is the honest reduction: it says
    // *which* edges meet, and the board's gradient says where along them. Drawn as
    // ordinary items, not free text, so the overlay window sizes to the labels.
    if (shell.settings().showSeams && shell.settings().showSeamLegend &&
        !v.geom.isBox() && !session.seams().empty()) {
      bool firstSeam = true;
      std::optional<std::uint8_t> hovered;
      for (const view::SeamLegendEntry& e : seamKey) {
        if (!firstSeam) ImGui::SameLine(0, px(8));
        firstSeam = false;
        ImGui::BeginGroup();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + px(2)),
                                                  ImVec2(p.x + px(11), p.y + px(13)),
                                                  u32(e.color), px(2));
        const std::string label = e.axis + " edges";
        ImGui::Dummy(ImVec2(px(15), px(12)));
        ImGui::SameLine(0.0f, px(6));
        ImGui::PushFont(small);
        ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
        ImGui::TextUnformatted(label.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) hovered = e.screenAxis;
      }
      seamHover_ = pinnedSeam_ ? pinnedSeam_ : hovered;
    }
    ImGui::PopStyleVar();
  }
  endFramed(hudLevel_);
  const HudRect variantRect = lastPanel;

  // ------------------------------------------------------------ status, top right
  const GameResult result = game.result();
  const float fxIntensity = shell.settings().presentationIntensity;
  const bool whiteMoves = game.position().sideToMove() == Color::White;
  std::vector<HudTextRow> status;
  view::TurnFlip flip{1.0f, 1.0f};
  if (result == GameResult::InProgress) {
    HudTextRow turn{whiteMoves ? "WHITE TO MOVE" : "BLACK TO MOVE", display, t.ember};
    // The turn flip: the other side's text fades out as this one fades in.
    if (hudEvents_.turn) {
      flip = view::turnFlip(*hudEvents_.turn, fxIntensity);
      turn.from = whiteMoves ? "BLACK TO MOVE" : "WHITE TO MOVE";
      turn.fromColour = t.ember;
      turn.mix = flip.mix;
    }
    status.push_back(std::move(turn));
  } else {
    status.push_back({upper(std::string(toString(result))), display, t.blood});
  }
  char statusLine[128];
  std::snprintf(statusLine, sizeof(statusLine), "ply %zu \xC2\xB7 %zu legal",
                game.plyCount(), game.legalMoves().size());
  status.push_back({statusLine, small, t.boneDim});
  if (result != GameResult::InProgress) {
    status.push_back(
        {"by " + std::string(toString(game.endReason())), small, t.boneFaint});
  } else if (game.inCheck()) {
    // The brightness spike is timed to the ripple's start.
    const float spike =
        hudEvents_.check ? view::checkSpike(*hudEvents_.check, fxIntensity) : 0.0f;
    view::Rgba c = t.blood;
    c.r += (t.bone.r - c.r) * spike;
    c.g += (t.bone.g - c.g) * spike;
    c.b += (t.bone.b - c.b) * spike;
    status.push_back({"CHECK", small, c});
  }
  const auto reviewing = session.reviewPly();
  if (reviewing) {
    status.push_back(
        {"REVIEW " + std::to_string(*reviewing) + " / " + std::to_string(game.plyCount()),
         small, t.ember});
  }
  // The computer's thinking indicator lives in the free left of the "ply" row, so the
  // panel (and the ledger anchored below it) never changes height when a search starts.
  if (hudEvents_.think && result == GameResult::InProgress && !reviewing) {
    auto* thinkFont = static_cast<ImFont*>(fontLabel_);
    const float age = *hudEvents_.think;
    const view::Rgba ember = t.ember;
    const float dotR = px(2.5f), dotStep = px(8.0f), dotGap = px(6.0f);
    status[1].after = [=](ImDrawList* dl, ImVec2 at, ImVec2 size, float leftX) {
      const float styleAlpha = ImGui::GetStyle().Alpha;
      const float label =
          thinkFont->CalcTextSizeA(thinkFont->FontSize, FLT_MAX, 0, "THINKING").x;
      const float dots = 3 * dotStep;
      const bool room = leftX + label + dotGap + dots + dotGap < at.x;
      float x = leftX;
      if (room) {
        dl->AddText(thinkFont, thinkFont->FontSize,
                    ImVec2(x, at.y + (size.y - thinkFont->FontSize) * 0.5f),
                    u32(ember, styleAlpha), "THINKING");
        x += label + dotGap;
      }
      for (int d = 0; d < 3; ++d) {
        const float level = view::thinkingDot(age, d, fxIntensity);
        const ImVec2 c(x + dotStep * (static_cast<float>(d) + 0.5f),
                       at.y + size.y * 0.5f);
        dl->AddCircleFilled(c, dotR * 1.8f, u32(ember, 0.18f * level * styleAlpha), 12);
        dl->AddCircleFilled(c, dotR, u32(ember, level * styleAlpha), 12);
      }
    };
  }
  if (session.resultCollapsed() && result != GameResult::InProgress)
    status.push_back({"RESULT", small, t.ember, [&] { (void)session.showResult(); }});
  // The status panel is exempt from the end power-down: it shows the result lit.
  HudPanelSpec statusPanel;
  statusPanel.theme = &t;
  statusPanel.scale = px(1.0f);
  statusPanel.draw = hudDraw_;
  statusPanel.content = hudContent_;
  const auto sideLed = [&](bool w) { return w ? t.whitePiece : t.blackPiece; };
  view::Rgba turnLed = sideLed(whiteMoves);
  if (hudEvents_.turn) {
    const view::Rgba from = sideLed(!whiteMoves);
    turnLed.r = from.r + (turnLed.r - from.r) * flip.mix;
    turnLed.g = from.g + (turnLed.g - from.g) * flip.mix;
    turnLed.b = from.b + (turnLed.b - from.b) * flip.mix;
  }
  statusPanel.ledLevel = flip.led;
  // Against the computer the panel is wide enough for "THINKING" and its dots beside the
  // ply line, so it does not change size when a search starts.
  if (session.engineSide() || hudEvents_.think) {
    auto* thinkFont = static_cast<ImFont*>(fontLabel_);
    ImGui::PushFont(small);
    const float plyW = ImGui::CalcTextSize(statusLine).x;
    ImGui::PopFont();
    const float labelW =
        thinkFont->CalcTextSizeA(thinkFont->FontSize, FLT_MAX, 0, "THINKING").x;
    statusPanel.size.x = plyW + labelW + px(8.0f) * 3 + px(6.0f) * 3 + px(20.0f);
  }
  if (result == GameResult::InProgress) statusPanel.led = &turnLed;
  const HudRect statusRect = drawHudText(
      "##status", status,
      ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin, vp->WorkPos.y + margin),
      ImVec2(1, 0), overlayFlags, ImGui::GetStyle().ItemSpacing.y, &statusPanel);

  // ------------------------------------------------------------ dock, bottom centre
  // The trailer's timeline strip takes the bottom edge; the controls sit above it.
  const float timelineLift = shell.attracting() ? buildTrailerTimeline(shell) : 0.0f;
  std::vector<float> dockWidths;
  if (!shell.attracting() && !shell.watching()) {
    // Review replaces the game actions with the cursor controls (M23.11 3b).
    if (reviewing)
      dockWidths = {px(56), px(56), px(140)};
    else
      dockWidths = {px(78), px(84)};
    if (v.dims.dims() > 2) dockWidths.push_back(px(78));
    dockWidths.push_back(px(64));
    if (hasPlaySurface(v)) {
      dockWidths.push_back(px(84));
      if (shell.settings().geometryView && !session.flatView()) {
        for (float width : {100.0f, 92.0f, 84.0f}) dockWidths.push_back(px(width));
        if (shell.settings().cameraMode != "off" && PlaySurface::slidesAlongRanks(v))
          dockWidths.push_back(px(92));
      }
    }
    if (shell.settings().cameraMode != "off" && shell.settings().geometryView)
      dockWidths.push_back(px(92));
    dockWidths.push_back(px(78));
  }
  const float dockAvailable = vp->WorkSize.x - 2 * margin - 2 * hudPad;
  const float fullRowHeight = std::ceil(controlHeight());
  const std::size_t gameButtons = reviewing ? 3 : 2;
  const auto fullDock = dockLayout(dockWidths, gameButtons, dockAvailable, fullRowHeight,
                                   ImGui::GetStyle().ItemSpacing.x, px(12));
  const bool compactDock = fullDock.height > fullRowHeight;
  if (compactDock)
    for (float& width : dockWidths) width *= .98f;
  const float dockRowHeight =
      compactDock
          ? std::ceil(std::max(controlHeight() * 20 / 42,
                               actionFont()->FontSize + 4 * ImGui::GetFontSize() / 15))
          : fullRowHeight;
  const auto dock =
      compactDock ? dockLayout(dockWidths, gameButtons, dockAvailable, dockRowHeight,
                               ImGui::GetStyle().ItemSpacing.x, px(12))
                  : fullDock;
  std::string hint;
  if (!shell.attracting() && !shell.watching()) {
    if (session.hotSeat()) {
      const Color mover = game.position().sideToMove();
      const auto partial = session.hotSeatKeys().partial(mover);
      hint = mover == Color::White ? "p1 (white): " : "p2 (black): ";
      if (partial.empty())
        hint += mover == Color::White ? "qwertasdfgzxcvb" : "yuiophjkl;nm,./";
      else
        for (std::int16_t c : partial) hint += std::to_string(c) + " ";
    } else if (reviewing) {
      // No hint line: it would grow the dock and reframe the board under the player.
    } else if (game.plyCount() == 0) {
      hint = surfaceView ? "middle-drag slides; right-drag orbits"
                         : "click a piece, then a lit cell";
    }
  }
  auto* labelFont = static_cast<ImFont*>(fontLabel_);
  const float hintWidth =
      dockWidths.empty() ? vp->WorkSize.x - 2 * margin - 2 * hudPad : dock.width;
  const ImVec2 hintSize = hint.empty()
                              ? ImVec2(0, 0)
                              : labelFont->CalcTextSizeA(labelFont->FontSize, FLT_MAX,
                                                         hintWidth, hint.c_str());
  const float dockHeight = dock.height + (hint.empty() ? 0 : hintSize.y + px(6));
  beginFramed("##controls",
              ImVec2(vp->WorkPos.x + vp->WorkSize.x * .5f,
                     vp->WorkPos.y + vp->WorkSize.y - margin - timelineLift),
              ImVec2(.5f, 1.0f),
              dockWidths.empty()
                  ? ImVec2(0, 0)
                  : ImVec2(dock.width + 2 * hudPad, dockHeight + 2 * hudPad));
  const ImVec2 dockOrigin = ImGui::GetCursorScreenPos();
  std::size_t dockIndex = 0;
  const int dockFirstVertex = ImGui::GetWindowDrawList()->VtxBuffer.Size;
  // Keep original button IDs and enable predicates. Boot adds a temporary input gate.
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
  ImGui::BeginDisabled(hudContent_ < 1.0f);
  const auto dockButton = [&](const char* name, const view::Theme& theme, float width,
                              bool primary, bool cold, bool enabled, ImFont* font) {
    if (!dockWidths.empty()) {
      const auto& rect = dock.buttons.at(dockIndex++);
      ImGui::SetCursorScreenPos(ImVec2(dockOrigin.x + rect.x, dockOrigin.y + rect.y));
      width = rect.width;
    }
    return widgets::button(name, theme, width, primary, cold, enabled, font, compactDock);
  };
  if (shell.attracting()) {
    // A trailer beat is a watch run under the hood, so `watching()` is also true here -
    // check `attracting()` first, the same precedence the Esc handler uses (M13.9
    // revision, Part G). Stopping the inner watch would leave the whole sequence running,
    // so this is the trailer's own way out; Esc does the same.
    const bool editing = shell.trailerEditing();
    // The shape buttons are offered while it plays too; the stepping and keyframe rows
    // only while editing. They sit above the main row, so the main row never moves.
    buildTrailerEditRows(shell, session, request, small, editing);
    if (dockButton("STOP TRAILER", t, px(180), false, true, true, actionFont())) {
      shell.stopAttract();
    }
    ImGui::SameLine();
    // Back to the first beat (deferred a frame - it replaces the session this frame
    // holds).
    if (dockButton("RESTART", t, px(110), false, true, true, actionFont())) {
      shell.requestTrailerRestart();
    }
    ImGui::SameLine();
    // Render the whole trailer to a video, frame by frame, in a separate process - what
    // plays here is as fast as this machine can draw it, what is rendered is every frame.
    if (dockButton("RENDER", t, px(110), renderRunning_, true, !renderRunning_,
                   actionFont())) {
      request.renderTrailer = true;
    }
    ImGui::SameLine();
    // Pause without the edit controls; edit mode pauses too, so then it only reads.
    {
      const bool paused = shell.trailerPaused() || editing;
      if (dockButton(paused ? "PLAY" : "PAUSE", t, px(100), paused, true, !editing,
                     actionFont())) {
        shell.setTrailerPaused(!shell.trailerPaused());
      }
    }
    ImGui::SameLine();
    // The camera mode the trailer uses (a separate mode, not the player's setting):
    // AUTO is the calm default, FOLLOW the chase behind the piece, AUTHORED the
    // keyframes.
    {
      const app::TrailerCamera cam = shell.trailerCamera();
      const char* label = cam == app::TrailerCamera::Auto     ? "CAM: AUTO"
                          : cam == app::TrailerCamera::Follow ? "CAM: FOLLOW"
                                                              : "CAM: AUTHORED";
      if (dockButton(label, t, px(150), false, true, true, actionFont())) {
        shell.setTrailerCamera(
            cam == app::TrailerCamera::Auto     ? app::TrailerCamera::Follow
            : cam == app::TrailerCamera::Follow ? app::TrailerCamera::Authored
                                                : app::TrailerCamera::Auto);
      }
    }
    ImGui::SameLine();
    if (dockButton(editing ? "DONE" : "EDIT CAMERA", t, px(editing ? 78 : 140), editing,
                   true, true, actionFont())) {
      shell.setTrailerEditing(!editing);
    }
    ImGui::SameLine();
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    if (!renderStatus_.empty()) {
      ImGui::TextUnformatted(renderStatus_.c_str());
    } else {
      ImGui::Text("%s  -  Esc leaves the trailer",
                  editing || shell.trailerPaused() ? "paused" : "playing");
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
  } else if (shell.watching()) {
    // Watching is not playing: the only control that matters is the way out, so it is
    // the only one offered. Esc does the same (M12.8).
    if (dockButton("STOP WATCHING", t, px(180), false, true, true, actionFont())) {
      shell.stopWatching();
    }
    ImGui::SameLine();
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    char watching[96];
    std::snprintf(watching, sizeof(watching), "move %zu of %zu   -   Esc leaves the game",
                  session.watchIndex(), session.watchCount());
    ImGui::TextUnformatted(watching);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  } else {
    if (reviewing) {
      // The triangles where the font has them; the plain arrows otherwise.
      const bool glyphs = actionFont()->FindGlyphNoFallback(0x25C0) != nullptr &&
                          actionFont()->FindGlyphNoFallback(0x25B6) != nullptr;
      // Without the triangles the button is blank (distinct labels keep distinct ids)
      // and a vector arrow is drawn over it.
      const auto overlay = [&](ArrowDir dir, bool on) {
        if (glyphs) return;
        drawArrow(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(),
                  ImGui::GetItemRectMax(), dir, u32(on ? t.bone : t.boneFaint), px(1.5f),
                  0.5f);
      };
      const std::size_t at = *reviewing;
      app::Action step;
      step.kind = app::ActionKind::ReviewStep;
      if (dockButton(glyphs ? "\xE2\x97\x80" : " ", t, px(56), false, false, at > 0,
                     actionFont())) {
        step.dx = -1;
        (void)session.apply(step);
      }
      overlay(ArrowDir::Left, at > 0);
      ImGui::SameLine();
      if (dockButton(glyphs ? "\xE2\x96\xB6" : "  ", t, px(56), false, false,
                     at < game.plyCount(), actionFont())) {
        step.dx = 1;
        (void)session.apply(step);
      }
      overlay(ArrowDir::Right, at < game.plyCount());
      ImGui::SameLine();
      if (dockButton("EXIT REVIEW", t, px(140), true, false, true, actionFont())) {
        app::Action end;
        end.kind = app::ActionKind::ReviewEnd;
        (void)session.apply(end);
      }
    } else {
      if (dockButton("UNDO", t, px(78), false, false, true, actionFont())) {
        app::Action a;
        a.kind = app::ActionKind::Undo;
        (void)session.apply(a);
      }
      ImGui::SameLine();
      if (dockButton("RESET", t, px(84), false, false, true, actionFont())) {
        app::Action a;
        a.kind = app::ActionKind::Reset;
        (void)session.apply(a);
      }
    }
    if (v.dims.dims() > 2) {
      // Cold, because it acts on the geometry rather than on the game.
      if (dockButton("AXES", t, px(78), false, true, true, actionFont())) {
        const DimSpec& d = v.dims;
        bool temporal = false;
        for (std::uint8_t ax = 0; ax < d.dims(); ++ax) {
          if (d.kind(ax) != AxisKind::Spatial) temporal = true;
        }
        app::Action a;
        a.kind = app::ActionKind::SetScreenAxes;
        if (temporal) {
          // Time travel is unreadable in a depth view, so a temporal board is only ever
          // shown with its spatial axes on screen and the boards laid out in full. The
          // control just swaps whether time runs across the screen or down it.
          std::string text;
          for (std::uint8_t ax = 0; ax < d.dims(); ++ax) {
            if (d.kind(ax) != AxisKind::Spatial) continue;
            if (!text.empty()) text += ",";
            text += d.name(ax);
          }
          a.text = text;
          axisRotation_ ^= 1;
          a.gridVertical = axisRotation_ != 0;
        } else {
          const int dims = static_cast<int>(d.dims());
          // The default view, then every pair of axes twice: once with the extra axes
          // laid across the screen and once down it. A 3-D board's depth axis is only
          // visible as a column of boards, so both arrangements have to be in the cycle.
          const int steps = 1 + dims * 2;
          axisRotation_ = (axisRotation_ + 1) % steps;
          if (axisRotation_ == 0) {
            a.text = "default";
          } else {
            const int s = axisRotation_ - 1;
            const int r = (s / 2) % dims;
            a.gridVertical = (s % 2) == 1;
            a.text = d.name(static_cast<std::size_t>(r)) + "," +
                     d.name(static_cast<std::size_t>((r + 1) % dims));
          }
        }
        (void)session.apply(a);
      }
    }
    ImGui::SameLine();
    // View mode travels with the player: a flat board is easier to read, a solid one
    // easier to understand. Cold, because it is a view of the geometry, not a move.
    // While SHAPE is on the same button is re-purposed (M17.25): a shape has no 2D board
    // to fall back to, but the shape's pieces can be flat icons or sculptural meshes, so
    // the control names that instead - the same 2D/3D language, a second, independent
    // meaning, and live where it used to be greyed out (M18.6). `viewModeButton` states
    // the branch so it can be tested with no device.
    const bool surfaceAvailable = hasPlaySurface(session.variant());
    const ViewModeButton mode =
        viewModeButton(shell.settings(), session.flatView(), surfaceAvailable);
    if (dockButton(mode.label, t, px(64), false, true, mode.enabled, actionFont())) {
      if (mode.pieceStyle) {
        shell.settings().shapePieceCylinders = !shell.settings().shapePieceCylinders;
      } else {
        const bool flat = !session.flatView();
        session.setFlatView(flat);
        shell.settings().flatView = flat;
      }
      request.settingsChanged = true;
    }
    // Turn the board into the shape its geometry describes (M17). Offered only where
    // there is a surface to become: a glued two-dimensional variant names one, and
    // nothing else does - so `standard` never shows a control that could not act.
    if (surfaceAvailable) {
      ImGui::SameLine();
      const bool on = shell.settings().geometryView;
      // Greyed out in 2D (M18.6): a donut has no 2D rendering, so SHAPE is refused there
      // rather than silently doing something. Visible but inert, as above.
      if (dockButton("SHAPE", t, px(84), on, true, !session.flatView(), actionFont())) {
        shell.settings().geometryView = !on;
        request.settingsChanged = true;
      }
      // Turn the shape inside out and back (M17.7). Only while the shape is showing, and
      // lit while it is inverted. It sets a target; the front end eases the pose to it,
      // so a pose stays a pure function of its number.
      if (on && !session.flatView()) {
        // Follow the pieces while a move plays (M17.19): the align / approach / travel /
        // return choreography. A quick on/off beside SHAPE - the same switch the settings
        // screen's "Move camera" sets, so a player can turn the shot on for a capture
        // without leaving the board. Lit while it follows.
        ImGui::SameLine();
        const bool follow = shell.settings().cameraMode != "off";
        if (dockButton("CAMERA", t, px(100), follow, true, true, actionFont())) {
          shell.settings().cameraMode = follow ? "off" : "route";
          request.settingsChanged = true;
        }
        ImGui::SameLine();
        const bool inverted = shell.settings().geometryInvert;
        if (dockButton("INVERT", t, px(92), inverted, true, true, actionFont())) {
          shell.settings().geometryInvert = !inverted;
          request.settingsChanged = true;
        }
        // Make the board a ghost, so the far side of the shape and the pieces on it show
        // through the near side (M17.10). The pieces stay opaque.
        ImGui::SameLine();
        const bool ghost = shell.settings().geometryGhost < 0.99f;
        if (dockButton("GHOST", t, px(84), ghost, true, true, actionFont())) {
          shell.settings().geometryGhost = ghost ? 1.0f : 0.35f;
          request.settingsChanged = true;
        }
        // ANTI-CLIP (M17.17): turn the ring so the piece a followed move travels on
        // presents its outer face to the camera, with nothing of the shape between.
        // Offered only while following, and only on a closed ring - an open tube or
        // ribbon has no inner/outer side to turn. On by default; a player who wants the
        // raw view turns it off here or in settings.
        if (shell.settings().cameraMode != "off" &&
            PlaySurface::slidesAlongRanks(session.variant())) {
          ImGui::SameLine();
          const bool aligned = shell.settings().geometryAlign;
          if (dockButton("NOCLIP", t, px(92), aligned, true, true, actionFont())) {
            shell.settings().geometryAlign = !aligned;
            request.settingsChanged = true;
          }
        }
      }
    }
    // The two follow camera styles (M17.16, revised): upright (the piece's own up up the
    // screen) or the tilt that lets it ride the shape. Offered whenever the camera
    // follows a move, on any shape.
    if (shell.settings().cameraMode != "off" && shell.settings().geometryView) {
      ImGui::SameLine();
      const bool upright = shell.settings().followUpright;
      if (dockButton(upright ? "UPRIGHT" : "TILT", t, px(92), upright, true, true,
                     actionFont())) {
        shell.settings().followUpright = !upright;
        request.settingsChanged = true;
      }
    }
    if (dockButton("MENU", t, px(78), false, false, true, actionFont())) shell.pause();

    if (!hint.empty()) {
      ImGui::SetCursorScreenPos(ImVec2(dockOrigin.x, dockOrigin.y + dock.height + px(6)));
      ImGui::PushFont(labelFont);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + dock.width);
      ImGui::TextUnformatted(hint.c_str());
      ImGui::PopTextWrapPos();
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
  }
  ImGui::EndDisabled();
  ImGui::PopStyleVar();
  ImDrawList* dockDl = ImGui::GetWindowDrawList();
  for (const auto& rule : dock.rules)
    dockDl->AddRectFilled(
        ImVec2(dockOrigin.x + rule.x, dockOrigin.y + rule.y),
        ImVec2(dockOrigin.x + rule.x + rule.width, dockOrigin.y + rule.y + rule.height),
        u32(t.ember, .35f));
  // Custom draw-list controls don't inherit ImGui alpha. Multiply their actual vertices
  // once, including text/fill, while leaving the panel's edge trace independent.
  const float dockAlpha = std::clamp(hudContent_ * hudLevel_, 0.0f, 1.0f);
  for (int i = dockFirstVertex; i < dockDl->VtxBuffer.Size; ++i) {
    auto& colour = dockDl->VtxBuffer[i].col;
    const auto alpha = static_cast<ImU32>(
        static_cast<float>((colour >> IM_COL32_A_SHIFT) & 255) * dockAlpha);
    colour = (colour & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
  }
  if (!dockWidths.empty()) {
    ImGui::SetCursorScreenPos(ImVec2(dockOrigin.x, dockOrigin.y));
    ImGui::Dummy(ImVec2(dock.width, dockHeight));
  }
  endFramed(hudLevel_);
  const HudRect dockRect = lastPanel;

  // ------------------------------------------------------- move ledger, right edge
  // A measured, scrolling column from status to dock: the newest row lit,
  // older rows fading. The rows are measured with the strings and fonts they are drawn
  // in.
  HudRect ledgerRect;
  if (game.plyCount() > 0) {
    auto* readout = static_cast<ImFont*>(fontMono_);
    const float ledgerTop = statusRect.y + statusRect.height + margin;
    const float ledgerHeight = std::max(px(30), dockRect.y - margin - ledgerTop);
    const auto& moves = game.moveHistory();
    const std::size_t show = moves.size();
    std::vector<HudTextRow> recent;
    recent.reserve(show);
    // The lit row is the newest, or the review cursor's; older rows fade away from it.
    const std::size_t lit = session.reviewPly().value_or(moves.size());
    for (std::size_t i = moves.size() - show; i < moves.size(); ++i) {
      const bool last = i + 1 == lit;
      const float age = static_cast<float>(
                            std::abs(static_cast<long>(lit) - 1 - static_cast<long>(i))) /
                        static_cast<float>(show);
      char row[96];
      std::snprintf(row, sizeof(row), "%2zu%s %s", i / 2 + 1, (i % 2 == 0) ? "." : "..",
                    moveText(v, moves[i]).c_str());
      view::Rgba c = last ? t.ember : t.boneDim;
      c.a *= last ? 1.0f : 1.0f - 0.7f * age;
      HudTextRow entry{row, readout, c};
      // The newest row reveals left to right while the older ones ease up to meet it.
      if (hudEvents_.move && !session.reviewPly()) {
        const view::LedgerRowFx fx = view::ledgerRowFx(*hudEvents_.move, fxIntensity);
        if (i + 1 == moves.size())
          entry.reveal = fx.reveal;
        else
          entry.shift = fx.shift;
      }
      // While reviewing, a row is a jump to the position after it.
      if (session.reviewPly()) {
        entry.clicked = [&session, i] {
          app::Action jump;
          jump.kind = app::ActionKind::ReviewJump;
          jump.text = std::to_string(i + 1);
          (void)session.apply(jump);
        };
      }
      recent.push_back(std::move(entry));
    }
    HudPanelSpec ledger = statusPanel;
    ledger.led = nullptr;
    ledger.leftAlign = true;
    ledger.scrollRows = true;
    ledger.header = "MOVES";
    ledger.headerFont = small;
    ledger.level = hudLevel_;
    if (session.reviewPly())
      ledger.focusRow =
          static_cast<int>(std::max<std::size_t>(*session.reviewPly(), 1)) - 1;
    ImGui::PushFont(readout);
    const float ledgerWidth = ImGui::CalcTextSize("000. Qxh8+ Qxh8+").x + px(20);
    ImGui::PopFont();
    ledger.size = ImVec2(ledgerWidth, ledgerHeight);
    ledgerRect = drawHudText("##recent", recent,
                             ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin, ledgerTop),
                             ImVec2(1, 0), overlayFlags, px(1), &ledger);
  }

  HudRect trayRect;
  // ------------------------------------------------------ captured trays, left edge
  if (!game.isTemporal() && game.plyCount() > 0) {
    if (captured_.game != &game || captured_.ply != game.plyCount()) {
      captured_.pieces = app::capturedBySide(game);
      captured_.game = &game;
      captured_.ply = game.plyCount();
    }
    const std::vector<CapturedGroup> sides[2] = {groupCaptured(captured_.pieces.byWhite),
                                                 groupCaptured(captured_.pieces.byBlack)};
    if (!sides[0].empty() || !sides[1].empty()) {
      const float r = px(11.0f);
      const float maxW = px(176.0f);
      const float gap = px(6.0f);
      // Width of one group: icons overlapped up to four, then one icon with a count.
      const auto groupW = [&](const CapturedGroup& g) {
        return g.count <= 4 ? 2 * r + static_cast<float>(g.count - 1) * r * 1.1f
                            : 2 * r + px(26);
      };
      struct Placed {
        float x, y;
        const CapturedGroup* g;
      };
      std::vector<Placed> placed;
      struct Label {
        float x, y;
        const char* text;
      };
      std::vector<Label> labels;
      float y = 0.0f, widest = 0.0f;
      ImGui::PushFont(small);
      const float labelH = ImGui::GetTextLineHeight() + px(3);
      ImGui::PopFont();
      const char* names[2] = {"BY WHITE", "BY BLACK"};
      for (int side = 0; side < 2; ++side) {
        labels.push_back({0, y, names[side]});
        y += labelH;
        float x = 0.0f;
        for (const CapturedGroup& g : sides[side]) {
          const float w = groupW(g);
          if (x > 0.0f && x + w > maxW) {
            x = 0.0f;
            y += 2 * r + gap;
          }
          placed.push_back({x, y, &g});
          x += w + gap;
          widest = std::max(widest, x - gap);
        }
        y += 2 * r + gap * 2;
      }
      y -= gap * 2;
      ImGui::PushFont(small);
      widest = std::max(widest,
                        ImGui::CalcTextSize("CAPTURED").x + 7 * small->FontSize * .12f);
      ImGui::PopFont();
      const float top = small->FontSize + px(6);
      const float trayRoom =
          dockRect.y - margin - variantRect.y - variantRect.height - margin;
      if (y + top + 2 * hudPad > trayRoom) {
        // On a short, highly scaled screen put both sections in one row, retaining both
        // labels and full-size icons. Each section is measured before Begin.
        placed.clear();
        labels.clear();
        float x = 0;
        y = 2 * r;
        for (int side = 0; side < 2; ++side) {
          const float labelWidth =
              small->CalcTextSizeA(small->FontSize, FLT_MAX, 0, names[side]).x;
          labels.push_back({x, r - small->FontSize * .5f, names[side]});
          x += labelWidth + gap;
          for (const auto& g : sides[side]) {
            placed.push_back({x, 0, &g});
            x += groupW(g) + gap;
          }
          x += gap;
        }
        widest = std::max(widest, x - gap);
      }
      ImGui::SetNextWindowPos(
          ImVec2(vp->WorkPos.x + margin, variantRect.y + variantRect.height + margin),
          ImGuiCond_Always, ImVec2(0, 0));
      ImGui::SetNextWindowSize(ImVec2(widest + 2 * hudPad, y + top + 2 * hudPad),
                               ImGuiCond_Always);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(hudPad, hudPad));
      ImGui::Begin("##captured", nullptr,
                   overlayFlags & ~ImGuiWindowFlags_AlwaysAutoResize);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 wp = ImGui::GetWindowPos();
      trayRect = {wp.x, wp.y, widest + 2 * hudPad, y + top + 2 * hudPad};
      widgets::hudPanel(dl, wp,
                        ImVec2(wp.x + widest + 2 * hudPad, wp.y + y + top + 2 * hudPad),
                        t, px(1.0f), hudDraw_, hudLevel_);
      const float alpha = std::clamp(hudContent_, 0.0f, 1.0f);
      const ImVec2 o(wp.x + hudPad, wp.y + hudPad);
      ImGui::PushFont(small);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember, .8f * alpha));
      widgets::trackedText(dl, o, "CAPTURED", small->FontSize * .12f);
      ImGui::PopStyleColor();
      ImGui::PopFont();
      for (const Label& l : labels) {
        dl->AddText(small, small->FontSize, ImVec2(o.x + l.x, o.y + top + l.y),
                    u32(t.boneFaint, alpha), l.text);
      }
      for (const Placed& p : placed) {
        const CapturedGroup& g = *p.g;
        const bool white = g.piece.colorOf() == Color::White;
        const view::Rgba figure = white ? t.whitePiece : t.blackPiece;
        const int icons = g.icons();
        const auto& newest = white ? captured_.pieces.byBlack : captured_.pieces.byWhite;
        const bool latest = session.capturePly() == game.plyCount() && !newest.empty() &&
                            newest.back().type == g.piece.type &&
                            game.position().sideToMove() == g.piece.colorOf();
        for (int k = 0; k < icons; ++k) {
          const ImVec2 c(o.x + p.x + r + static_cast<float>(k) * r * 1.1f,
                         o.y + top + p.y + r);
          const float reveal =
              latest && k + 1 == icons && captureAge_
                  ? captureReveal(*captureAge_, shell.settings().presentationIntensity)
                  : 1;
          const float edge = c.y + r - 2 * r * reveal;
          dl->PushClipRect(ImVec2(c.x - r - 1, edge), ImVec2(c.x + r + 1, c.y + r + 1),
                           true);
          dl->AddCircleFilled(c, r, u32(t.pieceToken, alpha), 20);
          dl->AddCircle(c, r, u32(t.boardRim, alpha), 20, px(1.0f));
          drawPieceOf(dl, c, r * 0.82f, u32(figure, alpha), u32(t.pieceToken, alpha),
                      v.pieces[g.piece.type], iconStyle_, false);
          if (reveal < 1 && reveal > 0)
            dl->AddRectFilled(ImVec2(c.x - r, edge), ImVec2(c.x + r, edge + px(2)),
                              u32(t.ember, alpha));
          dl->PopClipRect();
        }
        if (g.count > 4) {
          char count[16];
          std::snprintf(count, sizeof(count), "x%d", g.count);
          dl->AddText(small, small->FontSize,
                      ImVec2(o.x + p.x + 2 * r + px(4),
                             o.y + top + p.y + r - small->FontSize * 0.5f),
                      u32(t.bone, alpha), count);
        }
      }
      ImGui::End();
      ImGui::PopStyleVar();
    }
  }

  const StagePanel stagePanels[] = {{variantRect, StageEdge::Left},
                                    {statusRect, StageEdge::Top},
                                    {trayRect, StageEdge::Left},
                                    {ledgerRect, StageEdge::Right},
                                    {dockRect, StageEdge::Bottom}};
  const auto stage = render::stageRect({vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y},
                                       stagePanels, px(4));
  if (!shell.attracting()) {
    request.stageRect[0] = stage.x;
    request.stageRect[1] = stage.y;
    request.stageRect[2] = stage.width;
    request.stageRect[3] = stage.height;
  }

  // Result shares the end event's age; ordinary captures stay panel-free unless asked.
  if (game.result() != GameResult::InProgress && !session.resultCollapsed() &&
      !shell.watching() && !shell.attracting() &&
      (resultAge_ || session.resultRequested())) {
    const auto end = session.endEvent().value_or(view::EndFx{});
    int maxDistance = 0;
    if (end.origin != kInvalidCell)
      for (int d : session.distancesFrom(end.origin))
        maxDistance = std::max(maxDistance, d);
    const auto fx =
        resultPanelFx(session.resultRequested() ? 1000 : resultAge_.value_or(1000),
                      end.kind, maxDistance, shell.settings().presentationIntensity);
    if (fx.draw > 0) {
      const auto text = session.resultText();
      const bool reviewable = session.canReview();
      const float inset = px(16);
      ImGui::PushFont(display);
      const ImVec2 titleSize = ImGui::CalcTextSize(text.title.c_str());
      ImGui::PopFont();
      // A narrow column: wide enough for the title and its close button, with the
      // buttons stacked, so it fits beside the board rather than over it (M23.11 3b).
      const float width =
          std::min(stage.width, std::max(px(190), titleSize.x + px(36) + 2 * inset));
      const float rowH = std::ceil(controlHeight());
      const float lineH = ImGui::GetStyle().ItemSpacing.y;
      ImGui::PushFont(small);
      const float smallH = ImGui::GetTextLineHeight();
      ImGui::PopFont();
      const float bodyH = titleSize.y + ImGui::GetTextLineHeight() + smallH + 3 * lineH +
                          px(10) + (reviewable ? 3.0f : 2.0f) * (rowH + lineH);
      const ImVec2 size(width, std::min(stage.height, bodyH + 2 * px(12)));
      std::optional<HudPoint> king;
      if (end.mated) {
        for (const auto& pl : session.placements()) {
          if (pl.cell != end.origin) continue;
          const auto at = session.camera().project(
              {pl.x, pl.y, pl.z}, vp->Size.x / vp->Size.y, vp->Size.x, vp->Size.y);
          if (at.visible) king = HudPoint{at.x, at.y};
        }
      }
      // The board's projected bounds: every cell's corners and a piece's height above it.
      std::optional<HudRect> board;
      {
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        const float aspect = vp->Size.x / vp->Size.y;
        const auto cam = session.camera();
        for (const auto& pl : session.placements()) {
          for (const float dz : {0.0f, 1.0f})
            for (const float dx : {-.5f, .5f})
              for (const float dy : {-.5f, .5f}) {
                const auto at = cam.project({pl.x + dx, pl.y + dy, pl.z + dz}, aspect,
                                            vp->Size.x, vp->Size.y);
                if (!at.visible) continue;
                x0 = std::min(x0, at.x);
                y0 = std::min(y0, at.y);
                x1 = std::max(x1, at.x);
                y1 = std::max(y1, at.y);
              }
        }
        if (x1 > x0 && y1 > y0) board = HudRect{x0, y0, x1 - x0, y1 - y0};
      }
      const auto pos = resultPanelPos(stage, board, king, {size.x, size.y}, px(16));
      ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y));
      ImGui::SetNextWindowSize(size);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(inset, px(12)));
      ImGui::Begin("##result", nullptr,
                   overlayFlags & ~ImGuiWindowFlags_AlwaysAutoResize);
      auto* dl = ImGui::GetWindowDrawList();
      const auto wp = ImGui::GetWindowPos();
      widgets::hudPanel(dl, wp, ImVec2(wp.x + size.x, wp.y + size.y), t, px(1), fx.draw,
                        fx.level);
      const int first = dl->VtxBuffer.Size;
      ImGui::BeginDisabled(fx.content < 1);
      ImGui::PushFont(display);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
      ImGui::TextUnformatted(text.title.c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::SameLine();
      ImGui::SetCursorPosX(size.x - inset - px(24));
      if (button("X", t, px(24), false, false, true, actionFont(), true))
        session.collapseResult();
      const auto winner = game.result() == GameResult::WhiteWins   ? t.whitePiece
                          : game.result() == GameResult::BlackWins ? t.blackPiece
                                                                   : t.bone;
      // A piece's dark pigment needs light mixed in to read as text on a dark panel.
      ImGui::PushStyleColor(ImGuiCol_Text, col(widgets::mix(winner, t.bone, .65f)));
      ImGui::TextUnformatted(text.subtitle.c_str());
      ImGui::PopStyleColor();
      ImGui::PushFont(small);
      ImGui::TextUnformatted(text.detail.c_str());
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, px(10)));
      const float inner = size.x - 2 * inset;
      if (button("REMATCH", t, inner, true, false, true, actionFont())) {
        app::Action reset;
        reset.kind = app::ActionKind::Reset;
        (void)session.apply(reset);
      }
      if (reviewable && button("REVIEW", t, inner, false, false, true, actionFont())) {
        app::Action review;
        review.kind = app::ActionKind::ReviewStart;
        (void)session.apply(review);
      }
      if (button("MENU", t, inner, false, false, true, actionFont()))
        shell.go(app::Screen::MainMenu);
      ImGui::EndDisabled();
      for (int i = first; i < dl->VtxBuffer.Size; ++i) {
        auto& c = dl->VtxBuffer[i].col;
        const auto alpha = static_cast<ImU32>(
            static_cast<float>((c >> IM_COL32_A_SHIFT) & 255) * fx.content);
        c = (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
      }
      ImGui::End();
      ImGui::PopStyleVar();
    }
  }

  // --------------------------------------------------------------------- frame rate
  if (devOverlay(shell)) {
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##fps", nullptr, overlayFlags);
    recordToolRect();
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint, 0.55f));
    ImGui::Text("%3.0f fps", static_cast<double>(fps));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::End();
    ImGui::PopStyleVar();
  }

  // ------------------------------------------------------------------ promotion
  const auto& pending = session.pendingPromotion();
  if (pending.active) {
    ImGui::OpenPopup("promotion");
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            "promotion", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
      ImGui::PushFont(display);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
      ImGui::Text("PROMOTE ON %s", cellName(v.dims, pending.to).c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, px(4)));

      // Built from the variant's own promotion list, so a variant that promotes to a
      // unicorn offers a unicorn. Never a hardcoded four.
      for (std::size_t i = 0; i < pending.choices.size(); ++i) {
        if (i != 0) ImGui::SameLine();
        const PieceTypeId piece = pending.choices[i];
        if (button(upper(v.pieces[piece].name).c_str(), t, px(104), i == 0, false, true,
                   actionFont())) {
          (void)session.choosePromotion(piece);
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::Dummy(ImVec2(0, px(2)));
      if (button("CANCEL", t, px(104), false, false, true, actionFont())) {
        session.cancelPromotion();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  // A move waiting for confirmation: a yes or a no, and nothing else. The board is frozen
  // behind it until it is answered.
  const auto& waiting = session.pendingMove();
  if (waiting.active) {
    ImGui::OpenPopup("confirmmove");
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            "confirmmove", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
      ImGui::PushFont(display);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
      ImGui::Text("PLAY %s - %s?", cellName(v.dims, waiting.from).c_str(),
                  cellName(v.dims, waiting.to).c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, px(4)));
      if (button("PLAY", t, px(104), true, false, true, actionFont())) {
        (void)session.confirmMove();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (button("CANCEL", t, px(104), false, false, true, actionFont())) {
        session.cancelMove();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  // A flat board's pieces are drawn here rather than by the renderer: a circle token and
  // the piece's own icon, in screen space, which is also what lets the token follow the
  // move animation exactly. The background draw list puts them over the board but under
  // the panels, so a pause menu covers them rather than the other way round. The geometry
  // view draws its own pieces, so this stays out of the way there.
  if (session.flatView() && !surfaceView) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // These are drawn after the board, and therefore after the defocus pass. Fading them
    // out as the board goes soft is what stops a paused board showing sharp pieces
    // standing on blurred squares.
    const float focus = 1.0f - session.pullBack();
    const float w = vp->Size.x;
    const float h = vp->Size.y;
    const view::OrbitCamera& cam = session.camera();
    // One world unit is one cell along the drawn axes, so projecting it gives the
    // on-screen cell pitch and the token can be sized to the board instead of a guess.
    const view::OrbitCamera::ScreenPoint o0 =
        cam.project(view::Vec3{0, 0, 0}, w / h, w, h);
    const view::OrbitCamera::ScreenPoint o1 =
        cam.project(view::Vec3{1, 0, 0}, w / h, w, h);
    const float cellPx = std::max(px(6.0f), std::hypot(o1.x - o0.x, o1.y - o0.y));
    const float radius = cellPx * 0.34f;

    const auto drawToken = [&](const Piece& piece, const view::Vec3& world, CellId cell,
                               bool travelling) {
      if (piece.empty()) return;
      const view::OrbitCamera::ScreenPoint sp = cam.project(world, w / h, w, h);
      if (!sp.visible) return;
      const bool white = piece.colorOf() == Color::White;
      const ImVec2 centre(sp.x, sp.y);
      if (focus <= 0.01f) return;
      // One token colour for both sides, with the figure in its own. The disc is
      // furniture - it lifts the piece off the square and gives the outline something
      // to sit on - and the thing being read is the figure.
      dl->AddCircleFilled(centre, radius, u32(t.pieceToken, focus), 24);
      dl->AddCircle(centre, radius, u32(t.boardRim, focus), 24, px(1.5f));
      const view::Rgba figure = view::tintedPiece(
          white ? t.whitePiece : t.blackPiece, session.snapshot(), cell, travelling, t);
      drawPieceOf(dl, centre, radius * 0.82f, u32(figure, focus),
                  u32(t.pieceToken, focus), v.pieces[piece.type], iconStyle_,
                  piece.flipped());
    };

    const view::MoveAnimation& anim = session.animation();
    const CellId movingTo = anim.active() ? anim.travellingTo() : kInvalidCell;
    for (const view::Placement& pl : session.placements()) {
      if (pl.cell == movingTo) continue;  // the moving piece is drawn at its own point
      drawToken(session.snapshot().at(pl.cell), view::Vec3{pl.x, pl.y, pl.z}, pl.cell,
                false);
    }
    if (movingTo != kInvalidCell) {
      const view::MoveAnimation::Sample at = anim.sample();
      if (at.moving) {
        drawToken(session.snapshot().at(movingTo), view::Vec3{at.x, at.y, at.z},
                  kInvalidCell, true);
      }
    }
  }

  // Two players, one keyboard: put each square's keys on the board, for the player to
  // move, so the mapping is learnt in place rather than read off a line of legend. The
  // key list is indexed by coordinate on every axis, so a cell's label is one character
  // per board axis. Drawn in screen space over the board, flat or solid.
  if (session.hotSeat()) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Color mover = game.position().sideToMove();
    const float w = vp->Size.x;
    const float h = vp->Size.y;
    const view::OrbitCamera& cam = session.camera();
    const float size = small != nullptr ? small->FontSize : ImGui::GetFontSize();
    for (const view::Placement& pl : session.placements()) {
      if (!session.boardVisible(pl.cell)) continue;
      const std::string label = app::HotSeat::keysFor(mover, v.dims.toCoord(pl.cell));
      if (label.empty()) continue;
      const view::OrbitCamera::ScreenPoint sp =
          cam.project(view::Vec3{pl.x, pl.y, pl.z}, w / h, w, h);
      if (!sp.visible) continue;
      const ImVec2 ts = small != nullptr
                            ? small->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str())
                            : ImGui::CalcTextSize(label.c_str());
      const ImVec2 at(sp.x - ts.x * 0.5f, sp.y - ts.y * 0.5f);
      // A small chip behind the letters keeps them legible on a light or a dark square.
      dl->AddRectFilled(ImVec2(at.x - px(3.0f), at.y - px(1.0f)),
                        ImVec2(at.x + ts.x + px(3.0f), at.y + ts.y + px(1.0f)),
                        u32(t.soot, 0.72f), px(3.0f));
      if (small != nullptr) {
        dl->AddText(small, size, at, u32(t.bone), label.c_str());
      } else {
        dl->AddText(at, u32(t.bone), label.c_str());
      }
    }
  }

  return request;
}

void Ui::endFrame() {
  ImGui::Render();
}

void Ui::record(VkCommandBuffer cmd) {
  ImDrawData* data = ImGui::GetDrawData();
  if (data != nullptr) ImGui_ImplVulkan_RenderDrawData(data, cmd);
}

void Ui::resetBoardFieldList() {
  if (boardFieldList_ == nullptr) {
    boardFieldList_ = std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
  }
  // The list lives outside any window, so it is reset by hand the way ImGui resets its
  // own lists every frame. The clip rect and font texture are what ImGui's per-window
  // lists get in `NewFrame`, without which the field would be clipped to nothing.
  boardFieldList_->_ResetForNewFrame();
  boardFieldList_->PushClipRectFullScreen();
  boardFieldList_->PushTextureID(ImGui::GetIO().Fonts->TexID);
}

void Ui::recordDrawList(VkCommandBuffer cmd, ImDrawList* list, VkPipeline pipeline) {
  if (list == nullptr || list->VtxBuffer.Size == 0) return;
  // A minimal ImDrawData around the one list. The projection comes from the frame that
  // was just rendered, so the field's screen coordinates match the interface's.
  ImDrawData* frame = ImGui::GetDrawData();
  ImDrawData data;
  data.Valid = true;
  if (frame != nullptr) {
    data.DisplayPos = frame->DisplayPos;
    data.DisplaySize = frame->DisplaySize;
    data.FramebufferScale = frame->FramebufferScale;
  } else {
    const ImGuiIO& io = ImGui::GetIO();
    data.DisplayPos = ImVec2(0.0f, 0.0f);
    data.DisplaySize = io.DisplaySize;
    data.FramebufferScale = io.DisplayFramebufferScale;
  }
  data.AddDrawList(list);
  ImGui_ImplVulkan_RenderDrawData(&data, cmd, pipeline);
}

void Ui::recordBackground(VkCommandBuffer cmd, bool hdr) {
  recordDrawList(cmd, boardFieldList_.get(),
                 hdr && boardFieldHdrPipeline_ != VK_NULL_HANDLE ? boardFieldHdrPipeline_
                                                                 : boardFieldPipeline_);
}

Ui::~Ui() {
  if (initialised_) {
    vkDeviceWaitIdle(ctx_->device());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
  }
  const VkDevice device = ctx_ != nullptr ? ctx_->device() : VK_NULL_HANDLE;
  if (device != VK_NULL_HANDLE) {
    if (boardFieldPipeline_ != VK_NULL_HANDLE)
      vkDestroyPipeline(device, boardFieldPipeline_, nullptr);
    if (boardFieldHdrPipeline_ != VK_NULL_HANDLE)
      vkDestroyPipeline(device, boardFieldHdrPipeline_, nullptr);
    if (boardFieldLayout_ != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device, boardFieldLayout_, nullptr);
    if (boardFieldSetLayout_ != VK_NULL_HANDLE)
      vkDestroyDescriptorSetLayout(device, boardFieldSetLayout_, nullptr);
    if (boardFieldVert_ != VK_NULL_HANDLE)
      vkDestroyShaderModule(device, boardFieldVert_, nullptr);
    if (boardFieldFrag_ != VK_NULL_HANDLE)
      vkDestroyShaderModule(device, boardFieldFrag_, nullptr);
  }
  if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx_->device(), pool_, nullptr);
}

}  // namespace cb::render
