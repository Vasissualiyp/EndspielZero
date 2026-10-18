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
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

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
ImVec2 toScreen(const assets::ModelPoint& p, ImVec2 min, ImVec2 max, bool yUp,
                const DesignBox& box) {
  const float u = (static_cast<float>(p.x) - box.xMin) / (box.xMax - box.xMin);
  const float v = (static_cast<float>(p.y) - box.yMin) / (box.yMax - box.yMin);
  return ImVec2(min.x + u * (max.x - min.x),
                yUp ? max.y - v * (max.y - min.y) : min.y + v * (max.y - min.y));
}

assets::ModelPoint fromScreen(ImVec2 at, ImVec2 min, ImVec2 max, bool yUp,
                              const DesignBox& box) {
  const float u = (at.x - min.x) / std::max(1.0f, max.x - min.x);
  const float v = yUp ? (max.y - at.y) / std::max(1.0f, max.y - min.y)
                      : (at.y - min.y) / std::max(1.0f, max.y - min.y);
  // Snapped to a twentieth of the window. A grid an author can hit is worth more than
  // precision nobody can see, and it keeps the stored numbers short.
  const auto snap = [](float t, float lo, float hi) {
    const float step = (hi - lo) / 20.0f;
    const float x = lo + static_cast<float>(std::lround(std::clamp(t, 0.0f, 1.0f) * 20.0f)) * step;
    return static_cast<std::int16_t>(std::lround(std::clamp(x, lo, hi)));
  };
  return assets::ModelPoint{snap(u, box.xMin, box.xMax), snap(v, box.yMin, box.yMax)};
}

/// The grid an outline is drawn on, plus the axis the profile is swept about.
void drawGrid(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& t, bool axis,
              const DesignBox& box) {
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
    // The axis of revolution (x = 0): everything drawn is a distance from this line, and
    // the solid is what you get by spinning the shape about it.
    const float u = (0.0f - box.xMin) / (box.xMax - box.xMin);
    const float x = min.x + u * (max.x - min.x);
    dl->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), u32(t.ember, 0.85f), 2.0f);
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

/// The model name a chess piece's body starts from, for the "start from" buttons.
struct Generator {
  const char* piece;
  const char* model;
  Archetype archetype;
};
constexpr Generator kGenerators[] = {
    {"pawn", "dome", Archetype::Dome},         {"knight", "wedge", Archetype::Wedge},
    {"bishop", "spire", Archetype::Spire},     {"rook", "tower", Archetype::Tower},
    {"queen", "crown", Archetype::Crown},      {"king", "monolith", Archetype::Monolith},
    {"unicorn", "horn", Archetype::Horn},
};

/// A shipped flat icon, as the editable outlines the icon designer works on.
assets::IconModel iconModelOf(Archetype shape, IconStyle style) {
  const PieceIcon art = pieceIcon(style, shape);
  const auto convert = [](IconPoly poly) {
    assets::Outline o;
    for (const IconPoint& p : poly) {
      o.points.push_back(assets::ModelPoint{
          static_cast<std::int16_t>(std::clamp(std::lround(p.x * 10.0f), 0L, 1000L)),
          static_cast<std::int16_t>(std::clamp(std::lround(p.y * 10.0f), 0L, 1000L))});
    }
    return o;
  };
  assets::IconModel m;
  for (const IconPoly& poly : art.fills) m.fills.push_back(convert(poly));
  for (const IconPoly& poly : art.cuts) m.cuts.push_back(convert(poly));
  if (m.fills.empty()) m.fills.push_back(assets::Outline{});
  return m;
}

/// The shipped shape a piece's moves would have drawn it as.
Archetype seedArchetype(const VariantSpec* spec, const std::string& piece) {
  if (spec != nullptr) {
    const PieceTypeId id = spec->findPiece(piece);
    if (id != kNoPiece) return archetypeFor(spec->pieces[id]);
  }
  return Archetype::Dome;
}

/// The seed body for a piece: what its moves would have drawn it as.
assets::PieceModel seedBody(const VariantSpec* spec, const std::string& piece) {
  return assets::archetypeModel(modelNameFor(seedArchetype(spec, piece)));
}

/// A text box that keeps what is being typed while it is being typed and writes it back
/// only when the author is done (focus leaves, or Enter): the document is edited once per
/// edit, not once per keystroke, and a half-typed expression is never judged. Returns true
/// on the frame the edit is committed.
bool editText(const char* id, std::string& value, float width) {
  static std::map<ImGuiID, std::string> typing;
  static std::map<ImGuiID, bool> active;  // was this box being edited last frame
  const ImGuiID gid = ImGui::GetID(id);
  std::string& buf = typing[gid];
  if (!active[gid]) buf = value;
  char raw[1024];
  std::snprintf(raw, sizeof(raw), "%s", buf.c_str());
  if (width > 0.0f) ImGui::SetNextItemWidth(width);
  ImGui::InputText(id, raw, sizeof(raw));
  buf = raw;
  active[gid] = ImGui::IsItemActive();
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    value = buf;
    return true;
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------

void Ui::syncDesignAssets(app::Shell& shell) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const std::vector<std::string> names = editor->pieceNames();
  if (names.empty()) return;
  const int idx = std::clamp(editorPiece_, 0, static_cast<int>(names.size()) - 1);
  const std::string& piece = names[static_cast<std::size_t>(idx)];
  if (designFor_ == piece && designSyncedRevision_ == editor->revision()) return;

  // The document is the store. A piece with an authored body opens on it; one without
  // opens on the shipped archetype its moves would have been drawn as, so the designer
  // always has something editable - a piece is never left without a body for want of an
  // asset - and nothing is written until the author changes something.
  const bool samePiece = designFor_ == piece;
  designFor_ = piece;
  designSyncedRevision_ = editor->revision();
  const auto body = editor->pieceBody(piece);
  designModel_ = body.has_value() && body->has_value()
                     ? **body
                     : seedBody(shell.preview(shell.editorVariant()), piece);
  const auto icon = editor->pieceIcon(piece);
  // An icon starts as the shipped one for the shape the piece's moves suggest, so the
  // designer opens on what the flat board is already drawing.
  designIcon_ = icon.has_value() && icon->has_value()
                    ? **icon
                    : iconModelOf(seedArchetype(shell.preview(shell.editorVariant()), piece),
                                  IconStyle::Faceted);
  loadedModel_ = designModel_;
  loadedIcon_ = designIcon_;
  if (!samePiece) {
    designProfilePoint_ = -1;
    designIconPoint_ = -1;
    designIconPoly_ = 0;
    designElement_ = designModel_.elements.empty() ? -1 : 0;
    designElementPoint_ = -1;
    selectedAtom_ = -1;
    moveNote_.clear();
  }
  designElement_ = std::min(designElement_, static_cast<int>(designModel_.elements.size()) - 1);
}

void Ui::commitDesign(app::Shell& shell, const std::string& piece) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr || designFor_ != piece) return;
  // A drag is one edit, not sixty: wait for the button to come up.
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive()) return;
  if (!(designModel_ == loadedModel_) && assets::validate(designModel_).has_value()) {
    if (editor->setPieceBody(piece, designModel_).has_value()) {
      loadedModel_ = designModel_;
      designSyncedRevision_ = editor->revision();
    }
  }
  if (!(designIcon_ == loadedIcon_) && assets::validate(designIcon_).has_value()) {
    if (editor->setPieceIcon(piece, designIcon_).has_value()) {
      loadedIcon_ = designIcon_;
      designSyncedRevision_ = editor->revision();
    }
  }
}

