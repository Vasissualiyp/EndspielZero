// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/model_toml.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace cb {
namespace {

toml::array pointsToToml(const std::vector<assets::ModelPoint>& pts) {
  toml::array out;
  for (const auto& p : pts) {
    toml::array xy;
    xy.push_back(static_cast<std::int64_t>(p.x));
    xy.push_back(static_cast<std::int64_t>(p.y));
    out.push_back(std::move(xy));
  }
  return out;
}

std::vector<assets::ModelPoint> pointsFromToml(const toml::array* arr) {
  std::vector<assets::ModelPoint> out;
  if (arr == nullptr) return out;
  for (const auto& node : *arr) {
    const auto* xy = node.as_array();
    if (xy == nullptr || xy->size() != 2) continue;
    out.push_back(assets::ModelPoint{
        static_cast<std::int16_t>((*xy)[0].value_or<std::int64_t>(0)),
        static_cast<std::int16_t>((*xy)[1].value_or<std::int64_t>(0))});
  }
  return out;
}

toml::array outlinesToToml(const std::vector<assets::Outline>& list) {
  toml::array out;
  for (const auto& o : list) out.push_back(pointsToToml(o.points));
  return out;
}

std::vector<assets::Outline> outlinesFromToml(const toml::array* arr) {
  std::vector<assets::Outline> out;
  if (arr == nullptr) return out;
  for (const auto& node : *arr) {
    assets::Outline o;
    o.points = pointsFromToml(node.as_array());
    out.push_back(std::move(o));
  }
  return out;
}

}  // namespace

toml::table bodyToToml(const assets::PieceModel& body) {
  toml::table t;
  t.insert("segments", static_cast<std::int64_t>(body.segments));
  const char* kind = body.symmetry.kind == assets::SymmetryKind::KFold    ? "kfold"
                     : body.symmetry.kind == assets::SymmetryKind::Mirror ? "mirror"
                                                                          : "full";
  t.insert("symmetry", std::string(kind));
  t.insert("k", static_cast<std::int64_t>(body.symmetry.k));
  t.insert("profile", pointsToToml(body.profile.points));
  if (!body.elements.empty()) {
    toml::array elems;
    for (const assets::Element& e : body.elements) {
      toml::table et;
      et.insert("outline", pointsToToml(e.outline.points));
      et.insert("radius", static_cast<std::int64_t>(e.radius));
      et.insert("height", static_cast<std::int64_t>(e.height));
      if (e.lateral != 0) et.insert("lateral", static_cast<std::int64_t>(e.lateral));
      et.insert("thickness", static_cast<std::int64_t>(e.thickness));
      if (e.tangent) et.insert("tangent", true);
      elems.push_back(std::move(et));
    }
    t.insert("element", std::move(elems));
  }
  return t;
}

std::optional<assets::PieceModel> bodyFromToml(const toml::table& body) {
  assets::PieceModel m;
  m.segments = static_cast<std::uint8_t>(body["segments"].value_or<std::int64_t>(16));
  const std::string kind = body["symmetry"].value_or(std::string{"full"});
  m.symmetry.kind = kind == "kfold"    ? assets::SymmetryKind::KFold
                    : kind == "mirror" ? assets::SymmetryKind::Mirror
                                       : assets::SymmetryKind::Full;
  m.symmetry.k = static_cast<std::uint8_t>(body["k"].value_or<std::int64_t>(4));
  m.profile.points = pointsFromToml(body["profile"].as_array());
  if (const auto* elems = body["element"].as_array()) {
    for (const auto& node : *elems) {
      const auto* t = node.as_table();
      if (t == nullptr) continue;
      assets::Element e;
      e.outline.points = pointsFromToml((*t)["outline"].as_array());
      e.radius = static_cast<std::int16_t>((*t)["radius"].value_or<std::int64_t>(0));
      e.height = static_cast<std::int16_t>((*t)["height"].value_or<std::int64_t>(0));
      e.lateral = static_cast<std::int16_t>((*t)["lateral"].value_or<std::int64_t>(0));
      e.thickness =
          static_cast<std::int16_t>((*t)["thickness"].value_or<std::int64_t>(120));
      e.tangent = (*t)["tangent"].value_or(false);
      m.elements.push_back(std::move(e));
    }
  }
  return m;
}

toml::table iconToToml(const assets::IconModel& icon) {
  toml::table t;
  t.insert("fills", outlinesToToml(icon.fills));
  if (!icon.cuts.empty()) t.insert("cuts", outlinesToToml(icon.cuts));
  return t;
}

std::optional<assets::IconModel> iconFromToml(const toml::table& icon) {
  assets::IconModel m;
  m.fills = outlinesFromToml(icon["fills"].as_array());
  m.cuts = outlinesFromToml(icon["cuts"].as_array());
  return m;
}

}  // namespace cb
