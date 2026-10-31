<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ADR-0024: Explicit cell display response state

- **Status:** Accepted
- **Date:** 2026-10-31

## Context

ADR-0021 keeps materials cosmetic; ADR-0022 gives stateless materials explicit
presentation seconds; ADR-0023 gives each LED cell its own surface coordinates.
Manifold now needs sparse local interference and a brief response when a panel's
actual gameplay colour changes. Time and UV alone cannot identify a colour-change
event. Geometry boards contain many cells in one vertex-coloured identity instance,
so instance identity is also insufficient for per-cell phase or event data.

`BoardRenderer::buildInstances` is pure. Making it remember colours would introduce
call-order-dependent behaviour into tests, captures and repeated snapshots. Default
independent still and clip captures do not carry a common colour-event timeline.
Same-variant imports and debug-console FEN replacement can mutate an existing
Session without producing a GUI load request.

## Decision

Keep ambient interference a pure function of stable logical-cell seed, cell-local
UV and explicit presentation seconds. Append a zero-default three-float panel
signal to both mesh vertices and instances: seed, response gain and effect weight.
Exactly one source supplies a panel: instance data for ordinary slabs, vertex data
for sampled surfaces. Other materials ignore it. The existing panel coordinates,
material IDs, alpha, topology data and picking geometry retain their meanings.
This extends the payload described by ADR-0023; it does not replace its UV contract.

A GUI-owned presentation helper explicitly observes resolved cell-colour targets
and computes settling gains from supplied timestamps. Pure instance construction
may optionally emit target/range metadata but does not store previous frames.
New target colours become visible immediately; only a small luminance overdrive
settles. State never enters the engine, game identity or replay files.

Expose a Session presentation epoch for discontinuous replacement/reset operations.
The GUI checks it after UI/request processing, along with Session identity and
relevant display configuration. Moves, undo, hover and selection do not increment
it. Imports conservatively invalidate before the potentially partially mutating
application step; valid FEN replacement and reset invalidate at their mutation.
Console edits and replay seeks inherit this boundary through existing Session APIs.

Default independent CLI captures omit transient response history and render settled
LCD gains. Ambient effects remain explicit-time and capture reproducibly. Response
captures use an explicit target-event sequence in the offscreen harness; live play
uses observed interactive events. Extra warmup frames cannot manufacture history.
A future arbitrary replay response capture requires reconstructed event timestamps,
not a hidden renderer clock or a new meaning for overture position.

## Consequences

Vertex/instance strides and C++/GLSL attributes must change together; bandwidth
increases, and real rendering timing remains necessary. Optional event metadata is
only collected for consumers that request it. A caller owns response reset and
lifetime boundaries explicitly, including session replacement and time rewind.

The design preserves pure material sampling for independent captures while honestly
limiting them to settled colour-response state. It avoids delayed semantic colours,
new GPU passes, transparency sorting and engine-dependent visual randomness.
Tests must cover console reloads, partial failed imports, ordinary moves that retain
history, geometry-cell identity, repeated explicit samples and still/clip agreement.

Implementation detail and acceptance: `docs/plan/M23.8-led-ambient-and-response.md`.