bool Ui::outlineEditor(const char* id, ImVec2 min, ImVec2 max, assets::Outline& outline,
                       int& selected, bool yUp, const char* xLabel, const char* yLabel,
                       const DesignBox& box) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const view::Theme& t = theme_;
  bool changed = false;

  drawGrid(dl, min, max, t, yUp, box);

  // The filled silhouette first, so the shape reads as a shape and not as a wire.
  if (outline.points.size() >= 3) {
    dl->PathClear();
    for (const assets::ModelPoint& p : outline.points) {
      dl->PathLineTo(toScreen(p, min, max, yUp, box));
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
    const ImVec2 at = toScreen(outline.points[i], min, max, yUp, box);
    ImGui::SetCursorScreenPos(ImVec2(at.x - grab, at.y - grab));
    ImGui::PushID(static_cast<int>(i));
    ImGui::InvisibleButton("##pt", ImVec2(grab * 2.0f, grab * 2.0f));
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked()) selected = static_cast<int>(i);
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      const assets::ModelPoint want = fromScreen(ImGui::GetIO().MousePos, min, max, yUp, box);
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
  // The mesh the game would draw, rebuilt only when the model changed, and only while it
  // is a shape the assembly can build - an invalid one keeps showing the last good body.
  if ((!previewMeshValid_ || !(previewMeshModel_ == model)) &&
      assets::validate(model).has_value()) {
    previewMesh_ = assembleModel(model);
    previewMeshModel_ = model;
    previewMeshValid_ = true;
  }
  if (!previewMeshValid_ || previewMesh_.indices.empty()) return;

  const float pitch = 0.42f;
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  const float cs = std::cos(spin), ss = std::sin(spin);
  const float h = std::max(0.3f, previewMesh_.height);
  const float unit = ((max.y - min.y) - px(30.0f)) / (h * 1.1f);
  const ImVec2 base((min.x + max.x) * 0.5f, max.y - px(16.0f));

  struct P {
    ImVec2 at;
    float depth;
  };
  std::vector<P> pts(previewMesh_.vertices.size());
  struct N {
    float x, up, depth;
  };
  std::vector<N> nrm(previewMesh_.vertices.size());
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const MeshVertex& v = previewMesh_.vertices[i];
    const float x = v.pos[0] * cs - v.pos[1] * ss;
    const float y = v.pos[0] * ss + v.pos[1] * cs;
    pts[i] = P{ImVec2(base.x + x * unit, base.y - (v.pos[2] * cp + y * sp) * unit),
               y * cp - v.pos[2] * sp};
    const float nx = v.normal[0] * cs - v.normal[1] * ss;
    const float ny = v.normal[0] * ss + v.normal[1] * cs;
    nrm[i] = N{nx, v.normal[2] * cp + ny * sp, ny * cp - v.normal[2] * sp};
  }
  struct Tri {
    std::uint32_t a, b, c;
    float depth;
  };
  std::vector<Tri> tris;
  tris.reserve(previewMesh_.indices.size() / 3);
  for (std::size_t i = 0; i + 2 < previewMesh_.indices.size(); i += 3) {
    const std::uint32_t a = previewMesh_.indices[i];
    const std::uint32_t b = previewMesh_.indices[i + 1];
    const std::uint32_t c = previewMesh_.indices[i + 2];
    // Facing away from the camera: the near side hides it, so it is not drawn.
    const float facing = nrm[a].depth + nrm[b].depth + nrm[c].depth;
    if (facing > 0.0f) continue;
    tris.push_back(Tri{a, b, c, pts[a].depth + pts[b].depth + pts[c].depth});
  }
  std::sort(tris.begin(), tris.end(),
            [](const Tri& l, const Tri& r) { return l.depth > r.depth; });
  const float lx = -0.45f, ly = 0.70f, lz = -0.55f;  // from the upper left, in front
  const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
  for (const Tri& tr : tris) {
    const float nx = nrm[tr.a].x + nrm[tr.b].x + nrm[tr.c].x;
    const float ny = nrm[tr.a].up + nrm[tr.b].up + nrm[tr.c].up;
    const float nz = nrm[tr.a].depth + nrm[tr.b].depth + nrm[tr.c].depth;
    const float len = std::max(1e-6f, std::sqrt(nx * nx + ny * ny + nz * nz));
    const float lam = std::max(0.0f, (nx * lx + ny * ly + nz * lz) / (len * ll));
    // The two sides' own colours, so the body reads against the panel in either theme.
    const view::Rgba lit = mix(t.blackPiece, t.whitePiece, 0.62f);
    const view::Rgba c = mix(t.blackPiece, lit, 0.15f + 0.85f * lam);
    dl->AddTriangleFilled(pts[tr.a].at, pts[tr.b].at, pts[tr.c].at, u32(c));
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

  // ---- the same moves as text: the symbolic notation, which can be one line where the
  // table is a dozen rows. `U r in 1..D : [1 x r]^k` is the queen on any board.
  sectionHead("AS TEXT", t, fontSmall_, scale_);
  {
    ImGui::PushFont(small);
    auto have = editor->pieceMovesText(piece);
    std::string text = have.has_value() ? *have : std::string{};
    ImGui::PushID("moves-text");
    if (editText("##moves", text, px(560.0f))) {
      const Result<void> ok = editor->setPieceMovesText(piece, text);
      moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
    }
    ImGui::PopID();
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextWrapped(
        "[1,2]^1 is a knight, [1]^k a rook, exact [rank:+1] one step forward, "
        "expanded [rank:+1]^5 up to five. U joins, \\ removes, names such as queen are in the "
        "library. Editing the rows below turns this back into the moves it denotes.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

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
  const float colReach = colFwd + px(66.0f);
  const float colGone = colReach + px(92.0f);

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
    head(colReach, "REACH");
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
    {
      // The number opens the move's detail below the table: steps, home rank, en passant.
      const bool open = selectedAtom_ == static_cast<int>(i);
      ImGui::PushStyleColor(ImGuiCol_Button, open ? col(t.panelHi) : ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(t.panelHi));
      ImGui::PushStyleColor(ImGuiCol_Text, col(open ? t.ember : t.boneFaint));
      char num[32];
      std::snprintf(num, sizeof(num), "%zu", i + 1);
      if (ImGui::Button(num)) selectedAtom_ = open ? -1 : static_cast<int>(i);
      ImGui::PopStyleColor(3);
    }

    // A literal or pinned move is a vector the steppers cannot show as magnitudes; it is
    // read out as text until the table learns its columns (M19), and everything else about
    // the move - kind, capture, repeats - still edits.
    const bool literal = a.kind != MoveKind::Extrapolated || !a.pins.empty();
    if (literal) {
      at(colSteps, px(14.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.bone));
      ImGui::TextUnformatted(a.toString().substr(0, a.toString().find(' ', a.toString().find('}') + 1)).c_str());
      ImGui::PopStyleColor();
    }
    for (std::size_t k = 0; k < (literal ? 0 : a.mags.size()); ++k) {
      at(colSteps + static_cast<float>(k) * px(31.0f), 0.0f);
      int v = a.mags[k];
      char id[24];
      std::snprintf(id, sizeof(id), "m%d", static_cast<int>(k));
      if (stepper(id, v, 1, 9, t, scale_, px(24.0f))) {
        a.mags[k] = static_cast<std::int16_t>(v);
        changed = true;
      }
    }
    if (!literal) {
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
      char repeats[24];
      if (runs) {
        std::snprintf(repeats, sizeof(repeats), "to the edge");
      } else if (a.maxK == 1) {
        std::snprintf(repeats, sizeof(repeats), "once");
      } else if (a.minK == a.maxK) {
        std::snprintf(repeats, sizeof(repeats), "exactly %u", a.minK);
      } else {
        std::snprintf(repeats, sizeof(repeats), "up to %u", a.maxK);
      }
      if (cycle(repeats, runs ? t.bone : t.boneDim, px(90))) {
        // once -> to the edge -> once; finer counts live in the move's detail.
        a.maxK = runs ? 1 : kUnlimited;
        a.minK = 1;
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
      // What the move amounts to on this board. An atom of more axes than the board has
      // expands to nothing - the engine's deliberate behaviour, so it is said, not hidden.
      at(colReach, px(14.0f));
      const std::size_t boardAxes = editor->axes().size();
      const int orient = editor->orientationAxis();
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      const std::size_t needed = a.mags.size() + a.pins.size();
      if (needed > boardAxes) {
        ImGui::Text("needs %zu axes", needed);
      } else {
        const std::size_t dirs =
            expandAtomFor(a, static_cast<std::uint8_t>(boardAxes), orient, Color::White).size();
        ImGui::Text("%zu dirs", dirs);
      }
      ImGui::PopStyleColor();
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

  // ---- the selected move's detail: the fields the table has no room for.
  if (selectedAtom_ >= 0 && selectedAtom_ < static_cast<int>(list.size())) {
    MoveAtom& a = list[static_cast<std::size_t>(selectedAtom_)];
    char title[40];
    std::snprintf(title, sizeof(title), "MOVE %d IN DETAIL", selectedAtom_ + 1);
    sectionHead(title, t, fontSmall_, scale_);
    ImGui::PushFont(small);
    ImGui::PushID("atom-detail");
    // How many steps it takes: from `min` to `max`, or on to the edge.
    int minK = static_cast<int>(a.minK);
    const bool runs = a.maxK == kUnlimited;
    int maxK = runs ? std::max(1, minK) : static_cast<int>(a.maxK);
    ImGui::TextUnformatted("steps");
    ImGui::SameLine(px(70.0f));
    if (stepper("min", minK, 1, 9, t, scale_, px(26.0f))) {
      a.minK = static_cast<std::uint32_t>(minK);
      if (!runs && a.maxK < a.minK) a.maxK = a.minK;
      changed = true;
    }
    ImGui::SameLine(0.0f, px(8.0f));
    ImGui::TextUnformatted("to");
    ImGui::SameLine(0.0f, px(8.0f));
    if (!runs) {
      if (stepper("max", maxK, 1, 9, t, scale_, px(26.0f))) {
        a.maxK = static_cast<std::uint32_t>(maxK);
        if (a.minK > a.maxK) a.minK = a.maxK;
        changed = true;
      }
      ImGui::SameLine(0.0f, px(8.0f));
    }
    bool edge = runs;
    if (ImGui::Checkbox("to the edge", &edge)) {
      a.maxK = edge ? kUnlimited : std::max<std::uint32_t>(1, a.minK);
      changed = true;
    }
    // A double step is `min = max = 2` from the home rank, and it leaves a square behind
    // it that can be taken en passant: all three are properties of the one move.
    if (ImGui::Checkbox("only while the piece has never moved", &a.unmoved)) changed = true;
    if (ImGui::Checkbox("leaves an en passant square behind", &a.leavesEnPassant)) {
      changed = true;
    }

    // ---- the kind of move, and everything the table has no column for (M19).
    {
      const std::size_t axesN = editor->axes().size();
      std::vector<std::string> axisNames;
      for (const auto& ax : editor->axes()) axisNames.push_back(ax.name);
      const int orientAxis = editor->orientationAxis();
      std::vector<std::string> tagNames{"ep"};
      for (const auto& tag : editor->markTags()) tagNames.push_back(tag.name);

      // Type: extrapolated (free magnitudes, every axis and sign), exact (one literal vector
      // once), expanded (a literal vector times k, both senses or one).
      const char* kindWord = a.kind == MoveKind::Exact      ? "exact"
                             : a.kind == MoveKind::Expanded ? "expanded"
                                                            : "extrapolated";
      ImGui::TextUnformatted("type");
      ImGui::SameLine(px(70.0f));
      if (ImGui::Button(kindWord)) {
        const MoveKind next = a.kind == MoveKind::Extrapolated ? MoveKind::Exact
                              : a.kind == MoveKind::Exact      ? MoveKind::Expanded
                                                               : MoveKind::Extrapolated;
        if (next == MoveKind::Extrapolated) {
          // Back to free magnitudes: the literal vector's sizes, axes forgotten.
          a.mags.clear();
          for (const Pin& p : a.pins) a.mags.push(static_cast<std::int16_t>(std::abs(p.value)));
          a.pins.clear();
          a.flip = false;
        } else if (a.kind == MoveKind::Extrapolated) {
          // To a literal vector: the first magnitudes on the first axes, forward-positive.
          a.pins.clear();
          for (std::size_t m = 0; m < a.mags.size() && m < axesN; ++m) {
            a.pins.push(Pin{static_cast<std::uint8_t>(m), a.mags[m]});
          }
          a.mags.clear();
          a.oriented = false;
          a.minK = 1;
          a.maxK = next == MoveKind::Exact ? 1 : kUnlimited;
        }
        if (next == MoveKind::Exact) a.maxK = 1, a.minK = 1;
        a.kind = next;
        changed = true;
      }
      if (a.kind == MoveKind::Expanded) {
        ImGui::SameLine();
        const char* signWord = a.sign == StepSign::Both       ? "both ways"
                               : a.sign == StepSign::Positive ? "positive only"
                                                              : "negative only";
        if (ImGui::Button(signWord)) {
          a.sign = a.sign == StepSign::Both       ? StepSign::Positive
                   : a.sign == StepSign::Positive ? StepSign::Negative
                                                  : StepSign::Both;
          changed = true;
        }
      }

      // The literal vector, one signed number per axis (exact and expanded, and the pinned
      // components of an extrapolated move).
      if (a.kind != MoveKind::Extrapolated || !a.pins.empty() || true) {
        ImGui::TextUnformatted(a.kind == MoveKind::Extrapolated ? "pinned" : "vector");
        for (std::size_t ax = 0; ax < axesN; ++ax) {
          ImGui::SameLine(ax == 0 ? px(70.0f) : 0.0f);
          int value = 0;
          for (const Pin& p : a.pins) {
            if (p.axis == ax) value = p.value;
          }
          char id[24];
          std::snprintf(id, sizeof(id), "pin%zu", ax);
          if (stepper(id, value, -9, 9, t, scale_, px(30.0f))) {
            SmallVec<Pin, kMaxDims> kept;
            for (const Pin& p : a.pins) {
              if (p.axis != ax) kept.push(p);
            }
            if (value != 0) kept.push(Pin{static_cast<std::uint8_t>(ax), static_cast<std::int16_t>(value)});
            a.pins = kept;
            changed = true;
          }
          ImGui::SameLine(0.0f, px(2.0f));
          ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
          ImGui::TextUnformatted(ax < axisNames.size() ? axisNames[ax].c_str() : "?");
          ImGui::PopStyleColor();
        }
      }
      if (orientAxis >= 0) {
        if (ImGui::Checkbox("flip along the orientation axis for the other side", &a.flip)) {
          changed = true;
        }
      }

      // Capturing somewhere else: en passant, a checkers jump.
      ImGui::TextUnformatted("captures at");
      for (std::size_t ax = 0; ax < axesN; ++ax) {
        ImGui::SameLine(ax == 0 ? px(70.0f) : 0.0f);
        int value = 0;
        for (const Pin& p : a.captureAt) {
          if (p.axis == ax) value = p.value;
        }
        char id[24];
        std::snprintf(id, sizeof(id), "cap%zu", ax);
        if (stepper(id, value, -9, 9, t, scale_, px(30.0f))) {
          SmallVec<Pin, kMaxDims> kept;
          for (const Pin& p : a.captureAt) {
            if (p.axis != ax) kept.push(p);
          }
          if (value != 0) kept.push(Pin{static_cast<std::uint8_t>(ax), static_cast<std::int16_t>(value)});
          a.captureAt = kept;
          // Taking a piece beside the landing cell is a capture-only move onto an empty one.
          if (!a.captureAt.empty()) a.capture = CapturePolicy::Must;
          changed = true;
        }
        ImGui::SameLine(0.0f, px(2.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
        ImGui::TextUnformatted(ax < axisNames.size() ? axisNames[ax].c_str() : "?");
        ImGui::PopStyleColor();
      }
      if (!a.captureAt.empty()) {
        ImGui::TextUnformatted("only onto");
        ImGui::SameLine(px(70.0f));
        const std::string needs = a.needsMark < 0 ? std::string("any empty cell")
                                                  : "a cell marked " + tagNames[static_cast<std::size_t>(a.needsMark)];
        if (ImGui::Button(needs.c_str())) {
          const int next = a.needsMark + 1;
          a.needsMark = static_cast<std::int16_t>(next >= static_cast<int>(tagNames.size()) ? -1 : next);
          changed = true;
        }
      }

      // What the move leaves behind.
      ImGui::TextUnformatted("leaves");
      std::size_t dropMark = a.marks.size();
      for (std::size_t m = 0; m < a.marks.size(); ++m) {
        MarkSpec& mk = a.marks[m];
        ImGui::PushID(static_cast<int>(m));
        ImGui::SameLine(m == 0 ? px(70.0f) : 0.0f);
        if (ImGui::Button(tagNames[std::min<std::size_t>(mk.tag, tagNames.size() - 1)].c_str())) {
          mk.tag = static_cast<std::uint16_t>((mk.tag + 1) % tagNames.size());
          changed = true;
        }
        ImGui::SameLine();
        const char* where = mk.where == MarkWhere::Passed ? "on the cell passed"
                            : mk.where == MarkWhere::Landed ? "on the landing cell"
                                                            : "around the landing";
        if (ImGui::Button(where)) {
          mk.where = mk.where == MarkWhere::Passed   ? MarkWhere::Landed
                     : mk.where == MarkWhere::Landed ? MarkWhere::Around
                                                     : MarkWhere::Passed;
          changed = true;
        }
        if (mk.where == MarkWhere::Around) {
          ImGui::SameLine();
          int radius = mk.radius;
          if (stepper("radius", radius, 1, 9, t, scale_, px(26.0f))) {
            mk.radius = static_cast<std::uint16_t>(radius);
            changed = true;
          }
        }
        ImGui::SameLine();
        bool forever = mk.duration == 0;
        if (ImGui::Checkbox("forever", &forever)) {
          mk.duration = forever ? 0 : 1;
          changed = true;
        }
        if (!forever) {
          ImGui::SameLine();
          int plies = static_cast<int>(mk.duration);
          if (stepper("plies", plies, 1, 99, t, scale_, px(30.0f))) {
            mk.duration = static_cast<std::uint32_t>(plies);
            changed = true;
          }
          ImGui::SameLine(0.0f, px(2.0f));
          ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
          ImGui::TextUnformatted("plies");
          ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) dropMark = m;
        ImGui::PopID();
      }
      if (dropMark < a.marks.size()) {
        a.marks.erase(a.marks.begin() + static_cast<std::ptrdiff_t>(dropMark));
        changed = true;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+ mark")) {
        a.marks.push_back(MarkSpec{static_cast<std::uint16_t>(tagNames.size() > 1 ? 1 : 0),
                                   MarkWhere::Landed, 1, 0});
        changed = true;
      }
    }
    ImGui::PopID();
    ImGui::PopFont();
  }

  sectionHead("ADD A MOVE", t, fontSmall_, scale_);
  // Adding a move the piece already has is a no-op: bundles union, they do not stack.
  const auto addOne = [&](MoveAtom a) {
    auto canon = MoveAtom::canonicalize(a);
    if (!canon.has_value()) return false;
    for (const MoveAtom& have : list) {
      if (have == *canon) return false;
    }
    list.push_back(*canon);
    changed = true;
    return true;
  };
  const std::size_t boardAxes = editor->axes().size();
  const int orientAxis = editor->orientationAxis();
  const auto addBundle = [&](const char* name) {
    for (const MoveAtom& a : app::Editor::bundleAtoms(name, boardAxes, orientAxis)) addOne(a);
  };
  ImGui::PushFont(small);
  ImGui::PushID("add-a-move");
  bool firstButton = true;
  const auto add = [&](const char* label, float w, bool enabled = true) {
    // Wrapped by hand: the bundles are a row that is longer than the pane is wide.
    if (!firstButton) {
      ImGui::SameLine();
      if (ImGui::GetCursorScreenPos().x + w >
          ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) {
        ImGui::NewLine();
      }
    }
    firstButton = false;
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(t.panelHi));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(t.panelHi));
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
    const bool hit = ImGui::Button(label, ImVec2(w, 0.0f));
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();
    return hit;
  };
  // Bundles, not colours. Dropping one in unions its atoms, which is how rook + bishop
  // becomes a queen in two dimensions and something else entirely above them.
  const bool oriented = orientAxis >= 0;
  for (const char* name :
       {"rook", "bishop", "knight", "queen", "king", "unicorn", "step", "forward step",
        "forward capture", "pawn"}) {
    const std::string n = name;
    const bool needsForward = n == "forward step" || n == "forward capture" || n == "pawn";
    if (add(name, px(8.0f * static_cast<float>(n.size()) + 24.0f), !needsForward || oriented)) {
      addBundle(name);
    }
  }
  ImGui::PopID();
  if (!oriented) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted("forward moves need the variant to declare an orientation axis");
    ImGui::PopStyleColor();
  }
  // Or take another piece's moves wholesale.
  {
    const std::vector<std::string> others = editor->pieceNames();
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted("add everything");
    ImGui::PopStyleColor();
    ImGui::PushID("union-with");
    for (const std::string& other : others) {
      if (other == piece) continue;
      ImGui::SameLine();
      if (ImGui::SmallButton(other.c_str())) {
        const auto theirs = editor->pieceAtoms(other);
        if (theirs.has_value()) {
          for (const MoveAtom& a : *theirs) addOne(a);
        }
      }
    }
    ImGui::PopID();
  }
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
  // What a left click puts on the board. Enemies are captured, friends block: the two
  // things a move's capture rule and its kind (slide, leap, hop) are about.
  struct Tool {
    const char* label;
    int value;
  };
  for (const Tool tool : {Tool{"ENEMY", 1}, Tool{"FRIEND", 2}, Tool{"ERASE", 0}}) {
    if (tool.value != 1) ImGui::SameLine();
    if (button(tool.label, t, px(92), previewTool_ == tool.value, true, true, display)) {
      previewTool_ = tool.value;
    }
  }
  ImGui::SameLine();
  if (button("CLEAR", t, px(88), false, true, true, display)) {
    for (auto& cells : previewOcc_) std::fill(cells.begin(), cells.end(), 0);
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "Left-click a cell to place the chosen piece, again to take it off. Right or middle "
      "drag turns the board.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void Ui::drawBodyTab(app::Shell& shell) {
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  syncDesignAssets(shell);
  app::Editor* editor = shell.editor();
  const std::vector<std::string> names =
      editor != nullptr ? editor->pieceNames() : std::vector<std::string>{};
  const std::string piece =
      names.empty() ? std::string{}
                    : names[static_cast<std::size_t>(
                          std::clamp(editorPiece_, 0, static_cast<int>(names.size()) - 1))];

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "A side profile swept about the axis, plus parts that are not round - a knight's "
      "head, a crown's points. Drag a point to move it; the solid beside it is what the "
      "game draws.");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  // ---- generators: every shipped body is a starting point.
  sectionHead("START FROM", t, fontSmall_, scale_);
  ImGui::PushFont(small);
  ImGui::PushID("body-generators");
  for (std::size_t i = 0; i < std::size(kGenerators); ++i) {
    if (i != 0) ImGui::SameLine();
    if (ImGui::SmallButton(kGenerators[i].piece)) {
      designModel_ = assets::archetypeModel(kGenerators[i].model);
      designProfilePoint_ = -1;
      designElement_ = designModel_.elements.empty() ? -1 : 0;
      designElementPoint_ = -1;
    }
  }
  ImGui::PopID();
  // And the pieces this variant already has, with whatever bodies they have been given.
  if (names.size() > 1) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted("this variant:");
    ImGui::PopStyleColor();
    ImGui::PushID("body-from-piece");
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (names[i] == piece) continue;
      ImGui::SameLine();
      if (ImGui::SmallButton(names[i].c_str())) {
        const auto theirs = editor->pieceBody(names[i]);
        designModel_ = theirs.has_value() && theirs->has_value()
                           ? **theirs
                           : seedBody(shell.preview(shell.editorVariant()), names[i]);
        designProfilePoint_ = -1;
        designElement_ = designModel_.elements.empty() ? -1 : 0;
        designElementPoint_ = -1;
      }
    }
    ImGui::PopID();
  }
  ImGui::PopFont();

  // ---- step 1: the profile, with the placed elements riding on it.
  sectionHead("STEP 1  THE PROFILE", t, fontSmall_, scale_);
  const float side = std::min(ImGui::GetContentRegionAvail().x * 0.4f, px(240.0f));
  const ImVec2 gridMin = ImGui::GetCursorScreenPos();
  const ImVec2 gridMax(gridMin.x + side, gridMin.y + side);
  ImGui::Dummy(ImVec2(side, side));
  (void)outlineEditor("profile", gridMin, gridMax, designModel_.profile,
                      designProfilePoint_, true, "radius", "height");
  {
    // Where each element is anchored, as a diamond that can be taken hold of: placing a
    // part is dragging its anchor to the spot on the body where it belongs.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cursorWas = ImGui::GetCursorScreenPos();
    ImGui::PushID("anchors");
    for (std::size_t i = 0; i < designModel_.elements.size(); ++i) {
      assets::Element& e = designModel_.elements[i];
      const ImVec2 at = toScreen(assets::ModelPoint{e.radius, e.height}, gridMin, gridMax,
                                 true, DesignBox{});
      const float grab = px(8.0f);
      ImGui::SetCursorScreenPos(ImVec2(at.x - grab, at.y - grab));
      ImGui::PushID(static_cast<int>(i));
      ImGui::InvisibleButton("##anchor", ImVec2(grab * 2.0f, grab * 2.0f));
      if (ImGui::IsItemClicked()) {
        designElement_ = static_cast<int>(i);
        designElementPoint_ = -1;
      }
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        // A finer snap than the outline's: an element is placed against a surface.
        const float u = (m.x - gridMin.x) / std::max(1.0f, gridMax.x - gridMin.x);
        const float v = (gridMax.y - m.y) / std::max(1.0f, gridMax.y - gridMin.y);
        e.radius = static_cast<std::int16_t>(
            std::clamp(std::lround(u * 100.0f) * 10L, 0L, 1000L));
        e.height = static_cast<std::int16_t>(
            std::clamp(std::lround(v * 100.0f) * 10L, 0L, 1000L));
        designElement_ = static_cast<int>(i);
      }
      ImGui::PopID();
      const bool sel = designElement_ == static_cast<int>(i);
      const float r = sel ? px(7.0f) : px(5.5f);
      const ImVec2 q[4]{{at.x, at.y - r}, {at.x + r, at.y}, {at.x, at.y + r},
                        {at.x - r, at.y}};
      dl->AddConvexPolyFilled(q, 4, u32(sel ? t.ember : t.rift, 0.95f));
      dl->AddPolyline(q, 4, u32(t.ink), ImDrawFlags_Closed, 1.0f);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(cursorWas);
  }
  drawRevolvePreview(ImGui::GetWindowDrawList(),
                     ImVec2(gridMax.x + px(16.0f), gridMin.y),
                     ImVec2(gridMax.x + px(16.0f) + side, gridMax.y), designModel_,
                     clock_ * 0.6f);

  ImGui::PushID("profile-buttons");
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
    // Back to the shipped archetype this piece's moves would have been drawn as.
    designModel_ = seedBody(shell.preview(shell.editorVariant()), piece);
    designIcon_ = iconModelOf(seedArchetype(shell.preview(shell.editorVariant()), piece),
                              IconStyle::Faceted);
    designProfilePoint_ = -1;
    designElement_ = designModel_.elements.empty() ? -1 : 0;
  }
  ImGui::PopID();

  // ---- step 2: the sweep and its symmetry.
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
    // Going back to full drops the parts a round piece cannot carry, which is the only
    // way the result can still be a shape the game can build.
    if (designModel_.symmetry.kind == assets::SymmetryKind::Full) {
      designModel_.elements.clear();
      designElement_ = -1;
    }
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
        "Radially symmetric: the profile is the whole piece. Choose mirror (a knight) or "
        "k-fold (a crown) to place parts that are not round.");
  } else if (designModel_.symmetry.kind == assets::SymmetryKind::KFold) {
    char line[112];
    std::snprintf(line, sizeof(line), "every element is repeated %d times round the axis",
                  assets::copiesOf(designModel_.symmetry));
    ImGui::TextUnformatted(line);
  } else {
    ImGui::TextWrapped(
        "One part on the piece's front, facing +radius. Give it a sideways offset and it "
        "becomes a reflected pair - a knight's ears.");
  }
  ImGui::PopStyleColor();
  ImGui::PopFont();

  // ---- step 3: the parts that are not round.
  if (designModel_.symmetry.kind != assets::SymmetryKind::Full) {
    sectionHead("STEP 3  ELEMENTS", t, fontSmall_, scale_);
    const auto addElement = [&](const char* what) {
      assets::Element e;
      const std::string w = what;
      if (w == "spike") {
        e.outline.points = {{0, 0}, {90, 130}, {0, 210}, {-90, 130}};
        e.tangent = true;
        e.radius = 250;
        e.thickness = 90;
      } else if (w == "block") {
        e.outline.points = {{-80, 0}, {80, 0}, {80, 120}, {-80, 120}};
        e.tangent = true;
        e.radius = 250;
        e.thickness = 110;
      } else if (w == "ear") {
        e.outline.points = {{-50, 0}, {50, 0}, {30, 200}, {-30, 200}};
        e.lateral = 110;
        e.thickness = 60;
        e.radius = 100;
      } else {  // head: the knight's, whole
        e = assets::archetypeModel("wedge").elements.front();
      }
      int top = 0;
      for (const assets::ModelPoint& p : designModel_.profile.points) {
        top = std::max<int>(top, p.y);
      }
      if (w != "head") e.height = static_cast<std::int16_t>(std::max(0, top - 60));
      designModel_.elements.push_back(std::move(e));
      designElement_ = static_cast<int>(designModel_.elements.size()) - 1;
      designElementPoint_ = -1;
    };
    ImGui::PushFont(small);
    ImGui::PushID("add-element");
    for (const char* what : {"spike", "block", "ear", "head"}) {
      if (what != std::string("spike")) ImGui::SameLine();
      char label[24];
      std::snprintf(label, sizeof(label), "+ %s", what);
      if (ImGui::SmallButton(label)) addElement(what);
    }
    ImGui::PopID();
    ImGui::PopFont();

    if (!designModel_.elements.empty()) {
      ImGui::PushFont(small);
      ImGui::PushID("pick-element");
      for (std::size_t i = 0; i < designModel_.elements.size(); ++i) {
        if (i != 0) ImGui::SameLine();
        const bool selected = designElement_ == static_cast<int>(i);
        ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, selected ? 1.0f : 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Text, col(selected ? t.ember : t.boneDim));
        char label[48];
        std::snprintf(label, sizeof(label), "element %zu", i + 1);
        if (ImGui::SmallButton(label)) {
          designElement_ = static_cast<int>(i);
          designElementPoint_ = -1;
        }
        ImGui::PopStyleColor(2);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("remove") && designElement_ >= 0 &&
          designElement_ < static_cast<int>(designModel_.elements.size())) {
        designModel_.elements.erase(designModel_.elements.begin() + designElement_);
        designElement_ = designModel_.elements.empty() ? -1 : 0;
        designElementPoint_ = -1;
      }
      ImGui::PopID();
      ImGui::PopFont();
    }

    if (designElement_ >= 0 && designElement_ < static_cast<int>(designModel_.elements.size())) {
      assets::Element& e = designModel_.elements[static_cast<std::size_t>(designElement_)];
      // The element's own window: about its origin, so a part reaching to both sides of
      // the axis can be drawn.
      DesignBox box;
      box.xMin = -500.0f;
      box.xMax = 500.0f;
      box.yMin = 0.0f;
      box.yMax = 500.0f;
      const float ew = std::min(ImGui::GetContentRegionAvail().x * 0.4f, px(240.0f));
      const ImVec2 eMin = ImGui::GetCursorScreenPos();
      const ImVec2 eMax(eMin.x + ew, eMin.y + ew * 0.5f);
      ImGui::Dummy(ImVec2(ew, ew * 0.5f));
      (void)outlineEditor("element", eMin, eMax, e.outline, designElementPoint_, true,
                          e.tangent ? "round" : "out", "up", box);
      // The sliders beside it: exact numbers for what the anchor and the outline suggest.
      ImGui::SetCursorScreenPos(ImVec2(eMax.x + px(16.0f), eMin.y));
      ImGui::BeginGroup();
      ImGui::PushItemWidth(px(190.0f));
      int radius = e.radius, height = e.height, lateral = e.lateral, thick = e.thickness;
      if (ImGui::SliderInt("radius", &radius, 0, 1000)) {
        e.radius = static_cast<std::int16_t>(radius);
      }
      if (ImGui::SliderInt("height", &height, 0, 1000)) {
        e.height = static_cast<std::int16_t>(height);
      }
      if (ImGui::SliderInt("sideways", &lateral, -500, 500)) {
        e.lateral = static_cast<std::int16_t>(lateral);
      }
      if (ImGui::SliderInt("thickness", &thick, 20, 600)) {
        e.thickness = static_cast<std::int16_t>(thick);
      }
      ImGui::PopItemWidth();
      ImGui::Checkbox("broad round the rim (not outward)", &e.tangent);
      ImGui::EndGroup();
      ImGui::SetCursorScreenPos(ImVec2(eMin.x, std::max(ImGui::GetItemRectMax().y, eMax.y) + px(6.0f)));
      ImGui::PushID("element-buttons");
      if (button("+ POINT", t, px(92), false, true, true, display)) {
        auto& pts = e.outline.points;
        if (!pts.empty()) {
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
      }
      ImGui::SameLine();
      if (button("- POINT", t, px(92), false, true, true, display)) {
        auto& pts = e.outline.points;
        if (pts.size() > 3 && designElementPoint_ >= 0 &&
            designElementPoint_ < static_cast<int>(pts.size())) {
          pts.erase(pts.begin() + designElementPoint_);
          designElementPoint_ = -1;
        }
      }
      ImGui::PopID();
    }
  }

  const Result<void> ok = assets::validate(designModel_);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(ok.has_value() ? t.moss : t.blood));
  ImGui::TextWrapped("%s", ok.has_value() ? "the body closes; this shape can be built"
                                          : ok.error().message.c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  commitDesign(shell, piece);
}

