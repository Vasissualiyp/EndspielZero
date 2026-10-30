# Möbius chess

Definition: [`variants/mobius.toml`](../../variants/mobius.toml)

The files are glued into a loop with a half-twist: crossing the file seam reverses the
rank coordinate. A rook travelling along a rank returns on the mirrored rank and needs
two full circuits to come home - a fact asserted directly as a test.

The surface is non-orientable, so there is no consistent notion of which way is up -
and that is what the pawns are for. **A pawn's forward is carried across the seam like
everything else.** Pushes run along the ranks, which are not glued, so a pawn only
meets the twist when it captures diagonally across the file seam: it lands on the
mirrored rank facing the other way, and from then on it moves, double-steps and
promotes like a pawn of the other colour. Its colour is unchanged - it is a White pawn
walking towards White's home, and it promotes on White's first rank. Cross back and it
turns round again. The engine calls this *flipped* (`Piece::flipped`, toggled by a move
flagged `Reorient`); FEN writes a flipped piece with a trailing `'` (`P'`). Any piece
with a `forward = true` atom behaves this way on any board with an orientation-reversing
seam or wall unless it declares `flips_on_mirror = false`.

**Rules:** EndspielZero's own; no castling. En passant works as on any board, and the twist
makes it stranger: a black pawn on h7 answers White's a2-a4, because h7's forward diagonal
wraps across the file seam and lands on a3 - exactly the square the White pawn passed over.
Perft: 24, 578, 15782 - higher than standard chess, because the twist connects the h-file
to the a-file with the ranks mirrored, so the edge pawns and knights already see across it.
