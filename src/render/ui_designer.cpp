// SPDX-License-Identifier: GPL-3.0-or-later
//
// The two model designers: a piece's body, and the outline the flat board draws.
//
// They are one job on two sets of axes - a closed outline, edited point by point on a
// snapped grid - which is why M7.3 asks for one drawing surface and why `outlineEditor`
// below is shared. What differs is what the axes mean: the body's plane is
// (radius, height) and is swept about the height axis, while the icon's is the glyph box
// and is drawn as it stands.
//
// The model itself is `assets::PieceModel`, integer permille, with no floating point in
// it at all. Everything in this file is presentation: the grid, the handles, the preview.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "io/rule_catalogue.hpp"
#include "render/piece_mesh.hpp"
#include "render/ui.hpp"
#include "render/ui_widgets.hpp"

namespace cb::render {
namespace {

using namespace widgets;

constexpr float kBox = 1000.0f;

/// Where a model point lands in a rectangle. `yUp` is the profile's convention - height
/// grows upward, the way an author thinks about a piece standing on a board - while the
/// icon box is authored the way a glyph is, y downward.
ImVec2 toScreen(const assets::ModelPoint& p, ImVec2 min, ImVec2 max, bool yUp) {
  const float u = static_cast<float>(p.x) / kBox;
  const float v = static_cast<float>(p.y) / kBox;
  return ImVec2(min.x + u * (max.x - min.x),
                yUp ? max.y - v * (max.y - min.y) : min.y + v * (max.y - min.y));
}

assets::ModelPoint fromScreen(ImVec2 at, ImVec2 min, ImVec2 max, bool yUp) {
  const float u = (at.x - min.x) / std::max(1.0f, max.x - min.x);
  const float v = yUp ? (max.y - at.y) / std::max(1.0f, max.y - min.y)
                      : (at.y - min.y) / std::max(1.0f, max.y - min.y);
  // Snapped to a twentieth of the box. A grid an author can hit is worth more than
  // precision nobody can see, and it keeps the stored numbers short.
  const auto snap = [](float t) {
    return static_cast<std::int16_t>(std::clamp(std::lround(t * 20.0f) * 50L, 0L, 1000L));
  };
  return assets::ModelPoint{snap(u), snap(v)};
}

/// A text box over a std::string, growing as it is typed into.
bool inputString(const char* id, std::string& text, float width) {
  ImGui::SetNextItemWidth(width);
  return ImGui::InputText(
      id, text.data(), text.capacity() + 1, ImGuiInputTextFlags_CallbackResize,
      [](ImGuiInputTextCallbackData* data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
          auto* str = static_cast<std::string*>(data->UserData);
          str->resize(static_cast<std::size_t>(data->BufTextLen));
          data->Buf = str->data();
        }
        return 0;
      },
      &text);
}

/// Step a word through a fixed list - a trigger, an op, a piece name - so the form can
/// only ever hold a word the loader knows.
template <class List>
std::string nextWord(const List& list, const std::string& now) {
  if (list.empty()) return now;
  for (std::size_t i = 0; i < list.size(); ++i) {
    if (std::string_view(list[i]) == now) return std::string(list[(i + 1) % list.size()]);
  }
  return std::string(list[0]);
}

/// The grid an outline is drawn on, plus the axis the profile is swept about.
void drawGrid(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& t, bool axis) {
  dl->AddRectFilled(min, max, u32(t.panel, 0.55f));
  for (int i = 0; i <= 20; ++i) {
    const float s = static_cast<float>(i) / 20.0f;
    const bool major = i % 5 == 0;
    const ImU32 c = u32(t.rule, major ? 0.55f : 0.22f);
    const float x = min.x + s * (max.x - min.x);
    const float y = min.y + s * (max.y - min.y);
    dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), c, 1.0f);
    dl->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), c, 1.0f);
  }
  if (axis) {
    // The axis of revolution: everything drawn is a distance from this line, and the
    // solid is what you get by spinning the shape about it.
    dl->AddLine(ImVec2(min.x, min.y), ImVec2(min.x, max.y), u32(t.ember, 0.85f), 2.0f);
  }
  dl->AddRect(min, max, u32(t.rule), 0.0f, 0, 1.0f);
}

/// The model name an archetype seeds from. A switch rather than a table so that adding
/// an archetype is a compile error here rather than a piece that quietly gets a slab.
const char* modelNameFor(Archetype a) {
  switch (a) {
    case Archetype::Dome:
      return "dome";
    case Archetype::Tower:
      return "tower";
    case Archetype::Wedge:
      return "wedge";
    case Archetype::Spire:
      return "spire";
    case Archetype::Crown:
      return "crown";
    case Archetype::Monolith:
      return "monolith";
    case Archetype::Horn:
      return "horn";
    case Archetype::Cell:
    case Archetype::Portal:
    case Archetype::Arrow:
    case Archetype::Fillet:
    case Archetype::Disc:
    case Archetype::Count:
      break;
  }
  return "slab";
}

}  // namespace

// ---------------------------------------------------------------------------

