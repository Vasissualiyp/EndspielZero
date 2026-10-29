# M0 — Foundations & Infrastructure

**Goal:** a repo where a single command gives any human or agent a working,
reproducible toolchain, and where the first real test can be written with zero
friction. No game logic in this milestone.

**Exit condition:** `nix develop -c nix-build-check` (alias for the full gate)
passes on a clean checkout, with a deliberately trivial test suite, and the
layering test already enforces the dependency rules from ARCH §1.

## M0.1 Nix flake

- `flake.nix` with `devShells.default`, `packages.default`, `checks.*`, and
  `formatter`. Pin nixpkgs; commit `flake.lock`.
- Dev shell contents: gcc 15 and clang (both, for the two-compiler gate), cmake,
  ninja, ccache, gdb, lldb, valgrind, clang-tools (tidy/format), lcov/gcovr,
  python3 (tooling scripts), git, and the M4 deps behind a flag
  (`vulkan-headers`, `vulkan-loader`, `vulkan-validation-layers`, `shaderc`,
  `sdl3`, `vulkan-memory-allocator`, `spirv-tools`, `renderdoc`).
- Two shells: `default` (core dev, no graphics deps → fast) and `gfx`
  (adds Vulkan/SDL3). Keeps M1–M3 iteration lean.
- `packages.default` builds the release binary hermetically; `checks.default`
  runs the full gate so `nix flake check` *is* CI.
- `.envrc` (`use flake`) for direnv users; documented as optional.
- **Tests:** `nix flake check` on a clean clone in a temp dir; a script asserting
  the dev shell provides every tool the docs claim (`tools/check-shell.sh`).

## M0.2 CMake skeleton

- Top-level `CMakeLists.txt`, C++23, no compiler extensions.
- Targets: `endspielzero_base`, `endspielzero_core` (aggregates L1–L8), `endspielzero_io`,
  `endspielzero_cli`, `endspielzero_tests`, `endspielzero_bench`; `endspielzero_render`,
  `endspielzero_net`, `endspielzero_app` added in later milestones.
- One CMake target per architectural layer directory, with `target_link_libraries`
  expressing exactly the allowed downward edges — so violating the layer map is a
  *link error*, not a review comment.
- `CMakePresets.json`: `dev` (Debug, sanitizers off, fast), `asan` (ASan+UBSan),
  `tsan`, `release`, `coverage`, `bench`, and `clang-*` mirrors of each.
- Warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wold-style-cast
  -Werror`. Third-party headers included as SYSTEM.
- `-fno-exceptions` is **not** used (we want `std::expected`-style plus
  exceptions at the IO boundary), but the engine core is exception-free by
  convention, checked by a tidy rule.
- Dependencies fetched via nix, not vendored. `third_party/` holds only single-
  header shims we author.
- **Tests:** every preset configures and builds clean; a test asserting the
  layering link edges exist as declared (`tests/arch/test_cmake_edges.cpp`
  reads a generated manifest).

## M0.3 Test harness

- Catch2 v3 (ADR-0002). Test binary split into `unit`, `property`, `golden`,
  `perft`, `arch` labels registered with CTest, so `ctest -L unit` is fast.
- Helpers in `tests/support/`:
  - `RandomVariantFactory` — generates random valid `VariantSpec`s (dims,
    extents, atom sets, later geometry) from a seed. This is the engine of the
    property-testing strategy: most correctness bugs in a generalized engine are
    found by random variants, not hand-written cases.
  - `Oracle` — the naive reference engine (ARCH §7), added in M1.
  - `PositionBuilder` — fluent construction of positions in tests.
  - `AllocTripwire` — a global allocator hook that fails a test if any allocation
    occurs inside a scoped region.
  - `GoldenFile` — read/write/compare golden corpora with a `--update-golden`
    flag, so regenerating goldens is deliberate and reviewable.
- **Tests:** self-tests for each helper (a tripwire that does not trip is worse
  than no tripwire); `RandomVariantFactory` determinism test (same seed → same
  variant, across compilers).

## M0.4 L0 base library

Written test-first, each with its own test file. Deliberately small.

| Component | Why it exists | Key tests |
|---|---|---|
| `SmallVec<T,N>` | movegen output with no heap | capacity overflow behaviour, trivial-type memcpy path, no-alloc under tripwire |
| `Arena` / `ScratchArena` | per-search scratch, LIFO reset | alignment, reset semantics, high-water mark accounting |
| `Result<T,E>` (alias to `std::expected`) + `ErrorCode` | IO/validation errors without exceptions | error propagation, no-discard enforcement |
| `BitWords` | occupancy over arbitrary cell counts, with a 1-word fast path | set/clear/test/iterate-set-bits, popcount, equality with `std::bitset` reference |
| `Xoshiro256++` RNG | deterministic tests, later self-play | fixed-seed golden stream, identical across gcc/clang |
| `Zobrist` keys | incremental hashing | key uniqueness stats, derivation from (seed, variantId) reproducibility |
| `diag` counters/trace | profiling and debugging generalized code | compiled-out in release (a test asserts zero size impact) |
| `log` | structured, level-filtered, no allocation on the disabled path | disabled-path allocation test |

**No floats anywhere in `endspielzero_base` or `endspielzero_core`.** A tidy rule plus a
grep test enforces it. **[INVARIANT]**

## M0.5 CI

- GitHub Actions (also runnable locally): matrix over {gcc, clang} ×
  {dev, release} plus single jobs for asan, tsan, coverage, bench, and
  `nix flake check`.
- Bench job compares against `bench/baselines/` and comments the delta; fails
  >10% regression.
- Coverage uploaded as an artifact with the gate from the roadmap.
- A `tools/precommit.sh` that runs format + tidy + fast tests, documented in
  AGENTS.md as the thing to run before every commit.

## M0.6 Repo documentation

- `AGENTS.md`: build/test commands, layer map, where to add what, invariants,
  conventions, the TDD loop, and a file-by-file index. Written to be short and
  high-signal — it is loaded into every agent's context.
- `CLAUDE.md`: a pointer to AGENTS.md and nothing else.
- `README.md`: what EndspielZero is, quickstart, status, license.
- `LICENSE`: GPL-3.0 (ADR-0008). SPDX headers on every source file, checked by a
  test.
- `docs/adr/`: ADRs 0001–0008 recording the decisions already made, plus the
  template and the process.
- `CONTRIBUTING.md`: the TDD rules, ADR process, commit conventions.

## M0.7 Project skills for repetitive tasks

The repetitive work in this project is highly stereotyped (add a piece, add a
variant, add a geometry, add an effect primitive, run the gate). Each stereotype
gets a project-local skill in `.claude/skills/<name>/SKILL.md` so that the
procedure lives in the repo instead of in whoever happens to be working.

Full details and the per-skill contents: [skills plan](skills.md).

Shipped in M0 (the ones that are useful immediately):

| Skill | Replaces |
|---|---|
| `cb-tdd-step` | the red→green→gate→commit loop and which preset to run when |
| `cb-gate` | running the full milestone exit gate and reading its output |
| `cb-new-module` | scaffolding a layer module: dirs, CMake edge, test file, AGENTS.md row |
| `cb-adr` | creating the next numbered ADR from the template |

Added in the milestone that first needs them: `cb-new-piece`, `cb-new-variant`
(M1), `cb-new-geometry` (M3), `cb-new-effect` (M5), `cb-perft-golden` (M1),
`cb-bench-baseline` (M1), `cb-shader` (M4).

**Tests:** skills are checked like code — `tests/arch/test_skills.cpp` (or a
tooling script) asserts every skill's frontmatter is valid, every command it
tells you to run exists in the dev shell, and every path it references exists.
A skill that lies is worse than no skill.

## Generalization test

Adding a new architectural layer or a new build configuration must not require
touching more than its own directory plus one line in the top-level CMake and
one row in the AGENTS.md index.

## Acceptance facts

1. A clean clone plus `nix develop` yields a shell that builds and tests without
   network access beyond the flake inputs.
2. `nix flake check` runs the entire gate described in the roadmap.
3. Violating the layer map fails the build, not review.
4. A float introduced into `endspielzero_core` fails a test.
5. An allocation inside a tripwire region fails a test.
6. Every L0 component has a test file and ≥90% line coverage.
7. Every shipped skill's commands and paths are verified by a test.

---

## Status: complete

Recorded 2026-09-28.

All acceptance facts hold. Two things the plan did not anticipate, both caused by the
nix toolchain and both fixed at the source rather than worked around:

- **nix injects `-O2` through `NIX_CFLAGS_COMPILE`,** so a "Debug" build was silently
  optimized - and optimized away an unused container's allocation, which made the
  allocation tripwire's own self-test pass for the wrong reason.
  `CMAKE_CXX_FLAGS_DEBUG` is now pinned to `-g -O0`, and the tripwire self-test uses
  `::operator new` directly so it cannot be elided.
- **`_FORTIFY_SOURCE` emits a `#warning` at `-O0`,** which is fatal under `-Werror`.
  The dev shell sets `hardeningDisable = [ "fortify" "fortify3" ]` rather than
  weakening the project's warning settings.

Deferred: `tsan` and `coverage` presets exist and configure, but no threads exist yet
(they arrive with M4), and the coverage gate has not been enforced in anger.
