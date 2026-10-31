# Architecture Decision Records

One file per decision, numbered, **immutable once merged**. To change a decision,
write a new ADR that supersedes the old one and edit only the old one's Status
line to `Superseded by ADR-NNNN`.

Every decision that constrains future code belongs here. If a code review
argument would be settled by "we decided that already", the decision needs an ADR.

Template: [`template.md`](template.md).

| # | Decision | Status |
|---|---|---|
| [0001](0001-language-and-standard.md) | Raw C++23, no game engine | Accepted |
| [0002](0002-build-and-test.md) | CMake + Catch2 v3 + nix flake | Accepted |
| [0003](0003-dimension-representation.md) | Runtime dims, `kMaxDims = 8` budget | Accepted |
| [0004](0004-geometry-model.md) | Analytic transition group + boundary transport tables | Accepted |
| [0005](0005-variant-definition.md) | Declarative data + sandboxed rule-effect VM | Accepted |
| [0006](0006-graphics-stack.md) | SDL3 + Vulkan 1.3 | Accepted |
| [0007](0007-temporal-model.md) | Time travel as policies over extra axes | Accepted |
| [0008](0008-license.md) | GPL-3.0-or-later | Accepted |
| [0009](0009-tdd-and-oracle.md) | Naive oracle + differential/property testing | Accepted |
| [0010](0010-closed-surface-ray-semantics.md) | Ray termination, dedup, and attack queries on glued boards | Accepted |
| [0011](0011-headless-first-renderer.md) | Headless-first renderer; the window is a blit | Superseded by ADR-0018 |
| [0012](0012-quantum-chess.md) | Quantum chess needs an ensemble; not built, and why | Accepted |
| [0013](0013-axis-pitch.md) | An axis may be sampled more finely than a piece moves along it | Accepted |
| [0016](0016-move-camera-is-pure-over-the-trace.md) | The move camera is a pure function of the generalised move trace | Accepted |
| [0017](0017-camera-policy-is-data-keyed-by-geometry.md) | Camera policy is data, keyed by geometry kind, not by dimension | Accepted |
| [0018](0018-interactive-renders-into-the-swapchain.md) | The interactive path renders into the swapchain; two frames in flight | Accepted |
| [0019](0019-the-play-surface-is-posed-not-only-shaped.md) | A square on the play surface is a patch of it, and the board is posed | Accepted |
| [0020](0020-a-directional-piece-turns-round-at-a-reversing-seam.md) | A directional piece turns round at a seam that reverses its forward | Accepted |
| [0021](0021-theme-materials-are-presentation-only.md) | Theme materials are presentation-only per-instance shading | Accepted |
| [0022](0022-explicit-presentation-time-and-holograms.md) | Explicit presentation time and opaque holographic pieces | Accepted |
