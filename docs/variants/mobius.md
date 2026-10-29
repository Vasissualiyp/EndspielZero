# Möbius chess

Definition: [`variants/mobius.toml`](../../variants/mobius.toml)

The files are glued into a loop with a half-twist: crossing the file seam reverses the
rank coordinate. A rook travelling along a rank returns on the mirrored rank and needs
two full circuits to come home - a fact asserted directly as a test.

The surface is non-orientable, so there is no consistent notion of which way is up,
and **the variant fields no pawns.** A piece that always moves "forward" is not well
defined here: crossing the seam would reverse its forward direction, and any answer to
that is a design choice rather than a rule. Rather than bake one in silently, this
variant has no oriented pieces at all.

**Rules:** EndspielZero's own. Perft: 69 and 4003 - higher than standard chess, because
the twist connects White's first rank directly to Black's eighth.
