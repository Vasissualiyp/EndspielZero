// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

namespace cb {

/// What the rule VM can be asked to do, as the file spells it (M7.2.2): the triggers a
/// `[[rule]]` may name and, per effect op, the keys its `[[rule.effect]]` takes. The
/// editor's form offers exactly this, and the loader (variant_toml.cpp) reads exactly
/// this - the catalogue test holds the two together, so the form cannot offer an op or
/// a key the loader does not understand.
struct EffectKey {
  std::string_view key;
  bool required{true};
  /// What goes in it, for the form: "cell", "number", "piece", "field", "outcome" or
  /// "expr" (an S-expression).
  std::string_view kind;
};

struct EffectSpec {
  std::string_view op;
  std::span<const EffectKey> keys;
  std::string_view help;
};

[[nodiscard]] std::span<const std::string_view> ruleTriggers() noexcept;
[[nodiscard]] std::span<const EffectSpec> effectCatalogue() noexcept;
[[nodiscard]] const EffectSpec* findEffect(std::string_view op) noexcept;

}  // namespace cb
