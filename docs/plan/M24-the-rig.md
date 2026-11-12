<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# M24 — The Rig: one physical lab under an AR layer

Status: **planned 2026-11-12.** Design confirmed with the user 2026-11-12 (see "Decisions").
The decision record is ADR-0026 (`docs/adr/0026-world-is-hardware-screen-is-ar.md`).

Builds on:
- M23.9–M23.11: the LED slab, the hologram pieces, boot, power-down, post, the HUD panel
  language, the shell transition clock (M23.11 §6).
- M23.12 (`docs/plan/M23.12-seam-hover-and-hologram-shutdown.md`): §2 there (the
  holograms shutting down) is a prerequisite of §6 here.
- `docs/plan/shell-motion-and-timelines.md` Part A: the single shell transition clock this
  milestone replaces with a camera.

## The problem

The menu and the game are two different worlds. The menu is a flat 2-D drawing (the
drifting polygon field `DepthField`, a grid floor, the overture flattened by
`drawOverture` into an `ImDrawList`). The game is a lit 3-D scene (`BoardRenderer`, stars,
post). Every move between them is a cut or a cross-fade, however well it is timed. The
board "appears out of nowhere" because, in the menu, there is nowhere for it to have been.
The polygon field and grid floor read as abstract cyberspace; they are not physical.

## The design

### The rule (ADR-0026)

- **The world is hardware.** Everything rendered in 3-D is a physical object in one room:
  materials, mass, panel lines, cables, status LEDs. It is lit by lights that exist in the
  room. Every hologram has a visible projector: the slab projects the pieces, the bench
  projector projects the overture.
- **The screen is the player's AR glasses.** Everything flat (menus, HUD, result panel,
  review dock, debug console) is an AR layer drawn over the world. It keeps the M23.11
  panel language exactly. It is allowed to be futuristic precisely because it is not in
  the world.
- **One scene, one camera.** The menus and the game are the same lab seen from different
  camera stations. A screen change is a camera move plus AR panels changing, never a cut.
  The only exception is calm intensity, which uses the M23.11 cross-fade.

### The setting: a home lab, 2050

Someone's small lab or garage workshop, about 25 years from now: recognisably a garage
(concrete floor, a roller door, a pegboard, shelving, a workbench, cables in trays, a
skylight), but with 2050 equipment in it. Nothing in it is grand. It is a hobbyist's
space that happens to own a remarkable machine. "Slightly more futuristic than today"
means:
- materials: anodised aluminium extrusion, matte composite panels, braided cable,
  frosted polycarbonate, small OLED status strips; no chrome, no neon tubes;
- light: practical lights only. These are a bench task lamp, LED strip under the
  shelves, a dim ceiling panel, the skylight (night: sky with stars and a faint city
  glow), and everything the machine itself emits;
