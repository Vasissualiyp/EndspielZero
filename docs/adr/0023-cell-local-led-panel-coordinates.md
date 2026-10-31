<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ADR-0023: Cell-local coordinates for LED panels

- **Status:** Accepted
- **Date:** 2026-10-31

## Context

ADR-0021's glass and wood use existing object coordinates; a sampled play surface
is one mesh in world space. A pixel matrix projected through those coordinates
would swim or stretch as squares slide over a curved board. ADR-0022's holograms
already use normalized local height, independently of world orientation.

## Decision

Append a three-float panel coordinate to MeshVertex (56 bytes total). Its xy is
cell-local UV, and its tag is zero for ordinary mesh fallback, +1 for sampled faces,
-1 for sidewalls. The existing surface emitter derives UV from its subdivision
indices, identically on top and bottom; sidewall vertices retain endpoint UV but
disable face shading. Every cell spans [0,1]² regardless of world pose. Positions,
normals, indices and picking do not change. Ordinary authored meshes default to
zero and use slab-local xy for their panel fallback. Other materials ignore it.

Shader material 7 is LED, leaving turned wood at 6. Manifold selects LED cells,
Hologram pieces and Glass plinth. LED shape panels are opaque by default; explicit
geometry ghost remains available. Actual Glass retains its older automatic ghost.
Both Glass and LED portal rails glow; mirrors stay metal.

The face is a neutral 24×24 emitter matrix modulating the existing checker/highlight
colour, with a subdued bezel and unpatterned sidewalls. Derivatives antialias emitter
edges and blend undersampled pitches continuously toward their analytic area mean.
No texture asset, descriptors, RGB cycling or additional pass is introduced.

## Consequences

All mesh uploads/bindings must use sizeof(MeshVertex), and C++/GLSL location 12 must
agree. Surface sampling and picking retain one source of truth. The larger vertices
cost bandwidth, so compare real frame timings; fine-grid quality requires distant
and oblique artifact review. Existing explicit unsorted ghost overlap remains possible.
