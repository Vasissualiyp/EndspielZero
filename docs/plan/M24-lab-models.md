<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# M24 — 3-D models for the lab

Status: **planned 2026-11-12.** Made by a modelling agent, in parallel with the code
slices. Read by M24.1 Step 2b (the OBJ loader), and by M24.5, which needs the gantry
carriage and mast.

## What the lab is

A small home lab or garage in 2050 that contains one remarkable machine: an LED-slab
chess board system with holographic pieces. Read "The setting" and "The machine" in
`docs/plan/M24-the-rig.md`. Style: anodised aluminium extrusion, matte composite panels,
braided cable, frosted polycarbonate, small status-LED strips. No chrome, no neon tubes,
and nothing grand. It is a hobbyist's well-made gear, slightly more futuristic than
today. Everything is dark; the lights come from the machine.

## Conventions (must follow exactly)

- **Format:** Wavefront OBJ, ASCII. Use only `v`, `vn`, `f` (triangles or quads), and
  optionally `o`, `g`, `s` and comments. Don't use materials (`mtllib`, `usemtl`) or
  textures (`vt`); the game colours each part from the lab file.
- **Units:** metres. **Z is up.** X is right. −Y faces the viewer: the part's front faces
  −Y.
- **Origin:** at the centre of the part's footprint on its base (z = 0 at the bottom),
  unless the table below says otherwise.
- **Normals:** include `vn`. Use smooth normals on rounded surfaces and split vertices at
  hard edges (more than 30°), so edges look machined, not blobby.
- **Budget:** at most 6,000 triangles per file and at most 65,535 vertices. These are
  background props at ~1080p; spend the triangles on silhouettes and bevels, not hidden
  faces.
- **Bevels:** every visible hard edge gets a small chamfer (2–4 mm), so the lab's lights
  catch the edges. This is what makes them read as physical.
- **Closed and clean:** no holes visible from outside, no zero-area faces, no duplicate
  faces, consistent outward winding (counter-clockwise when seen from outside).
- **One material per file.** If a model needs two looks (a metal body and glowing LEDs),
  make **two files** with the same origin, such as `projector_body.obj` and
  `projector_leds.obj`. The lab file gives each its own material.

## The models

Each row is one file, or a set of files with the same origin, under `assets/lab/models/`.

| File(s) | What | Size (m) | Notes |
|---|---|---|---|
| `projector_body.obj`, `projector_lens.obj`, `projector_leds.obj` | The bench hologram projector | about Ø 0.24 × 0.075 high | A squat round unit: a weighted base ring with a slight chamfer, a recessed collar, and a lens array (7 small lenses in a hex pattern under a flush frosted disc) on top. Six tiny status-LED pills on the collar. The origin is the base centre. The lens disc's top centre must be at exactly (0, 0, 0.075); the hologram is placed from that point. |
| `lamp.obj` | A two-arm task lamp | base Ø 0.16; reaches about 0.55 up, 0.35 forward | A weighted disc base, two aluminium arms with visible pivot knuckles and a counter-spring, and a slim rectangular LED head (0.18 × 0.05) angled down. The origin is the base centre. Also deliver `lamp_head_emitter.obj`: only the emitter face, a 2 mm thick plate, so the game can make it glow. |
| `magazine.obj`, `magazine_leds.obj` | Wall-mounted slab cassette | 0.25 deep × 1.0 wide × 1.1 high | A frame of extrusion profiles with 8 vertical slots, each 0.03 wide and 0.11 apart, open at the front. Each slot has a small LED window. Cable glands at the top. **Origin:** the centre of the back face (the wall side), z = 0 at the bottom. "Width" runs along Y, because it is mounted on the right wall facing −X: model it facing −X. |
| `gantry_carriage.obj` | The carriage that rides the ceiling rails | 0.30 × 0.30 × 0.09 | A plate with four rail wheels/bearing blocks at the corners, a motor housing on top, a central hole underneath for the mast, and a cable drag-chain stub. The origin is the centre of the carriage's **bottom** face (where the mast hangs from). |
| `gantry_mast_outer.obj`, `gantry_mast_inner.obj` | A telescoping mast | outer Ø 0.05 × 0.60; inner Ø 0.038 × 0.60 | Two nested tubes; the game slides the inner one. Each has its origin at its **top** centre and extends **down** (−Z). The outer has a collar ring at the bottom; the inner has a clamp ring at the bottom: a 4-prong ring, Ø 0.09, that grips the slab's back connector. |
| `slab_back.obj` | The back of a board slab | 0.44 × 0.44 × 0.025 | The underside and rim of a board slab: a ribbed heat-sink back, a central connector boss (Ø 0.07) that the mast clamp grips, and a thin rim. **The top face is open and lies at z = 0**, because the game draws the LED face itself. The origin is the centre of the top plane. The game scales it to fit each board. |
| `bench_monitor.obj` | An old dark monitor on the bench (clutter) | 0.50 × 0.18 × 0.36 | A slim screen on a small stand. The screen is dark (it is off). |
| `clutter_mug.obj`, `clutter_toolbox.obj`, `clutter_spool.obj`, `clutter_multimeter.obj` | Small bench and shelf clutter | each ≤ 0.25 | Simple, recognisable silhouettes. |
| `door_roller.obj` | The roller door | 3.0 wide × 2.4 high × 0.08 | A slatted roller door with horizontal ribs every 0.1 m, a bottom rail and side guides. It faces +Y (into the room). The origin is the bottom centre. |

