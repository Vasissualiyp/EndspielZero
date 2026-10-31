// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

#include "view/theme.hpp"

/// Small drawing helpers shared by the game's HUD and its menus.
///
/// They exist because Dear ImGui's stock widgets are built for tools: a plain Button is
/// a flat tint and a plain header is just text. The game wants the opposite of a tool -
/// chunky cards, hard bevels, a drop shadow you could stand a coin on - so each control
/// here is a real draw-list sprite with the stock widget underneath only for input.
namespace cb::render::widgets {

/// Explicit, nested presentation-time input for widgets on the current ImGui context.
/// No backend clock is read; leaving the scope restores the caller's input.
class ScopedVisualTime {
 public:
  explicit ScopedVisualTime(double seconds) noexcept;
  ~ScopedVisualTime();
  ScopedVisualTime(const ScopedVisualTime&) = delete;
  ScopedVisualTime& operator=(const ScopedVisualTime&) = delete;

 private:
  ImGuiContext* previousContext_;
  double previousSeconds_;
};

inline ImVec4 col(const view::Rgba& c, float alpha = 1.0f) {
  return ImVec4{c.r, c.g, c.b, c.a * alpha};
}
inline ImU32 u32(const view::Rgba& c, float alpha = 1.0f) {
  return ImGui::ColorConvertFloat4ToU32(col(c, alpha));
}

inline std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}

/// Blend two colours, alpha included.
view::Rgba mix(const view::Rgba& a, const view::Rgba& b, float t);
/// Multiply a colour's brightness without touching its alpha.
view::Rgba shade(const view::Rgba& c, float factor);

/// An open frame, with no fill behind the subject or text. Sizes follow the UI scale.
void panelCorners(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                  float scale);

/// A section heading with a rule running out to the right, the way a plate is labelled.
void heading(const char* text, const view::Theme& theme, ImFont* font);

/// A right-aligned label and value pair, for readouts.
void keyValue(const char* key, const std::string& value, const view::Theme& theme,
              bool cold = false);

/// An action sharing its input and treatment with menu rows. Manifold uses finite
/// plates and local hover glitches; console retains its hairline and pointer wash.
/// "primary" sets the label in the accent; "cold" marks a control that acts on geometry
/// or time rather than on the game; `font` is the label typeface, the same one a menu
/// entry passes, or null for the interface's body font.
/// Triangulate a simple polygon, by ear clipping. Indices into `pts`, three per triangle.
///
/// ImGui's own concave fill does not handle every simple polygon, and the case it gets
/// wrong is the one an author reaches for first: a valley - a notch cut into an outline -
/// comes back filled in, so the notch disappears. This is the whole reason a piece's
/// outline needs a fill of its own rather than the draw list's.
///
/// This fills; it does not validate. Whether an outline is a shape at all - whether it
/// crosses itself - is `assets::validate`'s job, and the designers ask it before they let
/// an author save. What is guaranteed here is only that a polygon with no ear anywhere
/// comes back empty instead of spinning.
void triangulate(const ImVec2* pts, int n, std::vector<int>& outIndices);

/// Fill a simple polygon, valleys and all.
void fillPolygon(ImDrawList* dl, const ImVec2* pts, int n, ImU32 col);

/// A heading with a rule running to the right of it, the way a plate is labelled.
///
/// Shared because a screen made of unlabelled rows of controls is the thing that makes
/// an editor feel like a pile of widgets rather than a tool: the rule is what says where
/// one job ends and the next begins.
void sectionHead(const char* label, const view::Theme& theme, void* displayFont,
                 float scale);

/// A row of tabs. Returns true when the selection changed.
///
/// Underlined rather than boxed: a box around the active tab is a second rectangle
/// fighting the pane it sits in, and the only thing that has to read is which of the
/// words is the live one.
bool tabRow(std::initializer_list<const char*> labels, int& selected,
            const view::Theme& theme, void* displayFont, float scale);

/// A number with a step above it and a step below it, in a cell of its own.
///
/// Stacked rather than `- n +` in a line: the axes of a move sit next to each other, and
/// a row of horizontal steppers runs into one long strip where it is not obvious which
/// sign belongs to which number. Clicking the number opens a box to type it in, because
/// stepping from 1 to 9 is eight clicks.
bool stepper(const char* id, int& value, int lo, int hi, const view::Theme& theme,
             float scale, float width = 0.0f);

bool button(const char* label, const view::Theme& theme, float width = 0.0f,
            bool primary = false, bool cold = false, bool enabled = true,
            ImFont* font = nullptr);

/// A settings control that cycles its value on press. There are no drop-downs in the
/// interface: a settings screen is a list of states, and hiding the states behind a
/// second click makes the reader open a menu to learn what the options even are. The name
/// is on the left and the current value on the right; pressing advances to the next
/// value.
bool cycleButton(const char* label, const std::string& value, const view::Theme& theme,
                 float scale, float width = 0.0f);

/// Measured action pair. The same widths feed button() and the footer reservation;
/// narrow panes compact captions first, then stack instead of losing the second action.
struct ActionPairLayout {
  ImVec2 first, second;
  float height{};
  bool stacked{};
};
[[nodiscard]] ActionPairLayout actionPairLayout(const char* first, const char* second,
                                                const view::Theme& theme,
                                                float firstWidth, float secondWidth,
                                                ImFont* font = nullptr,
                                                float available = 0);

/// The height a `button` row draws at, so a layout can reserve room for one and the row
/// is never the thing that gets clipped when the interface scale grows.
[[nodiscard]] float controlHeight();

/// A large menu entry. The label is the whole entry: a control says what it is by being
/// named, and a line of grey text under every row is exactly the developer-tool look
/// this interface is trying not to have. Disabled entries stay visible.
/// A small monospaced label with a rule running out to the right. Marks what screen you
/// are on without spending the title's weight on it.
void eyebrow(const char* text, const view::Theme& theme, ImFont* monoFont, float scale);

/// The screen's name, set large. Drawn through the draw list rather than as ordinary
/// text because ImGui has one size per font, and a title wants several times the body.
void screenTitle(const char* text, const view::Theme& theme, ImFont* displayFont,
                 float scale, float sizeMul = 2.1f);

/// A menu row: console uses a hairline; manifold uses a bounded signal plate.
///
/// `index` is the row's number, set small and monospaced at the left - it says the list
/// is ordered and gives the eye somewhere to start. `scale` is required rather than
/// defaulted: a missed call site would silently draw one row at a different size from
/// the rest, which is exactly the bug that once made the main menu look ragged.
bool menuEntry(const char* label, int index, const view::Theme& theme, ImFont* labelFont,
               float width, bool enabled, float scale);

/// A framed card centred in the viewport, used by every screen that sits over the board.
/// One half of the shell: the menu column beside the screen's object.
///
/// `scale` grows or shrinks the whole card and its text about the pane's centre, and
/// `alpha` fades it. Both are supplied by the shell's transition rather than derived
/// here, so an arriving screen can grow from small while a departing one swells and
/// fades, and the two moves can be exact reverses of each other. `remember` is false for
/// a departing ghost: its scaled measurement must not become the height the settled pane
/// is later centred from.
void beginPane(const char* id, ImVec2 min, ImVec2 max, float scale, float alpha,
               float pad, bool remember = true);
void endPane();

void beginPlate(const char* id, const view::Theme& theme, ImVec2 size,
                float yBias = 0.5f);
void endPlate();

}  // namespace cb::render::widgets