void Ui::drawIconTab(app::Shell& shell) {
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  syncDesignAssets(shell);
  app::Editor* editor = shell.editor();
  const std::vector<std::string> names =
      editor != nullptr ? editor->pieceNames() : std::vector<std::string>{};
  const std::string piece =
      names.empty() ? std::string{}
                    : names[static_cast<std::size_t>(
                          std::clamp(editorPiece_, 0, static_cast<int>(names.size()) - 1))];
  if (designIcon_.fills.empty()) designIcon_ = assets::iconFromProfile(designModel_);
  const std::size_t total = designIcon_.fills.size() + designIcon_.cuts.size();
  designIconPoly_ = std::min(designIconPoly_, total - 1);

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "The outline the board draws when it is drawn flat. Straight segments only - that "
      "is the visual language, and it is exactly what fills without tessellation. Fills "
      "are the piece's colour; cut-outs are painted in what is behind it.");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  sectionHead("START FROM", t, fontSmall_, scale_);
  ImGui::PushFont(small);
  ImGui::PushID("icon-generators");
  for (std::size_t i = 0; i < std::size(kGenerators); ++i) {
    if (i != 0) ImGui::SameLine();
    if (ImGui::SmallButton(kGenerators[i].piece)) {
      designIcon_ = iconModelOf(kGenerators[i].archetype, iconStyle_);
      designIconPoly_ = 0;
      designIconPoint_ = -1;
    }
  }
  ImGui::PopID();
  if (names.size() > 1) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted("this variant:");
    ImGui::PopStyleColor();
    ImGui::PushID("icon-from-piece");
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (names[i] == piece) continue;
      ImGui::SameLine();
      if (ImGui::SmallButton(names[i].c_str())) {
        const auto theirs = editor->pieceIcon(names[i]);
        if (theirs.has_value() && theirs->has_value()) {
          designIcon_ = **theirs;
        } else {
          const VariantSpec* spec = shell.preview(shell.editorVariant());
          Archetype shape = Archetype::Dome;
          if (spec != nullptr) {
            const PieceTypeId id = spec->findPiece(names[i]);
            if (id != kNoPiece) shape = archetypeFor(spec->pieces[id]);
          }
          designIcon_ = iconModelOf(shape, iconStyle_);
        }
        designIconPoly_ = 0;
        designIconPoint_ = -1;
      }
    }
    ImGui::PopID();
  }
  ImGui::PopFont();

  // Which outline is being edited: the fills first, then the cut-outs.
  sectionHead("THE OUTLINE", t, fontSmall_, scale_);
  ImGui::PushFont(small);
  ImGui::PushID("icon-outlines");
  for (std::size_t i = 0; i < total; ++i) {
    if (i != 0) ImGui::SameLine();
    const bool isCut = i >= designIcon_.fills.size();
    const bool selected = i == designIconPoly_;
    ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, selected ? 1.0f : 0.45f));
    ImGui::PushStyleColor(ImGuiCol_Text, col(selected ? t.ember : t.boneDim));
    char label[48];
    if (isCut) {
      std::snprintf(label, sizeof(label), "cut %zu", i - designIcon_.fills.size() + 1);
    } else {
      std::snprintf(label, sizeof(label), "fill %zu", i + 1);
    }
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::SmallButton(label)) {
      designIconPoly_ = i;
      designIconPoint_ = -1;
    }
    ImGui::PopID();
    ImGui::PopStyleColor(2);
  }
  const auto fresh = [] {
    assets::Outline o;
    o.points = {{400, 400}, {600, 400}, {600, 600}, {400, 600}};
    return o;
  };
  ImGui::SameLine();
  if (ImGui::SmallButton("+ fill")) {
    designIcon_.fills.push_back(fresh());
    designIconPoly_ = designIcon_.fills.size() - 1;
    designIconPoint_ = -1;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("+ cut")) {
    designIcon_.cuts.push_back(fresh());
    designIconPoly_ = designIcon_.fills.size() + designIcon_.cuts.size() - 1;
    designIconPoint_ = -1;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("remove outline")) {
    if (designIconPoly_ < designIcon_.fills.size()) {
      if (designIcon_.fills.size() > 1) {
        designIcon_.fills.erase(designIcon_.fills.begin() +
                                static_cast<std::ptrdiff_t>(designIconPoly_));
      }
    } else {
      designIcon_.cuts.erase(designIcon_.cuts.begin() +
                             static_cast<std::ptrdiff_t>(designIconPoly_ -
                                                         designIcon_.fills.size()));
    }
    designIconPoly_ = 0;
    designIconPoint_ = -1;
  }
  ImGui::PopID();
  ImGui::PopFont();

  const float side = std::min(ImGui::GetContentRegionAvail().x, px(240.0f));
  const ImVec2 gridMin = ImGui::GetCursorScreenPos();
  const ImVec2 gridMax(gridMin.x + side, gridMin.y + side);
  ImGui::Dummy(ImVec2(side, side));
  const std::size_t shown = std::min(designIconPoly_, designIcon_.fills.size() +
                                                          designIcon_.cuts.size() - 1);
  assets::Outline& poly = shown < designIcon_.fills.size()
                              ? designIcon_.fills[shown]
                              : designIcon_.cuts[shown - designIcon_.fills.size()];
  if (outlineEditor("icon", gridMin, gridMax, poly, designIconPoint_, false, "x", "y") &&
      designIconMirror_) {
    const std::size_t n = poly.points.size();
    for (std::size_t i = 0; i < n / 2; ++i) {
      const assets::ModelPoint& src = poly.points[i];
      poly.points[n - 1 - i] =
          assets::ModelPoint{static_cast<std::int16_t>(1000 - src.x), src.y};
    }
  }

  ImGui::PushID("icon-buttons");
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
  ImGui::PopID();

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
      const auto trace = [&](const assets::Outline& o, ImU32 fill) {
        if (o.points.size() < 3) return;
        std::vector<ImVec2> pts;
        pts.reserve(o.points.size());
        for (const assets::ModelPoint& p : o.points) {
          pts.push_back(ImVec2(c.x + (static_cast<float>(p.x) / 1000.0f - 0.5f) * r * 2,
                               c.y + (static_cast<float>(p.y) / 1000.0f - 0.5f) * r * 2));
        }
        fillPolygon(dl, pts.data(), static_cast<int>(pts.size()), fill);
      };
      for (const assets::Outline& o : designIcon_.fills) trace(o, u32(t.whitePiece));
      for (const assets::Outline& o : designIcon_.cuts) trace(o, u32(t.pieceToken));
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
  commitDesign(shell, piece);
}

