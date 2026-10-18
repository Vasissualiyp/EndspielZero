// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

#include "rules/rule.hpp"

namespace cb::rules {

/// What a rule can see while it runs.
///
/// `hasCaptureFrom` is supplied by the orchestrating layer rather than answered here:
/// it needs move generation, which sits *above* this layer. Inverting the dependency
/// that way keeps the VM able to reason about moves without the rules layer having to
/// know that a move generator exists.
struct RuleEnv {
  Position* pos{nullptr};
  const Move* move{nullptr};
  Color mover{Color::White};
  CellId cellArg{kInvalidCell};
  std::function<bool(CellId)> hasCaptureFrom;
  /// Is the cell attacked by this side? Answered by the orchestrating layer, which owns movegen.
  std::function<bool(CellId, Color)> isAttacked;
};

struct RuleOutcome {
  bool forbidden{false};   ///< a rule vetoed the move
  bool repeatTurn{false};  ///< the mover moves again
  bool gameOver{false};
  Outcome outcome{Outcome::Draw};
  std::size_t steps{0};
  bool budgetExhausted{false};
};

/// Interprets a variant's rules.
///
/// Termination is structural, not enforced: expressions are trees with no loops and no
/// jumps, and the only iterating effect walks a bounded neighbourhood. The step budget
/// exists to bound *cost*, not to make a hostile rule set halt - it cannot fail to
/// (ADR-0005).
class RuleEngine {
 public:
  /// Cost ceiling for one trigger. Generous: the largest legitimate effect, a radius-1
  /// explosion on an 8-axis board, visits 6560 cells.
  static constexpr std::size_t kStepBudget = 1u << 18;

  explicit RuleEngine(RuleSet rules) : rules_(std::move(rules)) {}

  [[nodiscard]] const RuleSet& ruleSet() const noexcept { return rules_; }
  [[nodiscard]] bool empty() const noexcept { return rules_.empty(); }

  /// Type- and range-check every rule against the variant. Called at load, so an
  /// authoring mistake is a message with a rule name rather than a crash later.
  [[nodiscard]] Result<void> validate(const VariantSpec& v) const;

  /// Run every rule for one trigger. Effects mutate `env.pos` and record their undo
  /// information in `undo`.
  RuleOutcome run(Trigger t, RuleEnv& env, Undo* undo) const;

  /// Evaluate one expression. Exposed for tests and for the rule tracer.
  [[nodiscard]] std::int64_t eval(const Expr& e, const RuleEnv& env,
                                  std::size_t& steps) const;

 private:
  void applyEffect(const Effect& eff, RuleEnv& env, Undo* undo, RuleOutcome& out) const;

  RuleSet rules_;
};

}  // namespace cb::rules
