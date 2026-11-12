<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# M24 — The Rig: one physical lab under a neural-implant layer

Status: **planned 2026-11-12.** Design confirmed with the user 2026-11-12 (see "Decisions").
The decision record is ADR-0026 (`docs/adr/0026-world-is-hardware-screen-is-implant.md`).

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
- **The screen is the player's neural implant.** Everything flat (menus, HUD, result
  panel, review dock) is perceived through an implant on top of the visual field. It keeps
  the M23.11 panel geometry, in its own synaptic hue, and forms and dissolves in an
  organic way, never like the LEDs. It is allowed to be futuristic precisely because it
  is not in the world.
- **One scene, one camera.** The menus and the game are the same lab seen from different
  camera stations. A screen change is a camera move plus implant panels changing, never a cut.
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

### The implant layer

The player has a neural implant. The menus are not a display in front of the eyes. They
are written into the visual cortex and **perceived** on top of the lab. Glasses would
draw over the world; the implant changes how the world is seen. Every effect below
follows from that, and none of it looks like the LEDs.

**The contrast rule.** The LEDs are digital: square pixels, grid-aligned, scan lines,
hard steps, cyan. The implant is organic: radial and log-polar, soft, flowing noise,
continuous, colour fringes, never a grid and never a scan line. Neither ever uses the
other's motion vocabulary, so hardware and brain are always told apart.

- **Synaptic hue.** The implant layer gets its own palette, distinct from the LEDs' cyan:
  a pale violet-white body (`implant`, about `#D9D2FF`), a warm fringe (`implantWarm`,
  about `#FFB8A0`) used only in transient colour separation, and a dim `implantDim` for
  secondary text. Add these as theme tokens. The manifold HUD and menu panels use them
  for text, edges and brackets; panel fills stay the theme's ink. Gameplay colours stay
  as they are: the check colour, the side colours and the seam ramp, because they mean
  something on the board. The console theme is untouched.
- **Panels** stay screen-space (unchanged widgets, `hudPanel`, auto UI scale) and are
  perfectly stable. There is no lag and no frame, because nothing physical carries
  them.
- **Activation: the handshake.** At first launch only (§7), after the room lights come
  up:
  1. A few soft phosphenes appear near the centre of view: blurred points of light in the
     implant hue, slightly chromatic, appearing and fading at random over about 0.4 s.
  2. A travelling ring wave expands from the centre over about 0.9 s. Its spacing is
     log-polar: rings are tight at the centre and widen toward the periphery. That is how
     a wave across the visual cortex appears in the visual field, through the
     retinotopic map. The rings are faint brightness modulation, not drawn lines.
  3. Behind the front, colour separates organically: the implant's red and blue channels
     are displaced by a slowly flowing curl-noise field (amplitude up to about 6 px at
     1080p, spatial scale about 1/6 of the screen, flowing at about 0.3 screen per
     second), like a signal not yet in sync.
  4. The flow converges onto the menu: the displacement field is pulled toward zero near
     panel edges and glyphs first. Text resolves from blurred colour blobs (a noise
     dissolve plus a blur radius falling to 0) into crisp glyphs.
  5. At full sync the layer is exactly the plain UI (identity), so captures and tests
     compare byte-for-byte.

  The whole handshake is about 1.8 s and skippable.
- **Panels forming and dissolving (every other time).** About 0.35 s to form: a short
  local version of steps 3–5, centred on the panel. There is a brief warm and violet
  fringe flow, the panel's noise dissolve resolves, and glyphs sharpen. No ring wave. This
  replaces the M23.11 trace-on (edges, fill, scanline), which is an LED motion and now
  belongs to the hardware only. Dissolving reverses it over about 0.25 s: glyphs blur
  into drifting colour, the panel breaks into a few phosphenes, and the afterimage
  follows.
- **Afterimage.** When a panel dissolves, a faint negative of it (complementary hue,
  about 8 % strength, slightly blurred) lingers and fades over about 0.3 s, as a bright
  image does on the retina.
- **Salience instead of leader lines.** When a panel refers to an object (main menu or
  Library: the projector; Library list: the magazine; Quit: the roller door; the game's
  result panel: the board), the implant boosts how that object is seen. In the 3-D
  scene, the object's parts get a soft rim glow in the implant hue plus a small local
  contrast lift. This is a per-instance `salience` value (0..1) in the lab kit, eased
  over 0.3 s. Nothing is drawn on the screen between panel and object.
- **Saccadic dip.** During a camera flight between stations, the layer's opacity dips
  with the camera's angular speed (to at most −60 % at the fastest moment). This is
  saccadic suppression: perception blanks while the eyes swing. It never happens while
  orbiting the board in a game, during a game → pause pull-back, or at calm intensity.
