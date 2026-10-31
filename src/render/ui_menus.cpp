// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every screen that is not the board: the main menu, the variant picker, pause,
// settings, the editor, and the two reference screens.
//
// They read state from app::Shell and report intent back through UiRequest. Nothing
// here decides anything - which is the same rule the board follows, and is why the
// transitions between screens are covered by tests that never open a window.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "io/notation.hpp"
#include "render/piece_mesh.hpp"
#include "render/ui.hpp"
#include "render/ui_widgets.hpp"

namespace cb::render {
namespace {

/// The computer's difficulty as a 1..5 slider whose value reads as a word. True when an
/// edit has just been let go of (so the caller saves once, not on every drag step).
bool computerLevelSlider(int& level) {
  static const char* const kNames[] = {"1 - Easy", "2 - Casual", "3 - Normal", "4 - Hard",
                                       "5 - Expert"};
  level = std::clamp(level, 1, 5);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
  ImGui::SliderInt("Computer level", &level, 1, 5, kNames[level - 1]);
  return ImGui::IsItemDeactivatedAfterEdit();
}

using namespace widgets;

// Keep useful text width when scaling a narrow menu. Wide settled panes retain their
// existing padding; the content can use up to 84% of a narrow half-screen pane.
float menuPanePadding(const view::Theme& theme, ImVec2 min, ImVec2 max, float scale) {
  const float preferred = 46 * scale;
  if (!theme.liveControls) return preferred;
  const float width = std::max(0.0f, max.x - min.x);
  return std::min(preferred, (width - std::min(200 * scale, width * 0.84f)) * 0.5f);
}

/// The colour a difficulty is advertised with. A switch, not an array lookup, so that
/// adding a level to the enum is a compile error here rather than a silent wrap.
view::Rgba difficultyColor(const view::Theme& t, Difficulty d) {
  switch (d) {
    case Difficulty::Easy:
      return t.diffEasy;
    case Difficulty::Medium:
      return t.diffMedium;
    case Difficulty::Hard:
      return t.diffHard;
    case Difficulty::Impossible:
      return t.diffImpossible;
    case Difficulty::Other:
      return t.diffOther;
  }
  return t.diffOther;
}

/// A short, human description of a board's shape - the kind of thing a player wants
/// before starting a game, not a list of axis extents.
std::string describeBoard(const VariantSpec& v) {
  std::string s = std::to_string(v.dims.dims()) + " axes, " +
                  std::to_string(v.dims.cellCount()) + " cells";
  if (!v.geom.isBox()) {
    s += v.geom.isOrientable() ? ", glued" : ", glued with a twist";
  }
  return s;
}

/// Where the cells a piece can reach land, projected onto the first two axes.
///
/// It is the most useful thing the game can show about a variant nobody has played:
/// the atoms are exact but abstract, and a diagram of the squares is immediately
/// readable. Anything the piece can only do by leaving the plane is noted rather than
/// silently dropped.
void drawMoveDiagram(const VariantSpec& v, const PieceTypeDef& piece,
                     const view::Theme& theme, float cellSize) {
  constexpr int kRadius = 4;
  const int span = kRadius * 2 + 1;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // 0 = unreachable, 1 = by sliding, 2 = by leaping.
  std::vector<int> mark(static_cast<std::size_t>(span * span), 0);
  bool leavesPlane = false;

  for (const MoveAtom& atom : piece.atoms) {
    const auto begin = atom.dirBegin[static_cast<std::size_t>(Color::White)];
    const auto end = atom.dirEnd[static_cast<std::size_t>(Color::White)];
    for (std::uint32_t di = begin; di < end && di < v.dirTable.size(); ++di) {
      const Direction& d = v.dirTable[di];
      bool planar = true;
      for (std::uint8_t a = 2; a < d.n; ++a) {
        if (d.v[a] != 0) planar = false;
      }
      if (!planar) {
        leavesPlane = true;
        continue;
      }
      const std::uint32_t steps = atom.maxK == kUnlimited
                                      ? static_cast<std::uint32_t>(kRadius)
                                      : std::min<std::uint32_t>(atom.maxK, kRadius);
      for (std::uint32_t k = atom.minK; k <= steps; ++k) {
        const int dx = d.v[0] * static_cast<int>(k);
        const int dy = d.v[1] * static_cast<int>(k);
        if (std::abs(dx) > kRadius || std::abs(dy) > kRadius) break;
        const int gx = dx + kRadius;
        const int gy = kRadius - dy;  // drawn with rank increasing upward
        mark[static_cast<std::size_t>(gy * span + gx)] =
            atom.mode == MoveMode::Slide ? 1 : 2;
      }
    }
  }

  for (int y = 0; y < span; ++y) {
    for (int x = 0; x < span; ++x) {
      const ImVec2 a(origin.x + static_cast<float>(x) * cellSize,
                     origin.y + static_cast<float>(y) * cellSize);
      const ImVec2 b(a.x + cellSize - 1.0f, a.y + cellSize - 1.0f);
      const bool light = (x + y) % 2 == 0;
      dl->AddRectFilled(a, b, u32(light ? theme.boardLight : theme.boardDark, 0.35f));

      const int m = mark[static_cast<std::size_t>(y * span + x)];
      if (x == kRadius && y == kRadius) {
        dl->AddRectFilled(a, b, u32(theme.ember, 0.85f));
      } else if (m == 1) {
        dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f),
                            cellSize * 0.22f, u32(theme.moss));
      } else if (m == 2) {
        // A leap is drawn as a ring: it arrives without crossing what lies between.
        dl->AddCircle(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), cellSize * 0.26f,
                      u32(theme.moss), 0, 2.0f);
      }
    }
  }
  const float side = static_cast<float>(span) * cellSize;
  dl->AddRect(origin, ImVec2(origin.x + side, origin.y + side), u32(theme.rule));
  ImGui::Dummy(ImVec2(side, side));

  if (leavesPlane) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme.rift));
    ImGui::TextUnformatted("also leaves this plane");
    ImGui::PopStyleColor();
  }
}

}  // namespace

UiRequest Ui::buildMainMenu(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##mainmenu", menuMin, menuMax, move.scale, move.alpha,
            menuPanePadding(t, menuMin, menuMax, scale_), !ghosting_);
  screenTitle("ENDSPIELZERO", t, display, scale_, 2.4f);
  ImGui::Dummy(ImVec2(0, px(14)));

  const float width = ImGui::GetContentRegionAvail().x;
  if (menuEntry("New game", 1, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::NewGame);
  }
  // The demo action: watch a bundled game play itself, through the ordinary board and
  // move camera (M12.8). It opens a short picker rather than a fixed file, because two
  // games ship.
  if (menuEntry("Watch a game", 2, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::Watch);
  }
  // The trailer: a fixed, looping tour of the shapes, played on the game screen (M13.9).
  // Unlike "Watch a game" there is nothing to choose, so the row starts it in place
  // instead of opening a picker.
  if (menuEntry("Watch trailer", 3, t, actionFont(), width, true, scale_)) {
    request.startAttract = true;
  }
  // Disabled rows stay in the list, marked and nothing more. A menu that hides what is
  // coming is dishonest; one that explains itself every time the pointer crosses it is
  // just noisy.
  if (menuEntry("Continue", 4, t, actionFont(), width, false, scale_)) {
    // unreachable while disabled, but kept so enabling it is a one-word change
  }
  if (menuEntry("Multiplayer", 5, t, actionFont(), width, false, scale_)) {
  }
  // The curated "start here" path, summoned when the player asks for it rather than
  // opened on first launch (M14.1, revised).
  if (menuEntry("Tutorial", 6, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::Welcome);
  }
  if (menuEntry("Editor", 7, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::Editor);
  }
  if (menuEntry("Settings", 8, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::Settings);
  }
  if (menuEntry("Quit", 9, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::QuitConfirm);
  }

  endPane();

  return request;
}

