# hyper4 - four-dimensional chess

Definition: [`variants/hyper4.toml`](../../variants/hyper4.toml)

A 4×4×4×4 board on which both armies start on a single two-dimensional face, so the
opening looks like a tiny game of chess - and then pieces can leave the plane
entirely. A knight here has 48 directions, `P(4,2) × 2²`, and a rook has eight, all
from the same one-line atom declarations that give 8 and 4 in two dimensions.

**Depth-1 count derived by hand before comparing with the engine:** 6 pawn captures,
12 rook, 14 knight and 7 king moves - the king's two rank-1 steps are refused because
a black pawn covers them *through the level and aeon axes*, which is the sort of thing
only a genuine 4-D check test finds. 39 in total. Perft: 39 and 1380.

**Rules:** EndspielZero's own, fully stated in the variant file. The board is deliberately
small: 256 cells keeps perft depths meaningful.
