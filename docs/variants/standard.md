# Standard chess

Definition: [`variants/standard.toml`](../../variants/standard.toml)

The reference implementation target, defined entirely in EndspielZero's general
primitives: a rook is `[1]^inf` sliding, a knight is the magnitudes `{1,2}` on any two
distinct axes, castling is a displacement template with cell lists, and "forward" is a
declared orientation axis.

**Verification.** Reproduces the published perft node counts exactly: 119,060,324 at
depth 6 from the initial position, and depth 5 on Kiwipete, position 3, position 4 and
position 5. This is the only variant here with external ground truth, and it is what
proves the generalized machinery correct.

`variants/standard.toml` and the programmatic `makeStandardChess()` are two
independent descriptions of the same game; a golden test asserts they produce an
identical `variantId` and identical play, so the data path and the code path cannot
drift apart.

**Note.** The perft suite's usual sixth position is deliberately absent - see the
comment in `tests/perft/test_perft_positions.cpp`.