- clutter is allowed but sparse and dark, so it never competes with the board;
- palette: the room is near-neutral and dark (the manifold theme's ink), so the cyan
  LEDs and holograms stay the brightest things in frame.

The far stars of M23.10 §6 survive as the sky through the skylight.

### The machine ("the Rig")

Three parts, all physically plausible and mounted to the room, not floating:

1. **The bench projector.** A squat projector unit on the workbench: a lens array in a
   ring housing, with a frosted glass disc above it. It projects the overture hologram
   (the T6 knot on the main menu, the selected variant's overture in the Library) about
   30–40 cm above itself. It is the menu's centrepiece and the source of the light on the
   bench.
2. **The slab magazine.** A wall- or rack-mounted cassette beside the bench holding the
   LED slabs edge-on, like hard drives in a bay. Each slab is the board hardware we
   already draw (LED panel, bezel, plinth), plus a back with a connector edge.
3. **The gantry.** A ceiling-mounted XY rail frame over the open floor in front of the
   bench (the deployment volume), with carriages that each lower a slab on a telescoping
   mast. A carriage can stop at any height, so the gantry can hold slabs at any 3-D
   positions in its volume, not just one per carriage in a grid.

**Why a gantry and not a table.** A game is not one board in the middle of a room. It is
whatever the variant's layout is: one slab (standard), a stacked column (cube5), a 2-D
grid of boards (hyper4), a growing lattice of timelines (5D), a lattice (t6). The gantry
does not know any of these. It is told the **slab positions** that `view::layout` already
computes (`Placement`, `Slice`, `ViewConfig`), and its carriages go there. A future layout
needs no new hardware.

- **Fitting the volume.** The layout's bounds (`view::boundsOf`) are mapped into the
  deployment volume with one uniform scale `rigScale = min(1, volume / bounds)`. A
  standard board is about 40 cm across (cell pitch 5 cm). Large layouts use the gantry's
  smaller slabs: the magazine has two bays, full-size and quarter-size tiles. The scale
  eases when a layout grows.
- **5D.** A branching timeline is a physical event: a new slab leaves the magazine, rides
  the gantry to its place beside its parent, and boots (M23.10 §3), with the timeline
  connector (`TimelineLink`) drawn between the two masts as a light-guide cable. When the
  multiverse outgrows the volume, `rigScale` eases down over 1 s and the masts re-space.
- **The geometry view** (`--geometry`, formed shapes) is not something flat slabs can do.
  In that view the slabs dim to standby and retract to the gantry's top, and a ceiling
  projector on the gantry projects the formed surface as a hologram LED surface. This is
  the same look the geometry view has now, now with a source. Returning to the flat view
  lowers the slabs again.
- **The flat 2-D view** (`--flat`) keeps the camera straight down; it is a top view of the
  deployed slabs.

### The AR layer

The player wears AR glasses. The menus are not "a menu screen". They are what the
glasses overlay on the lab.

- **Panels** are screen-space (unchanged widgets, `hudPanel`, auto UI scale). They follow
  the head with a little lag: a critically damped spring, about 0.12 s, on a small offset
  driven by camera angular velocity, at most 1.5 % of screen height. During a camera move
  the panels visibly trail and settle. This is the cue that they belong to the glasses,
  not the room. Calm intensity: no lag.
- **Anchors.** A panel about an object is anchored to it: a thin leader line from the
  panel's edge to a small reticle on the object's projected position. The objects are
  the projector (main menu, Library preview), the magazine (Library list), the board
  (game plate, result panel) and the door (Quit). The reticle tracks the object as the
  camera moves. Anchors are optional per panel. The HUD in game keeps its edge layout
  (M23.11 §1); only the variant plate gets an anchor, a faint line to the nearest slab
  corner.
- **Materialising.** Panels appear with the M23.11 trace-on (edges first, then fill,
  scanline), and leave with its reverse. That is already the AR look; keep it.
- **Glasses edge.** A very faint lens vignette with chromatic fringe at the extreme
  corners, added in the post composite. Strength 0.2 of the current vignette; off at
  calm; never on captures with `--post 0`.

### Camera stations

Each shell screen is a camera pose in the lab. Poses are data (`src/view/rig_stations.*`),
in room coordinates (metres; Y up; origin at the floor below the gantry centre; the
bench along −Z).

| Screen | Station | Looks at | AR panel |
|---|---|---|---|
| Main menu | standing at the bench, slightly left | the projector and its overture | main menu, right |
| Library (New game) | turned 35° toward the magazine | magazine + projector showing the selected variant | list anchored to the magazine |
| Watch | same as Library | projector | list |
| Tutorial | at the bench, closer to the projector | projector | tutorial list |
| Settings / Game mode / Piece moves | stepped back, looking at the bench | bench | panel right |
| Quit prompt | turned toward the roller door | door | prompt |
| Game | free orbit around the deployed layout (unchanged controls) | layout centre | HUD |
| Paused | the game camera pulled back (existing pull-back) | layout | pause panel |
| Editor | unchanged for now (out of scope) | — | — |

The game camera keeps its controls and framing (`OrbitCamera::frame`, `--follow`,
`--cinema`). It is now constrained by the room:
- the orbit distance is clamped so the eye stays inside the room;
- any wall, shelf or bench between the eye and the target fades out with a screen-door
  dither, so a low orbit never sees a wall face;
- the lab's room lights dim to about 15 % while a game is running ("focus mode"), so the
  game reads as before: a dark scene lit by the board. The skylight stars stay. The room
  comes back up when the game is left.

### The moves

All are camera paths with mass (one shared easing: ease-in-out with a small settle,
`view::rigEase`). All can be skipped by any key or click (`Ui::skipTransition`).

**Menu ↔ menu.** The camera moves between stations (0.6–0.9 s depending on distance).
The old AR panel traces off as the camera starts; the new one traces on as it arrives.
The overture on the projector cross-fades to the new subject via the projector's own beam
(it collapses into the lens, then grows out).

**Menu → game ("start").** About 3.0 s total, every beat physical:
1. 0.0 – 0.3: the AR panel traces off. The projector's overture collapses into the lens.
2. 0.2 – 1.4: the room lights dim toward focus mode. The magazine's bay LEDs blink. The
   needed slabs slide out one after another, are picked up by carriages, and travel
   along the gantry. Each slab has mass: it accelerates, decelerates and settles with a
   2–3 mm overshoot and a clamp click (a short glow pulse on the clamp).
3. 0.4 – 2.2: the camera flies from the bench station to the game's framed pose,
   following the first slab, and arrives as the last slab locks.
4. 2.2 –: the board boots (M23.10 §3), unchanged, slab by slab in arrival order. The HUD
   traces on with the boot, as now.

A layout with many slabs keeps step 2 within 1.6 s by overlapping slab departures. The
boot overlaps rather than waits.

**Game → pause → game.** Unchanged (pull-back), plus the AR lag.

**Game → menu ("leave").** About 2.8 s:
1. If the game is still live: the LED power-down plus the hologram shutdown (M23.12 §2),
   compressed to 1.0 s, with the 3 s delay skipped. If the game already ended and shut
   down, this step is skipped.
2. The slabs unclamp, rise, ride back to the magazine and slide in. The camera returns to
   the bench station. The room lights come back up.
3. The projector's overture grows out of the lens. The main menu panel traces on.

**Game end.** Unchanged plus M23.12 §2. The slabs stay deployed, dark, with their standby
LEDs, until the player leaves or rematches. Rematch reboots in place: no slab movement.

**Calm intensity.** Every move is the M23.11 0.3 s cross-fade between the two camera
poses. There is no slab travel and no room dimming animation (the dim level simply
switches). Boot and shutdown use their own calm forms.

**First launch.** The app opens on the bench station with the room lights rising from
dark over 1.2 s and the projector powering on (a lens flare, then the overture growing).
It is skippable. This replaces the current menu fade-in.

## Rendering plan

Everything goes through the existing board pipeline unless noted. No second renderer.

- **Room and Rig geometry: a procedural kit first.** A `render::LabScene` builds
  instances from a few unit meshes the board renderer already has or can add cheaply:
  box, bevelled box, cylinder, the slab itself, and a quad for the skylight. Materials
  are the existing ones: matte (0) for concrete and composite, metal (1) for extrusion
  and gantry, glass (3) for the projector disc and skylight, glow (5) for status LEDs,
  hologram (4) for the overture and the geometry view surface, LED panel (7) for slabs.
  Add one material only if needed: `8` (painted metal with panel lines and roughness),
  evaluated in slice §2. The kit is data (`assets/lab/lab.toml`: a list of parts with
  mesh, transform, material, colour), so a human can move furniture without code. A
  later art pass can replace any part with a mesh file. The format must allow that from
  the start (`mesh = "box" | "file:assets/lab/bench.obj"`).
- **Lights.** The board shader has one directional light plus emission. A room needs a
  few local lights: add a small light list (at most 8 point or spot lights: position,
  colour, radius, optional cone) in a uniform buffer, evaluated with a simple
  Lambert + Blinn term and inverse-square-with-radius falloff. The board's own glow
  becomes one area-ish light per slab (a point light under the slab centre, above its
  surface, coloured by the panel's mean emission and scaled by `power`). This is the
  "physical" part: the bench is lit by the projector, the floor by the slabs, and
  power-down darkens the room.
  - ADR-0025's HDR scene and bloom are unchanged; lights write into the same HDR target.
  - **Budget.** At most 8 lights, per-fragment, no shadows in M24. Contact darkening
    under objects uses a baked AO term per part (a vertex colour from the kit) rather
    than shadows.
- **The overture as a real hologram.** `overtureScene()` already returns quads and
  tokens in 3-D (`OvVec3`). A new `render::overtureInstances(scene, xform)` turns them
  into hologram-material instances (quads) and token instances (the existing piece
  meshes at small scale), placed above the projector by `xform`. `drawOverture` (the
  `ImDrawList` path) stays for the editor and for `--post 0` legacy captures until §8
  removes it. Picking on the overture (`drawOverture`'s shared projection) moves to a
  3-D ray pick against the same instances.
- **The menu screens render the 3-D scene.** `main.cpp` currently passes an empty
  `OrbitCamera{}` and no instances when `!showsBoard()`. Instead, every screen renders
  the lab (`LabScene` + overture + parked slabs) from the screen's station camera.
  `DepthField`, the grid floor and the screen-space backdrop are removed from menu
  screens in §8, after the lab draws.
- **Board placement.** A world transform `rigXform` (`rigScale`, deployment volume
  origin) is applied to every board instance. Board-space coordinates and all existing
  board code stay as they are: picking, fx, move animation and seams work in board space,
  and the camera and lab know `rigXform`.
- **Slab hardware.** Per slice in the layout: a slab body (its existing bezel and plinth
  instances), a mast from the slab's top centre up to the gantry (`metal`, a telescoping
  cylinder: two segments), a clamp ring, and the standby LED of M23.12 §2.
- **Wall fade.** A per-instance `fade` (0..1) for room parts, applied as a screen-door
  discard in the fragment shader. The CPU sets it by testing each room part's bounds
  against the eye→target segment.

## State and time

- **Pure poses.** `view::rigPose(screen, layout, t)` and `view::slabPath(...)` are pure
  functions of explicit time. A move is a `RigMove { from, to, startAge }` stamped by
  `app::FxTimeline` like every other event, so `--transition-t` keeps reproducing.
- **One clock.** The shell transition clock (`Ui::enter_`, M23.11 §6) becomes the input
  to `RigMove`. The panel trace-on and trace-off keep their own timings within the move.
  There is no second transition system.
- **Settings.** `labScene` (bool, default true; false gives the M23.11 menus and stars
  game, the recovery path and the A/B), `roomLight` (0..1 multiplier, default 1),
  `arLag` (bool, default true).

## Decisions (user, 2026-11-12)

- World concept: **the Rig**, now in a **2050 home lab / garage** (user revision during
  design; replaces "floating in space").
- Menus: **screen-space AR glasses layer**, not world-space panels (user: "think about
  them as AR glasses layer on top of the world").
- Game end: pieces **wave out, the winning king last** (M23.12 §2).
- Themes: **manifold first.** The console theme keeps the M23.11 menus and game
  backdrop until a later pass (`labScene` is forced off for it).
- Not in a room's centre: boards are deployed by a gantry to the layout's own positions,
  so every dimensionality has a physical answer.

## Out of scope

- Sound (the user does it).
- Piece remodel (the user does it).
- The trailer. Cinema keeps its current look. Moving the trailer into the lab is a
  follow-up once §5 lands.
- The editor screen. It keeps its current flat backdrop.
- Shadows and global illumination.
- An art pass on the lab. M24 ships the procedural kit, laid out with care; a human may
  replace parts later.

## Slices

Each slice is test-first, captured, and committed on its own. Run everything through
`g.sh`. The precommit must print `precommit: OK`, plus the `[render][app][view]` and
`gui-*` tests. Never commit `out/`. Don't push.

| Slice | What | Lands visibly? |
|---|---|---|
| §1 | ADR-0026 text check, `LabScene` kit format + loader + procedural meshes, room layout v1 (static, no lights) | behind `labScene`, off by default |
| §2 | Light list (≤ 8), per-slab glow light, room lights, focus-mode dim, wall fade | behind `labScene` |
| §3 | Overture as 3-D hologram instances above the bench projector, 3-D overture picking | behind `labScene` |
| §4 | Menu screens render the lab from station cameras; menu ↔ menu camera moves; AR panel lag; anchors | `labScene` default on |
| §5 | `rigXform`, slab hardware (masts, clamps, magazine), deploy/stow paths, menu → game and game → menu moves | yes |
| §6 | Layout-driven gantry for multi-slab variants, quarter-size tiles, `rigScale` easing, 5D timeline slab arrival | yes |
| §7 | Geometry view as the gantry projector's hologram, slabs retracting; first-launch power-on; glasses-edge vignette | yes |
| §8 | Remove `DepthField`/grid floor/screen-space backdrop from manifold menus; contact sheets; perf note | yes |

### §1 Lab kit and room v1

- `assets/lab/lab.toml`: parts (`name`, `mesh`, `pos`, `rot` in degrees, `size`,
  `material`, `color`, `ao`), named anchors (`projector.lens`, `magazine.bay0`,
  `gantry.origin`, `gantry.volume`, `door`, `bench.top`, stations `station.menu`, ...).
  Parsed by `io/lab_doc.*` with errors and line numbers like the variant parser.
- `render/lab_scene.{hpp,cpp}`: `LabScene::build(doc, theme) -> InstanceSet part` and
  `anchor(name) -> Vec3`.
- Room v1, about 5 × 6 × 2.7 m: floor, three walls plus a roller door, ceiling with
  skylight, workbench with projector, magazine rack, gantry frame (rails only), shelving
  with boxes, pegboard with a few tools, cable tray, task lamp.
- **Debug access:** console `lab [on|off]`, `get`/`set labScene`; CLI `--lab 0|1`,
  `--station <name>` (render the static lab from a named station).
- **Tests:**
  - parser: a valid file; errors with line numbers; unknown mesh;
  - GPU: lab instances build and validate clean at scales 1 and 3;
  - capture from every station.

### §2 Lights

- Light list in a UBO (≤ 8), shader evaluation, room light parts in `lab.toml`
  (`[[light]]`), per-slab glow light, `roomLight` and focus-mode dim, wall fade.
- **Debug access:**
  - console `lights` lists the active lights;
  - `set roomLight`;
  - CLI `--room-light <0..1>`.
- **Tests:**
  - pure: falloff, cone;
  - a light at radius gives zero;
  - power-down at `--end-t` 14 lowers the slab light to its hold;
  - GPU validation;
  - captures with the room lit, in focus mode, and at end standby.
- Perf: record frame time on lavapipe before and after, and leave a note for a GPU run
  with `--bench-frame`.

### §3 Overture as a hologram

- `overtureInstances`, the projector transform from `projector.lens`, a beam cone (soft
  additive hologram quad), 3-D pick.
- The Library preview drag (`updateObjectDrag`) turns the hologram about the lens axis.
- **Debug access:** the existing overture controls work. Add CLI `--overture <name>
  --overture-t <0..1>` with the lab.
- **Tests:**
  - instance count matches the scene's quads and tokens;
  - the pick hits the same cell as the legacy projection for a set of rays;
  - captures.

### §4 Stations and menu moves

- `view::rig_stations`, `RigMove`, `rigEase`, AR lag spring, anchors with leader lines
  and reticles.
- `main.cpp` renders the lab for every non-editor screen.
- **Debug access:**
  - `transition <from> <to>` now flies the camera;
  - `station <name>` jumps there;
  - CLI `--from/--transition-t` unchanged;
  - `--ar-lag 0`.
- **Tests:**
  - pure pose interpolation (endpoints exact, monotonic arc length);
  - a capture at the move's end is byte-identical to the station's plain capture;
  - calm is a cross-fade;
  - existing `gui-screen-transition` is pinned to `--lab 0`.

### §5 Deploy and stow

- `rigXform`, slab hardware, `slabPath` (magazine → carriage → place, with settle),
  clamp pulse, menu → game and game → menu sequences, boot hand-off, compressed shutdown
  on leave.
- **Debug access:**
  - console `deploy` / `stow` replays them;
  - CLI `--deploy-t <s>`, `--stow-t <s>`.
- **Tests:**
  - pure: path endpoints;
  - no slab intersects another during travel for standard and cube5;
  - the board picks the same cells with `rigXform` applied (ray tests);
  - captures every 0.25 s.

### §6 Multi-slab gantry

- Carriages per slice, quarter tiles, `rigScale`, departure overlap, 5D arrival of new
  timeline slabs with a light-guide connector.
- **Debug access:** `--variant 5d --script ...` captures with `--event-t` on the arrival.
- **Tests:**
  - every shipped variant's layout fits the volume after `rigScale`;
  - no two masts collide;
  - a 5D branch triggers exactly one slab arrival;
  - captures for standard, cube5, hyper4, t6 and 5D.

### §7 Geometry view, first launch, lens

- The gantry ceiling projector, slabs retracting in the geometry view, first-launch
  power-on, glasses-edge vignette and fringe.
- **Debug access:**
  - console `poweron` replays the launch;
  - CLI `--launch-t <s>`.
- **Tests:** captures; existing geometry-view tests pinned to `--lab 0` where their
  framing changes.

### §8 Cleanup

- Remove the manifold menus' `DepthField`, grid floor and backdrop path (keep them for
  console theme and `--lab 0`).
- Contact sheets for every station and move in both intensities.
- A perf note.
- Update `visual_ideas.md`'s approved direction and `docs/debug-console.md`.

## Open questions for later

- Whether the Library should show variants as physical cartridges in the magazine
  (picking one pulls it out). This is nice but not needed for consistency. Revisit after
  §5.
- The console theme's lab: same room in daylight with a wooden bench, or its own room.
