// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/variant_doc.hpp"

#include <cstdint>
#include <sstream>
#include <string_view>
#include <utility>

#include <toml++/toml.hpp>

#include "io/atom_toml.hpp"
#include "io/model_toml.hpp"
#include "io/moveset_library.hpp"

namespace cb {
namespace {

toml::table* findPieceTable(toml::table& root, std::string_view name) {
  auto* arr = root["piece"].as_array();
  if (arr == nullptr) return nullptr;
  for (auto&& node : *arr) {
    auto* t = node.as_table();
    if (t != nullptr && (*t)["name"].value_or(std::string{}) == name) return t;
  }
  return nullptr;
}

/// The orientation axis as (index, extent), or (-1, 0) when the variant has none. The
/// index is what `Region.axis` wants; the extent is what mirrors a `from_rank` for
/// Black, exactly as the loader does it.
std::pair<int, std::int16_t> orientationAxisOf(const toml::table& root) {
  const auto oa = root["orientation_axis"].value<std::string>();
  if (!oa.has_value()) return {-1, 0};
  const auto* axes = root["axis"].as_array();
  if (axes == nullptr) return {-1, 0};
  int index = 0;
  for (const auto& node : *axes) {
    const auto* t = node.as_table();
    if (t == nullptr) continue;
    if ((*t)["name"].value_or(std::string{}) == *oa) {
      return {index, static_cast<std::int16_t>((*t)["extent"].value_or<std::int64_t>(0))};
    }
    ++index;
  }
  return {-1, 0};
}

std::vector<std::string> axisNamesOf(const toml::table& root) {
  std::vector<std::string> names;
  if (const auto* axes = root["axis"].as_array()) {
    for (const auto& node : *axes) {
      const auto* t = node.as_table();
      names.push_back(t != nullptr ? (*t)["name"].value_or(std::string{}) : std::string{});
    }
  }
  return names;
}

/// "ep", then the variant's own tags, in file order - the indices atoms refer to.
std::vector<std::string> markTagNamesOf(const toml::table& root) {
  std::vector<std::string> names{"ep"};
  if (const auto* tags = root["mark"].as_array()) {
    for (const auto& node : *tags) {
      const auto* t = node.as_table();
      const std::string n = t != nullptr ? (*t)["name"].value_or(std::string{}) : std::string{};
      if (!n.empty() && n != "ep") names.push_back(n);
    }
  }
  return names;
}

std::string_view modeWord(MoveMode m) {
  switch (m) {
    case MoveMode::Slide:
      return "slide";
    case MoveMode::Leap:
      return "leap";
    case MoveMode::Hop:
      return "hop";
  }
  return "leap";
}

std::string_view captureWord(CapturePolicy c) {
  switch (c) {
    case CapturePolicy::May:
      return "may";
    case CapturePolicy::Must:
      return "must";
    case CapturePolicy::Cannot:
      return "cannot";
  }
  return "may";
}


}  // namespace

struct VariantDoc::Impl {
  toml::table table;
};

VariantDoc::VariantDoc() = default;
VariantDoc::VariantDoc(VariantDoc&&) noexcept = default;
VariantDoc& VariantDoc::operator=(VariantDoc&&) noexcept = default;
VariantDoc::~VariantDoc() = default;

Result<VariantDoc> VariantDoc::parse(std::string_view text, std::string_view sourceName) {
  VariantDoc doc;
  doc.impl_ = std::make_unique<Impl>();
  try {
    doc.impl_->table = toml::parse(text, sourceName);
  } catch (const toml::parse_error& e) {
    return fail(ErrorCode::ParseError, std::string(e.description()),
                static_cast<int>(e.source().begin.line));
  }
  return doc;
}

std::string VariantDoc::serialize() const {
  std::ostringstream out;
  // toml++'s own formatter, so the writer and the parser agree by construction rather
  // than by a hand-rolled emitter that drifts from the dialect the loader accepts.
  out << toml::toml_formatter{impl_->table};
  return out.str();
}

std::vector<std::string> VariantDoc::pieceNames() const {
  std::vector<std::string> names;
  const auto* arr = impl_->table["piece"].as_array();
  if (arr == nullptr) return names;
  for (const auto& node : *arr) {
    const auto* t = node.as_table();
    if (t != nullptr) names.push_back((*t)["name"].value_or(std::string{}));
  }
  return names;
}

Result<std::vector<MoveAtom>> VariantDoc::pieceAtoms(std::string_view piece) const {
  const toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  const auto [orientAxis, orientExtent] = orientationAxisOf(impl_->table);
  std::vector<MoveAtom> out;
  // Moveset text first, as the loader reads it, then any explicit moves.
  if (const auto text = (*pt)["moves"].value<std::string>()) {
    auto atoms = evalMoveset(*text, movesetContextFor(impl_->table));
    if (!atoms.has_value()) return fail(atoms.error().code, atoms.error().message, atoms.error().line);
    out = std::move(*atoms);
  }
  const auto* moves = (*pt)["move"].as_array();
  if (moves == nullptr) return out;
  for (const auto& node : *moves) {
    const auto* mt = node.as_table();
    if (mt == nullptr) continue;
    MoveAtom a;
    {
      std::string why;
      if (!readAtomKind(*mt, axisNamesOf(impl_->table), orientAxis, markTagNamesOf(impl_->table), a, why)) continue;
    }

    const std::string maxText = (*mt)["max"].value_or(std::string{});
    if (maxText == "inf") {
      a.maxK = kUnlimited;
    } else if (const auto mk = (*mt)["max"].value<std::int64_t>()) {
      a.maxK = static_cast<std::uint32_t>(*mk);
    }
    a.minK = static_cast<std::uint32_t>((*mt)["min"].value_or<std::int64_t>(1));

    const std::string mode = (*mt)["mode"].value_or(std::string{"leap"});
    a.mode = mode == "slide" ? MoveMode::Slide
             : mode == "hop" ? MoveMode::Hop
                             : MoveMode::Leap;
    const std::string cap = (*mt)["capture"].value_or(std::string{"may"});
    a.capture = cap == "must"     ? CapturePolicy::Must
                : cap == "cannot" ? CapturePolicy::Cannot
                                  : CapturePolicy::May;
    a.oriented = (*mt)["forward"].value_or(false);
    a.leavesEnPassant = (*mt)["leaves_en_passant"].value_or(false);
    a.unmoved = (*mt)["unmoved"].value_or(false);

    if (const auto home = (*mt)["from_rank"].value<std::int64_t>()) {
      if (orientAxis >= 0) {
        const auto white = static_cast<std::int16_t>(*home);
        a.fromRegion[static_cast<std::size_t>(Color::White)] = Region{orientAxis, white};
        a.fromRegion[static_cast<std::size_t>(Color::Black)] =
            Region{orientAxis, static_cast<std::int16_t>(orientExtent - 1 - white)};
      }
    }

    // An atom the engine would refuse is still the author's atom: it stays in the list, so the
    // editor can show it (red) and let it be fixed, instead of it silently vanishing.
    auto canon = MoveAtom::canonicalize(a);
    out.push_back(canon.has_value() ? *canon : a);
  }
  return out;
}

Result<void> VariantDoc::setPieceAtoms(std::string_view piece,
                                       const std::vector<MoveAtom>& atoms) {
  toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  toml::array moves;
  for (const MoveAtom& a : atoms) {
    toml::table m;
    writeAtomKind(a, axisNamesOf(impl_->table), markTagNamesOf(impl_->table), m);
    if (a.minK != 1) m.insert("min", static_cast<std::int64_t>(a.minK));
    if (a.maxK == kUnlimited) {
      m.insert("max", std::string{"inf"});
    } else if (a.maxK != 1) {
      m.insert("max", static_cast<std::int64_t>(a.maxK));
    }
    if (a.mode != MoveMode::Leap) m.insert("mode", std::string(modeWord(a.mode)));
    if (a.capture != CapturePolicy::May) {
      m.insert("capture", std::string(captureWord(a.capture)));
    }
    if (a.oriented) m.insert("forward", true);
    if (a.fromRegion[static_cast<std::size_t>(Color::White)].active()) {
      m.insert("from_rank",
               static_cast<std::int64_t>(
                   a.fromRegion[static_cast<std::size_t>(Color::White)].value));
    }
    if (a.leavesEnPassant) m.insert("leaves_en_passant", true);
    if (a.unmoved) m.insert("unmoved", true);
    moves.push_back(std::move(m));
  }
  pt->insert_or_assign("move", std::move(moves));
  pt->erase("moves");  // the list is now the whole truth
  return {};
}

std::vector<VariantDoc::CompoundInfo> VariantDoc::compounds() const {
  std::vector<CompoundInfo> out;
  const auto* arr = impl_->table["compound"].as_array();
  if (arr == nullptr) return out;
  for (const auto& node : *arr) {
    const auto* t = node.as_table();
    if (t == nullptr) continue;
    CompoundInfo c;
    c.name = (*t)["name"].value_or(std::string{});
    c.fen = (*t)["fen"].value_or(std::string{});
    c.piece = (*t)["piece"].value_or(std::string{});
    if (const auto* vec = (*t)["vector"].as_array()) {
      for (const auto& x : *vec) c.vector.push_back(static_cast<int>(x.value_or<std::int64_t>(0)));
    }
    c.flip = (*t)["flip"].value_or(false);
    c.moverUnmoved = (*t)["unmoved"].value_or(true);
    c.partnerUnmoved = (*t)["partner_unmoved"].value_or(true);
    const auto strings = [&](const char* key, std::vector<std::string>& into) {
      if (const auto* a = (*t)[key].as_array()) {
        for (const auto& x : *a) into.push_back(x.value_or(std::string{}));
      }
    };
    strings("partner", c.partners);
    strings("safe", c.safe);
    out.push_back(std::move(c));
  }
  return out;
}

Result<void> VariantDoc::setCompounds(const std::vector<CompoundInfo>& list) {
  if (list.empty()) {
    impl_->table.erase("compound");
    return {};
  }
  toml::array arr;
  for (const CompoundInfo& c : list) {
    toml::table t;
    t.insert("name", c.name);
    t.insert("piece", c.piece);
    toml::array vec;
    for (const int x : c.vector) vec.push_back(static_cast<std::int64_t>(x));
    t.insert("vector", std::move(vec));
    if (c.flip) t.insert("flip", true);
    if (!c.moverUnmoved) t.insert("unmoved", false);
    toml::array partners;
    for (const std::string& p : c.partners) partners.push_back(p);
    t.insert("partner", std::move(partners));
    if (!c.partnerUnmoved) t.insert("partner_unmoved", false);
    toml::array safe;
    for (const std::string& s : c.safe) safe.push_back(s);
    t.insert("safe", std::move(safe));
    if (!c.fen.empty()) t.insert("fen", c.fen);
    arr.push_back(std::move(t));
  }
  impl_->table.insert_or_assign("compound", std::move(arr));
  return {};
}

std::vector<VariantDoc::MarkTagInfo> VariantDoc::markTags() const {
  std::vector<MarkTagInfo> out;
  const auto* tags = impl_->table["mark"].as_array();
  if (tags == nullptr) return out;
  for (const auto& node : *tags) {
    const auto* t = node.as_table();
    if (t == nullptr) continue;
    MarkTagInfo info;
    info.name = (*t)["name"].value_or(std::string{});
    info.blocks = (*t)["blocks"].value_or(false);
    if (!info.name.empty() && info.name != "ep") out.push_back(std::move(info));
  }
  return out;
}

Result<void> VariantDoc::setMarkTags(const std::vector<MarkTagInfo>& tags) {
  if (tags.empty()) {
    impl_->table.erase("mark");
    return {};
  }
  toml::array arr;
  for (const MarkTagInfo& tag : tags) {
    toml::table t;
    t.insert("name", tag.name);
    if (tag.blocks) t.insert("blocks", true);
    arr.push_back(std::move(t));
  }
  impl_->table.insert_or_assign("mark", std::move(arr));
  return {};
}

Result<std::string> VariantDoc::pieceMovesText(std::string_view piece) const {
  const toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  if (const auto text = (*pt)["moves"].value<std::string>(); text && !(*pt)["move"].is_array()) {
    return *text;
  }
  auto atoms = pieceAtoms(piece);
  if (!atoms.has_value()) return fail(atoms.error().code, atoms.error().message);
  const MovesetContext ctx = movesetContextFor(impl_->table);
  return printMoveset(*atoms, ctx.axisNames, ctx.orientAxis, ctx.markTags);
}

Result<void> VariantDoc::setPieceMovesText(std::string_view piece, std::string_view text) {
  toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  auto atoms = evalMoveset(text, movesetContextFor(impl_->table));
  if (!atoms.has_value()) return fail(atoms.error().code, atoms.error().message, atoms.error().line);
  if (atoms->empty()) return fail(ErrorCode::ValidationError, "a piece needs at least one move");
  pt->insert_or_assign("moves", std::string(text));
  pt->erase("move");
  return {};
}

std::string VariantDoc::description() const {
  return impl_->table["description"].value_or(std::string{});
}

void VariantDoc::setDescription(std::string text) {
  impl_->table.insert_or_assign("description", std::move(text));
}

std::string VariantDoc::name() const {
  return impl_->table["name"].value_or(std::string{});
}

void VariantDoc::setName(std::string name) {
  impl_->table.insert_or_assign("name", std::move(name));
}

int VariantDoc::orientationAxis() const {
  return orientationAxisOf(impl_->table).first;
}

namespace {

toml::table* axisAt(toml::table& root, std::size_t index) {
  auto* arr = root["axis"].as_array();
  if (arr == nullptr || index >= arr->size()) return nullptr;
  return (*arr)[index].as_table();
}

std::string_view kindWord(AxisKind kind) {
  switch (kind) {
    case AxisKind::Spatial:
      return "spatial";
    case AxisKind::Temporal:
      return "temporal";
    case AxisKind::Multiverse:
      return "multiverse";
  }
  return "spatial";
}

}  // namespace

std::vector<VariantDoc::AxisInfo> VariantDoc::axes() const {
  std::vector<AxisInfo> out;
  const auto* arr = impl_->table["axis"].as_array();
  if (arr == nullptr) return out;

  std::vector<std::string> periodicAxes;
  if (const auto* geom = impl_->table["geometry"].as_table()) {
    if (const auto* idents = (*geom)["identify"].as_array()) {
      for (const auto& node : *idents) {
        const auto* t = node.as_table();
        if (t == nullptr) continue;
        if ((*t)["kind"].value_or(std::string{"periodic"}) == "periodic") {
          periodicAxes.push_back((*t)["axis"].value_or(std::string{}));
        }
      }
    }
  }

  for (const auto& node : *arr) {
    const auto* t = node.as_table();
    if (t == nullptr) continue;
    AxisInfo info;
    info.name = (*t)["name"].value_or(std::string{});
    info.extent = static_cast<int>((*t)["extent"].value_or<std::int64_t>(0));
    const std::string kind = (*t)["kind"].value_or(std::string{"spatial"});
    info.kind = kind == "temporal"     ? AxisKind::Temporal
                : kind == "multiverse" ? AxisKind::Multiverse
                                       : AxisKind::Spatial;
    info.pitch = static_cast<int>((*t)["pitch"].value_or<std::int64_t>(1));
    for (const std::string& name : periodicAxes) {
      if (name == info.name) info.periodic = true;
    }
    out.push_back(std::move(info));
  }
  return out;
}

Result<void> VariantDoc::setAxisExtent(std::size_t index, int extent) {
  toml::table* axis = axisAt(impl_->table, index);
  if (axis == nullptr) return fail(ErrorCode::OutOfRange, "no such axis");
  if (extent < 1)
    return fail(ErrorCode::ValidationError, "an axis needs at least one cell");
  axis->insert_or_assign("extent", static_cast<std::int64_t>(extent));
  return {};
}

Result<void> VariantDoc::setAxisKind(std::size_t index, AxisKind kind) {
  toml::table* axis = axisAt(impl_->table, index);
  if (axis == nullptr) return fail(ErrorCode::OutOfRange, "no such axis");
  axis->insert_or_assign("kind", std::string(kindWord(kind)));
  return {};
}

Result<void> VariantDoc::setAxisPeriodic(std::size_t index, bool periodic) {
  toml::table* axis = axisAt(impl_->table, index);
  if (axis == nullptr) return fail(ErrorCode::OutOfRange, "no such axis");
  const std::string name = (*axis)["name"].value_or(std::string{});

  // Keep every identification that is not a periodic glue of this axis.
  toml::array kept;
  if (const auto* geom = impl_->table["geometry"].as_table()) {
    if (const auto* idents = (*geom)["identify"].as_array()) {
      for (const auto& node : *idents) {
        const auto* t = node.as_table();
        if (t == nullptr) continue;
        const bool isThisPeriodic =
            (*t)["axis"].value_or(std::string{}) == name &&
            (*t)["kind"].value_or(std::string{"periodic"}) == "periodic";
        if (!isThisPeriodic) kept.push_back(*t);
      }
    }
  }
  if (periodic) {
    toml::table glue;
    glue.insert("axis", name);
    glue.insert("kind", std::string("periodic"));
    kept.push_back(std::move(glue));
  }

  toml::table geom;
  if (const auto* existing = impl_->table["geometry"].as_table()) geom = *existing;
  geom.insert_or_assign("identify", std::move(kept));
  impl_->table.insert_or_assign("geometry", std::move(geom));
  return {};
}

Result<void> VariantDoc::addAxis() {
  auto* arr = impl_->table["axis"].as_array();
  if (arr == nullptr) return fail(ErrorCode::ValidationError, "the board has no axes");
  int n = static_cast<int>(arr->size()) + 1;
  std::string name;
  for (;; ++n) {
    name = "axis" + std::to_string(n);
    bool taken = false;
    for (const auto& node : *arr) {
      if (const auto* t = node.as_table()) {
        if ((*t)["name"].value_or(std::string{}) == name) taken = true;
      }
    }
    if (!taken) break;
  }
  toml::table axis;
  axis.insert("name", name);
  axis.insert("extent", static_cast<std::int64_t>(4));
  arr->push_back(std::move(axis));
  return {};
}

Result<void> VariantDoc::removeAxis(std::size_t index) {
  auto* arr = impl_->table["axis"].as_array();
  if (arr == nullptr || index >= arr->size()) {
    return fail(ErrorCode::OutOfRange, "no such axis");
  }
  if (arr->size() <= 1) {
    return fail(ErrorCode::ValidationError, "a board needs at least one axis");
  }
  arr->erase(arr->begin() + static_cast<std::ptrdiff_t>(index));
  return {};
}

}  // namespace cb

