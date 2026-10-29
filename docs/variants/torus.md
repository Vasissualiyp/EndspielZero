# Toroidal chess

Definition: [`variants/torus.toml`](../../variants/torus.toml)

Both axes glued: the board has no edges at all. Every cell has exactly eight
neighbours, no piece can ever be cornered, and a rook's rank is a closed loop of eight
cells.

Pawns survive - a torus is orientable, so "forward" still has a consistent global
meaning - but promotion does not, because there is no last rank to reach. No
`[promotion]` block is declared and pawns simply never promote. Castling is dropped:
with the files glued, "between the king and the rook" is ambiguous.

**The opening setup is not the usual array.** On a torus rank 1 and rank 8 are
adjacent, so the standard array puts the two back rows in contact and White begins in
an inescapable check. The armies are placed four ranks apart instead.

**Rules:** EndspielZero's own. Perft: 54 and 2535.
