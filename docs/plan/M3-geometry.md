# M3 — Boundary Geometry

**Goal:** arbitrary finite boundary topology, declared as data, correct for
non-orientable surfaces, with no cost to ordinary box boards.

**Exit condition:** the topology family below all play correctly, with
mathematically meaningful invariants tested (not just "it did not crash").

## M3.1 Identification language

```toml
[[geometry.identify]]
face = "x=max"; to = "x=min"                    # periodic
[[geometry.identify]]
face = "y=max"; to = "y=min"; map = "x -> W-1-x" # Klein seam
[[geometry.identify]]
face = "x=max"; to = "x=max"; kind = "mirror"    # reflecting wall
```

- Parsed into `(faceA, faceB, affine transform)`; the induced direction action is
  *derived*, not authored, from the transform's linear part — so an author cannot
  get the direction transport wrong. **[INVARIANT]**
- The identification set generates a transition group; the loader computes its
  closure and validates consistency (no contradictory identifications, no
  cell mapped to two distinct classes).
- **Tests:** group closure is finite and correct for each shipped topology;
  transforms compose associatively; each seam crossing is an involution where it
  should be.

## M3.2 Hot path

Implements ARCH §4.2: interior stride fast path, boundary transport tables built
for boundary cells only, analytic fallback past a memory budget.

- **Tests:** table path ≡ analytic path exhaustively for small boards, sampled
  for large ones; memory usage asserted against a formula; the budget fallback is
  exercised by a deliberately oversized variant.
- **Bench:** a box board must be no slower than in M2 (the interior test must not
  cost anything real).

## M3.3 Topology family (each: TOML + docs + goldens + skill checklist)

| Topology | Identifications | Invariant tested |
|---|---|---|
| Box | none | baseline |
| Cylinder | x periodic | rook orbit on a rank has length W |
| Torus | x,y periodic | every cell has the same neighbour count; bishop orbit lengths match the lattice's closed-geodesic formula |
| Möbius band | x periodic with y-flip | orientation reversal after one circuit: a bishop returns to its start on the *opposite* diagonal colour |
| Klein bottle | x periodic, y periodic with x-flip | no global colouring exists: the bishop's reachable set is the whole board (a strong, sharp fingerprint) |
| Real projective plane | antipodal identification | each cell class has exactly the predicted size |
| Reflecting walls | mirror faces | ray path length matches the unfolded reflection |
| N-D torus | all axes periodic | neighbour-count uniformity at D=3..7 |
| 3-D Klein | mixed periodic/flip | direction transport closure; orbit goldens |
| Mixed | e.g. x periodic, y mirrored, z open | per-axis independence |

The "invariant tested" column matters: these are *mathematical* facts about each
surface, so they detect subtly wrong transport that a hand-written expected-move
list would miss. **[INVARIANT]**

## M3.4 Rules that need a policy on exotic surfaces

Non-orientable boards make several standard rules genuinely ambiguous. Each
becomes an explicit, documented, per-variant policy with tests for every option:

- **Pawn direction** after crossing an orientation-reversing seam (keep global
  axis sign / follow transported direction / forbid crossing).
- **Promotion regions** when there is no "last rank" (region declared explicitly
  by predicate; a torus variant may have none).
- **Castling** when the path wraps (allow / forbid; and whether wrapping counts
  as "between").
- **En passant** when the passed cell is reached by two different transported
  paths.
- **Bishop colour-binding** loss, which changes material evaluation — recorded
  for M10, and surfaced in the variant docs.

## M3.5 Renderer metadata

Geometry exports seam descriptors (which faces glue to which, with what
transform) for M4's seam visualisation and wrap-preview rendering. Defined here,
consumed there, tested here by asserting the descriptor round-trips and matches
the transition group.

## Generalization test

Adding a topology is a `[[geometry.identify]]` block plus goldens. No engine code.

## Acceptance facts

1. Every topology in the M3.3 table plays legally, with its listed invariant
   asserted by a named test.
2. Table and analytic transport agree; the budget fallback is exercised.
3. Box-board performance unchanged from M2.
4. Each exotic-surface rule policy has tests for every option.
5. Dimension-lift invariance (M2.2) still holds, including for periodic axes.
6. A contradictory identification set yields a precise validation error.

---

## Status: substantially complete, with two real gaps

Recorded 2026-09-28, at the end of M3.

| Item | Status |
|---|---|
| M3.1 identification language | Done, in TOML, with the direction action derived rather than authored |
| M3.2 hot path | Interior fast path done; **transport tables not built** - analytic only |
| M3.3 topology family | Done except the real projective plane - see below |
| M3.4 rule policies on exotic surfaces | **Documented and avoided, not made configurable** |
| M3.5 renderer seam metadata | `Geometry::faceTransform` exposes it; nothing consumes it yet (M4) |

### Transport tables were not built

The plan's boundary-transport tables are an optimisation over the analytic fold, and
the analytic fold turned out to be cheap enough that building the tables would have
been speculative: it runs only on a boundary crossing, and the shipped glued variants
are not limited by it. The `kMaxDirections`-style budget machinery and the
table-vs-analytic differential test in M3.2 are therefore not present.

What *is* limiting on glued boards is `isAttacked`, which falls back to a full forward
scan (ADR-0010). That is the measured next piece of work, and it is a different
problem from transport tables.

### The projective plane is not expressible

M3.3 listed the real projective plane. The identification language pairs *faces* of
the box, and an antipodal identification is not of that shape, so it cannot currently
be declared. This is a genuine expressiveness gap, not an oversight of implementation:
extending the language to general cell-class identifications would also require the
loader to canonicalise cells into equivalence classes (ARCH §4.3), which nothing does
yet. Everything else in the M3.3 table ships, each with the mathematical invariant the
plan asked for.

### M3.4 became a design constraint rather than a feature

The plan proposed per-variant policies for pawn direction, promotion regions,
castling and en passant on exotic surfaces. What exists instead:

- Promotion regions are already optional, so a torus simply declares none. **Done.**
- Castling is already a list of cells, so a glued variant simply declares none.
  **Done.**
- Pawn direction across an orientation-reversing seam has **no policy**: the
  non-orientable variants field no oriented pieces at all, and a test asserts that.
  This is honest rather than complete - it means EndspielZero cannot yet express "pawns on
  a Klein bottle", and the options are still written up in §3.4 above for whoever
  needs them.

### An addition the plan did not anticipate

Surface orientability and face handedness had to be separated. A reflecting wall
reverses handedness but glues nothing, so the board stays an ordinary box of distinct
cells: `Geometry::isOrientable()` now describes the quotient surface and
`hasOrientationReversingFace()` describes the transforms. Conflating them classified
`mirrorbox` as non-orientable, which is simply wrong.

### The ambiguity ADR-0010 records

A step leaving the box on two axes at once folds in axis order, and for gluings whose
transforms disagree about a shared axis the result is order-dependent. That is a
property of the declared geometry, not of the code. It is why `isAttacked` uses a
forward scan on glued boards, and it is pinned by
`tests/unit/geometry/test_corner_folds.cpp`. Resolving it - by making the fold provably
order-independent, or by rejecting such gluings at load time - is the one piece of M3
follow-up work that would unlock a significant speedup.
