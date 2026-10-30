// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/variant_doc.hpp"

#include <charconv>
#include <cstdint>
#include <sstream>
#include <string_view>
#include <utility>

#include <toml++/toml.hpp>

#include "io/piece_model_toml.hpp"

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
  const auto* moves = (*pt)["move"].as_array();
  if (moves == nullptr) return out;
  for (const auto& node : *moves) {
    const auto* mt = node.as_table();
    if (mt == nullptr) continue;
    MoveAtom a;
    if (const auto* vec = (*mt)["vector"].as_array()) {
      for (const auto& v : *vec) {
        if (const auto n = v.value<std::int64_t>()) {
          a.mags.push(static_cast<std::int16_t>(*n));
        }
      }
    }
    if (a.mags.empty()) continue;

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

    if (const auto home = (*mt)["from_rank"].value<std::int64_t>()) {
      if (orientAxis >= 0) {
        const auto white = static_cast<std::int16_t>(*home);
        a.fromRegion[static_cast<std::size_t>(Color::White)] = Region{orientAxis, white};
        a.fromRegion[static_cast<std::size_t>(Color::Black)] =
            Region{orientAxis, static_cast<std::int16_t>(orientExtent - 1 - white)};
      }
    }

    auto canon = MoveAtom::canonicalize(a);
    if (canon.has_value()) out.push_back(*canon);
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
    toml::array vec;
    for (const auto mag : a.mags) vec.push_back(static_cast<std::int64_t>(mag));
    m.insert("vector", std::move(vec));
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
    moves.push_back(std::move(m));
  }
  pt->insert_or_assign("move", std::move(moves));
  return {};
}

Result<std::optional<assets::PieceModel>> VariantDoc::pieceModel(
    std::string_view piece) const {
  const toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  const auto* mt = (*pt)["model"].as_table();
  if (mt == nullptr) return std::optional<assets::PieceModel>{};
  auto m = pieceModelFromToml(*mt);
  if (!m.has_value()) return std::unexpected(m.error());
  return std::optional<assets::PieceModel>{std::move(*m)};
}

Result<std::optional<assets::IconModel>> VariantDoc::pieceIcon(
    std::string_view piece) const {
  const toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  const auto* it = (*pt)["icon"].as_table();
  if (it == nullptr) return std::optional<assets::IconModel>{};
  auto m = pieceIconFromToml(*it);
  if (!m.has_value()) return std::unexpected(m.error());
  return std::optional<assets::IconModel>{std::move(*m)};
}

Result<void> VariantDoc::setPieceModel(std::string_view piece,
                                       const std::optional<assets::PieceModel>& m) {
  toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  if (!m) {
    pt->erase("model");
    return {};
  }
  if (auto ok = assets::validate(*m); !ok.has_value()) return ok;
  pt->insert_or_assign("model", pieceModelToToml(*m));
  return {};
}

Result<void> VariantDoc::setPieceIcon(std::string_view piece,
                                      const std::optional<assets::IconModel>& m) {
  toml::table* pt = findPieceTable(impl_->table, piece);
  if (pt == nullptr) {
    return fail(ErrorCode::ValidationError,
                "no piece is called '" + std::string(piece) + "'");
  }
  if (!m) {
    pt->erase("icon");
    return {};
  }
  if (auto ok = assets::validate(*m); !ok.has_value()) return ok;
  pt->insert_or_assign("icon", pieceIconToToml(*m));
  return {};
}

std::vector<VariantDoc::FieldInfo> VariantDoc::fields(bool cellFields) const {
  std::vector<FieldInfo> out;
  const auto* arr = impl_->table[cellFields ? "cell_field" : "piece_field"].as_array();
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

void VariantDoc::setFields(bool cellFields, const std::vector<FieldInfo>& fields) {
  const char* key = cellFields ? "cell_field" : "piece_field";
  if (fields.empty()) {
    impl_->table.erase(key);
    return;
  }
  toml::array arr;
  for (const FieldInfo& f : fields) {
    toml::table t;
    t.insert("name", f.name);
    t.insert("default", static_cast<std::int64_t>(f.defaultValue));
    t.insert("min", static_cast<std::int64_t>(f.minValue));
    t.insert("max", static_cast<std::int64_t>(f.maxValue));
    if (!f.hashed) t.insert("hashed", false);
    arr.push_back(std::move(t));
  }
  impl_->table.insert_or_assign(key, std::move(arr));
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
    r.when = (*t)["when"].value_or(std::string{});
    r.condition = (*t)["if"].value_or(std::string{});
    if (const auto* effects = (*t)["effect"].as_array()) {
      for (const auto& enode : *effects) {
        const auto* et = enode.as_table();
        if (et == nullptr) continue;
        EffectInfo e;
        e.op = (*et)["op"].value_or(std::string{});
        for (const auto& [k, v] : *et) {
          if (k.str() == "op") continue;
          std::string text;
          if (const auto sv = v.value<std::string>()) {
            text = *sv;
          } else if (const auto iv = v.value<std::int64_t>()) {
            text = std::to_string(*iv);
          }
          e.args.emplace_back(std::string(k.str()), std::move(text));
        }
        r.effects.push_back(std::move(e));
      }
    }
    out.push_back(std::move(r));
  }
  return out;
}

void VariantDoc::setRules(const std::vector<RuleInfo>& rules) {
  if (rules.empty()) {
    impl_->table.erase("rule");
    return;
  }
  toml::array arr;
  for (const RuleInfo& r : rules) {
    toml::table t;
    t.insert("name", r.name);
    t.insert("when", r.when);
    if (!r.condition.empty()) t.insert("if", r.condition);
    toml::array effects;
    for (const EffectInfo& e : r.effects) {
      toml::table et;
      et.insert("op", e.op);
      for (const auto& [k, v] : e.args) {
        if (v.empty()) continue;  // an optional key left blank is an absent key
        // The one integer key in the catalogue; everything else is a word or an
        // expression, and the loader reads those as strings.
        std::int64_t n = 0;
        const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
        if (k == "radius" && ec == std::errc{} && end == v.data() + v.size()) {
          et.insert(k, n);
        } else {
          et.insert(k, v);
        }
      }
      effects.push_back(std::move(et));
    }
    t.insert("effect", std::move(effects));
    arr.push_back(std::move(t));
  }
  impl_->table.insert_or_assign("rule", std::move(arr));
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