void Ui::drawIdentityTab(app::Shell& shell, const std::string& piece) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);

  auto info = editor->pieceInfo(piece);
  if (!info.has_value()) return;
  app::Editor::PieceInfo edited = *info;
  bool changed = false;

  // The text boxes keep their own buffers, filled when the piece changes (or the document
  // does, by an undo): ImGui edits a char array, and writing every keystroke to the
  // document would put one undo entry on the stack per letter.
  if (identityFor_ != piece || designSyncedRevision_ != editor->revision()) {
    identityFor_ = piece;
    std::snprintf(nameBuf_.data(), nameBuf_.size(), "%s", info->name.c_str());
    std::snprintf(symbolBuf_.data(), symbolBuf_.size(), "%s", info->symbol.c_str());
  }

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "What the piece is called and how it counts, as the variant file says it. The name "
      "and symbol are how the starting position refers to it.");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  sectionHead("NAME AND SYMBOL", t, fontSmall_, scale_);
  ImGui::PushItemWidth(px(190.0f));
  ImGui::InputText("name", nameBuf_.data(), nameBuf_.size());
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    const Result<void> ok = editor->renamePiece(piece, nameBuf_.data());
    moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
  }
  ImGui::PopItemWidth();
  ImGui::PushItemWidth(px(50.0f));
  ImGui::InputText("symbol", symbolBuf_.data(), 2);
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    edited.symbol = symbolBuf_.data();
    changed = true;
  }
  ImGui::PopItemWidth();
  if (!moveNote_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", moveNote_.c_str());
    ImGui::PopStyleColor();
  }

  sectionHead("RULES IT CARRIES", t, fontSmall_, scale_);
  if (ImGui::Checkbox("royal (the game is lost if it is taken)", &edited.royal)) {
    changed = true;
  }
  if (ImGui::Checkbox("a move by it resets the draw clock", &edited.resetsDrawClock)) {
    changed = true;
  }

  sectionHead("HOW IT LOOKS", t, fontSmall_, scale_);
  {
    bool autoHeight = edited.heightPermille <= 0;
    if (ImGui::Checkbox("height from how it moves", &autoHeight)) {
      edited.heightPermille = autoHeight ? 0 : 1000;
      changed = true;
    }
    if (!autoHeight) {
      ImGui::PushItemWidth(px(220.0f));
      int h = edited.heightPermille;
      if (ImGui::SliderInt("height (a pawn is 1000)", &h, 200, 4000)) {
        edited.heightPermille = h;
        changed = true;
      }
      ImGui::PopItemWidth();
    }
    static const char* kShapes[]{"", "dome", "tower", "wedge", "spire", "crown", "monolith", "horn"};
    std::size_t at = 0;
    for (std::size_t i = 0; i < std::size(kShapes); ++i) {
      if (edited.shape == kShapes[i]) at = i;
    }
    char label[48];
    std::snprintf(label, sizeof(label), "fallback shape: %s",
                  at == 0 ? "from its moves" : kShapes[at]);
    if (button(label, t, px(260), false, true, true, display)) {
      edited.shape = kShapes[(at + 1) % std::size(kShapes)];
      changed = true;
    }
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextWrapped(
        "Used when the piece has no authored body. Give it one on the BODY tab and that "
        "is what is drawn.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }

  sectionHead("PROMOTES TO", t, fontSmall_, scale_);
  {
    const std::vector<std::string> names = editor->pieceNames();
    ImGui::PushFont(small);
    ImGui::PushID("promotes");
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (i != 0) ImGui::SameLine();
      const auto found =
          std::find(edited.promotesTo.begin(), edited.promotesTo.end(), names[i]);
      const bool on = found != edited.promotesTo.end();
      ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, on ? 1.0f : 0.45f));
      ImGui::PushStyleColor(ImGuiCol_Text, col(on ? t.ember : t.boneDim));
      if (ImGui::SmallButton(names[i].c_str())) {
        if (on) {
          edited.promotesTo.erase(found);
        } else {
          edited.promotesTo.push_back(names[i]);
        }
        changed = true;
      }
      ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    ImGui::PopFont();
  }
  if (changed) {
    const Result<void> ok = editor->setPieceInfo(piece, edited);
    moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
  }

  sectionHead("THE PIECE LIST", t, fontSmall_, scale_);
  {
    // The list's order is the pieces' ids, so a new piece goes last and nothing moves.
    const std::vector<std::string> names = editor->pieceNames();
    if (button("+ NEW FROM THIS", t, px(170), false, true, true, display)) {
      std::string name = "piece";
      for (int n = 2; std::find(names.begin(), names.end(), name) != names.end(); ++n) {
        name = "piece" + std::to_string(n);
      }
      // The first capital not already some piece's symbol.
      std::string used;
      for (const std::string& n : names) {
        if (const auto pi = editor->pieceInfo(n); pi.has_value()) used += pi->symbol;
      }
      char symbol = 'A';
      while (symbol < 'Z' && used.find(symbol) != std::string::npos) ++symbol;
      const Result<void> ok = editor->addPiece(name, std::string(1, symbol), piece);
      if (ok.has_value()) {
        editorPiece_ = static_cast<int>(editor->pieceNames().size()) - 1;
        moveNote_.clear();
      } else {
        moveNote_ = ok.error().message;
      }
    }
    ImGui::SameLine();
    if (button("DELETE THIS", t, px(150), false, true, names.size() > 1, display)) {
      const Result<void> ok = editor->removePiece(piece);
      if (ok.has_value()) {
        editorPiece_ = std::max(0, editorPiece_ - 1);
        moveNote_.clear();
      } else {
        moveNote_ = ok.error().message;
      }
    }
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextWrapped(
        "A new piece starts as a copy of this one - moves, body and icon - to edit from. "
        "Deleting a piece the starting position still uses is refused when you save.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
}

void Ui::drawFieldsTab(app::Shell& shell) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "A field is a named number every piece (or every cell) carries. It travels with the "
      "piece, is part of the position's hash, and is put back exactly by undo. Rules read "
      "and write it - see the RULES tab.");
  ImGui::TextWrapped(
      "Two ways to say \"has this piece moved?\": a move's 'only from the home rank' "
      "(position-derived, nothing to store - the pawn's double step) or a field set by a "
      "rule (state that survives - 'charge' in the charged variant). Prefer the first "
      "when the board already says the answer.");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  for (const bool pieceFields : {true, false}) {
    sectionHead(pieceFields ? "PIECE FIELDS" : "CELL FIELDS", t, fontSmall_, scale_);
    std::vector<app::Editor::FieldInfo> list = editor->fields(pieceFields);
    bool changed = false;
    std::size_t removeAt = list.size();
    ImGui::PushID(pieceFields ? "piece-fields" : "cell-fields");
    ImGui::PushFont(small);
    for (std::size_t i = 0; i < list.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      app::Editor::FieldInfo& f = list[i];
      if (editText("##name", f.name, px(150.0f))) changed = true;
      ImGui::SameLine();
      ImGui::TextUnformatted("start");
      ImGui::SameLine();
      if (stepper("default", f.defaultValue, -999, 999, t, scale_, px(40.0f))) changed = true;
      ImGui::SameLine();
      ImGui::TextUnformatted("from");
      ImGui::SameLine();
      if (stepper("min", f.minValue, -999, 999, t, scale_, px(40.0f))) changed = true;
      ImGui::SameLine();
      ImGui::TextUnformatted("to");
      ImGui::SameLine();
      if (stepper("max", f.maxValue, -999, 999, t, scale_, px(40.0f))) changed = true;
      ImGui::SameLine();
      if (ImGui::Checkbox("in the position hash", &f.hashed)) changed = true;
      ImGui::SameLine();
      if (ImGui::SmallButton("remove")) removeAt = i;
      ImGui::PopID();
    }
    ImGui::PopFont();
    if (removeAt < list.size()) {
      list.erase(list.begin() + static_cast<std::ptrdiff_t>(removeAt));
      changed = true;
    }
    if (button(pieceFields ? "+ PIECE FIELD" : "+ CELL FIELD", t, px(160), false, true,
               true, display)) {
      app::Editor::FieldInfo f;
      f.name = pieceFields ? "charge" : "heat";
      for (int n = 2; std::any_of(list.begin(), list.end(),
                                   [&](const auto& o) { return o.name == f.name; });
           ++n) {
        f.name = (pieceFields ? "charge" : "heat") + std::to_string(n);
      }
      f.minValue = 0;
      f.maxValue = 8;
      list.push_back(std::move(f));
      changed = true;
    }
    ImGui::PopID();
    if (changed) {
      const Result<void> ok = editor->setFields(pieceFields, list);
      moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
    }
  }
  if (!moveNote_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", moveNote_.c_str());
    ImGui::PopStyleColor();
  }
}