// ---------------------------------------------------------------------------
// Piece identity, the piece list, and authored bodies.
// ---------------------------------------------------------------------------

namespace cb {
namespace {

toml::array* pieceArray(toml::table& root) {
  return root["piece"].as_array();
}

void dropPromotionsTo(toml::table& root, std::string_view name) {
  auto* arr = pieceArray(root);
  if (arr == nullptr) return;
  for (auto&& node : *arr) {
    auto* t = node.as_table();
    if (t == nullptr) continue;
    auto* list = (*t)["promotes_to"].as_array();
    if (list == nullptr) continue;
    toml::array kept;
    for (const auto& x : *list) {
      if (x.value_or(std::string{}) != name) kept.push_back(*x.as_string());
    }
    if (kept.empty()) {
      t->erase("promotes_to");
    } else {
      t->insert_or_assign("promotes_to", std::move(kept));
    }
  }
}

}  // namespace

Result<VariantDoc::PieceInfo> VariantDoc::pieceInfo(std::string_view piece) const {
  const toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  PieceInfo info;
  info.name = std::string(piece);
  info.symbol = (*pt)["symbol"].value_or(std::string{});
  info.royal = (*pt)["royal"].value_or(false);
  info.resetsDrawClock = (*pt)["resets_draw_clock"].value_or(false);
  info.shape = (*pt)["shape"].value_or(std::string{});
  if (const auto h = (*pt)["height"].value<double>()) {
    info.heightPermille = static_cast<int>(*h * 1000.0 + 0.5);
  }
  if (const auto* list = (*pt)["promotes_to"].as_array()) {
    for (const auto& x : *list) info.promotesTo.push_back(x.value_or(std::string{}));
  }
  return info;
}

Result<void> VariantDoc::setPieceInfo(std::string_view piece, const PieceInfo& info) {
  toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  if (info.symbol.size() != 1) {
    return fail(ErrorCode::ValidationError, "a piece's symbol is a single character");
  }
  pt->insert_or_assign("symbol", info.symbol);
  const auto flag = [&](const char* key, bool on) {
    if (on) {
      pt->insert_or_assign(key, true);
    } else {
      pt->erase(key);
    }
  };
  flag("royal", info.royal);
  flag("resets_draw_clock", info.resetsDrawClock);
  if (info.shape.empty()) {
    pt->erase("shape");
  } else {
    pt->insert_or_assign("shape", info.shape);
  }
  if (info.heightPermille <= 0) {
    pt->erase("height");
  } else {
    pt->insert_or_assign("height", static_cast<double>(info.heightPermille) / 1000.0);
  }
  if (info.promotesTo.empty()) {
    pt->erase("promotes_to");
  } else {
    toml::array list;
    for (const std::string& n : info.promotesTo) list.push_back(n);
    pt->insert_or_assign("promotes_to", std::move(list));
  }
  return {};
}

Result<void> VariantDoc::renamePiece(std::string_view piece, std::string_view to) {
  toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  if (to.empty()) return fail(ErrorCode::ValidationError, "a piece needs a name");
  if (piece != to && findPieceTable(impl_->table, to) != nullptr) {
    return fail(ErrorCode::ValidationError,
                "a piece is already called '" + std::string(to) + "'");
  }
  pt->insert_or_assign("name", std::string(to));
  if (auto* arr = pieceArray(impl_->table)) {
    for (auto&& node : *arr) {
      auto* t = node.as_table();
      if (t == nullptr) continue;
      auto* list = (*t)["promotes_to"].as_array();
      if (list == nullptr) continue;
      toml::array renamed;
      for (const auto& x : *list) {
        const std::string n = x.value_or(std::string{});
        renamed.push_back(n == piece ? std::string(to) : n);
      }
      t->insert_or_assign("promotes_to", std::move(renamed));
    }
  }
  return {};
}

Result<void> VariantDoc::addPiece(std::string_view name, std::string_view symbol,
                                  std::string_view base) {
  if (name.empty()) return fail(ErrorCode::ValidationError, "a piece needs a name");
  if (symbol.size() != 1) {
    return fail(ErrorCode::ValidationError, "a piece's symbol is a single character");
  }
  if (findPieceTable(impl_->table, name) != nullptr) {
    return fail(ErrorCode::ValidationError,
                "a piece is already called '" + std::string(name) + "'");
  }
  toml::table fresh;
  if (!base.empty()) {
    const toml::table* from = findPieceTable(impl_->table, base);
    if (from == nullptr) {
      return fail(ErrorCode::ValidationError,
                  "no piece is called '" + std::string(base) + "'");
    }
    fresh = *from;
    // A copy is a new piece, not a second king: royalty and promotions are the author's
    // to decide again.
    fresh.erase("royal");
    fresh.erase("promotes_to");
  } else {
    toml::table move;
    toml::array vec;
    vec.push_back(static_cast<std::int64_t>(1));
    move.insert("vector", std::move(vec));
    toml::array moves;
    moves.push_back(std::move(move));
    fresh.insert("move", std::move(moves));
  }
  fresh.insert_or_assign("name", std::string(name));
  fresh.insert_or_assign("symbol", std::string(symbol));
  auto* arr = pieceArray(impl_->table);
  if (arr == nullptr) {
    impl_->table.insert_or_assign("piece", toml::array{});
    arr = pieceArray(impl_->table);
  }
  arr->push_back(std::move(fresh));
  return {};
}

Result<void> VariantDoc::removePiece(std::string_view piece) {
  auto* arr = pieceArray(impl_->table);
  if (arr == nullptr) return fail(ErrorCode::ValidationError, "no pieces to remove");
  if (arr->size() <= 1) {
    return fail(ErrorCode::ValidationError, "a variant keeps at least one piece");
  }
  for (std::size_t i = 0; i < arr->size(); ++i) {
    const auto* t = (*arr)[i].as_table();
    if (t != nullptr && (*t)["name"].value_or(std::string{}) == piece) {
      arr->erase(arr->cbegin() + static_cast<std::ptrdiff_t>(i));
      dropPromotionsTo(impl_->table, piece);
      return {};
    }
  }
  return fail(ErrorCode::ValidationError, "no piece is called '" + std::string(piece) + "'");
}

Result<std::optional<assets::PieceModel>> VariantDoc::pieceBody(
    std::string_view piece) const {
  const toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  const auto* body = (*pt)["body"].as_table();
  if (body == nullptr) return std::optional<assets::PieceModel>{};
  return bodyFromToml(*body);
}

Result<void> VariantDoc::setPieceBody(std::string_view piece,
                                      const std::optional<assets::PieceModel>& body) {
  toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  if (!body.has_value()) {
    pt->erase("body");
    return {};
  }
  toml::table t = bodyToToml(*body);
  pt->insert_or_assign("body", std::move(t));
  return {};
}

Result<std::optional<assets::IconModel>> VariantDoc::pieceIcon(
    std::string_view piece) const {
  const toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  const auto* icon = (*pt)["icon"].as_table();
  if (icon == nullptr) return std::optional<assets::IconModel>{};
  return iconFromToml(*icon);
}

Result<void> VariantDoc::setPieceIcon(std::string_view piece,
                                      const std::optional<assets::IconModel>& icon) {
  toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  if (!icon.has_value()) {
    pt->erase("icon");
    return {};
  }
  toml::table t = iconToToml(*icon);
  pt->insert_or_assign("icon", std::move(t));
  return {};
}


// ---------------------------------------------------------------------------
// Custom fields and rules.
// ---------------------------------------------------------------------------

std::vector<VariantDoc::FieldInfo> VariantDoc::fields(bool pieceFields) const {
  std::vector<FieldInfo> out;
  const auto* arr = impl_->table[pieceFields ? "piece_field" : "cell_field"].as_array();
  if (arr == nullptr) return out;
  for (const auto& node : *arr) {
    const auto* t = node.as_table();
    if (t == nullptr) continue;
    FieldInfo f;
    f.name = (*t)["name"].value_or(std::string{});
    f.defaultValue = static_cast<int>((*t)["default"].value_or<std::int64_t>(0));
    f.minValue = static_cast<int>((*t)["min"].value_or<std::int64_t>(-32768));
    f.maxValue = static_cast<int>((*t)["max"].value_or<std::int64_t>(32767));
    f.hashed = (*t)["hashed"].value_or(true);
    out.push_back(std::move(f));
  }
  return out;
}

Result<void> VariantDoc::setFields(bool pieceFields, const std::vector<FieldInfo>& list) {
  const char* key = pieceFields ? "piece_field" : "cell_field";
  if (list.empty()) {
    impl_->table.erase(key);
    return {};
  }
  toml::array arr;
  for (const FieldInfo& f : list) {
    toml::table t;
    t.insert("name", f.name);
    t.insert("default", static_cast<std::int64_t>(f.defaultValue));
    t.insert("min", static_cast<std::int64_t>(f.minValue));
    t.insert("max", static_cast<std::int64_t>(f.maxValue));
    if (!f.hashed) t.insert("hashed", false);
    arr.push_back(std::move(t));
  }
  impl_->table.insert_or_assign(key, std::move(arr));
  return {};
}

std::vector<VariantDoc::RuleInfo> VariantDoc::rules() const {
  std::vector<RuleInfo> out;
  const auto* arr = impl_->table["rule"].as_array();
  if (arr == nullptr) return out;
  for (const auto& node : *arr) {
    const auto* t = node.as_table();
    if (t == nullptr) continue;
    RuleInfo r;
    r.name = (*t)["name"].value_or(std::string{});
    r.when = (*t)["when"].value_or(std::string{"on_move_end"});
    r.condition = (*t)["if"].value_or(std::string{});
    if (const auto* effects = (*t)["effect"].as_array()) {
      for (const auto& enode : *effects) {
        const auto* et = enode.as_table();
        if (et == nullptr) continue;
        EffectInfo e;
        e.op = (*et)["op"].value_or(std::string{});
        for (const auto& [key, value] : *et) {
          const std::string k(key.str());
          if (k == "op") continue;
          if (const auto* str = value.as_string()) {
            e.args.emplace_back(k, str->get());
          } else if (const auto* num = value.as_integer()) {
            e.args.emplace_back(k, std::to_string(num->get()));
          }
        }
        r.effects.push_back(std::move(e));
      }
    }
    out.push_back(std::move(r));
  }
  return out;
}

Result<void> VariantDoc::setRules(const std::vector<RuleInfo>& list) {
  if (list.empty()) {
    impl_->table.erase("rule");
    return {};
  }
  toml::array arr;
  for (const RuleInfo& r : list) {
    toml::table t;
    t.insert("name", r.name);
    t.insert("when", r.when);
    if (!r.condition.empty()) t.insert("if", r.condition);
    toml::array effects;
    for (const EffectInfo& e : r.effects) {
      toml::table et;
      et.insert("op", e.op);
      for (const auto& [key, value] : e.args) {
        // The one numeric argument is written as a number; everything else is the file's
        // own text (an expression, a piece or field name, an outcome).
        if (key == "radius" && !value.empty() &&
            value.find_first_not_of("0123456789") == std::string::npos) {
          et.insert(key, static_cast<std::int64_t>(std::stoll(value)));
        } else if (!value.empty()) {
          et.insert(key, value);
        }
      }
      effects.push_back(std::move(et));
    }
    t.insert("effect", std::move(effects));
    arr.push_back(std::move(t));
  }
  impl_->table.insert_or_assign("rule", std::move(arr));
  return {};
}

}  // namespace cb
