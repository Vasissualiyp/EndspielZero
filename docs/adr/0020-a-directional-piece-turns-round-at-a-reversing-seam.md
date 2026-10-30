# ADR-0020: A directional piece turns round at a seam that reverses its forward

- **Status:** Accepted
- **Date:** 2026-10-29

## Context

M3.4 listed three candidate policies for a pawn on a non-orientable board - keep the
global axis sign, follow the transported direction, forbid crossing - and chose none: the
non-orientable variants simply fielded no oriented piece, and a test asserted it. That
kept EndspielZero honest but left "pawns on a Möbius band" inexpressible, and the band's
whole point - that up is not a global fact - had nothing on the board to show it.

The engine already transports a ray's *direction* across every seam (ADR-0004,
`Geometry::stepBoundary`). What it did not carry was a piece's *frame*: whether the
orientation axis it set off along comes back pointing the same way.

## Decision

**Follow the transported direction.** A piece type with a `forward = true` atom
*reorients* (`PieceTypeDef::reorients`, derived) when the geometry has an
orientation-reversing face, unless it declares `flips_on_mirror = false`.

- Movegen tracks the unit vector along the orientation axis through every seam a move
  crosses (`Walker::upAxis/upSign`, only for reorienting pieces). If it lands reversed,
  the move carries `MoveFlag::Reorient`.
- `Position::make` toggles the moved piece's `Piece::flipped` bit (`kPieceFlipped` in the
  previously reserved `flags` byte); `unmake` restores the mover's flags from `Undo`.
- A flipped piece uses the **other colour's** forward directions, home region and
  promotion region (`Piece::orientation()`); occupancy and capture still follow its own
  colour. A flipped White pawn walks towards White's home and promotes on White's first
  rank. Promotion is judged by the orientation *after* the move, so a capture that crosses
  the seam onto the new last rank promotes.
- The bit is hashed (`Zobrist::flipped`), written in FEN as a trailing `'` (`P'`), and
  refused by FEN on a piece that cannot flip. A piece that promotes sheds it unless the new
  type also reorients.

## Consequences

- Möbius ships with pawns; any future board with a reversing seam gets the same rule for
  free, and a reflecting wall across the orientation axis turns a pawn round too.
- Boards without a reversing face pay nothing: `reorients` is false, no walker tracks a
  frame, and no shipped perft count other than Möbius's changed.
- Two paths to one cell that differ only in parity are two distinct moves (the flag is
  part of `Move`'s identity). Only a forward-only *slider* could produce that; no shipped
  piece is one.
- The box fast path of `isAttacked` assumes no piece is flipped, which holds because only
  a non-box geometry can flip one; glued boards use the forward scan, which generates moves
  and so sees flipped pieces exactly.
- The oracle (`tests/oracle`) carries the frame with its own fold over `faceTransform`, so
  the property sweep checks the engine's tracking against an independent implementation.

## Alternatives considered

- **Keep the global axis sign.** A pawn would ignore the twist entirely. Simple, but the
  pawn's moves would then disagree with the board's own transport: a forward step across
  the seam would be a backward step in the frame the piece arrived in, which is exactly the
  inconsistency the band exists to show.
- **Forbid crossing.** Makes the seam a wall for pawns only; an arbitrary special case that
  also removes the band's most interesting captures.
- **A custom field and rule.** The field machinery exists (M5), but movegen cannot read
  fields, and a rule cannot change which directions an atom expands to. The bit had to be
  visible to move generation, so it lives in `Piece::flags`.

## How to reverse this

Set `flips_on_mirror = false` on a piece to get "keep the global sign" for it. To remove
the policy entirely: drop `MoveFlag::Reorient` and the walker's frame fields, clear the
flag handling in `Position::make/unmake` and FEN, and take the pawns back out of
`mobius.toml` - a few hundred lines, with the perft golden for Möbius reverting to
69/4003.