- **Heartbeat.** The whole layer's brightness carries a faint double pulse ("lub-dub",
  about 2 %, 60 bpm, the second beat 0.25 s after the first and smaller). It is pure in
  visual time and off at calm.
- **Calm intensity.** No phosphenes, ring, flow, dip, heartbeat or afterimage. Panels
  cross-fade (0.2 s). The synaptic hue stays: it is a colour, not an effect.

**How it renders.** The ImGui layer is recorded into its own offscreen RGBA target (the
same size as the swapchain) instead of directly over the post output. A composite pass,
`render::ImplantComposite` with shader `implant.frag`, then draws it over the scene:
- inputs: the UI target, a per-frame `sync` (global, for the handshake), up to 16 panel
  records (`rect`, `age`, `entering|leaving`), the ring-wave age, heartbeat phase, dip
  amount, and the previous panels' afterimage records;
- per pixel: the sync value (the global one combined with the containing panel's age) →
  curl-noise displacement of R and B → noise-threshold dissolve → blur radius (a small
  Poisson tap set) → phosphene sparks (hashed points, implant hue) → ring modulation →
  heartbeat × dip;
- `sync == 1` everywhere with no records is an exact pass-through, so `--post 0`,
  existing captures and every UI test are unchanged. Add a test that asserts this.
- the console, the dev overlay and the editor bypass the implant composite (drawn after
  it); they are tools, not perception.

The salience rim is in the board shader (lab parts only), not in the composite.

### Camera stations

Each shell screen is a camera pose in the lab. The poses are data: `[[station]]` entries in
`assets/lab/lab.toml`, in metres, **Z up** (the board renderer's convention), X right and
Y away from a yaw-0 camera. The bench is against the far (+Y) wall and the roller door is
on the near (−Y) wall. The renderer's world is the lab's metres minus `gantry.centre`,
times 20 (one cell = 5 cm). M24.1 gives the exact numbers.

| Screen | Station | Looks at | Implant panel (salience on) |
|---|---|---|---|
| Main menu | standing at the bench, slightly left | the projector and its overture | main menu, right |
| Library (New game) | turned 35° toward the magazine | magazine + projector showing the selected variant | list (magazine) |
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
The old implant panel dissolves as the camera starts; the new one forms as it arrives,
and the layer dips during the flight (saccadic dip).
The overture on the projector cross-fades to the new subject via the projector's own beam
(it collapses into the lens, then grows out).

**Menu → game ("start").** About 3.0 s total, every beat physical:
1. 0.0 – 0.3: the implant panel dissolves. The projector's overture collapses into the lens.
2. 0.2 – 1.4: the room lights dim toward focus mode. The magazine's bay LEDs blink. The
   needed slabs slide out one after another, are picked up by carriages, and travel
   along the gantry. Each slab has mass: it accelerates, decelerates and settles with a
   2–3 mm overshoot and a clamp click (a short glow pulse on the clamp).
3. 0.4 – 2.2: the camera flies from the bench station to the game's framed pose,
   following the first slab, and arrives as the last slab locks.
4. 2.2 –: the board boots (M23.10 §3), unchanged, slab by slab in arrival order. The HUD
   forms as implant panels while the boot runs (it no longer traces on with the bezel).

A layout with many slabs keeps step 2 within 1.6 s by overlapping slab departures. The
boot overlaps rather than waits.

**Game → pause → game.** Unchanged (pull-back). The pause panel forms and dissolves as an
implant panel.

**Game → menu ("leave").** About 2.8 s:
1. If the game is still live: the LED power-down plus the hologram shutdown (M23.12 §2),
   compressed to 1.0 s, with the 3 s delay skipped. If the game already ended and shut
   down, this step is skipped.
2. The slabs unclamp, rise, ride back to the magazine and slide in. The camera returns to
   the bench station. The room lights come back up.
3. The projector's overture grows out of the lens. The main menu panel forms.

**Game end.** Unchanged plus M23.12 §2. The slabs stay deployed, dark, with their standby
LEDs, until the player leaves or rematches. Rematch reboots in place: no slab movement.

**Calm intensity.** Every move is the M23.11 0.3 s cross-fade between the two camera
poses. There is no slab travel and no room dimming animation (the dim level simply
switches). Boot and shutdown use their own calm forms.

**First launch: waking up** (user, 2026-11-12). Starting the app is the player opening
their eyes in the lab. Then the implant comes online. It is about 5 s, skippable by any
key or click, and replaces the current menu fade-in. Each launch plays it once; no other
screen change does.

1. **Eyes closed (0 – 0.6 s).** Black, with a very faint warm glow through the lids (a
   dim reddish wash, about 3 %, slowly varying), so it reads as closed eyes rather than
   a black screen.
2. **Eyes opening (0.6 – 1.6 s).** Two black eyelid regions, one from the top and one from
   the bottom, pull apart:
   - **Shape.** The lid edges are curved (an elliptical eye opening, wider in the middle,
     so the corners open last) and soft, with a feathered edge of about 4 % of the screen
     height.
   - **Movement.** The upper lid moves farther and faster than the lower, as real lids
     do. The opening hesitates once (it stops at about 40 % for 0.15 s), then completes.
   - **The vision behind them** starts heavily defocused (a blur radius of about 2.5 % of
     the screen), dim (exposure −1.5 EV) and slightly desaturated. It clears over 1.5 s,
     overlapping the next step.
3. **Blinks (1.9 s and 2.6 s).** Two blinks, the second quicker. A blink is the same two
   black lid regions closing to meet slightly below the centre line and reopening:
   - the first takes 0.18 s to close and 0.22 s to open;
   - the second takes 0.12 s and 0.16 s.

   Each blink takes the defocus a step closer to sharp, and after the second the vision
   is fully sharp at normal exposure.
4. **The room settles (2.0 – 3.2 s).** The lab's lights are already on (the player's own
   lab). The projector powers on (a lens flare, then the overture growing out of the
   lens).
5. **The implant handshake (3.0 – 4.8 s).** Phosphenes, the log-polar ring wave, and the
   colour separation converging onto the main menu, as in "The implant layer".

Each step is a pure function of the launch age (`view::wakeState(age, intensity)` returns
lid openness, top and bottom edge curves, blur radius, exposure, saturation and the
lid-glow amount).
- The lids and blur render in the post composite, after the scene and before the implant
  layer: the implant is in the brain, so the eyelids cover the world but not the overlay.
  The menu is not visible before step 5 in any case.
- Captures: `--wake-t <s>`. Console `wake` replays it. Setting `wakeOnLaunch` (bool,
  default true) disables it.
- **Calm intensity:** no blinks or hesitation; the lids open once over 0.6 s while the blur
  clears, then the menu cross-fades in.
- **Pure-black lids.** The lid regions are exactly black (after the glow in step 1 ends),
  so a capture mid-blink has rows of zero pixels. That is easy to test.

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
  `implantFx` (bool, default true; false keeps the synaptic hue with plain cross-fades).

## Decisions (user, 2026-11-12)

- World concept: **the Rig**, now in a **2050 home lab / garage** (user revision during
  design; replaces "floating in space").
- Menus: **a neural-implant layer** on top of the visual field (user revision of the
  earlier "AR glasses" idea): screen-space panels, not world-space panels.
  - Its own **synaptic hue**.
  - All four perception effects: **salience**, **saccadic dip**, **heartbeat**,
    **afterimage**.
  - The full **handshake at first launch only**; short form and dissolve afterwards.
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

## Morphs in the game: the slab's skin (user, 2026-11-12)

During a game, the board morphs from flat into its topological shape and back:
- cylinder, torus, Klein and Möbius, with the staged rolls of `src/view/morph.hpp`
  (`kTorusRollEnd`, `kKleinRingEnd` and `kKleinFoldEnd`);
- the shape-follow choreography;
- the layout views of multi-board variants.

In the lab, every morph must read as something **the hardware physically does**. The
user chose:

1. **Mechanism: a stretch skin on ribs.**
   - **The skin.** The slab's LED face is a stretchable skin: rigid cell tiles (the LEDs)
     joined by thin serpentine bridges. It is the 2050 version of island-bridge
     stretchable electronics.
   - **Unlatching.** At rest the skin lies on the slab's rigid back plate
     (`slab_back.obj`). To morph, it unlatches from the plate.
   - **The ribs.** Thin metal ribs extend from the mast's clamp ring and bend the skin
     into the shape. The surface maths is the existing morph maths, unchanged; the
     hardware is dressing driven by it.
   - **Stretch is honest.** Where the shape stretches the sheet, the tiles spread apart
     and their bridges glow faintly in the gaps. Where it compresses, the tiles crowd
     together and the gaps close. The stretch is measured per cell as the area ratio of
     the surface's local Jacobian against flat: for example, a torus's outer equator
     stretches and its inner one compresses. The tiles themselves never distort: a cell
     is rigid.
   - **Visible ribs.** The ribs stay visible behind the formed skin, so the player can see
     what holds it up. They are matte dark metal, and the lab lights catch them.
2. **Impossible parts: light completes them.** The hardware forms everything 3-D
   allows; where it cannot, the projector fills in.
   - **Klein bottle.** The real skin cannot pass through itself. It leaves a ring-shaped
     gap where the immersion would self-intersect, and the cells in that band are drawn
     as **hologram cells** (hologram material, beam from the nearest projector) instead
     of LED tiles. They are continuous with the LED cells on either side.
   - **Multi-slab and 4-D+ variants** (cube5, hyper4, T6, 5D). Slab edges that are glued
     in the variant but cannot touch in 3-D are joined by **light bridges**: thin beams
     between the two edges, in that seam's axis colour (the same colours as the seam
     legend). A piece crossing such a seam becomes a hologram and travels along the
     bridge.
   - **The rule:** hardware where possible, light where impossible. Never a physical
     surface passing through another.