void Ui::syncDesignAssets(app::Shell& shell) {
  const app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const std::vector<std::string> names = editor->pieceNames();
  if (names.empty()) return;
  const int idx = std::clamp(editorPiece_, 0, static_cast<int>(names.size()) - 1);
  const std::string& piece = names[static_cast<std::size_t>(idx)];

  auto docModel = editor->pieceModel(piece);
  auto docIcon = editor->pieceIcon(piece);
  const std::optional<assets::PieceModel> model =
      docModel.has_value() ? *docModel : std::nullopt;
  const std::optional<assets::IconModel> icon =
      docIcon.has_value() ? *docIcon : std::nullopt;
  if (designFor_ == piece && model == designModelSeen_ && icon == designIconSeen_) return;

  // A different piece, or the document changed underneath the working copy (undo, redo,
  // reset): reload from the document, and from the shipped archetype where it holds
  // nothing - so the designer always opens on something editable, and a piece is never
  // left without a body for want of an asset.
  if (designFor_ != piece) {
    designProfilePoint_ = -1;
    designElement_ = -1;
    designElementPoint_ = -1;
    designIconPoint_ = -1;
    designIconPoly_ = 0;
  }
  designFor_ = piece;
  designModelSeen_ = model;
  designIconSeen_ = icon;
  if (model) {
    designModel_ = *model;
  } else {
    // Which body a piece starts from is derived from how it *moves*, the same way the
    // game derives a shape for a piece that names none - so the designer opens on the
    // figure the player would already have seen.
    const char* seed = "dome";
    if (const VariantSpec* spec = shell.preview(shell.editorVariant()); spec != nullptr) {
      const PieceTypeId id = spec->findPiece(piece);
      if (id != kNoPiece) seed = modelNameFor(archetypeFor(spec->pieces[id]));
    }
    designModel_ = assets::archetypeModel(seed);
  }
  designIcon_ = icon ? *icon : assets::iconFromProfile(designModel_);
  designModelBase_ = designModel_;
  designIconBase_ = designIcon_;
  designIconPoly_ = std::min(designIconPoly_, designIcon_.fills.size() - 1);
}

void Ui::commitDesignAssets(app::Shell& shell) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr || designFor_.empty()) return;
  // One undo step per gesture, not per frame of a drag.
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
  if (!(designModel_ == designModelBase_) && assets::validate(designModel_).has_value()) {
    if (editor->setPieceModel(designFor_, designModel_).has_value()) {
      designModelBase_ = designModel_;
      designModelSeen_ = designModel_;
    }
  }
  if (!(designIcon_ == designIconBase_) && assets::validate(designIcon_).has_value()) {
    if (editor->setPieceIcon(designFor_, designIcon_).has_value()) {
      designIconBase_ = designIcon_;
      designIconSeen_ = designIcon_;
    }
  }
}

bool Ui::outlineEditor(const char* id, ImVec2 min, ImVec2 max, assets::Outline& outline,
                       int& selected, bool yUp, const char* xLabel, const char* yLabel) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const view::Theme& t = theme_;
  bool changed = false;

  drawGrid(dl, min, max, t, yUp);

  // The filled silhouette first, so the shape reads as a shape and not as a wire.
  if (outline.points.size() >= 3) {
    dl->PathClear();
    for (const assets::ModelPoint& p : outline.points) {
      dl->PathLineTo(toScreen(p, min, max, yUp));
    }
    const ImVector<ImVec2> path = dl->_Path;
    fillPolygon(dl, dl->_Path.Data, dl->_Path.Size, u32(t.boneDim, 0.35f));
    dl->_Path = path;
    dl->PathStroke(u32(t.bone, 0.9f), ImDrawFlags_Closed, px(2.0f));
  }

  // The handles are invisible buttons placed by screen position, which moves the layout
  // cursor. Put it back afterwards, or every control drawn after the grid lands inside
  // it - which is exactly what "all over the place" looks like.
  const ImVec2 cursorWas = ImGui::GetCursorScreenPos();
  ImGui::PushID(id);
  const float grab = px(7.0f);
  for (std::size_t i = 0; i < outline.points.size(); ++i) {
    const ImVec2 at = toScreen(outline.points[i], min, max, yUp);
    ImGui::SetCursorScreenPos(ImVec2(at.x - grab, at.y - grab));
    ImGui::PushID(static_cast<int>(i));
    ImGui::InvisibleButton("##pt", ImVec2(grab * 2.0f, grab * 2.0f));
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) selected = static_cast<int>(i);
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      const assets::ModelPoint want = fromScreen(ImGui::GetIO().MousePos, min, max, yUp);
      if (!(want == outline.points[i])) {
        outline.points[i] = want;
        changed = true;
      }
      selected = static_cast<int>(i);
    }
    ImGui::PopID();
    const bool isSel = selected == static_cast<int>(i);
    dl->AddCircleFilled(at, isSel ? px(5.0f) : px(3.5f),
                        u32(isSel ? t.ember : t.bone, hovered || active ? 1.0f : 0.8f),
                        12);
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(cursorWas);

  // The readout belongs on the drawing, not in a status line somewhere else: the number
  // an author is changing should be next to the thing they are dragging.
  if (selected >= 0 && selected < static_cast<int>(outline.points.size())) {
    const assets::ModelPoint& p = outline.points[static_cast<std::size_t>(selected)];
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s %d   %s %d", xLabel, static_cast<int>(p.x),
                  yLabel, static_cast<int>(p.y));
    dl->AddText(ImVec2(min.x + px(8), max.y - px(18)), u32(t.boneDim), buf);
  }
  return changed;
}

