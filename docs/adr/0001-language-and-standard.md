# ADR-0001: Raw C++23, no game engine

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
Performance is a stated paramount requirement: the worst named case is a 7-axis
lattice (5-D board + turn + timeline) where move generation is the inner loop of
both play and, later, AI search. The data structures are unusual enough
(N-dimensional lattices, non-orientable transport tables) that no engine's scene
or entity model helps, and several would actively obstruct them.

## Decision
Raw C++23. Engine core (`endspielzero_base`, `endspielzero_core`) depends on nothing but
the standard library. No game engine, no ECS framework, no scripting runtime in
the core.

## Consequences
Full control of memory layout, which is where the performance lives. More code to
write, especially for the renderer (ADR-0006). Trivial portability and trivial
embedding into a server binary. Long-term maintenance burden is ours alone.

## Alternatives considered
- **A game engine (Godot/Unreal/Unity):** their spatial and entity abstractions
  are wrong for N-D lattices, and licensing conflicts with GPL-3.0 (ADR-0008).
- **Rust:** genuinely attractive for correctness, but the spec asks for C++, and
  C++ has the stronger Steam/Vulkan ecosystem story.

## How to reverse this
Not reversible cheaply. The layering (ARCH §1) at least confines a rewrite to one
layer at a time.
