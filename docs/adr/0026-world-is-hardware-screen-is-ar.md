# ADR-0026: The world is hardware, the screen is AR

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
The menus were a flat 2-D drawing (a polygon field, a grid floor, the overture flattened
into an ImGui draw list), while the game was a lit 3-D scene. Moving between them could
only be a cut or a cross-fade. The game's look is meant to be futuristic and physical at
once: an LED slab carrying holographic pieces. Higher-dimensional variants need many
boards at arbitrary positions, so "one board on a table" cannot be the model.

## Decision
- Everything rendered in 3-D is a physical object in one room, a 2050 home lab: real
  materials, lit only by lights that exist in the room. Every hologram has a visible
  projector.
- Everything flat on the screen (menus, HUD, panels, console) is the player's AR glasses
  layer, drawn over the world in the existing panel language.
- The menus and the game share one scene and one camera. Screens are camera stations;
  screen changes are camera moves, except at calm intensity (cross-fade).
- Boards are physical slabs that a ceiling gantry places at the positions `view::layout`
  computes, so any layout has a physical form without per-variant hardware.
- The lab is data (`assets/lab/lab.toml`), built from a procedural kit through the
  existing board pipeline. A setting (`labScene`) restores the old presentation.

## Consequences
- Easy: consistent transitions; a physical story for boot, shutdown, timelines; art can
  replace kit parts without code.
- Hard: the board shader gains a small light list; the free game camera must respect room
  bounds (clamp and wall fade); the geometry view needs a projector story.
- Cost: menus now render the 3-D scene every frame; the overture moves from ImGui to
  instances.

## Alternatives considered
- Boards floating in open space with a mechanical rig: rejected by the user in favour of
  a lab, which is more grounded.
- An endless truss lattice: scales to many boards, but has no centre and is busy.
- World-space hologram panels for the menus: more diegetic, but less readable at angles,
  conflicts with auto UI scale, and costs more.

## How to reverse this
Set `labScene` off (the M23.11 presentation stays behind it until M24 §8 removes the
manifold menu backdrop). Fully reversing it means restoring the backdrop path from git,
which is a few days of work.