void Ui::drawRulesTab(app::Shell& shell) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "Rules run in the order listed, and the order is meaning: a rule that adds one to a "
      "counter must come before the rule that tests the counter. Each has a moment it runs "
      "at, an optional condition, and effects that run in order. Conditions and values "
      "are expressions such as (gt (piece_field charge move.to) 2) or (is_capture); "
      "move.from and move.to name the cells of the move.");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  static const char* kTriggers[]{"on_move_filter", "on_capture", "on_move_end",
                                 "on_turn_end", "on_result_query"};
  static const char* kTriggerWords[]{"before a move is allowed", "when a piece is taken",
                                     "after a move", "after the turn passes",
                                     "when the result is asked"};
  struct OpArgs {
    const char* op;
    std::vector<const char*> args;
  };
  static const OpArgs kOps[]{
      {"destroy", {"at"}},
      {"destroy_region", {"at", "radius", "affects"}},
      {"transform", {"at", "piece"}},
      {"spawn", {"at", "piece", "color"}},
      {"set_piece_field", {"at", "field", "value"}},
      {"set_cell_field", {"at", "field", "value"}},
      {"forbid_move", {}},
      {"repeat_turn", {}},
      {"end_game", {"outcome"}},
  };
  const auto opIndex = [&](const std::string& op) {
    for (std::size_t i = 0; i < std::size(kOps); ++i) {
      if (op == kOps[i].op) return i;
    }
    return std::size_t{0};
  };

  std::vector<app::Editor::RuleInfo> list = editor->rules();
  bool changed = false;
  const std::vector<std::string> pieceNames = editor->pieceNames();
  const auto pieceFields = editor->fields(true);

  const auto argText = [](app::Editor::EffectInfo& e, const char* key) -> std::string& {
    for (auto& [k, v] : e.args) {
      if (k == key) return v;
    }
    e.args.emplace_back(key, std::string{});
    return e.args.back().second;
  };
  // A new effect starts with the arguments an author would most often type.
  const auto freshEffect = [&](const char* op) {
    app::Editor::EffectInfo e;
    e.op = op;
    const std::string o = op;
    if (o != "forbid_move" && o != "repeat_turn" && o != "end_game") {
      e.args.emplace_back("at", "move.to");
    }
    if (o == "destroy_region") e.args.emplace_back("radius", "1");
    if ((o == "transform" || o == "spawn") && !pieceNames.empty()) {
      e.args.emplace_back("piece", pieceNames.back());
    }
    if ((o == "set_piece_field" || o == "set_cell_field")) {
      const auto fields = editor->fields(o == "set_piece_field");
      e.args.emplace_back("field", fields.empty() ? std::string{} : fields.front().name);
      e.args.emplace_back("value", "0");
    }
    if (o == "end_game") e.args.emplace_back("outcome", "draw");
    return e;
  };

  std::size_t removeRule = list.size();
  int moveRule = 0;
  std::size_t moveRuleAt = 0;
  for (std::size_t r = 0; r < list.size(); ++r) {
    app::Editor::RuleInfo& rule = list[r];
    ImGui::PushID(static_cast<int>(r));
    char head[48];
    std::snprintf(head, sizeof(head), "RULE %zu", r + 1);
    sectionHead(head, t, fontSmall_, scale_);
    ImGui::PushFont(small);
    if (editText("##rname", rule.name, px(260.0f))) changed = true;
    ImGui::SameLine();
    std::size_t when = 2;
    for (std::size_t i = 0; i < std::size(kTriggers); ++i) {
      if (rule.when == kTriggers[i]) when = i;
    }
    if (ImGui::Button(kTriggerWords[when])) {
      rule.when = kTriggers[(when + 1) % std::size(kTriggers)];
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("up") && r > 0) { moveRule = -1; moveRuleAt = r; }
    ImGui::SameLine();
    if (ImGui::SmallButton("down") && r + 1 < list.size()) { moveRule = 1; moveRuleAt = r; }
    ImGui::SameLine();
    if (ImGui::SmallButton("remove rule")) removeRule = r;

    ImGui::TextUnformatted("only when");
    ImGui::SameLine(px(80.0f));
    if (editText("##cond", rule.condition, px(480.0f))) changed = true;
    if (rule.condition.empty()) {
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::TextUnformatted("(always)");
      ImGui::PopStyleColor();
    }

    std::size_t removeEffect = rule.effects.size();
    int moveEffect = 0;
    std::size_t moveEffectAt = 0;
    for (std::size_t e = 0; e < rule.effects.size(); ++e) {
      app::Editor::EffectInfo& eff = rule.effects[e];
      ImGui::PushID(static_cast<int>(e));
      char label[48];
      std::snprintf(label, sizeof(label), "%zu  %s", e + 1, eff.op.c_str());
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
      if (ImGui::Button(label, ImVec2(px(170.0f), 0.0f))) {
        // Pressing the name steps to the next effect kind, with that kind's arguments.
        eff = freshEffect(kOps[(opIndex(eff.op) + 1) % std::size(kOps)].op);
        changed = true;
      }
      ImGui::PopStyleColor();
      for (const char* arg : kOps[opIndex(eff.op)].args) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
        ImGui::TextUnformatted(arg);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushID(arg);
        if (editText("##a", argText(eff, arg), std::string(arg) == "value" || std::string(arg) == "affects" ? px(200.0f) : px(110.0f))) {
          changed = true;
        }
        ImGui::PopID();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("up") && e > 0) { moveEffect = -1; moveEffectAt = e; }
      ImGui::SameLine();
      if (ImGui::SmallButton("down") && e + 1 < rule.effects.size()) { moveEffect = 1; moveEffectAt = e; }
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) removeEffect = e;
      ImGui::PopID();
    }
    if (removeEffect < rule.effects.size()) {
      rule.effects.erase(rule.effects.begin() + static_cast<std::ptrdiff_t>(removeEffect));
      changed = true;
    }
    if (moveEffect != 0) {
      std::swap(rule.effects[moveEffectAt],
                rule.effects[static_cast<std::size_t>(static_cast<int>(moveEffectAt) + moveEffect)]);
      changed = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted("add an effect:");
    ImGui::PopStyleColor();
    ImGui::PushID("add-effect");
    for (const OpArgs& op : kOps) {
      ImGui::SameLine();
      if (ImGui::SmallButton(op.op)) {
        rule.effects.push_back(freshEffect(op.op));
        changed = true;
      }
    }
    ImGui::PopID();
    ImGui::PopFont();
    ImGui::PopID();
  }
  if (removeRule < list.size()) {
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(removeRule));
    changed = true;
  }
  if (moveRule != 0) {
    std::swap(list[moveRuleAt],
              list[static_cast<std::size_t>(static_cast<int>(moveRuleAt) + moveRule)]);
    changed = true;
  }

  sectionHead("ADD A RULE", t, fontSmall_, scale_);
  if (button("+ RULE", t, px(110), false, true, true, display)) {
    app::Editor::RuleInfo rule;
    rule.name = "new rule";
    rule.effects.push_back(freshEffect("forbid_move"));
    rule.when = "on_move_filter";
    list.push_back(std::move(rule));
    changed = true;
  }
  (void)pieceFields;
  if (changed) {
    const Result<void> ok = editor->setRules(list);
    moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
  }

  // What the engine reads: the loader's own description, or its own complaint.
  sectionHead("THE ENGINE READS THIS AS", t, fontSmall_, scale_);
  const Result<std::string> described = editor->describeRules();
  ImGui::PushFont(small);
  if (described.has_value()) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.moss));
    ImGui::TextWrapped("%s", described->empty() ? "no rules" : described->c_str());
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", described.error().format().c_str());
  }
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

