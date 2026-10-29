# Mirror-walled chess

Definition: [`variants/mirrorbox.toml`](../../variants/mirrorbox.toml)

The file walls reflect instead of stopping. A ray reaching the a-file or the h-file
bounces and continues in the mirrored direction, exactly as light would.

Unlike every other geometry here, nothing is glued: the board still has 64 distinct
cells and the surface remains orientable, so pawns and promotion are kept. The face
transform does reverse handedness, which is why the engine tracks "the surface is
orientable" and "a face reverses handedness" as two separate properties.

**Rules:** EndspielZero's own. Perft: 20 and 396.
