<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ADR-0027: Neural space beyond the physical rack

- **Status:** Accepted
- **Date:** 2026-11-24
- **Supersedes:** ADR-0026 only for the neural-space exception below.

## Context
ADR-0026 requires every 3-D object to belong to the lab and every hologram to
have a visible projector. M25.5 explicitly introduces a virtual space for games
whose deployed slab count exceeds the authored rack capacity. Rendering hundreds
of physical carriers is both visually unsuitable and too costly.

## Decision
The lab remains physical under ADR-0026. When deployed slab count exceeds the
rack's counted bays, the implant may present a virtual 3-D game space. Its boards,
pieces and bridges are light and require neither visible projectors nor physical
carriers. The transition uses one physical stand-in slab, neural code, implant
handshake and eyelid closure; leaving restores the physical room and stows that
slab. Virtual space builds and draws no rig hardware.

The threshold follows the current layout and live game, including new timelines;
it is not a list of variant names. Explicit debug forcing remains available.
The implant's front-on 2-D picture is likewise a perception mode. These changes
are presentation only: engine state, movement, topology, picking correspondence
and the lab-disabled presentation keep their existing contracts.

## Consequences
Large layouts can use inexpensive light sheets and streaks without pretending
that every cell rests on physical machinery. Layout changes still carry the
visible cells and bridge endpoints continuously, with interaction guarded while
the picture moves. Physical and neural-space ownership must stay explicit through
handoffs so room hardware does not leak into the virtual space.

## Alternatives considered
- More physical rack bays and carriers: repeats hardware cost for every slab and
  cannot satisfy the requested virtual presentation.
- Hide cells or connections: loses playable information and is not an acceptable
  performance optimization.

## How to reverse this
Disable the neural-space route and restore physical deployment for every layout;
large-layout performance and framing would then require another solution. Keep
ADR-0026's original text unchanged as the record of its original decision.

## Amendment (2026-11-28, M25.7)
The threshold above also holds for a variant that can add boards. A variant with a
temporal or multiverse axis (5D) creates boards during play, so it enters neural space at
game start, decided from the variant (`view::canAddSlabs`) and not from the starting slab
count. The live-count test remains as the fallback for any other layout that grows past
the rack mid-game.
