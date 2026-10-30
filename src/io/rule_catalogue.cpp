// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/rule_catalogue.hpp"

#include <array>

namespace cb {
namespace {

constexpr std::array<std::string_view, 5> kTriggers{
    "on_move_filter", "on_capture", "on_move_end", "on_turn_end", "on_result_query"};

constexpr std::array<EffectKey, 1> kAt{{{"at", true, "cell"}}};
constexpr std::array<EffectKey, 3> kRegion{
    {{"at", true, "cell"}, {"radius", false, "number"}, {"affects", false, "expr"}}};
constexpr std::array<EffectKey, 2> kTransform{
    {{"at", true, "cell"}, {"piece", true, "piece"}}};
constexpr std::array<EffectKey, 3> kSpawn{
    {{"at", true, "cell"}, {"piece", true, "piece"}, {"color", false, "expr"}}};
constexpr std::array<EffectKey, 3> kSetField{
    {{"field", true, "field"}, {"at", true, "cell"}, {"value", true, "expr"}}};
constexpr std::array<EffectKey, 1> kOutcome{{{"outcome", true, "outcome"}}};

constexpr std::array<EffectSpec, 9> kEffects{{
    {"destroy", kAt, "remove the piece on a cell"},
    {"destroy_region", kRegion,
     "remove every piece within a radius, if 'affects' allows"},
    {"transform", kTransform, "turn the piece on a cell into another piece"},
    {"spawn", kSpawn, "place a new piece on a cell"},
    {"set_piece_field", kSetField, "set a field on the piece standing on a cell"},
    {"set_cell_field", kSetField, "set a field on a cell itself"},
    {"forbid_move", {}, "under on_move_filter: this move is not allowed"},
    {"repeat_turn", {}, "the side that moved moves again"},
    {"end_game", kOutcome, "end the game: mover_wins, mover_loses or draw"},
}};

}  // namespace

std::span<const std::string_view> ruleTriggers() noexcept {
  return kTriggers;
}

std::span<const EffectSpec> effectCatalogue() noexcept {
  return kEffects;
}

const EffectSpec* findEffect(std::string_view op) noexcept {
  for (const EffectSpec& e : kEffects) {
    if (e.op == op) return &e;
  }
  return nullptr;
}

}  // namespace cb
