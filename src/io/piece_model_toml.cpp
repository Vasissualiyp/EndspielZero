// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/piece_model_toml.hpp"

#include <string>

namespace cb {
namespace {

Result<assets::ModelPoint> pointFrom(const toml::node& n, const std::string& where) {
  const auto* pair = n.as_array();
  if (pair == nullptr || pair->size() != 2) {
    return fail(ErrorCode::ValidationError, where + ": a point is a pair [x, y]");
  }
  const auto x = (*pair)[0].value<std::int64_t>();
  const auto y = (*pair)[1].value<std::int64_t>();
  if (!x || !y || *x < -1000 || *x > 1000 || *y < -1000 || *y > 1000) {
    return fail(ErrorCode::ValidationError,
                where + ": a point is two integers in permille (-1000..1000)");
  }
  return assets::ModelPoint{static_cast<std::int16_t>(*x), static_cast<std::int16_t>(*y)};
}

Result<assets::Outline> outlineFrom(const toml::node* n, const std::string& where) {
  assets::Outline o;
  const auto* arr = n == nullptr ? nullptr : n->as_array();
  if (arr == nullptr) {
    return fail(ErrorCode::ValidationError, where + " must be a list of [x, y] points");
  }
  for (const auto& p : *arr) {
    auto pt = pointFrom(p, where);
    if (!pt.has_value()) return std::unexpected(pt.error());
    o.points.push_back(*pt);
  }
  return o;
}

Result<std::vector<assets::Outline>> outlinesFrom(const toml::node* n,
                                                  const std::string& where) {
  std::vector<assets::Outline> out;
  if (n == nullptr) return out;
  const auto* arr = n->as_array();
  if (arr == nullptr) {
    return fail(ErrorCode::ValidationError, where + " must be a list of outlines");
  }
  for (std::size_t i = 0; i < arr->size(); ++i) {
    auto o = outlineFrom(arr->get(i), where + " " + std::to_string(i + 1));
    if (!o.has_value()) return std::unexpected(o.error());
    out.push_back(std::move(*o));
  }
  return out;
}

toml::array outlineTo(const assets::Outline& o) {
  toml::array arr;
  for (const assets::ModelPoint& p : o.points) {
    arr.push_back(
        toml::array{static_cast<std::int64_t>(p.x), static_cast<std::int64_t>(p.y)});
  }
  return arr;
}

}  // namespace

Result<assets::PieceModel> pieceModelFromToml(const toml::table& t) {
  assets::PieceModel m;
  auto profile = outlineFrom(t.get("profile"), "the model's profile");
  if (!profile.has_value()) return std::unexpected(profile.error());
  m.profile = std::move(*profile);
  const std::int64_t segs = t["segments"].value_or<std::int64_t>(m.segments);
  if (segs < 3 || segs > 64) {
    return fail(ErrorCode::ValidationError, "the model's segments must be 3..64");
  }
  m.segments = static_cast<std::uint8_t>(segs);
  const std::string sym = t["symmetry"].value_or(std::string{"full"});
  if (sym == "full") {
    m.symmetry.kind = assets::SymmetryKind::Full;
  } else if (sym == "mirror") {
    m.symmetry.kind = assets::SymmetryKind::Mirror;
  } else if (sym == "kfold") {
    m.symmetry.kind = assets::SymmetryKind::KFold;
    const std::int64_t k = t["k"].value_or<std::int64_t>(4);
    if (k < 2 || k > 12) {
      return fail(ErrorCode::ValidationError, "the model's k must be 2..12");
    }
    m.symmetry.k = static_cast<std::uint8_t>(k);
  } else {
    return fail(ErrorCode::ValidationError,
                "the model's symmetry is full, mirror or kfold, not '" + sym + "'");
  }
  if (const auto* els = t["element"].as_array()) {
    for (std::size_t i = 0; i < els->size(); ++i) {
      const std::string where = "model element " + std::to_string(i + 1);
      const auto* et = els->get(i)->as_table();
      if (et == nullptr)
        return fail(ErrorCode::ValidationError, where + " must be a table");
      assets::Element e;
      auto o = outlineFrom(et->get("outline"), where + "'s outline");
      if (!o.has_value()) return std::unexpected(o.error());
      e.outline = std::move(*o);
      if (const auto* at = et->get("at")) {
        auto p = pointFrom(*at, where + "'s 'at'");
        if (!p.has_value()) return std::unexpected(p.error());
        e.radius = p->x;
        e.height = p->y;
      }
      m.elements.push_back(std::move(e));
    }
  }
  if (auto ok = assets::validate(m); !ok.has_value()) return std::unexpected(ok.error());
  return m;
}

Result<assets::IconModel> pieceIconFromToml(const toml::table& t) {
  assets::IconModel m;
  auto fills = outlinesFrom(t.get("fills"), "icon fill");
  if (!fills.has_value()) return std::unexpected(fills.error());
  auto cuts = outlinesFrom(t.get("cuts"), "icon cut");
  if (!cuts.has_value()) return std::unexpected(cuts.error());
  m.fills = std::move(*fills);
  m.cuts = std::move(*cuts);
  if (auto ok = assets::validate(m); !ok.has_value()) return std::unexpected(ok.error());
  return m;
}

toml::table pieceModelToToml(const assets::PieceModel& m) {
  toml::table t;
  t.insert("segments", static_cast<std::int64_t>(m.segments));
  switch (m.symmetry.kind) {
    case assets::SymmetryKind::Full:
      t.insert("symmetry", std::string{"full"});
      break;
    case assets::SymmetryKind::Mirror:
      t.insert("symmetry", std::string{"mirror"});
      break;
    case assets::SymmetryKind::KFold:
      t.insert("symmetry", std::string{"kfold"});
      t.insert("k", static_cast<std::int64_t>(m.symmetry.k));
      break;
  }
  t.insert("profile", outlineTo(m.profile));
  if (!m.elements.empty()) {
    toml::array els;
    for (const assets::Element& e : m.elements) {
      toml::table et;
      et.insert("at", toml::array{static_cast<std::int64_t>(e.radius),
                                  static_cast<std::int64_t>(e.height)});
      et.insert("outline", outlineTo(e.outline));
      els.push_back(std::move(et));
    }
    t.insert("element", std::move(els));
  }
  return t;
}

toml::table pieceIconToToml(const assets::IconModel& m) {
  toml::table t;
  toml::array fills;
  for (const assets::Outline& o : m.fills) fills.push_back(outlineTo(o));
  t.insert("fills", std::move(fills));
  if (!m.cuts.empty()) {
    toml::array cuts;
    for (const assets::Outline& o : m.cuts) cuts.push_back(outlineTo(o));
    t.insert("cuts", std::move(cuts));
  }
  return t;
}

}  // namespace cb
