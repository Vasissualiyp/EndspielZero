# EndspielZero

A FOSS sandbox for chess-like games of essentially arbitrary shape.

- **Any number of dimensions** — 2-D, 3-D, 4-D and beyond from one engine.
- **Arbitrary boundary topology** — cylinders, tori, Möbius bands, Klein bottles,
  projective planes, reflecting walls, and their higher-dimensional analogues.
- **One movement algebra** — every piece is a set of *vector-move atoms*
  (`rook = [1]^∞`, `knight = [1,2]^1`, `king = [1]^1 + [1,1]^1`), which expand
  correctly into any number of dimensions.
- **Custom data fields and declarative rules** — explosive chess, checkers,
  regional variants, all as data.
- **Time travel** — faithful 5D-chess-style timelines, built as extra lattice axes.
- **Steam Workshop, multiplayer, and per-variant trainable AI** on the roadmap.

Performance and extensibility are the two hard constraints: the target worst case
is a 5-D board on a torus *with* time travel — a seven-axis lattice — and adding
the next variant, dimension, or topology should be data, not code.

## Status

**M0–M6 complete.** The engine, the Vulkan interface, the rule VM and the temporal /
multiverse model all ship; the game plays N-dimensional and time-travel variants. Next
is M7, the GUI editors. The design and plan are the reference:

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — the design
- [`docs/plan/00-roadmap.md`](docs/plan/00-roadmap.md) — milestones M0–M10, each
  with its own detailed plan
- [`docs/adr/`](docs/adr/README.md) — decisions and why
- [`AGENTS.md`](AGENTS.md) — the map, for humans and agents alike

## Quickstart

```bash
nix develop                    # reproducible toolchain
cmake --preset dev && cmake --build build/dev
ctest --preset dev
```

Nix is the supported path; a plain CMake build with GCC 15 / Clang and Vulkan SDK
installed will also work.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE) and [ADR-0008](docs/adr/0008-license.md).