void Ui::drawRevolvePreview(ImDrawList* dl, ImVec2 min, ImVec2 max,
                            const assets::PieceModel& model, float spin) {
  const view::Theme& t = theme_;
  dl->AddRectFilled(min, max, u32(t.panel, 0.4f));
  dl->AddRect(min, max, u32(t.rule), 0.0f, 0, 1.0f);
  if (model.profile.points.size() < 3) return;

  const ImVec2 c((min.x + max.x) * 0.5f, max.y - px(18.0f));
  const float h = (max.y - min.y) - px(34.0f);
  const float unit = h / kBox;
  const int segs = std::max<int>(3, model.segments);
  // The same sweep the mesh would do, drawn as rings and ribs rather than triangles:
  // enough to read the solid, and it costs a draw list rather than a pipeline.
  const auto at = [&](float r, float hgt, float ang) {
    const float x = r * unit * std::cos(ang);
    const float z = r * unit * std::sin(ang);
    // A fixed three-quarter view, tilted, so the rings read as ellipses.
    return ImVec2(c.x + x - z * 0.35f, c.y - hgt * unit - z * 0.22f);
  };
  for (std::size_t i = 0; i < model.profile.points.size(); ++i) {
    const assets::ModelPoint& p = model.profile.points[i];
    if (p.x <= 0) continue;
    dl->PathClear();
    for (int s = 0; s <= segs; ++s) {
      const float a =
          spin + 6.2831853f * static_cast<float>(s) / static_cast<float>(segs);
      dl->PathLineTo(at(static_cast<float>(p.x), static_cast<float>(p.y), a));
    }
    dl->PathStroke(u32(t.boneFaint, 0.55f), ImDrawFlags_None, 1.0f);
  }
  for (int s = 0; s < segs; ++s) {
    const float a = spin + 6.2831853f * static_cast<float>(s) / static_cast<float>(segs);
    dl->PathClear();
    for (const assets::ModelPoint& p : model.profile.points) {
      dl->PathLineTo(at(static_cast<float>(p.x), static_cast<float>(p.y), a));
    }
    dl->PathStroke(u32(t.bone, s == 0 ? 0.9f : 0.28f), ImDrawFlags_Closed,
                   s == 0 ? px(1.8f) : 1.0f);
  }
  // Where the symmetry puts the elements, as marks around the piece: a count you can
  // see beats a number you have to believe.
  const int copies = assets::copiesOf(model.symmetry);
  for (const assets::Element& e : model.elements) {
    for (int i = 0; i < copies; ++i) {
      const float a =
          spin + 6.2831853f * static_cast<float>(i) / static_cast<float>(copies);
      // The element's own outline, in its plane at this copy's angle.
      dl->PathClear();
      for (const assets::ModelPoint& p : e.outline.points) {
        dl->PathLineTo(at(static_cast<float>(e.radius + p.x),
                          static_cast<float>(e.height + p.y), a));
      }
      dl->PathStroke(u32(t.ember, 0.85f), ImDrawFlags_Closed, px(1.4f));
    }
  }
}

// ===========================================================================
// The three tabs.
//
// Colour is spent only where it carries meaning. The old designer painted every control
// a different hue, which made the row look like a toolbar and left nothing for the one
// state that actually matters - whether a move can take. Here the words are the words,
// and the palette says: this one must capture, this one cannot, this one is gone.
// ===========================================================================

