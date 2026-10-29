---
name: cb-new-variant
description: Add a new EndspielZero variant as a data file - board, pieces, geometry, starting position, goldens and docs. Use when asked to add, define or port a chess variant, or to turn a description of a game into something playable.
---

# Add a variant

A variant is data. If you find yourself editing `src/` to add one, stop: either an
existing primitive covers it, or you need a new rule primitive (`cb-new-effect`).

## 1. Write `variants/<name>.toml`

Start from the closest shipped variant, not from scratch. `variants/standard.toml` is
the fully-featured example; `variants/klein.toml` shows a minimal glued board.

The checklist of things authors forget:

- [ ] `orientation_axis` - required by anything with `forward = true`, `from_rank`, or
      a `[promotion]` block.
- [ ] `[promotion] rank` - and **omit it deliberately** if the board has no last rank.
      A glued rank axis has none.
- [ ] One `royal = true` piece per side, or none for both. A variant with a royal on
      one side only is rejected.
- [ ] `resets_draw_clock = true` on pawn-like pieces, or the draw counter never resets.
- [ ] `max = "inf"` for sliders; `min` when a move must travel an exact distance.
- [ ] `[[castle]]` cell lists, `empty` and `safe` both, and a distinct `rights_bit`
      per template - the bits are what FEN round-trips.
- [ ] `[start] board` - cell count must match exactly; the loader will tell you the
      number it expected.

## 2. Board strings

FEN-N order: rank axis descending, file axis ascending, higher axes ascending. Ranks
are separated by `/`, the third axis by `|`, the fourth by `||`.

Do not write a multi-axis board string by hand. Build the variant with an empty-ish
board, then let the engine print the canonical form:

```bash
cmake --build build/dev
./build/dev/src/cli/endspielzero "load <name>" fen board info
```

`fen` gives you the exact string to paste back, and `board` shows the slices so you can
see what you actually declared.

## 3. Sanity-check the opening position

```bash
./build/dev/src/cli/endspielzero "load <name>" board "moves" "perft 2"
```

**A glued board changes what a sensible starting position is.** On a torus rank 1 and
rank 8 are adjacent, so the ordinary chess array starts in an inescapable check; on a
Klein bottle a bishop attacks from almost anywhere. If `moves` reports 0, or a
suspiciously small number, the setup is the problem, not the engine.

## 4. Goldens and docs

- Add a row to the count table in `tests/golden/test_shipped_variants.cpp`. Everything
  else in that file finds the variant by scanning `variants/`, so loading, FEN
  round-trip, oracle agreement and the has-a-legal-move check come for free.
- Derive the depth-1 count **by hand, piece by piece, before** looking at the engine's
  answer, and say so in the docs page. That is the only external authority these
  numbers have.
- Write `docs/variants/<name>.md` and add it to the table in
  `docs/variants/README.md`. State explicitly which rules are your own choice rather
  than an established definition - never imply an authority the content lacks.

## 5. Gate

```bash
ctest --preset dev -L golden
tools/precommit.sh
```
