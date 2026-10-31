# ADR-0021: Theme materials are presentation-only per-instance shading

- **Status:** Accepted
- **Date:** 2026-10-30

## Context

The board and pieces shared a single matte shader with one metal switch for mirrors.
Console's wood and manifold's glass/crystal need different shading without putting
appearance in variant identity, saved games, or the deterministic engine. The
geometry-view board is one authored mesh rather than a cell instance per square.

## Decision

`view::Theme::materials` names the material of cells, pieces and plinth. The renderer
maps those names to the existing last float in the 84-byte `Instance`: integer shader
ids 0 matte, 1 metal, 2 plank wood, 3 glass, 4 crystal, 5 glow and 6 turned wood;
the fractional part is wood's stable variation. Every piece of the same type and
colour receives the same variation regardless of position. The vertex shader supplies
object-space coordinates before orientation and translation. The existing matte and
mirror shader calculations remain unchanged. No material enters `VariantSpec`,
`Settings` or game files. Captures can override the theme and force matte without
persisting either choice.

The surface mesh shares an identity instance, so its wood coordinate is in world
space; slides across a shape can move squares through that grain. Its glass is drawn
with the existing ghost blend pipeline at reduced alpha by default, whereas ordinary
glass cells use opaque Fresnel shading until a separately ordered transparent board
pass exists.

## Consequences

One theme switch styles all boards and both front ends without new instance buffers,
textures or pipelines. All-matte instances retain their old values, including metal
mirror rails. A fraction of a float gives finite grain variation and ids must remain
integer-exact. The procedural noise costs fragment time; quality and frame time need
to be checked on actual target GPUs. Transparent shape patches are not depth-sorted.

## Alternatives considered

- A `BoardOptions` material setting: duplicates the theme state through GUI wiring.
- Texture assets / normal maps: extra asset packaging, UV layout and sampling paths
  before the piece models and board surfaces even have stable UVs.
- A new pipeline per material: needless draw splitting and more Vulkan state.
- World-space grain on every instance: makes moving pieces visibly swim.

## How to reverse this

Add explicit material/UV fields to mesh vertices and a material buffer or descriptor,
then migrate all emission sites and shaders together. This changes vertex formats,
shader interfaces and capture expectations; it is a renderer-wide migration rather
than a palette adjustment.