UiRequest Ui::buildWatch(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  const std::vector<app::WatchGame>& games = shell.bundledGames();
  if (pickedWatch_.empty() && !games.empty()) pickedWatch_ = games.front().name;

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##watch", menuMin, menuMax, move.scale, move.alpha,
            menuPanePadding(t, menuMin, menuMax, scale_), !ghosting_);
  eyebrow("watch", t, mono, scale_);
  screenTitle("Watch a game", t, display, scale_, 2.0f);
  ImGui::Dummy(ImVec2(0, px(6)));
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  ImGui::TextWrapped(
      "The engine plays a recorded game move by move and you watch. The board is not "
      "yours to touch until you stop.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(10)));

  const float rowH = px(26.0f);
  const float listW = ImGui::GetContentRegionAvail().x * 0.44f;
  const auto actions =
      actionPairLayout("START WATCHING", "BACK", t, px(180), px(110), actionFont());
  const float footer =
      t.liveControls ? px(10) + actions.height + px(6) : px(10) + px(40) + px(6);
  const float wantH = static_cast<float>(games.size()) * rowH + px(18.0f);
  const float room = std::max(1.0f, ImGui::GetContentRegionAvail().y - footer);
  const float listH = std::clamp(wantH, std::min(px(60.0f), room), room);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, col(t.ink, 0.45f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::BeginChild("##watchlist", ImVec2(listW, listH), ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NavFlattened);
  for (const app::WatchGame& g : games) {
    const bool active = g.name == pickedWatch_;
    ImGui::PushID(g.name.c_str());
    const float rowW = ImGui::GetContentRegionAvail().x;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + rowW, min.y + rowH);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
      dl->AddRectFilled(min, max, u32(t.emberDeep, 0.88f), px(4));
    } else if (hovered) {
      dl->AddRectFilled(min, max, u32(t.panelHi), px(4));
    }
    dl->AddText(ImVec2(min.x + px(12), min.y + (rowH - ImGui::GetFontSize()) * 0.5f),
                u32(active ? t.bone : (hovered ? t.bone : t.boneDim)),
                upper(g.name).c_str());
    if (clicked) pickedWatch_ = g.name;
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::SameLine();
  ImGui::BeginChild("##watchdetail", ImVec2(0, listH), 0, ImGuiWindowFlags_NavFlattened);
  const app::WatchGame* sel = nullptr;
  for (const app::WatchGame& g : games) {
    if (g.name == pickedWatch_) sel = &g;
  }
  if (sel != nullptr) {
    ImGui::PushFont(display);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.bone));
    ImGui::TextUnformatted(upper(sel->file.variant).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(6)));
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    char line[96];
    std::snprintf(line, sizeof(line), "%zu moves",
                  static_cast<std::size_t>(sel->file.moves.size()));
    ImGui::TextUnformatted(line);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(8)));
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    if (watchDescribedFor_ != sel->file.variant) {
      watchDescribedFor_ = sel->file.variant;
      watchDescription_ = shell.variantDescription(sel->file.variant);
    }
    if (!watchDescription_.empty()) ImGui::TextWrapped("%s", watchDescription_.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::EndChild();

  ImGui::Dummy(ImVec2(0, px(10)));
  const bool enter = !ghosting_ && !games.empty() && !ImGui::GetIO().WantTextInput &&
                     (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
  if (!games.empty() &&
      (button("START WATCHING", t, actions.first.x, true, false, true, actionFont()) ||
       enter)) {
    request.watchGame = pickedWatch_;
  } else if (games.empty()) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted("no bundled games are installed");
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  if (!actions.stacked) ImGui::SameLine();
  if (button("BACK", t, actions.second.x, false, false, true, actionFont())) shell.back();
  if (!shell.message().empty()) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", shell.message().c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

  endPane();
  return request;
}

UiRequest Ui::buildWelcome(app::Shell& shell) {
  // The first launch is a designed sequence, not the main menu (M14.1): what this is,
  // then a curated path from the board a newcomer knows to the board they cannot picture,
  // one legible step at a time (M14.2).
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##welcome", menuMin, menuMax, move.scale, move.alpha,
            menuPanePadding(t, menuMin, menuMax, scale_), !ghosting_);
  eyebrow("tutorial", t, mono, scale_);
  screenTitle("Start here", t, display, scale_, 2.2f);
  ImGui::Dummy(ImVec2(0, px(6)));
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextWrapped(
      "chess on any board you can describe: cylinders, Moebius bands, "
      "six-dimensional tori, and time travel.");
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(10)));

  const float width = ImGui::GetContentRegionAvail().x;

  const auto start = [&](const std::string& name) {
    shell.dismissWelcome();
    request.loadVariant = name;
  };
  const auto caption = [&](const char* text) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(6)));
  };

  // The curated path: standard first, exotic last. A front door, not a gate.
  int row = 1;
  for (const app::CuratedVariant& c : app::curatedVariants()) {
    if (menuEntry(c.name.c_str(), row++, t, actionFont(), width, true, scale_)) {
      start(c.name);
      break;
    }
    caption(c.what.c_str());
  }
  if (menuEntry("Surprise me", row++, t, actionFont(), width, true, scale_)) {
    start(shell.surpriseVariant());
  }
  const std::string surprise = shell.currentVariant();
  if (!surprise.empty()) {
    // Say why the last pick is strange, so a random choice still teaches.
    const std::string why = shell.variantDescription(surprise);
    if (!why.empty()) caption(why.c_str());
  }
  if (menuEntry("Show me all the boards", row++, t, actionFont(), width, true, scale_)) {
    shell.dismissWelcome();
    shell.go(app::Screen::NewGame);
  }

  endPane();
  return request;
}

UiRequest Ui::buildQuitConfirm(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##quitconfirm", menuMin, menuMax, move.scale, move.alpha, px(46.0f),
            !ghosting_);
  eyebrow("quit", t, mono, scale_);
  screenTitle("Are you sure?", t, display, scale_, 1.9f);
  ImGui::Dummy(ImVec2(0, px(6)));
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  ImGui::TextWrapped(
      "The window will close. A position that has not been saved is lost.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(14)));

  // Leaving is the destructive choice, so it is not the one under the pointer by
  // default and it is not the accent: backing out is.
  if (button("BACK", t, px(150), true, false, true, actionFont())) shell.back();
  ImGui::SameLine();
  if (button("QUIT", t, px(110), false, false, true, actionFont())) request.quit = true;
  endPane();
  return request;
}

