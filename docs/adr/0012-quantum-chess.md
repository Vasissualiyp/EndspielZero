# ADR-0012: Quantum chess needs a position ensemble, and is not being built now

- **Status:** Accepted
- **Date:** 2026-09-29

## Context

The project spec lists quantum chess among the variants that custom data fields should
reproduce. Having now built the field system and the rule VM, that turns out to be
wrong, and it is worth writing down exactly why rather than leaving it as an
unexplained gap.

A custom field attaches a value to a piece or a cell in **one** position. Superposition
is not a property of a piece; it is a property of the *set of positions the game is
in*. A knight that is "60% on c3 and 40% on e2" is not one board with a field on it —
it is two boards with amplitudes. No amount of per-piece state expresses that, because
the thing being described is not in the board.

Concretely, the engine assumes throughout that:

- a `Position` is one board and `Position::at(cell)` returns one piece;
- a move maps one position to one position, reversibly (`make`/`unmake`);
- legality is a predicate on a single position;
- the Zobrist hash identifies a position.

Superposition breaks all four.

## Decision

**Quantum chess is not in scope for M5, and custom fields are not claimed to deliver
it.** What M5 delivers instead is the interface hook that would let it be added later
without touching move generation:

- `view::PositionView` already decouples anything that *reads* a position from the
  `Position` type itself.
- The rule VM reaches the board only through `Position`, so an ensemble implementation
  would substitute at one seam rather than everywhere.

The design, if it is built:

- An `EnsemblePosition` holds a list of basis positions with **integer** amplitudes -
  rationals with a common denominator, never floats, because determinism across
  machines is a hard requirement (ADR-0005).
- Move generation over an ensemble is the union of the per-basis move sets, so cost is
  O(basis states) - and the basis-state count grows multiplicatively with every split.
  A cap plus a merge rule for identical boards is mandatory, not an optimisation.
- Measurement happens on capture: the ensemble collapses to the basis states consistent
  with the observed outcome, chosen by the game's seeded RNG so a replay reproduces it.
- Hashing must cover the ensemble, not a board, so the transposition table and the
  replay corpus both need a different key.

## Consequences

The spec's claim that custom fields cover quantum chess is not met, and saying so now
is cheaper than discovering it at the end. Everything else the spec lists in the same
sentence - explosive chess, regional variants, draughts-like capture rules - *is*
covered by fields and rules, and ships.

The honest summary for anyone reading the variant catalogue: EndspielZero can express games
whose state is a board. It cannot yet express games whose state is a distribution over
boards.

## Alternatives considered

- **A field holding a probability.** Does not work: the correlations between pieces are
  the whole game, and per-piece state cannot represent them. Two pieces each "50% here"
  is not the same as the two-board ensemble that produced those marginals.
- **Building it in M5 anyway.** It would double the milestone and put an unproven,
  performance-hostile abstraction underneath an engine whose other features are not
  finished. Better after the temporal layer (M6), which also generalises what a
  "position" is and may share machinery.

## How to reverse this

Nothing has been built that would have to be undone. The work is additive: an ensemble
type behind the interfaces named above.
