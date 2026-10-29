---
name: cb-gate
description: Run the EndspielZero milestone exit gate (both compilers, sanitizers, coverage, goldens, bench) and interpret failures. Use before finishing a milestone, opening a PR, or when asked whether the build is green.
---

# The gate

`nix flake check` is the single source of truth (docs/plan/00-roadmap.md). The
staged version below is what to run locally, cheapest first.

```bash
# 1. fast loop (seconds)
cmake --preset dev && cmake --build build/dev && ctest --preset dev -L unit

# 2. full dev suite (includes property tests; minutes). The `slow` label is excluded
#    by the preset - deep perft at -O0 is over an hour.
ctest --preset dev

# 3. the other compiler - catches UB-dependent and iteration-order bugs
cmake --preset clang-dev && cmake --build build/clang-dev && ctest --preset clang-dev

# 4. sanitizers
cmake --preset asan && cmake --build build/asan && ctest --preset asan

# 5. release, because -O2 finds different bugs than -O0
cmake --preset release && cmake --build build/release && ctest --preset release

# 6. coverage thresholds: core >= 90% lines, >= 80% branches
cmake --preset coverage && cmake --build build/coverage && ctest --preset coverage
gcovr --root . --filter 'src/' --print-summary

# 7. the deep counts and the wide property sweep - in RELEASE, roughly ten minutes
cmake --preset release && cmake --build build/release && ctest --preset release-slow

# 8. everything, hermetically
nix flake check
```

## Reading failures

| Symptom | What it usually means | What NOT to do |
|---|---|---|
| A `[golden]` or `[perft]` count differs | A real movegen or rules regression | **Never** regenerate the golden. Bisect with `cb-perft-golden`. |
| Passes on gcc, fails on clang (or Debug vs Release) | UB, or a dependence on unspecified evaluation/iteration order | Do not "fix" by pinning a compiler |
| A `[property]` test fails for one seed | A real bug in an input region your hand-written cases miss | Do not delete the seed. Reproduce it with `-c "<test name>"` and minimise |
| `NoAllocScope` trips | An allocation crept into a hot path | Do not relax the assertion; reserve outside the loop |
| An arch test flags a float in core | Determinism breach | Do not add an exception; use integers |
| Layer violation at configure time | A dependency pointing upward | Do not add the link edge; move the code or invert the dependency |

## Milestone exit

A milestone is done only when every bullet in its plan file's **Acceptance
facts** section is backed by a named, passing test - and the roadmap's gate list
holds. Check them off explicitly; "the tests pass" is not the same claim.