## Free models from the web as a starting point (user, 2026-11-12)

You may search the web for free 3-D models and use them as a **starting point** for the
custom models above. Rules:

- **Licences: CC0 / public domain only**, for example Poly Haven, Kenney, ambientCG, or
  CC0 items on Sketchfab and OpenGameArt. The game is GPL-3.0 and its assets ship with it.
  No CC-BY-NC, no "free for personal use", no unclear licences. If the licence is not
  stated clearly on the source page, do not use the model.
- **Record provenance.** For every file that started from a downloaded model, record the
  source URL, the author, the licence and what you changed, in
  `assets/lab/models/README.md` and in a `# source:` comment at the top of the OBJ.
- **Downloads are untrusted data.** Put each download in its own new directory under
  `out/m24-models/downloads/<name>/`; that directory is untracked and never committed.
  Never run scripts or executables that come with a download. Read model files only with
  your own converter scripts, which live in `assets/lab/models/src/`, not in the download
  directory.
- **Make it ours.** Convert the model to the conventions above: OBJ with only `v`, `vn`
  and `f`; metres; Z up; the stated origin and size. Then fit it to the budget
  (decimate or retopologise to at most 6,000 triangles), remove textures and materials,
  and adapt it to the design: proportions, bevels, and the 2050 look. Every file must
  still pass the validator.
- If nothing suitable and clearly licensed exists, build the model from primitives as in
  "Process" below. That is always acceptable.

## Process

1. **Generate each model with a script, not by hand-typing coordinates.** For example,
   write a small Python script (standard library only) per model that builds the
   geometry from primitives: extruded profiles, lathed shapes, chamfered boxes. Then
   write the OBJ. Keep the scripts beside the models in `assets/lab/models/src/`, so a
   human can regenerate or tweak them.
2. **Validate each OBJ** with a script that checks:
   - the triangle and vertex counts are within budget;
   - the bounds match the size column within ±5 %;
   - there are no degenerate faces;
   - every normal has unit length;
   - the mesh is watertight (every edge is shared by exactly 2 faces) for every model
     except the open-topped `slab_back.obj` and the emitter plates;
   - the winding faces outward. Check that the signed volume is positive for closed
     meshes.

   Print a one-line report per file.
3. **Preview.** Render each model to a PNG preview, from three-quarter front and from the
   side, with a simple software rasteriser or matplotlib's `mplot3d`, if it is
   available as Python's standard library plus whatever is on the system. Put them in
   `out/m24-models/`. Look at every preview and fix anything that reads wrong.
4. **Do not touch** `src/`, `tests/` or any other code, or `assets/lab/lab.toml`. The code
   agents own those. Write only `assets/lab/models/**` and `out/m24-models/**`.
5. **Do not commit.** The orchestrator reviews the previews and commits.
6. When done, write `assets/lab/models/README.md`. It lists each file with its triangle
   count, bounds and origin, and has one paragraph on how to regenerate the models.
