// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/atom_toml.hpp"

#include <cstdlib>
#include <string>

namespace cb {
namespace {

int axisIndex(const std::vector<std::string>& names, const std::string& name) {
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (names[i] == name) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

bool readAtomKind(const toml::table& move, const std::vector<std::string>& axisNames,
                  int orientAxis, const std::vector<std::string>& markTags, MoveAtom& atom,
                  std::string& error) {
  const std::string type = move["type"].value_or(std::string{"extrapolated"});
  if (type == "extrapolated") {
    atom.kind = MoveKind::Extrapolated;
  } else if (type == "exact") {
    atom.kind = MoveKind::Exact;
  } else if (type == "expanded") {
    atom.kind = MoveKind::Expanded;
  } else {
    error = "a move's type is exact, expanded or extrapolated - not '" + type + "'";
    return false;
  }

  const auto* vec = move["vector"].as_array();
  if (vec == nullptr || vec->empty()) {
    error = "a move needs a 'vector', e.g. vector = [1, 2]";
    return false;
  }
  if (atom.kind == MoveKind::Extrapolated) {
    for (const auto& m : *vec) {
      const auto value = m.value<std::int64_t>();
      if (!value.has_value()) {
        error = "move vector entries must be integers";
        return false;
      }
      if (atom.mags.size() >= kMaxDims) {
        error = "a move vector cannot have more than " + std::to_string(kMaxDims) +
                " magnitudes";
        return false;
      }
      atom.mags.push(static_cast<std::int16_t>(*value));
    }
  } else {
    // Literal: one signed entry per axis, in the board's axis order.
    if (vec->size() > axisNames.size()) {
      error = "an exact or expanded vector has one entry per axis, and this board has " +
              std::to_string(axisNames.size());
      return false;
    }
    for (std::size_t i = 0; i < vec->size(); ++i) {
      const auto value = (*vec)[i].value<std::int64_t>();
      if (!value.has_value()) {
        error = "move vector entries must be integers";
        return false;
      }
      if (*value != 0) {
        atom.pins.push(Pin{static_cast<std::uint8_t>(i), static_cast<std::int16_t>(*value)});
      }
    }
  }

  // Pinned components of an extrapolated move: "rank:+1".
  if (const auto* pins = move["pin"].as_array()) {
    if (atom.kind != MoveKind::Extrapolated) {
      error = "'pin' is for extrapolated moves; an exact or expanded vector is already literal";
      return false;
    }
    for (const auto& node : *pins) {
      const std::string text = node.value_or(std::string{});
      const auto colon = text.find(':');
      if (colon == std::string::npos) {
        error = "a pin is written axis:value, e.g. \"rank:+1\" - not '" + text + "'";
        return false;
      }
      const int axis = axisIndex(axisNames, text.substr(0, colon));
      if (axis < 0) {
        error = "no axis is called '" + text.substr(0, colon) + "'";
        return false;
      }
      const long value = std::strtol(text.c_str() + colon + 1, nullptr, 10);
      atom.pins.push(Pin{static_cast<std::uint8_t>(axis), static_cast<std::int16_t>(value)});
    }
  }

  // Flip defaults on wherever a move names the orientation axis: that is what a pawn's
  // push is, and it is what "forward for the other side" means.
  bool names = false;
  for (const Pin& p : atom.pins) names = names || static_cast<int>(p.axis) == orientAxis;
  atom.flip = move["flip"].value_or(orientAxis >= 0 && names);

  const std::string sign = move["sign"].value_or(std::string{"both"});
  atom.sign = sign == "positive" ? StepSign::Positive
              : sign == "negative" ? StepSign::Negative
                                   : StepSign::Both;

  const auto tagIndex = [&](const std::string& name) {
    for (std::size_t i = 0; i < markTags.size(); ++i) {
      if (markTags[i] == name) return static_cast<int>(i);
    }
    return -1;
  };
  if (const auto* marks = move["marks"].as_array()) {
    for (const auto& node : *marks) {
      const auto* t = node.as_table();
      if (t == nullptr) {
        error = "each entry of 'marks' is a table: { tag = \"destroyed\", where = \"around\" }";
        return false;
      }
      MarkSpec spec;
      const std::string tag = (*t)["tag"].value_or(std::string{});
      const int index = tagIndex(tag);
      if (index < 0) {
        error = "a move leaves a mark with the tag '" + tag + "', which this variant does not declare "
                "(declare it with [[mark]])";
        return false;
      }
      spec.tag = static_cast<std::uint16_t>(index);
      const std::string where = (*t)["where"].value_or(std::string{"passed"});
      if (where == "passed") {
        spec.where = MarkWhere::Passed;
      } else if (where == "landed") {
        spec.where = MarkWhere::Landed;
      } else if (where == "around") {
        spec.where = MarkWhere::Around;
      } else {
        error = "a mark's 'where' is passed, landed or around - not '" + where + "'";
        return false;
      }
      spec.radius = static_cast<std::uint16_t>((*t)["radius"].value_or<std::int64_t>(1));
      spec.duration = static_cast<std::uint32_t>((*t)["duration"].value_or<std::int64_t>(0));
      atom.marks.push_back(spec);
    }
  }
  if (const auto* at = move["captures_at"].as_array()) {
    for (const auto& node : *at) {
      const std::string text = node.value_or(std::string{});
      const auto colon = text.find(':');
      const int axis = colon == std::string::npos ? -1 : axisIndex(axisNames, text.substr(0, colon));
      if (axis < 0) {
        error = "a capture offset is written axis:value, e.g. \"rank:-1\" - not '" + text + "'";
        return false;
      }
      atom.captureAt.push(Pin{static_cast<std::uint8_t>(axis),
                              static_cast<std::int16_t>(std::strtol(text.c_str() + colon + 1, nullptr, 10))});
    }
  }
  if (const auto needs = move["needs_mark"].value<std::string>()) {
    const int index = tagIndex(*needs);
    if (index < 0) {
      error = "needs_mark names the tag '" + *needs + "', which this variant does not declare";
      return false;
    }
    atom.needsMark = static_cast<std::int16_t>(index);
  }
  return true;
}

void writeAtomKind(const MoveAtom& atom, const std::vector<std::string>& axisNames,
                   const std::vector<std::string>& markTags, toml::table& move) {
  if (atom.kind == MoveKind::Extrapolated) {
    toml::array vec;
    for (const auto mag : atom.mags) vec.push_back(static_cast<std::int64_t>(mag));
    move.insert("vector", std::move(vec));
    if (!atom.pins.empty()) {
      toml::array pins;
      for (const Pin& p : atom.pins) {
        const std::string name = p.axis < axisNames.size() ? axisNames[p.axis]
                                                           : std::to_string(p.axis);
        pins.push_back(name + ":" + (p.value > 0 ? "+" : "") + std::to_string(p.value));
      }
      move.insert("pin", std::move(pins));
    }
  } else {
    move.insert("type", std::string(atom.kind == MoveKind::Exact ? "exact" : "expanded"));
    toml::array vec;
    for (std::size_t i = 0; i < axisNames.size(); ++i) {
      std::int64_t value = 0;
      for (const Pin& p : atom.pins) {
        if (p.axis == i) value = p.value;
      }
      vec.push_back(value);
    }
    move.insert("vector", std::move(vec));
    if (atom.kind == MoveKind::Expanded && atom.sign != StepSign::Both) {
      move.insert("sign", std::string(atom.sign == StepSign::Positive ? "positive" : "negative"));
    }
  }
  // Written explicitly whenever it is on, so what is saved does not depend on a default.
  if (atom.flip) move.insert("flip", true);
  const auto tagName = [&](std::size_t i) {
    return i < markTags.size() ? markTags[i] : std::to_string(i);
  };
  if (!atom.marks.empty()) {
    toml::array marks;
    for (const MarkSpec& m : atom.marks) {
      toml::table t;
      t.insert("tag", tagName(m.tag));
      t.insert("where", std::string(m.where == MarkWhere::Passed ? "passed"
                                    : m.where == MarkWhere::Landed ? "landed"
                                                                   : "around"));
      if (m.where == MarkWhere::Around) t.insert("radius", static_cast<std::int64_t>(m.radius));
      if (m.duration != 0) t.insert("duration", static_cast<std::int64_t>(m.duration));
      marks.push_back(std::move(t));
    }
    move.insert("marks", std::move(marks));
  }
  if (!atom.captureAt.empty()) {
    toml::array at;
    for (const Pin& p : atom.captureAt) {
      const std::string name = p.axis < axisNames.size() ? axisNames[p.axis] : std::to_string(p.axis);
      at.push_back(name + ":" + (p.value > 0 ? "+" : "") + std::to_string(p.value));
    }
    move.insert("captures_at", std::move(at));
    if (atom.needsMark >= 0) {
      move.insert("needs_mark", tagName(static_cast<std::size_t>(atom.needsMark)));
    }
  }
}

}  // namespace cb
