# cube5 - three-dimensional chess

Definition: [`variants/cube5.toml`](../../variants/cube5.toml)

A 5×5×5 board. The piece set follows Raumschach (Maack, 1907). The **unicorn** travels
the triagonals (`[1,1,1]`) and the **bishop** the planar diagonals (12 directions in three
dimensions). The **queen** moves along *every straight line*: one axis (6 directions), two
(12) and three (8 triagonals), 26 in all - the historical Raumschach queen. The **king**
steps one cell along a line of one or two axes (18 directions). The queen's atoms come
from `straightLineAtoms(kUnlimited, dims)` (`src/pieces/atom.hpp`): one slider `[1]^r` for
every r up to the board's dimension, so on a tesseract she also rides the quadragonals.

*Revision 2026-10-18:* the queen used to be a "line mover" of one or two axes only, on the
reasoning that a cube diagonal belonged to the unicorn. The project owner's rule is that
the queen goes in a straight line in every dimension, so she now includes the triagonals;
the unicorn is the piece that has *only* them.

**The pawn rules and promotion are EndspielZero's own choice, not a reconstruction.** A
pawn moves one step along the rank axis and captures on the forward diagonals - which
in three dimensions means four capture directions, two inside the level and two
between levels, from the same single `[1,1]` atom. A faithful Raumschach needs its
pawn rules taken from a primary source first; this variant does not claim to be one.

**Depth-1 count derived by hand before comparing with the engine:** 10 pawn pushes,
12 knight, 12 bishop, 8 unicorn and 14 queen moves (the ten she had, plus four
triagonals), with both rooks and the king completely blocked by their own army - 56 in
total. Perft: 56 and 3095.