UiRequest Ui::buildPauseQuitConfirm(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  // The pause section's own quit prompt, a step back from the game rather than the main
  // menu's. Same board-over-scrim frame and pane as its neighbours, so it belongs here.
  ImVec2 menuMin, menuMax;
  pauseFrame(request, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##pausequit", menuMin, menuMax, move.scale, move.alpha, px(46.0f),
            !ghosting_);
  eyebrow("quit", t, mono, scale_);
  screenTitle("Leave the game?", t, display, scale_, 1.9f);
  ImGui::Dummy(ImVec2(0, px(6)));
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  ImGui::TextWrapped("The window will close and the game in progress will be lost.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(14)));

  if (button("BACK", t, px(150), true, false, true, actionFont())) shell.back();
  ImGui::SameLine();
  if (button("QUIT", t, px(110), false, false, true, actionFont())) request.quit = true;
  endPane();
  return request;
}

UiRequest Ui::buildNewGame(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  // Settled before the frame is drawn: the object beside the menu is the selected
  // variant's overture, and it is drawn first.
  if (pickedVariant_.empty()) {
    const auto& lib = shell.library();
    const bool hasStandard = std::find(lib.begin(), lib.end(), "standard") != lib.end();
    // The first entry to the library in a session opens on standard, because that is
    // the overture that builds the board out of nothing and it only ever happens once.
    // After that the picker reopens wherever the player last was.
    if (shell.overtures().intro() && hasStandard) {
      pickedVariant_ = "standard";
    } else {
      pickedVariant_ = shell.currentVariant();
      if (pickedVariant_.empty() && !lib.empty()) pickedVariant_ = lib.front();
    }
  }
  // The picker reports what the player is looking at; the shell decides what that does
  // to the overture - including that a shape already on screen unwinds to the flat board
  // before the next one begins. Not while building the departing ghost: its request is
  // discarded, and advancing the cycle twice in a frame would run it at double speed.
  // A pinned player belongs to a capture, which has already said what it wants to see.
  if (!ghosting_ && !shell.overtures().pinned()) {
    shell.overtures().select(pickedVariant_);
    shell.overtures().advance(lastDt_);
  }

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##newgame", menuMin, menuMax, move.scale, move.alpha,
            menuPanePadding(t, menuMin, menuMax, scale_), !ghosting_);
  eyebrow("new game", t, mono, scale_);
  screenTitle("Library", t, display, scale_, 2.2f);
  ImGui::Dummy(ImVec2(0, px(6)));
  // The description lives in the variant file, so it is read once per selection rather
  // than every frame. A variant that will not load simply has no blurb.
  if (describedFor_ != pickedVariant_) {
    describedFor_ = pickedVariant_;
    pickedDescription_ = shell.variantDescription(pickedVariant_);
  }

  // Keep actions outside the scrolling body. The list and game options can scroll
  // together even when the large-scale form is taller than the whole pane.
  const auto actions =
      actionPairLayout("START", "BACK", t, px(150), px(110), actionFont());
  const float actionH = actions.height + px(8);
  ImGui::BeginChild("##newgamebody",
                    ImVec2(0, std::max(1.0f, ImGui::GetContentRegionAvail().y - actionH -
                                                 ImGui::GetStyle().ItemSpacing.y)),
                    0, ImGuiWindowFlags_NavFlattened);
  const float rowH = px(26.0f);
  // Proportional, not a pixel count: the pane is half the window, and a width chosen
  // for a fixed plate squeezes the description to one letter per line.
  const float listW = ImGui::GetContentRegionAvail().x * 0.44f;
  // Two checkboxes, their note and the computer level slider. Keep a useful list
  // height when those options cannot all fit: the containing body scrolls then.
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  const float optionsH = px(6) + spacing + 3.0f * ImGui::GetFrameHeightWithSpacing() +
                         small->FontSize + spacing + px(6) + spacing;
  // And no taller than the list actually is. Stretching it to the full pane left a
  // field of empty panel between the last variant and the buttons, which reads as the
  // controls having fallen to the bottom of the screen rather than as a group sitting
  // under the thing they act on.
  const float wantH = static_cast<float>(shell.library().size()) * rowH + px(18.0f);
  const float room = std::max(px(80), ImGui::GetContentRegionAvail().y - optionsH);
  const float listH = std::clamp(wantH, std::min(px(140.0f), room), room);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, col(t.ink, 0.45f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::BeginChild("##library", ImVec2(listW, listH), ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NavFlattened);
  for (const std::string& name : shell.library()) {
    const bool active = name == pickedVariant_;
    // The difficulty is a property of the variant, resolved once by the shell; the
    // colour follows from it. The selected row is filled with its own difficulty
    // colour - the choice states how hard the thing you picked is - and every row
    // carries a small tab of that colour at its left, so the level reads before a
    // click. `bone` is the type colour that contrasts with the page in both themes,
    // which is what keeps the text legible on the saturated fill.
    const view::Rgba diff = difficultyColor(t, shell.difficultyOf(name));
    ImGui::PushID(name.c_str());
    const float rowW = ImGui::GetContentRegionAvail().x;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + rowW, min.y + rowH);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
      dl->AddRectFilled(min, max, u32(diff, 0.88f), px(4));
    } else if (hovered) {
      dl->AddRectFilled(min, max, u32(t.panelHi), px(4));
    }
    const float tabW = px(5.0f);
    dl->AddRectFilled(ImVec2(min.x, min.y + px(3)), ImVec2(min.x + tabW, max.y - px(3)),
                      u32(diff, active ? 1.0f : 0.78f), px(2));
    const ImU32 textCol = u32(active ? t.bone : (hovered ? t.bone : t.boneDim));
    dl->AddText(
        ImVec2(min.x + px(12) + tabW, min.y + (rowH - ImGui::GetFontSize()) * 0.5f),
        textCol, upper(name).c_str());
    if (clicked) pickedVariant_ = name;
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::SameLine();
  ImGui::BeginChild("##details", ImVec2(0, listH), 0, ImGuiWindowFlags_NavFlattened);
  ImGui::PushFont(display);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.bone));
  ImGui::TextUnformatted(upper(pickedVariant_).c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(6)));
  // The board's shape, once the game is actually loaded - real numbers rather than a
  // promise. Before that the variant's own one-line pitch is the honest thing to show.
  if (const app::Session* loaded = shell.session();
      loaded != nullptr && loaded->variant().name == pickedVariant_) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted(describeBoard(loaded->variant()).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(8)));
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  if (!pickedDescription_.empty()) {
    ImGui::TextWrapped("%s", pickedDescription_.c_str());
  }
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::EndChild();

  ImGui::Dummy(ImVec2(0, px(6)));
  // Two-player keyboard mode, chosen here so it can be set for the game about to start
  // as well as from Settings; both write the one saved setting.
  ImGui::Checkbox("Two players, one keyboard", &shell.settings().hotSeat);
  if (ImGui::IsItemDeactivatedAfterEdit()) request.settingsChanged = true;
  ImGui::Checkbox("Play against the computer", &shell.settings().vsComputer);
  if (ImGui::IsItemDeactivatedAfterEdit()) request.settingsChanged = true;
  if (shell.settings().vsComputer) {
    ImGui::SameLine();
    ImGui::Checkbox("it plays White", &shell.settings().computerPlaysWhite);
    if (ImGui::IsItemDeactivatedAfterEdit()) request.settingsChanged = true;
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("each player types their own half of the keyboard");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  if (computerLevelSlider(shell.settings().computerLevel)) request.settingsChanged = true;
  ImGui::Dummy(ImVec2(0, px(6)));
  ImGui::EndChild();
  // Enter starts whatever the list has selected. The library is a list of things to
  // play, so the key that means "yes, this one" should work without reaching for the
  // mouse - and it is the only control here that needs no aim.
  const bool enter = !ghosting_ && !ImGui::GetIO().WantTextInput &&
                     (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                      ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
  if (button("START", t, actions.first.x, true, false, true, actionFont()) || enter) {
    request.loadVariant = pickedVariant_;
  }
  if (!actions.stacked) ImGui::SameLine();
  if (button("BACK", t, actions.second.x, false, false, true, actionFont())) shell.back();
  if (!shell.message().empty()) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", shell.message().c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  endPane();
  return request;
}

UiRequest Ui::buildPause(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  // Pause sits over the position: the board keeps the left of the frame, pulled back and
  // blurred, and the menu takes the right over a thin scrim.
  ImVec2 menuMin, menuMax;
  pauseFrame(request, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##pause", menuMin, menuMax, move.scale, move.alpha,
            menuPanePadding(t, menuMin, menuMax, scale_), !ghosting_);
  eyebrow(shell.currentVariant().c_str(), t, mono, scale_);
  screenTitle("Paused", t, display, scale_, 1.8f);
  ImGui::Dummy(ImVec2(0, px(8)));

  const float width = ImGui::GetContentRegionAvail().x;
  if (menuEntry("Continue", 1, t, actionFont(), width, true, scale_)) shell.resume();
  // Resigning or agreeing a draw only means anything while the game is unfinished, and
  // they sit right under Continue because ending the game is the decision a pause is
  // most often opened for.
  if (shell.session()->game().result() == GameResult::InProgress) {
    if (menuEntry("Resign", 2, t, actionFont(), width, true, scale_)) {
      app::Action a;
      a.kind = app::ActionKind::Resign;
      (void)shell.session()->apply(a);
      shell.resume();
    }
    if (menuEntry("Offer draw", 3, t, actionFont(), width, true, scale_)) {
      app::Action a;
      a.kind = app::ActionKind::AgreeDraw;
      (void)shell.session()->apply(a);
      shell.resume();
    }
  }
  if (menuEntry("Game mode", 4, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::GameInfo);
  }
  if (menuEntry("Piece moves", 5, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::PieceMoves);
  }
  if (menuEntry("Settings", 6, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::Settings);
  }
  if (menuEntry("Main menu", 7, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::MainMenu);
  }
  // A pause-specific confirmation, so quitting from the game keeps the pause section's
  // own look and step rather than jumping to the main menu's prompt.
  if (menuEntry("Quit", 8, t, actionFont(), width, true, scale_)) {
    shell.go(app::Screen::PauseQuitConfirm);
  }

  // Saved games (M12.7): write the running game to a named file, or reopen one. The files
  // live in a `games` folder beside the settings; the list is whatever is already there.
  ImGui::Dummy(ImVec2(0, px(10)));
  ImGui::PushFont(static_cast<ImFont*>(fontSmall_));
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("GAME FILES");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::SetNextItemWidth(px(228.0f));
  ImGui::InputText("##gamename", gameName_, sizeof(gameName_));
  if (button("Save", t, px(108), false, false, true, actionFont())) {
    request.saveGame = true;
    request.gameName = gameName_;
  }
  ImGui::SameLine();
  if (button("Load", t, px(108), false, false, true, actionFont())) {
    request.loadGame = true;
    request.gameName = gameName_;
  }
  if (!shell.message().empty()) {
    ImGui::PushFont(static_cast<ImFont*>(fontSmall_));
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted(shell.message().c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  for (const std::string& g : shell.savedGames()) {
    if (ImGui::Selectable(g.c_str())) {
      std::snprintf(gameName_, sizeof(gameName_), "%s", g.c_str());
    }
  }

  // The keys, where a player looks for them.
  ImGui::Dummy(ImVec2(0, px(10)));
  ImGui::PushFont(static_cast<ImFont*>(fontSmall_));
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted(
      "Esc pause   U undo   R reset   F flat view   Q quit (from the menu)");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  endPane();
  return request;
}

UiRequest Ui::buildEditor(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);

  const PaneMove move = paneMove();
  beginPane("##editor", menuMin, menuMax, move.scale, move.alpha,
            editorPage_ == 0 ? menuPanePadding(t, menuMin, menuMax, scale_) : px(46.0f),
            !ghosting_);
  eyebrow("editor", t, mono, scale_);

  // Leaving with unsaved changes is asked about, never assumed: `Shell::back` (a BACK or
  // Esc anywhere) raises a request instead of leaving, and this is where it is answered.
  if (shell.editorLeaveRequested()) {
    leaveTarget_ = 2;
    shell.clearEditorLeaveRequest();
  }
  if (leaveTarget_ != 0 && shell.editor() != nullptr) {
    app::Editor* ed = shell.editor();
    screenTitle("Unsaved changes", t, display, scale_, 1.9f);
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    ImGui::TextWrapped("%s has changes that are not saved. Leaving now throws them away.",
                       shell.editorVariant().c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    const bool valid = ed->validate().has_value();
    if (!valid) {
      ImGui::PushFont(small);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
      ImGui::TextWrapped("It cannot be saved as it is: something in it is invalid.");
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0, px(10)));
    const auto finish = [&] {
      if (leaveTarget_ == 1) {
        editorPage_ = 0;
      } else {
        shell.leaveEditor();
      }
      leaveTarget_ = 0;
    };
    if (button("SAVE AND LEAVE", t, px(220), true, false, valid, actionFont())) {
      if (shell.saveEditor().has_value()) finish();
    }
    if (button("LEAVE WITHOUT SAVING", t, px(260), false, false, true, actionFont())) {
      shell.discardEditorChanges();
      designSyncedRevision_ = ~0ull;
      finish();
    }
    if (button("STAY", t, px(120), false, false, true, actionFont())) leaveTarget_ = 0;
    endPane();
    return request;
  }

  // The editor's home: choose which designer. The piece designer is built; the board
  // designer is not, so it is offered but does nothing yet.
  if (editorPage_ == 0) {
    screenTitle("Editor", t, display, scale_, 1.9f);
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    ImGui::TextWrapped(
        "A variant is already data - axes, geometry, pieces and rules in one file. "
        "These designers write that file for you.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(12)));
    const float homeWidth = ImGui::GetContentRegionAvail().x;
    if (menuEntry("Piece designer", 1, t, actionFont(), homeWidth, true, scale_)) {
      editorPage_ = 1;
    }
    if (menuEntry("Board designer", 2, t, actionFont(), homeWidth, true, scale_)) {
      editorPage_ = 3;
    }
    ImGui::Dummy(ImVec2(0, px(10)));
    if (button("BACK", t, px(120), false, false, true, actionFont())) shell.back();
    endPane();
    return request;
  }

  // Help: what a piece's movement actually is, in the engine's own terms. Reached from
  // the piece designer, so the words on it are the words the controls use.
  if (editorPage_ == 2) {
    screenTitle("How movement works", t, display, scale_, 1.6f);
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(
        "A piece is a list of moves. Each move is a set of step sizes - [1] is one "
        "square, [1,2] is a knight's jump - and the piece makes it in any direction, "
        "along any combination of the board's axes. The same numbers give four moves on "
        "a "
        "flat board and many more in higher dimensions.");
    ImGui::Spacing();
    ImGui::TextUnformatted(
        "Kind: slide runs along until something blocks it; leap jumps straight there; "
        "hop jumps over exactly one piece. Capture: 'may' takes or moves quietly, 'must' "
        "only takes, 'cannot' only moves to an empty square - a pawn's step is 'cannot' "
        "and its capture is 'must'. Runs keeps going to the edge; forward keeps only the "
        "half facing the board's forward direction, which is what makes a pawn in any "
        "number of dimensions.");
    ImGui::Spacing();
    ImGui::TextUnformatted(
        "Build a piece by adding moves. A rook plus a bishop is a queen on a flat board; "
        "the queen button adds every straight line - triagonals and beyond in more "
        "dimensions. "
        "The board on the left shows the piece in white at the centre and where it can "
        "go: green for a quiet move, red for a capture. Click the board to place black "
        "and white pieces - a pawn's capture only lights up when there is something to "
        "take. CLEAR empties the board again.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(12)));
    if (button("BACK", t, px(120), false, false, true, actionFont())) editorPage_ = 1;
    endPane();
    return request;
  }

  // The board designer: the shape of the board - its axes, and whether an axis is glued
  // to its opposite face. Piecing the army together is the other designer's job.
  if (editorPage_ == 3) {
    screenTitle("Board designer", t, display, scale_, 1.7f);
    app::Editor* editor = shell.editor();
    if (editor == nullptr) {
      ImGui::PushFont(small);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
      ImGui::TextWrapped("No variant is open to edit.");
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, px(10)));
      if (button("BACK", t, px(120), false, false, true, actionFont())) editorPage_ = 0;
      endPane();
      return request;
    }

    const auto chip = [&](const char* label, const view::Rgba& c, float w) {
      ImGui::PushStyleColor(ImGuiCol_Button, col(c, 0.85f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(c));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(c));
      const bool pressed = ImGui::Button(label, ImVec2(w, 0.0f));
      ImGui::PopStyleColor(3);
      return pressed;
    };

    ImGui::PushFont(small);
    const auto axes = editor->axes();
    for (std::size_t i = 0; i < axes.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      ImGui::TextUnformatted(axes[i].name.c_str());
      ImGui::SameLine(px(120.0f));
      if (chip("-", t.panelHi, px(22)) && axes[i].extent > 1) {
        (void)editor->setAxisExtent(i, axes[i].extent - 1);
      }
      ImGui::SameLine();
      ImGui::Text("%d", axes[i].extent);
      ImGui::SameLine();
      if (chip("+", t.ember, px(22))) {
        (void)editor->setAxisExtent(i, axes[i].extent + 1);
      }
      ImGui::SameLine();
      const char* kindLabel = axes[i].kind == AxisKind::Spatial    ? "spatial"
                              : axes[i].kind == AxisKind::Temporal ? "temporal"
                                                                   : "multiverse";
      const view::Rgba kindCol = axes[i].kind == AxisKind::Spatial    ? t.moss
                                 : axes[i].kind == AxisKind::Temporal ? t.diffMedium
                                                                      : t.diffImpossible;
      if (chip(kindLabel, kindCol, px(86))) {
        const AxisKind next = axes[i].kind == AxisKind::Spatial    ? AxisKind::Temporal
                              : axes[i].kind == AxisKind::Temporal ? AxisKind::Multiverse
                                                                   : AxisKind::Spatial;
        (void)editor->setAxisKind(i, next);
      }
      ImGui::SameLine();
      if (chip(axes[i].periodic ? "glued" : "open", axes[i].periodic ? t.rift : t.panelHi,
               px(64))) {
        (void)editor->setAxisPeriodic(i, !axes[i].periodic);
      }
      ImGui::SameLine();
      if (chip("x", t.blood, px(22))) (void)editor->removeAxis(i);
      ImGui::PopID();
    }
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, px(6)));
    ImGui::PushFont(small);
    if (chip("ADD AXIS", t.emberDeep, 0.0f)) (void)editor->addAxis();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextWrapped(
        "An extent is how many cells an axis has; a glued axis joins its two ends, so a "
        "cylinder is one glued axis and a torus two.");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, px(8)));
    // The editor is a front end to the loader: a board the game would refuse cannot be
    // saved, and the reason is the loader's own, shown where the button is (M7.0.3).
    const Result<void> loads = editor->validate();
    if (!loads.has_value()) {
      ImGui::PushFont(small);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
      ImGui::TextWrapped("%s", loads.error().format().c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
    ImGui::PushFont(small);
    if (button("SAVE", t, px(90), editor->dirty(), false, loads.has_value(),
               actionFont())) {
      (void)shell.saveEditor();
    }
    ImGui::SameLine();
    if (button("BACK", t, px(120), false, false, true, actionFont())) editorPage_ = 0;
    ImGui::PopFont();
    endPane();
    return request;
  }

  screenTitle("Piece designer", t, display, scale_, 1.9f);

  app::Editor* editor = shell.editor();
  if (editor == nullptr) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    ImGui::TextWrapped("No variant is open to edit. Start one, then return here.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(10)));
    if (button("BACK", t, px(120), false, false, true, actionFont())) editorPage_ = 0;
    endPane();
    return request;
  }

  // ---- header: what is open, whether it is saved, and the edit controls.
  ImGui::PushFont(mono);
  ImGui::PushStyleColor(ImGuiCol_Text, col(editor->dirty() ? t.ember : t.boneDim));
  ImGui::Text("%s%s", shell.editorVariant().c_str(), editor->dirty() ? "  *unsaved" : "");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(4)));
  if (button("UNDO", t, px(84), false, true, true, actionFont())) editor->undo();
  ImGui::SameLine();
  if (button("REDO", t, px(84), false, true, true, actionFont())) editor->redo();
  ImGui::SameLine();
  // The loader's verdict, cached per revision: one validator, and the message is its own.
  // SAVE is held back while the document would not load - there is no "save anyway".
  if (validatedRevision_ != editor->revision()) {
    const Result<void> ok = editor->validate();
    validateNote_ = ok.has_value() ? std::string{} : ok.error().format();
    validatedRevision_ = editor->revision();
    // Is a move row somewhere one the engine refuses? Then the rows say so, in red, and
    // the sentence stays out of the header.
    movesInvalid_ = false;
    for (const std::string& name : editor->pieceNames()) {
      const auto atoms = editor->pieceAtoms(name);
      if (!atoms.has_value()) continue;
      for (const MoveAtom& a : *atoms) {
        if (!MoveAtom::canonicalize(a).has_value()) movesInvalid_ = true;
      }
    }
  }
  if (button("SAVE", t, px(84), editor->dirty(), false, validateNote_.empty(),
             actionFont())) {
    (void)shell.saveEditor();
  }
  ImGui::SameLine();
  if (button("HELP", t, px(84), false, true, true, actionFont())) editorPage_ = 2;
  ImGui::SameLine();
  if (button("CLEAR", t, px(88), false, true, true, actionFont())) {
    for (auto& cells : previewOcc_) std::fill(cells.begin(), cells.end(), 0);
  }
  // A bad move row is shown red where it is; the sentence is only for what has no row.
  if (!validateNote_.empty() && !movesInvalid_) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("cannot save: %s", validateNote_.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

  // The controls are ImGui's, but they wear the game's clothes: rounded, panel-coloured,
  // ember for the marks, so the designer sits in the same world as the menus rather than
  // looking like a developer tool.
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(6.0f), px(3.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(5.0f), px(5.0f)));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, col(t.panelHi, 0.55f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, col(t.panelHi));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, col(t.emberDeep, 0.75f));
  ImGui::PushStyleColor(ImGuiCol_CheckMark, col(t.ember));
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.bone));
  ImGui::PushStyleColor(ImGuiCol_PopupBg, col(t.panel));
  ImGui::PushStyleColor(ImGuiCol_Header, col(t.panelHi));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col(t.panelHi));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, col(t.emberDeep));

  const std::vector<std::string> names = editor->pieceNames();
  if (names.empty()) {
    ImGui::TextUnformatted("This variant declares no pieces.");
  } else {
    if (!pendingPiece_.empty()) {
      for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        if (names[static_cast<std::size_t>(i)] == pendingPiece_) editorPiece_ = i;
      }
      pendingPiece_.clear();
    }
    if (editorPiece_ < 0 || editorPiece_ >= static_cast<int>(names.size())) {
      editorPiece_ = 0;
    }
    sectionHead("PIECE", t, fontSmall_, scale_);
    ImGui::PushFont(small);
    // The names here and the "add a move" bundles below are both called rook, bishop,
    // knight and king; without a scope of their own they share an ImGui ID, and the
    // second row answers the first one's clicks.
    // While something is invalid there is no moving on: not to another piece, not to
    // another tab. Fix it, or leave without saving.
    const bool locked = !validateNote_.empty();
    ImGui::BeginDisabled(locked);
    ImGui::PushID("piece-chips");
    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
      if (i != 0) ImGui::SameLine();
      const bool selected = i == editorPiece_;
      ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, selected ? 1.0f : 0.45f));
      ImGui::PushStyleColor(ImGuiCol_Text, col(selected ? t.ember : t.boneDim));
      if (ImGui::SmallButton(names[static_cast<std::size_t>(i)].c_str())) {
        editorPiece_ = i;
      }
      ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    ImGui::PopFont();
    ImGui::EndDisabled();

    // Three jobs on one piece - how it moves, what its body is, what its flat outline
    // is - so three tabs rather than three screens. Moving between them must not feel
    // like leaving the piece behind.
    designTab_ = std::clamp(designTab_, 0, 7);
    ImGui::BeginDisabled(locked);
    tabRow({"MOVES", "BODY", "FLAT ICON", "IDENTITY", "CASTLING", "MARKS", "FIELDS",
            "RULES"},
           designTab_, t, fontDisplay_, scale_);
    ImGui::EndDisabled();

    const std::string piece = names[static_cast<std::size_t>(editorPiece_)];
    // The header stays put and the tab scrolls under it: a designer this long would
    // otherwise carry its own undo and tabs off the top of the screen.
    ImGui::BeginChild("##designbody",
                      ImVec2(0, std::max(px(160.0f), ImGui::GetContentRegionAvail().y -
                                                         controlHeight() - px(26.0f))),
                      false, ImGuiWindowFlags_NavFlattened);
    if (designTab_ == 0) {
      drawMovesTab(shell, piece);
    } else if (designTab_ == 1) {
      drawBodyTab(shell);
    } else if (designTab_ == 2) {
      drawIconTab(shell);
    } else if (designTab_ == 3) {
      drawIdentityTab(shell, piece);
    } else if (designTab_ == 4) {
      drawCompoundTab(shell);
    } else if (designTab_ == 5) {
      drawMarksTab(shell);
    } else if (designTab_ == 6) {
      drawFieldsTab(shell);
    } else {
      drawRulesTab(shell);
    }
    ImGui::EndChild();
  }
  ImGui::PopStyleColor(9);
  ImGui::PopStyleVar(3);

  ImGui::Dummy(ImVec2(0, px(10)));
  if (button("BACK", t, px(120), false, false, true, actionFont())) {
    if (editor->dirty()) {
      leaveTarget_ = 1;  // ask first
    } else {
      editorPage_ = 0;
    }
  }
  endPane();
  return request;
}

