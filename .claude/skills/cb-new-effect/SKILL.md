---
name: cb-new-effect
description: Add a primitive to the EndspielZero rule VM - a new effect opcode or expression - when no existing one can express a variant's mechanic. Use when a variant needs something the rule catalogue cannot say.
---

# Add a rule primitive

## First: does it already exist?

Most mechanics are a combination of what is there. Check `docs/variants/rules.md` and the
shipped rule variants before adding anything. In particular:

- "Destroy several things" is `destroy_region` with an `affects` filter, and the filter
  can say anything an expression can - the atomic variant spares pawns that way.
- "This move is not allowed" is `on_move_filter` + `forbid_move`, and the condition can
  consult `any_capture` or `has_capture_from`.
- "Win when X happens" is `on_move_end` + `end_game`, with a condition on `coord`.
- Per-piece or per-cell state is a **custom field**, not a new opcode.

Adding an opcode is cheap (about 100 lines with tests) and then reusable by everything,
so the bar is "no combination expresses it", not "a combination is ugly".

## Where the pieces go

An expression:

1. `src/rules/expr.hpp` - the `ExprOp` enum value, with a one-line comment saying what
   its arguments are.
2. `src/rules/expr.cpp` - `toString` and `arity`. **Both switches are exhaustive and
   `-Werror=switch` will find you if you miss one.**
3. `src/rules/vm.cpp` - the `eval` case.
4. `src/io/rule_parse.cpp` - the name, in `nullaryNames()` or `opNames()`. An operator
   that takes a *name* first (like `piece_field charge move.to`) is handled explicitly in
   `Parser::parseOne`.
5. `src/rules/validate.cpp` - any range check its immediate needs.

An effect:

1. `src/rules/rule.hpp` - the `EffectOp` value; `src/rules/rule.cpp` - `toString`.
2. `src/rules/vm.cpp` - the `applyEffect` case. **Every board change must go through
   `Position::setCell`, `setPieceField` or `setCellField` with the `Undo*`**, or undo
   will not restore it and the search, the replay and the undo button all break.
3. `src/io/variant_toml.cpp` - the TOML surface, in the effect `if` chain.
4. `src/rules/validate.cpp` - which triggers it makes sense under, and its argument
   requirements. Say *why* in the message: "forbid_move only means anything under
   on_move_filter; by any later trigger the move has already been played."

## Rules the VM must keep

- **No floating point.** Rule evaluation is compared across machines for multiplayer and
  replay; an arch test enforces this for the core layers.
- **No unbounded iteration.** Expressions are trees with no loops. An iterating effect
  must be bounded by something small and declared, and must charge the step budget.
- **Deterministic order.** No iteration over hash containers, nothing address-dependent.
- **The rule VM cannot see move generation.** Anything needing it goes through the
  `RuleEnv` callback, the way `has_capture_from` does. That is what keeps the dependency
  pointing down.

## Tests

- The effect in isolation, from a FEN, asserting what it changed *and what it left alone*.
- Reversibility: play and undo, and check the position hash returns exactly. Add the
  variant to the loop in `tests/unit/rules/test_rule_variants.cpp`.
- A rejection case for each new validation message.
- **At least one shipped variant that uses it**, with a docs page. A primitive with no
  user is untested surface.

Then `ctest --preset dev -L unit` and `tools/precommit.sh`.