void Ui::drawMovesTab(app::Shell& shell, const std::string& piece) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const view::Theme& t = theme_;
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* display = static_cast<ImFont*>(fontDisplay_);

  auto atoms = editor->pieceAtoms(piece);
  if (!atoms.has_value()) {
    ImGui::TextUnformatted("this piece could not be read");
    return;
  }
  std::vector<MoveAtom> list = *atoms;
  bool changed = false;
  std::size_t removeAt = list.size();

  // A plain word that cycles. No fill, no hue - the state is the word, and a button that
  // looks like a button already says it can be pressed.
  const auto cycle = [&](const char* label, const view::Rgba& c, float w) {
    ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(t.panelHi));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(t.panelHi));
    ImGui::PushStyleColor(ImGuiCol_Text, col(c));
    const bool hit = ImGui::Button(label, ImVec2(w, 0.0f));
    ImGui::PopStyleColor(4);
    return hit;
  };

  // Columns at fixed offsets rather than an ImGui table: the point of a table here is
  // that like sits under like - every kind in one column, every capture rule in another -
  // and laying the columns out by hand is both exact and one less widget with a theme of
  // its own to fight. A row tells you what one move does; a column tells you what the
  // piece does.
  const float colNum = 0.0f;
  const float colSteps = px(26.0f);
  const float colAxes = colSteps + px(248.0f);
  const float colKind = colAxes + px(44.0f);
  const float colCapture = colKind + px(74.0f);
  const float colRepeat = colCapture + px(80.0f);
  const float colFwd = colRepeat + px(100.0f);
  const float colGone = colFwd + px(86.0f);

  ImGui::PushFont(small);
  {
    const auto head = [&](float x, const char* label) {
      ImGui::SameLine(x);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::TextUnformatted(label);
      ImGui::PopStyleColor();
    };
    ImGui::NewLine();
    head(colNum, "#");
    head(colSteps, "STEPS PER AXIS");
    head(colAxes, "AXES");
    head(colKind, "KIND");
    head(colCapture, "CAPTURE");
    head(colRepeat, "REPEATS");
    head(colFwd, "FORWARD");
    const ImVec2 lo = ImGui::GetItemRectMin();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x,
               lo.y + ImGui::GetTextLineHeight() + px(3.0f)),
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x,
               lo.y + ImGui::GetTextLineHeight() + px(3.0f)),
        u32(t.rule, 0.7f), 1.0f);
    ImGui::Dummy(ImVec2(0, px(6.0f)));
  }

  for (std::size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    MoveAtom& a = list[i];
    const float rowTop = ImGui::GetCursorPosY();
    // Every control in the row hangs off the same top, so a stacked stepper does not
    // drag the words next to it down with it.
    const auto at = [&](float x, float drop) {
      ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x + x, rowTop + drop));
    };

    at(colNum, px(14.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::Text("%zu", i + 1);
    ImGui::PopStyleColor();

    for (std::size_t k = 0; k < a.mags.size(); ++k) {
      at(colSteps + static_cast<float>(k) * px(31.0f), 0.0f);
      int v = a.mags[k];
      char id[24];
      std::snprintf(id, sizeof(id), "m%d", static_cast<int>(k));
      if (stepper(id, v, 1, 9, t, scale_, px(24.0f))) {
        a.mags[k] = static_cast<std::int16_t>(v);
        changed = true;
      }
    }
    {
      at(colAxes, 0.0f);
      int n = static_cast<int>(a.mags.size());
      if (stepper("axes", n, 1, static_cast<int>(kMaxDims), t, scale_, px(24.0f))) {
        while (static_cast<int>(a.mags.size()) > n) a.mags.pop();
        while (static_cast<int>(a.mags.size()) < n) a.mags.push(1);
        changed = true;
      }
    }
    {
      at(colKind, px(14.0f));
      const bool slide = a.mode == MoveMode::Slide;
      const bool hop = a.mode == MoveMode::Hop;
      if (cycle(slide ? "slide" : hop ? "hop" : "leap", t.bone, px(64))) {
        a.mode = slide ? MoveMode::Leap : hop ? MoveMode::Slide : MoveMode::Hop;
        changed = true;
      }
    }
    {
      at(colCapture, px(14.0f));
      const bool may = a.capture == CapturePolicy::May;
      const bool cannot = a.capture == CapturePolicy::Cannot;
      // The one place a hue earns its keep: "must" is a constraint on the player and
      // "cannot" is an absence, so neither should look like the ordinary case.
      const view::Rgba c = may ? t.bone : cannot ? t.boneFaint : t.blood;
      if (cycle(may ? "may" : cannot ? "cannot" : "must", c, px(70))) {
        a.capture = may      ? CapturePolicy::Must
                    : cannot ? CapturePolicy::May
                             : CapturePolicy::Cannot;
        changed = true;
      }
    }
    {
      at(colRepeat, px(14.0f));
      const bool runs = a.maxK == kUnlimited;
      if (cycle(runs ? "to the edge" : "once", runs ? t.bone : t.boneDim, px(90))) {
        a.maxK = runs ? 1 : kUnlimited;
        changed = true;
      }
    }
    {
      at(colFwd, px(14.0f));
      bool fwd = a.oriented;
      if (ImGui::Checkbox("##fwd", &fwd)) {
        a.oriented = fwd;
        changed = true;
      }
    }
    {
      at(colGone, px(14.0f));
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(t.blood, 0.25f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(t.blood, 0.4f));
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      if (ImGui::Button("remove")) removeAt = i;
      ImGui::PopStyleColor(4);
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x, rowTop + px(54.0f)));
    ImGui::PopID();
  }
  ImGui::PopFont();

  if (removeAt < list.size()) {
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(removeAt));
    changed = true;
  }

  sectionHead("ADD A MOVE", t, fontSmall_, scale_);
  const auto addAtom = [&](std::initializer_list<std::int16_t> mags, std::uint32_t maxK,
                           MoveMode m, CapturePolicy c, bool fwd) {
    MoveAtom a;
    for (const std::int16_t v : mags) a.mags.push(v);
    a.maxK = maxK;
    a.minK = 1;
    a.mode = m;
    a.capture = c;
    a.oriented = fwd;
    list.push_back(a);
    changed = true;
  };
  ImGui::PushFont(small);
  ImGui::PushID("add-a-move");
  const auto add = [&](const char* label, float w) {
    ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(t.panelHi));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(t.panelHi));
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
    const bool hit = ImGui::Button(label, ImVec2(w, 0.0f));
    ImGui::PopStyleColor(4);
    return hit;
  };
  // Bundles, not colours. Dropping one in unions its atoms, which is how rook + bishop
  // becomes a queen in two dimensions and something else entirely above them.
  if (add("rook", px(72)))
    addAtom({1}, kUnlimited, MoveMode::Slide, CapturePolicy::May, false);
  ImGui::SameLine();
  if (add("bishop", px(78))) {
    addAtom({1, 1}, kUnlimited, MoveMode::Slide, CapturePolicy::May, false);
  }
  ImGui::SameLine();
  if (add("knight", px(78))) {
    addAtom({1, 2}, 1, MoveMode::Leap, CapturePolicy::May, false);
  }
  ImGui::SameLine();
  if (add("queen", px(72))) {
    // Every straight line the board has: one axis, two, three... up to its dimension.
    for (const MoveAtom& a :
         straightLineAtoms(kUnlimited, std::max<std::size_t>(2, editor->axes().size()))) {
      list.push_back(a);
    }
    changed = true;
  }
  ImGui::SameLine();
  if (add("king", px(66))) {
    addAtom({1}, 1, MoveMode::Slide, CapturePolicy::May, false);
    addAtom({1, 1}, 1, MoveMode::Slide, CapturePolicy::May, false);
  }
  ImGui::SameLine();
  if (add("step", px(66))) addAtom({1}, 1, MoveMode::Slide, CapturePolicy::May, false);
  ImGui::SameLine();
  if (add("forward step", px(112))) {
    addAtom({1}, 1, MoveMode::Slide, CapturePolicy::Cannot, true);
  }
  ImGui::SameLine();
  if (add("forward capture", px(130))) {
    addAtom({1, 1}, 1, MoveMode::Slide, CapturePolicy::Must, true);
  }
  ImGui::PopID();
  ImGui::PopFont();

  if (changed) {
    const Result<void> ok = editor->setPieceAtoms(piece, list);
    moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
  }
  if (!moveNote_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", moveNote_.c_str());
    ImGui::PopStyleColor();
  }

  sectionHead("PREVIEW", t, fontSmall_, scale_);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "A piece is declared once and played on whatever board a variant declares. The "
      "same atoms reach further in more dimensions - a knight has eight destinations in "
      "two and forty-eight in four.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  for (int d = 2; d <= 4; ++d) {
    if (d != 2) ImGui::SameLine();
    char label[8];
    std::snprintf(label, sizeof(label), "%d-D", d);
    if (button(label, t, px(64), previewDims_ == d, true, true, display)) {
      previewDims_ = d;
    }
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("drag the board to turn it");
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void Ui::drawBodyTab(app::Shell& shell) {
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  syncDesignAssets(shell);

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "A side profile, swept about the axis. Drag a point to move it; the shape beside "
      "the panel is what the sweep makes.");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  sectionHead("STEP 1  THE PROFILE", t, fontSmall_, scale_);
  const float side = std::min(ImGui::GetContentRegionAvail().x, px(220.0f));
  const ImVec2 gridMin = ImGui::GetCursorScreenPos();
  const ImVec2 gridMax(gridMin.x + side, gridMin.y + side);
  ImGui::Dummy(ImVec2(side, side));
  (void)outlineEditor("profile", gridMin, gridMax, designModel_.profile,
                      designProfilePoint_, true, "radius", "height");
  // Where each element sits, on the profile it hangs off: an ember diamond, and the
  // selected one can be dragged - placing a part is pointing at where it goes.
  if (designModel_.symmetry.kind != assets::SymmetryKind::Full) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cursorWas = ImGui::GetCursorScreenPos();
    for (std::size_t i = 0; i < designModel_.elements.size(); ++i) {
      assets::Element& e = designModel_.elements[i];
      const ImVec2 at =
          toScreen(assets::ModelPoint{e.radius, e.height}, gridMin, gridMax, true);
      const bool sel = static_cast<int>(i) == designElement_;
      const float g = px(7.0f);
      ImGui::SetCursorScreenPos(ImVec2(at.x - g, at.y - g));
      ImGui::PushID(static_cast<int>(1000 + i));
      ImGui::InvisibleButton("##anchor", ImVec2(g * 2.0f, g * 2.0f));
      if (ImGui::IsItemClicked()) designElement_ = static_cast<int>(i);
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const assets::ModelPoint want =
            fromScreen(ImGui::GetIO().MousePos, gridMin, gridMax, true);
        e.radius = want.x;
        e.height = want.y;
        designElement_ = static_cast<int>(i);
      }
      ImGui::PopID();
      const float d = sel ? px(6.0f) : px(4.5f);
      dl->AddQuadFilled(ImVec2(at.x, at.y - d), ImVec2(at.x + d, at.y),
                        ImVec2(at.x, at.y + d), ImVec2(at.x - d, at.y),
                        u32(sel ? t.ember : t.emberDeep, 0.95f));
    }
    ImGui::SetCursorScreenPos(cursorWas);
  }
  // What the sweep makes, beside the profile it is made from, turning slowly so the
  // facets and the placed elements both read.
  drawRevolvePreview(ImGui::GetWindowDrawList(), ImVec2(gridMax.x + px(16.0f), gridMin.y),
                     ImVec2(gridMax.x + px(16.0f) + side, gridMax.y), designModel_,
                     clock_ * 0.6f);

  if (button("+ POINT", t, px(92), false, true, true, display)) {
    // Inserted after the selected point, halfway to the next, so an insert lands where
    // the author was looking rather than at the end of a list.
    auto& pts = designModel_.profile.points;
    if (!pts.empty()) {
      const std::size_t i =
          designProfilePoint_ >= 0 && designProfilePoint_ < static_cast<int>(pts.size())
              ? static_cast<std::size_t>(designProfilePoint_)
              : pts.size() - 1;
      const assets::ModelPoint& a = pts[i];
      const assets::ModelPoint& b = pts[(i + 1) % pts.size()];
      pts.insert(pts.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                 assets::ModelPoint{static_cast<std::int16_t>((a.x + b.x) / 2),
                                    static_cast<std::int16_t>((a.y + b.y) / 2)});
      designProfilePoint_ = static_cast<int>(i) + 1;
    }
  }
  ImGui::SameLine();
  if (button("- POINT", t, px(92), false, true, true, display)) {
    auto& pts = designModel_.profile.points;
    if (pts.size() > 3 && designProfilePoint_ >= 0 &&
        designProfilePoint_ < static_cast<int>(pts.size())) {
      pts.erase(pts.begin() + designProfilePoint_);
      designProfilePoint_ = -1;
    }
  }
  ImGui::SameLine();
  if (button("RESET", t, px(88), false, true, true, display)) {
    // Back to the archetype: the document forgets the body, and the next sync reseeds.
    if (app::Editor* editor = shell.editor(); editor != nullptr) {
      (void)editor->setPieceModel(designFor_, std::nullopt);
    }
  }

  sectionHead("STEP 2  REVOLVE AND SYMMETRY", t, fontSmall_, scale_);
  int segs = designModel_.segments;
  if (stepper("segments", segs, 3, 64, t, scale_, px(30.0f))) {
    designModel_.segments = static_cast<std::uint8_t>(segs);
  }
  ImGui::SameLine(0.0f, px(10.0f));
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("segments");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  const assets::SymmetryKind kind = designModel_.symmetry.kind;
  const char* kindName = kind == assets::SymmetryKind::Full     ? "full"
                         : kind == assets::SymmetryKind::Mirror ? "mirror"
                                                                : "k-fold";
  if (button(kindName, t, px(104), false, true, true, display)) {
    designModel_.symmetry.kind =
        kind == assets::SymmetryKind::Full     ? assets::SymmetryKind::Mirror
        : kind == assets::SymmetryKind::Mirror ? assets::SymmetryKind::KFold
                                               : assets::SymmetryKind::Full;
  }
  if (designModel_.symmetry.kind == assets::SymmetryKind::KFold) {
    ImGui::SameLine(0.0f, px(10.0f));
    int k = designModel_.symmetry.k;
    if (stepper("k", k, 2, 12, t, scale_, px(28.0f))) {
      designModel_.symmetry.k = static_cast<std::uint8_t>(k);
    }
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  if (designModel_.symmetry.kind == assets::SymmetryKind::Full) {
    ImGui::TextWrapped(
        "Radially symmetric. The profile is the whole piece - there is no orientation to "
        "place an element at, so the element tool stays off.");
  } else {
    char line[96];
    std::snprintf(line, sizeof(line), "places %d copies of each element",
                  assets::copiesOf(designModel_.symmetry));
    ImGui::TextUnformatted(line);
  }
  ImGui::PopStyleColor();
  ImGui::PopFont();

  // ---- step 3: the elements the symmetry group repeats -----------------------------
  sectionHead("STEP 3  ELEMENTS", t, fontSmall_, scale_);
  const bool canPlace = designModel_.symmetry.kind != assets::SymmetryKind::Full;
  auto& els = designModel_.elements;
  if (designElement_ >= static_cast<int>(els.size())) designElement_ = -1;
  if (designElement_ < 0 && !els.empty()) designElement_ = 0;
  if (button("+ ELEMENT", t, px(118), false, true, canPlace, display)) {
    // A small plate on the shoulder of the profile: somewhere visible, to be dragged.
    std::int16_t top = 0;
    for (const assets::ModelPoint& p : designModel_.profile.points)
      top = std::max(top, p.y);
    assets::Element e;
    e.outline.points = {{-60, 0}, {60, 0}, {0, 150}};
    e.radius = 200;
    e.height = static_cast<std::int16_t>(std::max(0, top - 150));
    els.push_back(e);
    designElement_ = static_cast<int>(els.size()) - 1;
    designElementPoint_ = -1;
  }
  ImGui::SameLine();
  if (button("- ELEMENT", t, px(118), false, true, canPlace && designElement_ >= 0,
             display) &&
      designElement_ >= 0) {
    els.erase(els.begin() + designElement_);
    designElement_ = els.empty() ? -1 : 0;
    designElementPoint_ = -1;
  }
  ImGui::SameLine();
  if (button("NEXT", t, px(84), false, true, els.size() > 1, display) && !els.empty()) {
    designElement_ = (designElement_ + 1) % static_cast<int>(els.size());
    designElementPoint_ = -1;
  }
  if (!canPlace) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextWrapped(
        "Choose mirror or k-fold first: a radially symmetric piece has no direction to "
        "place a part along.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
  } else if (designElement_ >= 0) {
    assets::Element& e = els[static_cast<std::size_t>(designElement_)];
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::Text("element %d of %d   at radius %d, height %d", designElement_ + 1,
                static_cast<int>(els.size()), static_cast<int>(e.radius),
                static_cast<int>(e.height));
    ImGui::TextWrapped(
        "Its outline, drawn in the plane through the axis: across is outward, up is up. "
        "Drag the ember diamond on the profile to move it.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    // The element's own outline may reach either side of its anchor, so it is edited
    // shifted half a box to the right and shifted back.
    const ImVec2 eMin = ImGui::GetCursorScreenPos();
    const ImVec2 eMax(eMin.x + side, eMin.y + side);
    ImGui::Dummy(ImVec2(side, side));
    assets::Outline shifted = e.outline;
    for (assets::ModelPoint& p : shifted.points)
      p.x = static_cast<std::int16_t>(p.x + 500);
    if (outlineEditor("element", eMin, eMax, shifted, designElementPoint_, true, "across",
                      "up")) {
      for (assets::ModelPoint& p : shifted.points)
        p.x = static_cast<std::int16_t>(p.x - 500);
      e.outline = shifted;
    }
    ImGui::GetWindowDrawList()->AddLine(ImVec2((eMin.x + eMax.x) * 0.5f, eMin.y),
                                        ImVec2((eMin.x + eMax.x) * 0.5f, eMax.y),
                                        u32(t.ember, 0.5f), 1.0f);
    if (button("+ POINT##el", t, px(92), false, true, true, display)) {
      auto& pts = e.outline.points;
      const std::size_t i =
          designElementPoint_ >= 0 && designElementPoint_ < static_cast<int>(pts.size())
              ? static_cast<std::size_t>(designElementPoint_)
              : pts.size() - 1;
      const assets::ModelPoint& a = pts[i];
      const assets::ModelPoint& b = pts[(i + 1) % pts.size()];
      pts.insert(pts.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                 assets::ModelPoint{static_cast<std::int16_t>((a.x + b.x) / 2),
                                    static_cast<std::int16_t>((a.y + b.y) / 2)});
      designElementPoint_ = static_cast<int>(i) + 1;
    }
    ImGui::SameLine();
    if (button("- POINT##el", t, px(92), false, true, true, display)) {
      auto& pts = e.outline.points;
      if (pts.size() > 3 && designElementPoint_ >= 0 &&
          designElementPoint_ < static_cast<int>(pts.size())) {
        pts.erase(pts.begin() + designElementPoint_);
        designElementPoint_ = -1;
      }
    }
  }

  const Result<void> ok = assets::validate(designModel_);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(ok.has_value() ? t.moss : t.blood));
  ImGui::TextWrapped("%s", ok.has_value() ? "the sweep closes; this shape can be built"
                                          : ok.error().message.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  commitDesignAssets(shell);
}

void Ui::drawIconTab(app::Shell& shell) {
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  syncDesignAssets(shell);
  if (designIcon_.fills.empty()) designIcon_ = assets::iconFromProfile(designModel_);
  designIconPoly_ = std::min(designIconPoly_, designIcon_.fills.size() - 1);

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "The outline the board draws when it is drawn flat. Straight segments only - that "
      "is the visual language, and it is exactly what fills without tessellation.");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  sectionHead("THE OUTLINE", t, fontSmall_, scale_);
  const float side = std::min(ImGui::GetContentRegionAvail().x, px(220.0f));
  const ImVec2 gridMin = ImGui::GetCursorScreenPos();
  const ImVec2 gridMax(gridMin.x + side, gridMin.y + side);
  ImGui::Dummy(ImVec2(side, side));
  assets::Outline& poly = designIcon_.fills[designIconPoly_];
  if (outlineEditor("icon", gridMin, gridMax, poly, designIconPoint_, false, "x", "y") &&
      designIconMirror_) {
    const std::size_t n = poly.points.size();
    for (std::size_t i = 0; i < n / 2; ++i) {
      const assets::ModelPoint& src = poly.points[i];
      poly.points[n - 1 - i] =
          assets::ModelPoint{static_cast<std::int16_t>(1000 - src.x), src.y};
    }
  }

  if (button(designIconMirror_ ? "MIRROR ON" : "MIRROR OFF", t, px(130),
             designIconMirror_, true, true, display)) {
    designIconMirror_ = !designIconMirror_;
  }
  ImGui::SameLine();
  if (button("+ POINT", t, px(92), false, true, true, display)) {
    auto& pts = poly.points;
    if (!pts.empty()) {
      const std::size_t i =
          designIconPoint_ >= 0 && designIconPoint_ < static_cast<int>(pts.size())
              ? static_cast<std::size_t>(designIconPoint_)
              : pts.size() - 1;
      const assets::ModelPoint& a = pts[i];
      const assets::ModelPoint& b = pts[(i + 1) % pts.size()];
      pts.insert(pts.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                 assets::ModelPoint{static_cast<std::int16_t>((a.x + b.x) / 2),
                                    static_cast<std::int16_t>((a.y + b.y) / 2)});
      designIconPoint_ = static_cast<int>(i) + 1;
    }
  }
  ImGui::SameLine();
  if (button("- POINT", t, px(92), false, true, true, display)) {
    auto& pts = poly.points;
    if (pts.size() > 3 && designIconPoint_ >= 0 &&
        designIconPoint_ < static_cast<int>(pts.size())) {
      pts.erase(pts.begin() + designIconPoint_);
      designIconPoint_ = -1;
    }
  }
  ImGui::SameLine();
  if (button("FROM BODY", t, px(122), false, true, true, display)) {
    designIcon_ = assets::iconFromProfile(designModel_);
    designIconPoint_ = -1;
    designIconPoly_ = 0;
  }

  sectionHead("AT THE SIZE IT IS DRAWN", t, fontSmall_, scale_);
  {
    // What actually matters about an icon is whether it survives being small, so the
    // preview is at the sizes the board really draws rather than a comfortable one.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float row = px(46.0f);
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, row));
    float x = at.x + px(24);
    for (const float r : {px(22.0f), px(14.0f), px(9.0f)}) {
      const ImVec2 c(x, at.y + row * 0.5f);
      dl->AddCircleFilled(c, r, u32(t.pieceToken), 20);
      dl->AddCircle(c, r, u32(t.boardRim), 20, 1.0f);
      for (const assets::Outline& o : designIcon_.fills) {
        if (o.points.size() < 3) continue;
        std::vector<ImVec2> pts;
        pts.reserve(o.points.size());
        for (const assets::ModelPoint& p : o.points) {
          pts.push_back(ImVec2(c.x + (static_cast<float>(p.x) / 1000.0f - 0.5f) * r * 2,
                               c.y + (static_cast<float>(p.y) / 1000.0f - 0.5f) * r * 2));
        }
        fillPolygon(dl, pts.data(), static_cast<int>(pts.size()), u32(t.whitePiece));
      }
      x += r * 2.0f + px(22);
    }
  }

  const Result<void> ok = assets::validate(designIcon_);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(ok.has_value() ? t.moss : t.blood));
  ImGui::TextWrapped("%s", ok.has_value() ? "the outline closes and stays in its box"
                                          : ok.error().message.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  commitDesignAssets(shell);
}