3. **Cues** (the user chose these three; there is **no** unlatch beat):
   - **visible ribs** (above);
   - **seam latch flash:** when two edges meet, for example a cylinder's seam closing or
     a torus's ring closing, magnetic edge latches click shut. A short flash runs along
     the seam in its axis colour (0.25 s, LED vocabulary: hard steps, it is hardware);
   - **the gantry rearranges slabs:** for multi-slab variants, changing the layout view
     makes the gantry physically move and turn the slabs on their masts, with the mass and
     settle of `slabPath`. Nothing teleports.

These are **complex 3-D manipulations**: per the user, Codex **gpt-6-astra** implements
them (M24.5b and M24.6), not gpt-6.1-sol.

## Rules for every M24 slice

Every slice file repeats what it needs, but these hold throughout:

1. **Environment.** There is no GPU. Run every build, test and capture through `g.sh`.
   Its exact content is in `docs/plan/M23.12-seam-hover-and-hologram-shutdown.md`,
   "Rules", item 1; recreate it if it is missing.
2. **Test first.** Write each test, see it fail, then implement.
3. **Pure in explicit time.** New animation is a pure function of an age or progress the
   caller passes in. Captures reproduce bit for bit.
4. **Intensity.** Below `presentationIntensity` 0.5, every effect takes its calm form.
5. **Lab off is byte-identical.** With `labScene` off, every frame equals what it was
   before M24. Captures (`--shot`, `--clip`, `--trailer-clip`) default the lab **off**,
   as `--ui-auto` does; `--lab 1` turns it on. Existing tests therefore never change.
