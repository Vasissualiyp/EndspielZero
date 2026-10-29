# Cylindrical chess

Definition: [`variants/cylinder.toml`](../../variants/cylinder.toml)

Standard chess with the a-file and h-file glued together. A rook on a1 attacks h1; a
bishop leaving the left edge reappears on the right, still on the same diagonal,
because the geometry transports the direction and not merely the position.

The rank axis keeps its walls and the gluing preserves orientation, so everything that
depends on "forward" or on "the last rank" carries over untouched: pawns, promotion,
en passant and castling all work exactly as in standard chess. That makes this the
cleanest demonstration that a topology change is a single data block.

**Rules:** EndspielZero's own reading of a common variant idea. Perft: 20 and 392 at
depths 1 and 2 - note that the wrap makes some moves illegal that are legal in
standard chess, so the depth-2 count is *lower* than 400.
