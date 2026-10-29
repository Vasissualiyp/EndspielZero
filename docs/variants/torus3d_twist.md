# torus3d_twist - a three-dimensional torus with a twisted seam

Definition: [`variants/torus3d_twist.toml`](../../variants/torus3d_twist.toml)

A 4×4×4 board with all three axes glued, exactly like [`torus3d`](torus3d.md), except
that crossing the rank seam reverses the file coordinate - the same
`flip = ["file"]` that makes [`klein`](klein.md) a Klein bottle in two dimensions.
The result is a **non-orientable** three-torus.

This variant is EndspielZero's own construction. It exists to prove the identification
language has no dimension-specific case: a three-axis variant declares a `flip` the
same way a two-axis one does, and the direction transport that makes a Klein bottle
work in 2-D is derived from the declaration rather than written per-axis-count. It is
not a named game from the literature.

Because the board is non-orientable there is no global "forward", so - as on `klein`
and `mobius` - the file declares no pawn and no promotion. The bishop and queen remain
declared so a position can still contain them after a promotion or an edit.

**Rules:** EndspielZero's own. Perft: 42 and 1616.
