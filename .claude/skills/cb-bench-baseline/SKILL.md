---
name: cb-bench-baseline
description: Run EndspielZero benchmarks and record or compare a baseline. Use before and after any change to a hot path, or when asked whether something got faster or slower.
---

# Benchmarks and baselines

## Run

```bash
cmake --preset release && cmake --build build/release
./build/release/bench/endspielzero_bench
```

Release only. A Debug build is `-O0` by deliberate choice (nix injects `-O2`, which
would otherwise make "Debug" silently optimized) and its numbers mean nothing.

Quiesce the machine first: no other build running, and note the CPU governor. A number
without its machine is not comparable to anything.

## Record

Write `bench/baselines/<arch>/<milestone>.md` with the CPU model, governor, compiler,
preset, commit and date above the table. Follow
`bench/baselines/x86_64/M1.md`, which also shows the part that matters most: **a short
reading of where the time goes and what is recoverable.** A table of numbers with no
interpretation is not a baseline, it is a log.

## Compare

Same machine, same governor, same preset. The threshold is 10%; below that, assume
noise unless it reproduces across runs. If a regression is real and intended, record it
in the new baseline with the reason.

## What currently dominates

From the M1 baseline: `isAttacked` costs roughly as much as everything else combined,
because it walks every atom of every piece type. On glued boards it is worse - it falls
back to a full forward scan (ADR-0010). Anything that speeds it up is the highest-value
optimisation available, and it must land behind the oracle differential test in
`tests/property/test_movegen_props.cpp`, never before it.

Do **not** speculatively optimise dimension handling: the ray walk iterates a
direction's support, not the dimension count, so templating on dimensions unrolls a
loop that does not run. `docs/plan/M2-nd-generalization.md` records why.