UiRequest Ui::buildSettings(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);
  app::Settings& s = shell.settings();
  const app::Settings before = s;

  // From the main menu, Settings is a shell screen with the field and a deco; opened over
  // a game it keeps the position behind it, like the rest of the pause section, and only
  // takes the right of the frame.
  const bool overBoard = shell.showsBoard();
  ImVec2 menuMin, menuMax;
  if (overBoard) {
    pauseFrame(request, menuMin, menuMax);
  } else {
    drawShellFrame(shell, menuMin, menuMax);
  }
  const PaneMove move = paneMove();
  beginPane("##settings", menuMin, menuMax, move.scale, move.alpha,
            menuPanePadding(t, menuMin, menuMax, scale_), !ghosting_);
  ImGui::PushFont(static_cast<ImFont*>(t.monoLabels ? fontLabel_ : fontBody_));
  eyebrow("settings", t, mono, scale_);
  screenTitle("Settings", t, display, scale_, 2.0f);
  ImGui::Dummy(ImVec2(0, px(4)));

  // The body is the one scroller and takes whatever is left once a footer row's height
  // has been reserved. The footer is a box of its own below, so its buttons keep their
  // full height however large the interface scale grows - the body scrolls instead.
  const float footerWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - px(16) -
                                               2 * ImGui::GetStyle().ChildBorderSize);
  const auto actions = actionPairLayout("BACK", "RESET TO DEFAULTS", t, px(120), px(200),
                                        actionFont(), footerWidth);
  const float noteH = small
                          ->CalcTextSizeA(small->FontSize, FLT_MAX, footerWidth,
                                          "saved as you change them")
                          .y;
  const float footerH = t.liveControls
                            ? actions.height + px(8) + ImGui::GetStyle().ItemSpacing.y +
                                  std::ceil(noteH) + 2 * ImGui::GetStyle().ChildBorderSize
                            : controlHeight() + px(14.0f);
  ImGui::BeginChild(
      "##settingsbody",
      ImVec2(0, std::max(t.liveControls ? 1.0f : px(120.0f),
                         ImGui::GetContentRegionAvail().y - footerH - px(8.0f))),
      0, ImGuiWindowFlags_NavFlattened);
  ImGui::PushItemWidth(px(220));

  heading("DISPLAY", t, small);
  ImGui::SliderFloat("Interface scale", &s.guiScale, 0.6f, 3.0f, "%.2fx");
  if (ImGui::IsItemDeactivatedAfterEdit()) request.applyScale = true;
  ImGui::Checkbox("Fullscreen", &s.fullscreen);
  if (ImGui::IsItemDeactivatedAfterEdit()) request.toggleFullscreen = true;
  ImGui::Checkbox("Wait for vertical sync", &s.vsync);
  // A cap matters when vsync is off: the menus and the drifting field animate forever, so
  // an uncapped loop would spin a core on a static screen.
  ImGui::SliderInt("Frame cap", &s.frameCap, 0, 240,
                   s.frameCap == 0 ? "uncapped" : "%d fps");
  ImGui::Checkbox("Flat 2D view", &s.flatView);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("look straight down, pieces as tokens: cheaper to draw");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("LOOKS", t, small);
  {
    // Two sets, and the choice is stored by name so a settings file stays readable and
    // a set added later needs no migration. A press cycles; there is no drop-down.
    if (cycleButton("Piece icons", s.pieceIcons, t, scale_, px(230))) {
      s.pieceIcons = s.pieceIcons == "primitive" ? "faceted" : "primitive";
      request.applyLooks = true;
    }
  }
  {
    // The two palettes, chosen by name for the same reason as the icon set.
    if (cycleButton("Theme", s.theme, t, scale_, px(230))) {
      s.theme = s.theme == "console" ? "manifold" : "console";
    }
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped("%s", s.theme == "manifold"
                               ? "Manifold: cool glass and a circuit grid floor."
                               : "Console: warm wood and drifting shapes.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("BOARD", t, small);
  ImGui::Checkbox("Highlight legal moves", &s.showLegalMoves);
  ImGui::Checkbox("Ctrl over a piece shows its moves", &s.peekMovesOnCtrl);
  ImGui::Checkbox("Ctrl over a square shows what attacks and defends it",
                  &s.showAttackHints);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("Ctrl hints only; the opponent's moves ignore check");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Checkbox("Mark the last move", &s.showLastMove);
  ImGui::Checkbox("Warn when in check", &s.showCheck);
  ImGui::Checkbox("Show seams on glued boards", &s.showSeams);
  ImGui::BeginDisabled(!s.showSeams);
  ImGui::Checkbox("Name the seam colours", &s.showSeamLegend);
  ImGui::EndDisabled();
  ImGui::Checkbox("Show coordinates", &s.showCoordinates);
  ImGui::SliderFloat("Trailer: shape morph speed", &s.trailerMorphSpeed, 0.25f, 4.0f,
                     "%.2fx");
  ImGui::SliderFloat("Trailer: near clip", &s.trailerNearClip, view::kMinNearClip, 8.0f,
                     "%.2f cells");
  ImGui::SliderFloat("Trailer: piece move speed", &s.trailerMoveSpeed, 0.25f, 4.0f,
                     "%.2fx");
  ImGui::SliderFloat("Trailer: show each move first", &s.trailerMovePreview, 0.0f, 0.5f,
                     "%.2f s");
  ImGui::Checkbox("Alternate square colours on every other layer (3-D boards)",
                  &s.swapLayerColors);
  ImGui::SliderFloat("Piece height", &s.pieceHeightScale, 0.0f, 2.0f, "%.2f");
  ImGui::SliderFloat("Near clip (nothing closer is drawn)", &s.nearClip,
                     view::kMinNearClip, view::kMaxNearClip, "%.2f cells");
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("height encodes value; 0 makes every piece the same");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  // A diagnostic knob for the Klein bottle's ring twist. Only whole half-turns close the
  // figure-eight cross-section, so the slider steps by one half-turn.
  ImGui::SliderFloat("Klein twist (half-turns)", &s.kleinTwist, 0.0f, 6.0f, "%.0f");
  // The ring/loop radius: opens a tight torus or Klein bottle (a wider hole) or lengthens
  // the Moebius loop, leaving the cross-section thickness alone.
  ImGui::SliderFloat("Shape width", &s.geometryWidth, 0.4f, 3.0f, "%.2fx");
  ImGui::SliderFloat("Tube width", &s.geometryThickness, 0.3f, 2.5f, "%.2fx");
  // Rotate one rank end of the cylinder relative to the other (one square = 360/nx
  // degrees) before the figure-eight collapse, so the square lines spiral round the tube.
  // Signed: a negative value spirals the other way directly, rather than winding all the
  // way round through the positive values to reach the same square the short way.
  ImGui::SliderFloat("Klein square shift", &s.kleinShift, -7.0f, 7.0f, "%.0f");
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("CAMERA", t, small);
  ImGui::SliderFloat("Orbit sensitivity", &s.orbitSensitivity, 0.1f, 4.0f, "%.2f");
  ImGui::SliderFloat("Zoom sensitivity", &s.zoomSensitivity, 0.1f, 4.0f, "%.2f");
  ImGui::Checkbox("Invert vertical orbit", &s.invertOrbitY);
  {
    // The move camera: off keeps the player's own view; the other two let the shot lead
    // it. A press cycles off -> piece -> route -> off.
    const std::string shown = s.cameraMode == "piece"   ? "follow the piece"
                              : s.cameraMode == "route" ? "follow the route"
                                                        : "off";
    if (cycleButton("Move camera", shown, t, scale_, px(230))) {
      s.cameraMode = s.cameraMode == "off"     ? "piece"
                     : s.cameraMode == "piece" ? "route"
                                               : "off";
    }
  }
  ImGui::BeginDisabled(s.cameraMode == "off");
  {
    // How a move is followed when the board is its own shape: turn the board under a
    // fixed camera angle, or chase the piece with the camera. A press toggles.
    const std::string shown =
        s.shapeFollow == "chase" ? "chase the piece" : "turn the board";
    if (cycleButton("Follow shape", shown, t, scale_, px(230))) {
      s.shapeFollow = s.shapeFollow == "chase" ? "turntable" : "chase";
    }
  }
  ImGui::SliderFloat("Follow strength", &s.followStrength, 0.0f, 1.0f, "%.2f");
  ImGui::SliderFloat("Camera above the piece", &s.followElevationDeg, 5.0f, 80.0f,
                     "%.0f deg");
  // How fast the shape-follow choreography morphs and moves (M17.19).
  ImGui::SliderFloat("Morph speed", &s.shapeMorphSpeed, 0.25f, 2.5f, "%.2fx");
  ImGui::Checkbox("Keep the followed piece upright", &s.followUpright);
  ImGui::Checkbox("Stop the shape clipping the followed piece", &s.geometryAlign);
  ImGui::EndDisabled();
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("how far the camera leads your own orbit while a move plays");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  // The SHAPE button's own flat<->shape roll (M18.6 revision), the same kind of control
  // as Morph speed above but for the board's morph rather than the follow choreography's.
  // Outside the move-camera gate: SHAPE works whether or not a camera follows a move.
  ImGui::SliderFloat("SHAPE morph speed", &s.geometryFormSpeed, 0.25f, 2.5f, "%.2fx");
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("how fast SHAPE rolls the board between flat and its shape");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("ANIMATION", t, small);
  ImGui::Checkbox("Animate moves", &s.animateMoves);
  ImGui::BeginDisabled(!s.animateMoves);
  ImGui::SliderFloat("Animation speed", &s.animationSpeed, 0.05f, 4.0f, "%.2fx");
  ImGui::EndDisabled();
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("higher is faster; a wrapping move opens a portal on the edge");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  // The New Game screen's overtures run on their own clock, so they get their own speed.
  ImGui::SliderFloat("Library animation speed", &s.overtureSpeed, 0.25f, 4.0f, "%.2fx");
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("the variant animations on the New Game screen");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("GAMEPLAY", t, small);
  ImGui::Checkbox("Confirm before moving", &s.confirmMoves);
  ImGui::Checkbox("Two players, one keyboard", &s.hotSeat);
  ImGui::Checkbox("Play against the computer", &s.vsComputer);
  if (s.vsComputer) {
    ImGui::SameLine();
    ImGui::Checkbox("it plays White", &s.computerPlaysWhite);
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted(
      "p1 types qwertasdfgzxcvb, p2 the other half; a square is one key per axis");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  computerLevelSlider(s.computerLevel);
  {
    // The promotion list belongs to the variant, so the choices are read from it rather
    // than hardcoded; with no game loaded there is nothing to promote to yet. A press
    // cycles through them, "always ask" first.
    std::vector<std::string> options{"always ask"};
    if (const app::Session* session = shell.session(); session != nullptr) {
      const VariantSpec& v = session->variant();
      for (std::size_t i = 1; i < v.pieces.size(); ++i) {
        if (!v.pieces[i].promotesTo.empty()) continue;
        options.push_back(v.pieces[i].name);
      }
    }
    std::size_t current = 0;
    for (std::size_t i = 1; i < options.size(); ++i) {
      if (options[i] == s.autoPromoteTo) current = i;
    }
    const std::string shown = s.autoPromoteTo.empty() ? options.front() : s.autoPromoteTo;
    if (cycleButton("Promote to", shown, t, scale_, px(230))) {
      current = (current + 1) % options.size();
      s.autoPromoteTo = current == 0 ? std::string{} : options[current];
    }
  }
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("AUDIO", t, small);
  ImGui::SliderFloat("Master", &s.volumeMaster, 0.0f, 1.0f, "%.2f");
  // Music is deliberately not in this build (M18.5): there is no sourced loop to play, so
  // its slider stays visible but inert rather than hidden, and the screen keeps its shape
  // for when one arrives.
  ImGui::BeginDisabled();
  ImGui::SliderFloat("Music", &s.volumeMusic, 0.0f, 1.0f, "%.2f");
  ImGui::EndDisabled();
  ImGui::SliderFloat("Effects", &s.volumeEffects, 0.0f, 1.0f, "%.2f");
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("sound effects play; music is not in this build yet");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("CONTROLS", t, small);
  ImGui::PushFont(small);
  keyValue("select / move", "left click", t);
  keyValue("orbit", "right drag", t);
  keyValue("zoom", "wheel", t);
  keyValue("pause", "Esc", t);
  keyValue("undo", "U", t);
  keyValue("reset", "R", t);
  ImGui::PopFont();

  ImGui::PopItemWidth();
  ImGui::EndChild();

  // A box of its own under the scroller. Reserving its height and drawing it here is what
  // keeps the buttons whole as the scale grows: the body gives up the room instead, and
  // scrolls if it must. The border is the box the buttons live in.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8.0f), px(4.0f)));
  ImGui::BeginChild("##settingsfooter", ImVec2(0, footerH), ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NavFlattened |
                        (t.liveControls ? ImGuiWindowFlags_NoScrollbar |
                                              ImGuiWindowFlags_NoScrollWithMouse
                                        : 0));
  if (button("BACK", t, actions.first.x, true, false, true, actionFont())) shell.back();
  if (!actions.stacked) ImGui::SameLine();
  if (button("RESET TO DEFAULTS", t, actions.second.x, false, false, true,
             actionFont())) {
    const std::string keepVariant = s.lastVariant;
    s = app::Settings{};
    s.lastVariant = keepVariant;
    request.applyScale = true;
    request.settingsChanged = true;
  }
  if (!t.liveControls) ImGui::SameLine();
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  if (t.liveControls)
    ImGui::TextWrapped("saved as you change them");
  else
    ImGui::TextUnformatted("saved as you change them");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopFont();
  endPane();

  // Persist whenever anything actually moved, rather than on a Save button nobody
  // should have to find.
  // A struct with a std::string in it cannot be compared byte-wise; compare the
  // fields that a control can actually change.
  const bool moved =
      before.guiScale != s.guiScale || before.fullscreen != s.fullscreen ||
      before.vsync != s.vsync || before.frameCap != s.frameCap ||
      before.showLegalMoves != s.showLegalMoves ||
      before.peekMovesOnCtrl != s.peekMovesOnCtrl ||
      before.showAttackHints != s.showAttackHints ||
      before.showLastMove != s.showLastMove || before.showCheck != s.showCheck ||
      before.showSeams != s.showSeams || before.showSeamLegend != s.showSeamLegend ||
      before.showCoordinates != s.showCoordinates ||
      before.swapLayerColors != s.swapLayerColors ||
      before.trailerMovePreview != s.trailerMovePreview ||
      before.trailerMorphSpeed != s.trailerMorphSpeed ||
      before.trailerMoveSpeed != s.trailerMoveSpeed ||
      before.trailerNearClip != s.trailerNearClip || before.kleinTwist != s.kleinTwist ||
      before.pieceHeightScale != s.pieceHeightScale || before.nearClip != s.nearClip ||
      before.flatView != s.flatView || before.pieceIcons != s.pieceIcons ||
      before.animateMoves != s.animateMoves || before.theme != s.theme ||
      before.animationSpeed != s.animationSpeed ||
      before.overtureSpeed != s.overtureSpeed ||
      before.orbitSensitivity != s.orbitSensitivity ||
      before.zoomSensitivity != s.zoomSensitivity ||
      before.invertOrbitY != s.invertOrbitY || before.cameraMode != s.cameraMode ||
      before.followStrength != s.followStrength ||
      before.followElevationDeg != s.followElevationDeg ||
      before.shapeMorphSpeed != s.shapeMorphSpeed ||
      before.geometryFormSpeed != s.geometryFormSpeed ||
      before.followUpright != s.followUpright ||
      before.geometryAlign != s.geometryAlign || before.shapeFollow != s.shapeFollow ||
      before.confirmMoves != s.confirmMoves || before.autoPromoteTo != s.autoPromoteTo ||
      before.volumeMaster != s.volumeMaster || before.volumeMusic != s.volumeMusic ||
      before.volumeEffects != s.volumeEffects;
  if (moved) request.settingsChanged = true;
  return request;
}

UiRequest Ui::buildGameInfo(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);
  const app::Session* session = shell.session();
  if (session == nullptr) {
    shell.back();
    return request;
  }
  const VariantSpec& v = session->variant();

  // Over the position, like pause: the board keeps the left of the frame.
  ImVec2 menuMin, menuMax;
  pauseFrame(request, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##gameinfo", menuMin, menuMax, move.scale, move.alpha, px(46.0f),
            !ghosting_);
  eyebrow("game mode", t, mono, scale_);
  screenTitle(v.name.c_str(), t, display, scale_, 2.0f);
  ImGui::Dummy(ImVec2(0, px(4)));

  const float footerH = controlHeight() + px(14.0f);
  ImGui::BeginChild("##infobody",
                    ImVec2(0, std::max(px(120.0f), ImGui::GetContentRegionAvail().y -
                                                       footerH - px(8.0f))),
                    0, ImGuiWindowFlags_NavFlattened);
  heading("BOARD", t, small);
  ImGui::PushFont(small);
  for (std::uint8_t a = 0; a < v.dims.dims(); ++a) {
    keyValue(v.dims.name(a).c_str(), std::to_string(v.dims.extent(a)) + " cells", t);
  }
  keyValue("total cells", std::to_string(v.dims.cellCount()), t);
  keyValue("geometry",
           v.geom.isBox() ? "a plain box"
                          : (v.geom.isOrientable() ? "glued" : "glued with a twist"),
           t, !v.geom.isBox());
  if (!v.geom.isBox()) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.rift));
    ImGui::TextWrapped(
        v.geom.isOrientable()
            ? "Edges of this board are joined to each other, so a piece leaving one side "
              "arrives at another. Cyan edges mark where."
            : "This board is joined to itself with a twist, so it has no consistent "
              "sense "
              "of up. A bishop here is not confined to one colour.");
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("RULES", t, small);
  ImGui::PushFont(small);
  keyValue("en passant", v.enPassant ? "yes" : "no", t);
  keyValue("castling", (v.castles.empty() && v.compounds.empty()) ? "no" : "yes", t);
  const auto& promo = v.promotion[static_cast<std::size_t>(Color::White)];
  keyValue("promotion", promo.active() ? "on the far rank" : "none on this board", t);
  keyValue("stalemate",
           v.stalemate == StalematePolicy::Draw
               ? "a draw"
               : (v.stalemate == StalematePolicy::Loss ? "a loss" : "a win"),
           t);
  keyValue("draw clock",
           v.halfmoveDrawLimit > 0 ? std::to_string(v.halfmoveDrawLimit) + " half-moves"
                                   : "off",
           t);
  ImGui::PopFont();

  // The exotic terms this board's own description uses, defined rather than assumed
  // (M14.4): a first-run player should not have to leave the game to learn what a torus
  // is. A term is shown when the description mentions it or when the board's name is its
  // first word - "torus" on the torus, "Mobius band" under "mobius". Case-blind, because
  // a name and a definition need not agree on capitals.
  {
    const auto fold = [](const std::string& s) {
      std::string out;
      out.reserve(s.size());
      for (const char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
      return out;
    };
    const std::string desc = fold(v.description);
    const std::string name = fold(v.name);
    std::vector<const app::GlossaryEntry*> used;
    for (const app::GlossaryEntry& e : app::glossary()) {
      const std::string term = fold(std::string(e.term));
      const std::string first = term.substr(0, term.find(' '));
      if (desc.find(term) != std::string::npos || name == first ||
          (!first.empty() && name.find(first) != std::string::npos)) {
        used.push_back(&e);
      }
    }
    if (!used.empty()) {
      ImGui::Dummy(ImVec2(0, px(8)));
      heading("TERMS", t, small);
      ImGui::PushFont(small);
      for (const app::GlossaryEntry* e : used) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(t.rift));
        ImGui::TextUnformatted(std::string(e->term).c_str());
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
        ImGui::TextWrapped("%s", std::string(e->definition).c_str());
        ImGui::PopStyleColor();
      }
      ImGui::PopFont();
    }
  }

  if (v.ruleSet) {
    ImGui::Dummy(ImVec2(0, px(8)));
    heading("SPECIAL RULES", t, small);
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    ImGui::TextWrapped("%s", session->game().ruleEngine().ruleSet().describe().c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::EndChild();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8.0f), px(4.0f)));
  ImGui::BeginChild("##infofooter", ImVec2(0, footerH), ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NavFlattened);
  if (button("BACK", t, px(120), true, false, true, actionFont())) shell.back();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  endPane();
  return request;
}