void Ui::drawRulesTab(app::Shell& shell) {
  const view::Theme& t = theme_;
  auto* small = static_cast<ImFont*>(fontSmall_);
  app::Editor* editor = shell.editor();
  if (editor == nullptr) return;

  // ---- sync: the document changed underneath (undo, redo) or this is the first look.
  {
    auto pf = editor->fields(false);
    auto cf = editor->fields(true);
    auto rs = editor->rules();
    if (!formLoaded_ || pf != formPieceFieldsSeen_ || cf != formCellFieldsSeen_ ||
        rs != formRulesSeen_) {
      formPieceFields_ = formPieceFieldsSeen_ = std::move(pf);
      formCellFields_ = formCellFieldsSeen_ = std::move(cf);
      formRules_ = formRulesSeen_ = std::move(rs);
      formLoaded_ = true;
    }
  }
  const auto note = [&](const char* text) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
  };
  const float wide = ImGui::GetContentRegionAvail().x;
  ImGui::PushFont(small);

  // ---- fields -------------------------------------------------------------------
  sectionHead("FIELDS", t, fontSmall_, scale_);
  note(
      "A field is a number every piece (or every cell) carries. Two ways to say \"has "
      "not "
      "moved yet\": the FEN way is a from-rank on the move itself (the pawn's double "
      "step) - nothing to store, and a position written down says it all. The state way "
      "is a piece field 'moved' a rule sets - it survives anywhere and is part of the "
      "position. 'charged' is the worked example: a 'charge' field, a rule that adds one "
      "on every move, and a rule that transforms at three, in that order.");
  for (int group = 0; group < 2; ++group) {
    auto& list = group == 0 ? formPieceFields_ : formCellFields_;
    ImGui::PushID(group);
    for (std::size_t i = 0; i < list.size(); ++i) {
      auto& f = list[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TextUnformatted(group == 0 ? "piece" : "cell ");
      ImGui::SameLine();
      inputString("##name", f.name, px(110));
      ImGui::SameLine();
      ImGui::SetNextItemWidth(px(70));
      ImGui::InputInt("##def", &f.defaultValue, 0);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(px(70));
      ImGui::InputInt("##min", &f.minValue, 0);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(px(70));
      ImGui::InputInt("##max", &f.maxValue, 0);
      ImGui::SameLine();
      ImGui::Checkbox("hashed", &f.hashed);
      ImGui::SameLine();
      const bool drop = ImGui::SmallButton("x");
      ImGui::PopID();
      if (drop) {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      }
    }
    ImGui::PopID();
  }
  note("name, default, min, max - and whether it is part of the position (hashed).");
  if (ImGui::SmallButton("+ PIECE FIELD"))
    formPieceFields_.push_back({"field", 0, 0, 8, true});
  ImGui::SameLine();
  if (ImGui::SmallButton("+ CELL FIELD"))
    formCellFields_.push_back({"mark", 0, 0, 8, true});

  // ---- rules --------------------------------------------------------------------
  sectionHead("RULES", t, fontSmall_, scale_);
  note(
      "Rules run in the order listed, and their effects in the order listed: order is "
      "meaning. A condition is an S-expression, e.g. (and is_capture (eq (type_at "
      "move.to) piece:pawn)); leave it empty for \"always\".");
  const std::vector<std::string> pieces = editor->pieceNames();
  std::vector<std::string> fieldNames;
  for (const auto& f : formPieceFields_) fieldNames.push_back(f.name);
  for (const auto& f : formCellFields_) fieldNames.push_back(f.name);
  constexpr std::array<std::string_view, 3> kOutcomes{"mover_wins", "mover_loses",
                                                      "draw"};
  std::vector<std::string_view> ops;
  for (const EffectSpec& e : effectCatalogue()) ops.push_back(e.op);

  for (std::size_t ri = 0; ri < formRules_.size(); ++ri) {
    auto& r = formRules_[ri];
    ImGui::PushID(static_cast<int>(100 + ri));
    ImGui::Separator();
    ImGui::Text("%d.", static_cast<int>(ri + 1));
    ImGui::SameLine();
    inputString("##rname", r.name, std::min(wide * 0.45f, px(220)));
    ImGui::SameLine();
    if (ImGui::SmallButton(r.when.c_str())) r.when = nextWord(ruleTriggers(), r.when);
    ImGui::SameLine();
    const bool up = ImGui::SmallButton("up");
    ImGui::SameLine();
    const bool down = ImGui::SmallButton("down");
    ImGui::SameLine();
    const bool drop = ImGui::SmallButton("x");
    ImGui::TextUnformatted("if");
    ImGui::SameLine();
    inputString("##cond", r.condition, wide - px(30));

    for (std::size_t ei = 0; ei < r.effects.size(); ++ei) {
      auto& e = r.effects[ei];
      ImGui::PushID(static_cast<int>(ei));
      ImGui::Indent(px(14));
      if (ImGui::SmallButton(e.op.c_str())) {
        e.op = nextWord(ops, e.op);
      }
      const EffectSpec* spec = findEffect(e.op);
      // The keys the op takes, in the catalogue's order; keys it does not take go.
      std::vector<std::pair<std::string, std::string>> args;
      if (spec != nullptr) {
        for (const EffectKey& k : spec->keys) {
          std::string value;
          for (const auto& [ak, av] : e.args) {
            if (ak == k.key) value = av;
          }
          args.emplace_back(std::string(k.key), std::move(value));
        }
      }
      for (std::size_t ki = 0; spec != nullptr && ki < args.size(); ++ki) {
        const EffectKey& k = spec->keys[ki];
        auto& value = args[ki].second;
        ImGui::SameLine();
        ImGui::Text("%s%s", std::string(k.key).c_str(), k.required ? "" : "?");
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(ki));
        if (k.kind == "piece") {
          if (ImGui::SmallButton(value.empty() ? "(piece)" : value.c_str())) {
            value = nextWord(pieces, value);
          }
        } else if (k.kind == "field") {
          if (ImGui::SmallButton(value.empty() ? "(field)" : value.c_str())) {
            value = nextWord(fieldNames, value);
          }
        } else if (k.kind == "outcome") {
          if (ImGui::SmallButton(value.empty() ? "draw" : value.c_str())) {
            value = nextWord(kOutcomes, value.empty() ? std::string("draw") : value);
          }
        } else {
          inputString("##v", value, k.kind == "number" ? px(50) : px(170));
        }
        ImGui::PopID();
      }
      e.args = std::move(args);
      ImGui::SameLine();
      const bool eup = ImGui::SmallButton("up");
      ImGui::SameLine();
      const bool edrop = ImGui::SmallButton("x");
      ImGui::Unindent(px(14));
      ImGui::PopID();
      if (eup && ei > 0) std::swap(r.effects[ei], r.effects[ei - 1]);
      if (edrop) {
        r.effects.erase(r.effects.begin() + static_cast<std::ptrdiff_t>(ei));
        break;
      }
    }
    ImGui::Indent(px(14));
    if (ImGui::SmallButton("+ EFFECT"))
      r.effects.push_back({"destroy", {{"at", "move.to"}}});
    ImGui::Unindent(px(14));
    ImGui::PopID();
    if (up && ri > 0) std::swap(formRules_[ri], formRules_[ri - 1]);
    if (down && ri + 1 < formRules_.size()) std::swap(formRules_[ri], formRules_[ri + 1]);
    if (drop) {
      formRules_.erase(formRules_.begin() + static_cast<std::ptrdiff_t>(ri));
      break;
    }
  }
  ImGui::Separator();
  if (ImGui::SmallButton("+ RULE")) {
    formRules_.push_back({"new rule", "on_move_end", "", {{"repeat_turn", {}}}});
  }
  note(
      "Effects: destroy, destroy_region, transform, spawn, set_piece_field, "
      "set_cell_field, forbid_move (on_move_filter only), repeat_turn, end_game. Cells "
      "are expressions: move.from, move.to, move.capture_cell.");

  // What the loader thinks, in its own words: the form holds text, and only the one
  // parser decides whether it means anything.
  const Result<void> loads = editor->validate();
  ImGui::PushStyleColor(ImGuiCol_Text, col(loads.has_value() ? t.moss : t.blood));
  ImGui::TextWrapped("%s", loads.has_value() ? "the variant loads with these rules"
                                             : loads.error().format().c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();

  // ---- commit: once nothing is being typed into, so one edit is one undo step.
  if (!ImGui::IsAnyItemActive()) {
    if (formPieceFields_ != formPieceFieldsSeen_ &&
        editor->setFields(false, formPieceFields_).has_value()) {
      formPieceFieldsSeen_ = formPieceFields_;
    }
    if (formCellFields_ != formCellFieldsSeen_ &&
        editor->setFields(true, formCellFields_).has_value()) {
      formCellFieldsSeen_ = formCellFields_;
    }
    if (formRules_ != formRulesSeen_ && editor->setRules(formRules_).has_value()) {
      formRulesSeen_ = formRules_;
    }
  }
}

}  // namespace cb::render
