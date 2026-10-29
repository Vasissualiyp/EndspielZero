---
name: cb-perft-golden
description: Generate, verify or debug EndspielZero perft node counts, and localize a movegen bug to a single move. Use when a perft or golden count mismatches, or when adding node counts for a variant.
---

# Perft goldens

## The rule

**A golden is never edited to make a build pass.** A mismatch is a bug until proven
otherwise. If you genuinely must change one, the commit message says why.

## Where the authority comes from

- **Standard chess** has external ground truth: the published counts in
  `tests/perft/test_perft_positions.cpp`. These are the only numbers here that prove
  anything.
- **Every other variant's counts are this engine's own output.** They are pinned two
  ways instead: the depth-1 count is derived *by hand* before comparing with the
  engine, and deeper counts are checked against the naive oracle, which re-derives
  every expansion independently. Their job is to notice change.
- If you cannot verify a FEN/count pair from a primary source, **do not ship it.** An
  unverifiable golden sends the next person hunting a bug that is not there - see the
  note about the absent sixth perft position.

## Localizing a mismatch

Bisect with `divide`, which splits perft by first move:

```bash
./build/dev/src/cli/endspielzero "load <variant>" "fen <fen>" "divide 4"
```

Compare against a reference divide, descend into the first move whose subtotal is
wrong, and repeat until depth 1. Then:

```bash
./build/dev/src/cli/endspielzero "fen <the bad position>" moves board
```

A count wrong at depth 1 is a move-generation bug. A count right at depth 1 but wrong
at depth 2 is usually legality, `make`/`unmake`, or the attack query - and the property
tests in `tests/property/test_movegen_props.cpp` will localize it faster than perft:
they compare against the oracle move by move and print both lists.

## Depth budget

`ctest -L perft` runs to depth 4 and takes seconds. Depths 5 and 6 are tagged `[slow]`
and `[perft-deep]` and run as `perft-slow` / by name - depth 6 is about seven minutes.

A hidden `[.]` test still runs if a filter matches any of its *other* tags, which is
why the deep suite is tagged outside `[perft]`. Do not put it back.

## Adding counts for a variant

Use the `cb-new-variant` skill's checklist; the count table lives in
`tests/golden/test_shipped_variants.cpp`.
