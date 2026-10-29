---
name: cb-new-piece
description: Add a piece type to a EndspielZero variant by declaring its vector-move atoms. Use when asked to add a piece, a custom mover, or to express how something moves.
---

# Add a piece

Movement is declared, never coded. A piece is a set of **atoms**, each a multiset of
magnitudes expanded over every assignment to distinct axes and every sign.

## The algebra

```toml
[[piece]]
name = "nightrider"
symbol = "S"

  [[piece.move]]
  vector = [1, 2]     # magnitudes; zero components are implied by omission
  max = "inf"         # 1 = leaper, n = limited, "inf" = rider
  min = 1             # exact-distance moves use min = max
  mode = "slide"      # slide (blocked) | leap (ignores blockers) | hop (needs a hurdle)
  capture = "may"     # may | must | cannot
  forward = false     # restrict to the forward half-space of orientation_axis
```

Familiar pieces: rook `[1]` inf slide; bishop `[1,1]` inf slide; knight `[1,2]` max 1
leap; king `[1]` + `[1,1]` at max 1; queen `[1]` + `[1,1]` inf.

## Expansion counts - check these

`P(D,r) / prod(multiplicity!) × 2^r`, where `r` is the number of magnitudes:

| atom | 2-D | 3-D | 4-D | 8-D |
|---|---|---|---|---|
| `[1]` | 4 | 6 | 8 | 16 |
| `[1,1]` | 4 | 12 | 24 | 112 |
| `[1,2]` | 8 | 24 | 48 | 448 |
| `[1,1,1]` | 0 | 8 | 32 | 448 |
| `[1,2,3]` | 0 | 48 | 192 | 2688 |

Two consequences worth knowing before you declare anything:

- An atom of order `r > D` expands to **nothing**. A `[1,1,1]` piece on a 2-D board
  simply has no moves of that kind - deliberate, not an error.
- Counts explode with dimension. There is a load-time budget; a variant that trips it
  gets an error naming the piece and the atom.

## Verify

```bash
./build/dev/src/cli/endspielzero "load <variant>" info
```

`info` prints every piece's atoms as the engine canonicalised them - which is how you
confirm that `[2,1]` became `[1,2]`, that `min`/`max` are what you meant, and how many
directions each atom actually produced.

## Tests

- Direction counts: assert them in the variant's golden test, against the formula
  above. `tests/golden/test_nd_variants.cpp` shows the pattern.
- Movement: a hand-built position and an expected move list, in
  `tests/unit/pieces/` or the variant's golden file.
- If the piece joins a variant with pinned node counts, those counts change: update
  them **and explain why in the commit message**.

Then `tools/precommit.sh`.
