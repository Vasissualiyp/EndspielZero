---
name: cb-new-geometry
description: Add a boundary topology to EndspielZero - cylinder, torus, Mobius, Klein, mirrors, or a higher-dimensional analogue. Use when asked to make a board wrap, glue, reflect, or take a non-trivial shape.
---

# Add a topology

Geometry is declared as face identifications. The action on **direction** vectors is
derived from the coordinate map, so it is not possible to declare a geometry whose
transport is wrong - which is the single most dangerous class of silent error here.

```toml
[[geometry.identify]]
axis = "file"          # the axis whose faces are identified
kind = "periodic"      # periodic (glues both faces) | mirror (one reflecting wall)
side = "max"           # mirror only: which wall
flip = ["rank"]        # coordinates reversed on crossing - this is what makes a twist
swap = ["file", "rank"]  # optional axis exchange; the axes must have equal extents
```

Recipes: cylinder = periodic on one axis. Torus = periodic on both. Möbius = periodic
with `flip` of another axis. Klein bottle = one straight periodic axis plus one
periodic axis with `flip` of the first. Mirror box = `kind = "mirror"` on both sides of
an axis. N-D torus = periodic on every axis.

## Test it by its mathematical fingerprint, not by a move list

This matters. A hand-written expected-move list will pass even when transport is subtly
wrong; a topological invariant will not. Use the ones in
`tests/unit/geometry/test_geometry.cpp`:

- **Torus:** every cell has the full neighbour count; no ray ever ends.
- **Cylinder:** a rook's orbit along the glued axis closes after exactly the extent.
- **Möbius:** a rook needs *two* circuits to return home, arriving on the mirrored
  rank after one.
- **Klein bottle:** a bishop's reachable set is the **whole board** - no global
  two-colouring exists - with a torus as the control at exactly half.
- **Mirror:** a ray's path length matches the unfolded reflection.

Also add the reversibility check from `test_corner_folds.cpp`: step forward, step back,
and assert the ray comes home.

## Things that stop being definable

Read `docs/plan/M3-geometry.md` §3.4 before giving a glued board pawns.

- **"Forward"** needs a globally consistent orientation. Non-orientable boards have
  none, and the shipped variants therefore field no oriented pieces there. If your
  variant needs pawns on such a board, you are choosing a policy, not applying a rule -
  say so in its docs page.
- **"The last rank"** does not exist on a glued axis: omit `[promotion]`.
- **Castling** is ambiguous once the files wrap: omit it.
- **Bishop colour binding** is a theorem about orientable boards only.

## Known limitations

- An antipodal (projective-plane) identification is not expressible: the language pairs
  faces, and that is not of that shape.
- A corner fold - one step leaving on two axes at once - is order-dependent for
  gluings whose transforms disagree about a shared axis. See ADR-0010. This is why
  `isAttacked` uses a forward scan on glued boards; do not "optimise" that away without
  reading the ADR.

Then `ctest --preset dev -L unit` and `tools/precommit.sh`.
