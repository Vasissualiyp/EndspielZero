# Klein bottle chess

Definition: [`variants/klein.toml`](../../variants/klein.toml)

The files are glued straight and the ranks are glued with a flip of the file
coordinate, giving a closed, non-orientable surface.

It has a consequence you can feel while playing: **a bishop is no longer colour-bound.**
No global two-colouring of a Klein bottle exists, so a bishop can reach every cell on
the board. The test suite asserts exactly that, with a torus as the control showing
the usual half - it is a far sharper check on the direction-transport code than any
hand-written move list, because getting transport subtly wrong changes the reachable
set immediately.

As on the Möbius board there are no pawns, for the same reason. The army is also
small - kings, rooks and knights - and offset in both rank and file: with bishops
giving check from almost anywhere and the ranks glued, larger or aligned armies start
the game already lost.

**Rules:** EndspielZero's own. Perft: 48 and 1977.