void Ui::drawMarksTab(app::Shell& shell) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "A mark is a named tag a move leaves on cells - the cell it passed over, the cell it "
      "landed on, or the cells around where it landed - for a number of plies or forever. "
      "En passant is the built-in tag \"ep\" on the passed cell for one ply. A tag that "
      "blocks makes the cells it marks impassable: \"destroyed\" squares. Moves leave marks "
      "(the MOVES tab, a move's detail), and rules can ask (marked tag cell).");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  sectionHead("TAGS", t, fontSmall_, scale_);
  std::vector<app::Editor::MarkTagInfo> list = editor->markTags();
  bool changed = false;
  std::size_t removeAt = list.size();
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("ep   (built in: the en-passant target; blocks nothing)");
  ImGui::PopStyleColor();
  for (std::size_t i = 0; i < list.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    if (editText("##tag", list[i].name, px(200.0f))) changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("blocks (impassable)", &list[i].blocks)) changed = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("remove")) removeAt = i;
    ImGui::PopID();
  }
  ImGui::PopFont();
  if (removeAt < list.size()) {
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(removeAt));
    changed = true;
  }
  if (button("+ TAG", t, px(100), false, true, true, display)) {
    app::Editor::MarkTagInfo tag;
    tag.name = "destroyed";
    for (int n = 2; std::any_of(list.begin(), list.end(),
                                 [&](const auto& o) { return o.name == tag.name; });
         ++n) {
      tag.name = "destroyed" + std::to_string(n);
    }
    tag.blocks = true;
    list.push_back(std::move(tag));
    changed = true;
  }
  if (changed) {
    const Result<void> ok = editor->setMarkTags(list);
    moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
  }
  if (!moveNote_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", moveNote_.c_str());
    ImGui::PopStyleColor();
  }
}

