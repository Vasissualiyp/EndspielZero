# torus3d - a three-dimensional torus

Definition: [`variants/torus3d.toml`](../../variants/torus3d.toml)

A 4×4×4 board with all three axes glued. Every one of a king's 26 directions is
always available and no cell is special - the most uniform board shipped.

The armies are separated along the level axis rather than the rank axis: with every
axis glued, a 4-cell axis puts opposite ends only two steps apart, and facing each
other across the ranks starts the game with the two kings attacking one another.

**Rules:** EndspielZero's own. Perft: 34 and 1028.
