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
                  int orientAxis, MoveAtom& atom, std::string& error) {
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
  return true;
}

void writeAtomKind(const MoveAtom& atom, const std::vector<std::string>& axisNames,
                   toml::table& move) {
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
}

}  // namespace cb