UiRequest Ui::buildPieceMoves(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);
  const app::Session* session = shell.session();
  if (session == nullptr) {
    shell.back();
    return request;
  }
  const VariantSpec& v = session->variant();

  // Over the position, like pause: the board keeps the left of the frame, and the move
  // diagram is drawn on top of it there.
  ImVec2 menuMin, menuMax;
  pauseFrame(request, menuMin, menuMax);
  if (v.pieces.size() <= 1) {
    pieceMovesPick_ = 0;
  } else if (pieceMovesPick_ < 1 ||
             pieceMovesPick_ >= static_cast<int>(v.pieces.size())) {
    pieceMovesPick_ = 1;
  }

  // The left half is the move diagram of whichever piece the menu on the right has
  // selected, on a soft card of its own so it reads over the blurred position behind.
  {
    ImGui::SetNextWindowPos(decoMin_);
    ImGui::SetNextWindowSize(ImVec2(decoMax_.x - decoMin_.x, decoMax_.y - decoMin_.y));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(24.0f), px(24.0f)));
    ImGui::Begin("##movediagram", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    const PieceTypeDef& piece = v.pieces[static_cast<std::size_t>(pieceMovesPick_)];
    ImGui::PushFont(display);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.bone));
    const std::string caption = upper(piece.name);
    const float nameW = ImGui::CalcTextSize(caption.c_str()).x;
    const float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         std::max(0.0f, (availW - nameW) * 0.5f));
    ImGui::TextUnformatted(caption.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(10)));

    // The diagram draws a fixed 9-cell grid; size it to the room that is left and centre
    // it rather than letting it sit in a corner.
    constexpr float kSpan = 9.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float cell = std::max(px(8.0f), std::min(avail.x, avail.y) / kSpan * 0.9f);
    const float side = kSpan * cell;
    ImGui::SetCursorPos(
        ImVec2(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - side) * 0.5f),
               ImGui::GetCursorPosY() + std::max(0.0f, (avail.y - side) * 0.5f)));
    // A soft card just behind the grid, so the blurred board does not show through the
    // squares and turn the diagram to noise.
    const ImVec2 gridAt = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(gridAt.x - px(10.0f), gridAt.y - px(10.0f)),
        ImVec2(gridAt.x + side + px(10.0f), gridAt.y + side + px(10.0f)),
        u32(t.soot, 0.82f), px(10.0f));
    drawMoveDiagram(v, piece, t, cell);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }

  // The right half is the menu: choose a piece, read its atoms.
  const PaneMove move = paneMove();
  beginPane("##pieces", menuMin, menuMax, move.scale, move.alpha, px(46.0f), !ghosting_);
  eyebrow("reference", t, mono, scale_);
  screenTitle("Piece moves", t, display, scale_, 1.9f);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("the diagram is on the left; pick a piece to read how it moves");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(6)));

  const float footerH = controlHeight() + px(14.0f);
  ImGui::BeginChild("##piecebody",
                    ImVec2(0, std::max(px(120.0f), ImGui::GetContentRegionAvail().y -
                                                       footerH - px(8.0f))),
                    0, ImGuiWindowFlags_NavFlattened);
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    const PieceTypeDef& piece = v.pieces[i];
    const bool selected = static_cast<int>(i) == pieceMovesPick_;
    ImGui::PushID(static_cast<int>(i));
    ImGui::PushStyleColor(ImGuiCol_Text, col(selected ? t.ember : t.bone));
    if (ImGui::Selectable(upper(piece.name).c_str(), selected)) {
      pieceMovesPick_ = static_cast<int>(i);
    }
    ImGui::PopStyleColor();
    ImGui::PopID();
  }

  // The selected piece's own atoms, under the list that chose it.
  const PieceTypeDef& sel = v.pieces[static_cast<std::size_t>(pieceMovesPick_)];
  ImGui::Dummy(ImVec2(0, px(10)));
  heading("MOVES", t, small);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::Text("symbol %c%s%s", sel.symbol, sel.royal ? "  -  royal" : "",
              sel.promotesTo.empty() ? "" : "  -  promotes");
  ImGui::Text("drawn as a %s", std::string(archetypeName(archetypeFor(sel))).c_str());
  ImGui::PopStyleColor();
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  for (const MoveAtom& atom : sel.atoms) {
    const std::uint32_t count = atom.dirCount(Color::White);
    ImGui::Text("%s  ->  %u direction%s", atom.toString().c_str(), count,
                count == 1 ? "" : "s");
  }
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::EndChild();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8.0f), px(4.0f)));
  ImGui::BeginChild("##piecesfooter", ImVec2(0, footerH), ImGuiChildFlags_Border,
                    ImGuiWindowFlags_NavFlattened);
  if (button("BACK", t, px(120), true, false, true, actionFont())) shell.back();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  endPane();
  return request;
}

}  // namespace cb::render