6. **Debug access** (AGENTS.md rule 9). Every new state has a console command, a setting,
   or a CLI flag where captures need one. Keep `DebugConsole::help()` and
   `docs/debug-console.md` in step.
7. **Gates before each commit, all through `g.sh`:** `tools/precommit.sh` prints
   `precommit: OK`; the `[render],[app],[view]` tests pass; `ctest -L gui` passes.
8. **Look at every capture** you make, as a contact sheet in `out/m24.N/`. Never commit
   `out/`.
9. **One commit per slice**, of your own files only. Don't push. Add a dated Status entry
   to the slice file: what was built, deviations, test counts, and sheet paths.
10. **If a name in a spec does not exist, stop and report it.** Do not invent a
    substitute. The names were checked on 2026-11-12, and earlier slices may have
    renamed things; their Status says so.

## Slices

| Slice | File | What | Lands visibly? |
|---|---|---|---|
| M24.1 | `docs/plan/M24.1-lab-kit-and-room.md` | Lab file format and parser, lab meshes, `LabScene`, room v1, menus show the static room | behind `labScene` (off) |
| M24.2 | `docs/plan/M24.2-lab-lights.md` | Up to 8 room lights, near-off key in the lab, the night sky in the skylight, `roomLight` | behind `labScene` |
| M24.3 | `docs/plan/M24.3-projector-holograms.md` | Manifold, overture and tesseract as 3-D holograms above the bench projector, with a beam | behind `labScene` |
| M24.4a | `docs/plan/M24.4a-stations-and-camera-moves.md` | Screens as stations, menu ↔ menu camera flights, hologram collapse/grow, salience | behind `labScene` |
| M24.4b | `docs/plan/M24.4b-implant-layer.md` | Synaptic hue, implant composite: organic form/dissolve, afterimage, heartbeat, saccadic dip | behind `labScene`; at the end of M24.4b, switch `labScene` to default **on** for interactive play (captures stay off) |
| M24.4c | `docs/plan/M24.4c-living-vision.md` | Living vision: lens warp, frameless glowing panels, peripheral softening, gaze wander with focus shift (depth of field), micro-tremor; the main-menu hologram takes a fifth of the screen | behind `livingVision` (on the lab path only) |
| M24.5 | `docs/plan/M24.5-deploy-and-stow.md` | Deploy and stow: the game in the lab (gpt-6-astra) | yes |
| M24.5b | `docs/plan/M24.5b-slab-skin.md` | The slab's skin: physical in-game morphs, stretch, ribs, seam latches, light-completed Klein (gpt-6-astra) | yes |
| M24.6 | to be written | Multi-slab gantry, light bridges, layout changes as gantry moves (gpt-6-astra) | yes |
| M24.7 | to be written | Geometry view projector, the first-launch wake-up and implant handshake | yes |
| M24.8 | to be written | Cleanup | yes |