void Ui::drawCompoundTab(app::Shell& shell) {
  app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);

  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "A move of two pieces: castling, and anything like it. The mover makes an exact step "
      "(the king's two cells). The partner is found, never named - the first piece of its kind "
      "along that line, beyond where the mover lands - and carried to the cell the mover "
      "crossed. The cells chosen must not be attacked. No coordinate is written anywhere, so "
      "the same definition castles on any board, along any axis, in any dimension.");
  ImGui::PopStyleColor();
  ImGui::PopFont();

  const std::vector<std::string> pieces = editor->pieceNames();
  const std::size_t axes = editor->axes().size();
  const auto axisNames = [&] {
    std::vector<std::string> n;
    for (const auto& a : editor->axes()) n.push_back(a.name);
    return n;
  }();
  std::vector<app::Editor::CompoundInfo> list = editor->compounds();
  bool changed = false;
  std::size_t removeAt = list.size();

  for (std::size_t i = 0; i < list.size(); ++i) {
    app::Editor::CompoundInfo& c = list[i];
    ImGui::PushID(static_cast<int>(i));
    char head[64];
    std::snprintf(head, sizeof(head), "COMPOUND MOVE %zu", i + 1);
    sectionHead(head, t, fontSmall_, scale_);
    ImGui::PushFont(small);
    if (editText("##name", c.name, px(220.0f))) changed = true;
    ImGui::SameLine();
    ImGui::TextUnformatted("FEN letter");
    ImGui::SameLine();
    if (editText("##fen", c.fen, px(36.0f))) changed = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("remove")) removeAt = i;

    // The mover: a piece, and the exact step it takes, one signed number per axis.
    ImGui::TextUnformatted("mover");
    ImGui::SameLine();
    if (ImGui::Button(c.piece.empty() ? "(choose)" : c.piece.c_str()) && !pieces.empty()) {
      const auto at = std::find(pieces.begin(), pieces.end(), c.piece);
      const std::size_t next = at == pieces.end() ? 0 : (static_cast<std::size_t>(at - pieces.begin()) + 1) % pieces.size();
      c.piece = pieces[next];
      changed = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("steps");
    c.vector.resize(axes, 0);
    for (std::size_t a = 0; a < axes; ++a) {
      ImGui::SameLine();
      char id[24];
      std::snprintf(id, sizeof(id), "ax%zu", a);
      if (stepper(id, c.vector[a], -9, 9, t, scale_, px(30.0f))) changed = true;
      ImGui::SameLine(0.0f, px(2.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::TextUnformatted(axisNames[a].c_str());
      ImGui::PopStyleColor();
    }
    if (ImGui::Checkbox("the mover must never have moved", &c.moverUnmoved)) changed = true;
    if (editor->orientationAxis() >= 0 &&
        ImGui::Checkbox("flip along the orientation axis for the other side", &c.flip)) {
      changed = true;
    }

    // The partner: which kinds, and unmoved.
    ImGui::TextUnformatted("partner is found among");
    ImGui::PushID("partners");
    for (const std::string& name : pieces) {
      ImGui::SameLine();
      const auto found = std::find(c.partners.begin(), c.partners.end(), name);
      const bool on = found != c.partners.end();
      ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, on ? 1.0f : 0.45f));
      ImGui::PushStyleColor(ImGuiCol_Text, col(on ? t.ember : t.boneDim));
      if (ImGui::SmallButton(name.c_str())) {
        if (on) {
          c.partners.erase(found);
        } else {
          c.partners.push_back(name);
        }
        changed = true;
      }
      ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    if (ImGui::Checkbox("the partner must never have moved", &c.partnerUnmoved)) changed = true;

    // The cells that must not be attacked.
    ImGui::TextUnformatted("none of these may be attacked:");
    ImGui::PushID("safe");
    for (const char* cell : {"from", "path", "to"}) {
      ImGui::SameLine();
      const auto found = std::find(c.safe.begin(), c.safe.end(), cell);
      const bool on = found != c.safe.end();
      ImGui::PushStyleColor(ImGuiCol_Button, col(t.panelHi, on ? 1.0f : 0.45f));
      ImGui::PushStyleColor(ImGuiCol_Text, col(on ? t.ember : t.boneDim));
      if (ImGui::SmallButton(cell)) {
        if (on) {
          c.safe.erase(found);
        } else {
          c.safe.push_back(cell);
        }
        changed = true;
      }
      ImGui::PopStyleColor(2);
    }
    ImGui::PopID();
    ImGui::PopFont();
    ImGui::PopID();
  }
  if (removeAt < list.size()) {
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(removeAt));
    changed = true;
  }

  sectionHead("ADD", t, fontSmall_, scale_);
  if (button("+ COMPOUND MOVE", t, px(190), false, true, true, display)) {
    app::Editor::CompoundInfo c;
    c.name = "compound " + std::to_string(list.size() + 1);
    c.piece = pieces.empty() ? std::string{} : pieces.front();
    c.vector.assign(axes, 0);
    if (!c.vector.empty()) c.vector[0] = 2;
    c.safe = {"from", "path", "to"};
    list.push_back(std::move(c));
    changed = true;
  }
  ImGui::SameLine();
  if (button("+ STANDARD CASTLING", t, px(220), false, true, true, display)) {
    // The variant's royal piece and a piece called rook, else the first two pieces.
    std::string king = pieces.empty() ? std::string{} : pieces.back();
    std::string rook = pieces.size() > 1 ? pieces[pieces.size() - 2] : king;
    for (const std::string& n : pieces) {
      if (n == "king") king = n;
      if (n == "rook") rook = n;
    }
    const Result<void> ok = editor->addCastling(king, rook);
    moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
    list = editor->compounds();
    changed = false;
  }
  if (changed) {
    const Result<void> ok = editor->setCompounds(list);
    moveNote_ = ok.has_value() ? std::string{} : ok.error().message;
  }
  if (!moveNote_.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", moveNote_.c_str());
    ImGui::PopStyleColor();
  }
}

}  // namespace cb::render
