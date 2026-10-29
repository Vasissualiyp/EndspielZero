---
name: cb-tdd-step
description: Add one behaviour to EndspielZero test-first. Use for any engine change - new function, bug fix, new rule - to get the red/green/gate/commit loop and the right preset for each stage.
---

# One TDD step in EndspielZero

The loop is not optional here (AGENTS.md rule 1-2): no production line before a
failing test, and a red suite blocks the next step.

## 1. Place the behaviour

Find its layer in the layer map (`AGENTS.md`). Two questions settle it:

- Does it need to know about *coordinates*? Then it belongs at L9/L10, not in
  movegen. Movegen speaks `CellId` and `DirId` only.
- Does it need to know a *rule*? Then it is L6 data or a policy, not L5 code.

Adding a file to a layer: use the `cb-new-module` skill instead of doing it by hand.

## 2. Write the failing test first

| Kind of behaviour | Where | Label |
|---|---|---|
| A specific case with a known answer | `tests/unit/<layer>/test_<thing>.cpp` | `[unit][<layer>]` |
| An invariant that should hold for *all* inputs | `tests/property/test_<area>_props.cpp` | `[property][<area>]` |
| External ground truth (perft, FEN corpus, reference counts) | `tests/golden/` or `tests/perft/` | `[golden]` / `[perft]` |
| A source-tree or build rule | `tests/arch/` | `[arch]` |

Tag every test case with both its label and its area, e.g.
`TEST_CASE("...", "[unit][geometry]")`.

Run it and **watch it fail for the reason you expect**:

```bash
cmake --build build/dev && ./build/dev/tests/endspielzero_tests "[unit][geometry]"
```

A test that passes before the implementation exists is testing nothing.

## 3. Implement minimally

Then:

```bash
cmake --build build/dev && ctest --preset dev -L unit    # fast loop, seconds
```

## 4. Before committing

```bash
tools/precommit.sh          # format, tidy, full dev suite
```

If the change touches a hot path (geometry step, movegen, position mutation), also:

```bash
cmake --build --preset release && ./build/release/bench/endspielzero_bench
```

and compare against `bench/baselines/`. Use the `cb-bench-baseline` skill to record.

## 5. Commit

`<area>: <imperative summary>`, e.g. `geometry: transport directions across Klein seams`.

Never bundle a golden change with anything else, and always say in the message
why the golden moved.

## Specific traps in this codebase

- **Adding a new `Direction` consumer?** Remember the ray walk carries the
  coordinate; if you add a code path that re-derives it with `toCoord` inside a
  loop, you have put a division in the hot path.
- **Touching movegen?** Add a `NoAllocScope` assertion to the test, or the
  no-allocation invariant is unguarded for your new path.
- **Touching geometry?** The differential test against the naive oracle is the
  real test; the hand-written case is just documentation.
- **Adding a rule?** Ask whether it should be a per-variant *policy* instead.
  "Forward", "last rank" and colour binding are meaningless on some topologies.