**Why M24.5–M24.8 are not yet written at full detail.** They hook into code that M24.1–M24.4b
create, such as `LabScene`, the light block, `HologramPlacement`, the Station move and
`ImplantLayer`. Exact names and signatures for those should come from what was built, not
from a guess, or a smaller model would be told to call functions that turned out
differently. Write each one at the level of M24.1–M24.4b once the previous slice's Status
is in. The outlines below are binding on **what** gets built.

### M24.5 Deploy and stow (outline)

- The board in the lab: a world transform `rigXform` (uniform scale `rigScale`, origin at
  `gantry.centre`) applied to every board instance and to the board camera. Picking
  goes through the inverse, so cells pick identically (ray tests).
- Slab hardware per slice: a mast from the gantry carriage, a clamp ring, and the standby
  LED from M23.12 §2.
- `slabPath`: magazine → carriage → place, with mass and a settle; a clamp pulse.
- Menu → game (about 3.0 s) and game → menu (about 2.8 s) exactly as in "The moves",
  including the boot hand-off. When leaving a live game, use the compressed shutdown
  from M23.12 §2.
- Moved here from the old §2:
  - the per-slab glow light, one point light per slab coloured by the panel's mean
    emission and scaled by power. Merge slabs into at most 2 lights when there are more;
  - focus mode, with room lights at 15 % while a game runs;
  - wall fade (screen-door) for room parts between the eye and the target;
  - the game camera's orbit clamped to stay inside the room.
- The HUD and pause panels become implant panels (they form as the board boots).
- **Debug:** `deploy`, `stow`, `--deploy-t`, `--stow-t`.
- **Tests:** path endpoints; no slab intersections for standard and cube5; pick
  equivalence; captures every 0.25 s.

### M24.6 Multi-slab gantry (outline)

- One carriage per slice, positions from `view::layout`.
- Quarter-size tiles when a layout does not fit; `rigScale` eases over 1 s when a layout
  grows.
- Departures overlap.
- 5D: a new timeline's slab arrives and boots, joined to its parent by a light-guide
  connector (`TimelineLink`).
- Light bridges between glued slab edges that cannot touch in 3-D, in seam-axis colours.
  Pieces crossing them travel as holograms ("Morphs in the game", item 2).
- Changing the layout view moves and turns the slabs on their masts; nothing teleports.
- **Tests:** every shipped variant fits; no mast collisions; one arrival per branch;
  captures for standard, cube5, hyper4, t6 and 5D.

### M24.7 Geometry view, first launch (outline)

- The geometry view is the **physical** morph of M24.5b (the skin on ribs), not a
  hologram. This replaces the earlier outline, in which the slabs retracted and a ceiling
  projector showed the formed surface. Only the impossible parts are light.
- The first-launch wake-up exactly as in "First launch: waking up": `view::wakeState`,
  eyelids and defocus in the post composite before the implant layer, `--wake-t`, `wake`.
- The implant handshake: phosphenes and a log-polar ring wave via `ImplantParams::globalSync`
  and new ring parameters, curl-noise colour separation converging onto the main menu,
  `--handshake-t`, `poweron`.
- **Tests** as listed in "First launch: waking up", plus captures. Pin existing
  geometry-view tests to `--lab 0` where their framing changes.

### M24.8 Cleanup (outline)

- Remove the manifold menus' `DepthField`, grid floor and flat decoration path on the lab
  path. Keep them for the console theme and `--lab 0`.
- Contact sheets for every station and move, at both intensities.
- A perf note.
- Update `visual_ideas.md`'s approved direction and `docs/debug-console.md`.

## Open questions for later

- Whether the Library should show variants as physical cartridges in the magazine
  (picking one pulls it out). This is nice but not needed for consistency. Revisit after
  §5.
- The console theme's lab: same room in daylight with a wooden bench, or its own room.
