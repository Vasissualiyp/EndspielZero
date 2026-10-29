# Handover — next steps

Written 2026-09-30, last refreshed 2026-10-04. Read [`AGENTS.md`](../../AGENTS.md) first,
then [`00-roadmap.md`](00-roadmap.md); this page is only "what is done, what is next, and
where". The numbered sections below (1-8) are a dated log of how Wave 1 and M18 actually got
built, kept for the reasoning; this top section is the thing to trust for current status.

## Where this is (as of 2026-10-04)

**Wave 1 is fully complete**: M11, M12.1-M12.3 and M12.6 (the marketing runner), M13, M16.2,
and M17 (play on the shape) - including M17.20-M17.24, a run of camera-correctness bug fixes
found by actually using the shipped work (the chase anti-clip mismatch, the Approach/Return
camera losing the board, the SHAPE morph drifting off-centre). **M18** (production polish -
not a release-sequence wave, see its own header) is also fully complete: M18.1-M18.6, plus a
revision round on M18.4 and M18.6 from direct playtest feedback (the ambient background's
containment/rotation/the quintic's return, a speed setting for the morph). Every item was
implemented by opencode from a Claude-written spec and independently re-verified (rebuild,
the relevant test tags, and a visual capture inspected directly) before being committed -
see each milestone's own "Status" section in `M17-geometry-view.md`/`M12-broadcast.md`/
`M18-production-polish.md` for the detail and any deviations.

**Since the 2026-10-04 refresh:** the previously-open "Watch a game" item landed as M12.8
(`0a1f742`), and the M13.9 main-menu attract/trailer sequence was built out and revised
through several rounds (continuity, material-preserving trade-down, loop-to-standard, the
camera-keyframe scaffold). Nothing in Wave 1 or M18 is open now.

**Reprioritised 2026-10-16 (the user's direction):** the **piece editor of M7 is moved into
Wave 1** (only the piece editor - not the board or gamemode editors), ahead of M16.1's store
page - the shipped models are placeholders that need hand re-authoring and show in every
clip. That is the next build; see the **Next** pointer at the end of this file.

**Known, deliberately deferred, not blocking anything:** `torus3d_twist`'s own 3-D shell
shape (M17.12/M17.22 - the flat-lattice variant plays correctly; only its *shape view* is
deferred, as research-grade); the SHAPE morph's `formed = 0` mirror-flip (M18.6 - the morph
itself works, only its very first frame is a reflection rather than the bit-exact flat
board). Neither is on the critical path to Wave 2; pick either up only if asked.

**Not committed / minor:** nothing outstanding in the tree at the time of this refresh.

## The next steps, in order

### 1. M4.8 remainder: two frames in flight, and rendering into the swapchain — BUILT

**Built 2026-09-30, ADR-0018 superseding ADR-0011.** The interactive path renders straight
into the acquired swapchain image (each image wraps it in a target that owns its MSAA,
depth and scratch), `Window` carries a two-slot frame ring, and the renderer records into
the caller's buffer so two frames overlap. The colour format is now a runtime property.
Files: `src/render/window.*`, `src/render/board_renderer.*`, `src/render/offscreen_target.*`,
`src/render/ui.*`, `src/gui/main.cpp`. A headless test drives the recording path into an
image with the swapchain's usages, both frame slots, validation-clean; all `render` and
`gui-*` tests pass.

**Needs a run on a real display before it is trusted.** `vkAcquireNextImageKHR` and
`vkQueuePresentKHR` need a surface, so the acquire/present sequence is the one part not
covered by a test. Run `./build/dev/src/gui/endspielzero_gui standard` and check: the board
draws, the interface lays out, resizing works, pause (the defocus pass, now into the
swapchain image) looks right, and the validation layers stay silent.

### 2. M4.9 remainder: concrete `Pos` — MEASURED, DEFERRED

`render/overture_scene.cpp` still uses `using Pos = std::function<OvVec3(float, float)>`,
the one indirect call in the overture build. **Measured 2026-09-30 and deliberately not
done.** With the quintic memo and the built-scene cache in, `Pos` is off the steady-state
path: a pinned `t` reuses one build. Forcing a rebuild every frame via a `--clip`, the worst
case (`t6`, all 1600 quads) costs about **5 ms/frame more than a variant with no overture**,
at Debug `-O0` and including PPM IO. Making `Pos` concrete touches every builder in a
3100-line file to recover a fraction of a frame at `-O2`; the plan records the measurement
and defers it to the instanced-path rewrite (M4.9's "long-term fix").

### 3. M11.7: golden frames — DECIDED (no pixel images)

The plan asked for committed reference frames. **Decided against committing pixels**
(2026-09-30). A committed frame depends on the GPU driver **and** the compiler, and
`nix flake check` runs gcc and clang, so a pixel golden would be flaky or pinned to one
machine - the objection ADR-0011 already recorded. The driver-independent strength is
already in: the frame-the-cell oracle pins the pose at every shot boundary, the safe-zone
property sweeps random moves, and the topology/dimension tests pin the shot kinds. The
plan (`docs/plan/M11-move-camera.md`, M11.7) now records this; if a snapshot is wanted
later, pin quantised poses, not pixels.

### 4. M17: play on the shape (the geometry view) — BUILT for the glued 2-D case

New milestone, planned in [`M17-geometry-view.md`](M17-geometry-view.md). **Built
2026-09-30, reworked to the renderer after a first cut that drew the surface in the
interface over the flat board (no camera control, labels showed through).**

- `render::PlaySurface` (`src/render/play_surface.cpp`, new) owns every placement: the
  **patch of surface each square is**, the seat a piece stands on, and the ray a click is
  tested against - one sampling, so the pick cannot disagree with the picture.
  `BoardRenderer::buildInstances` turns the patches into one mesh with its colours in its
  vertices and leaves the pieces instanced; the session's own camera, depth and MSAA apply
  and orbit and zoom work. `standard --geometry` is a strict no-op.
- A square is a *patch*, not a rectangle: a flat tile is tangent at one point, so on a
  fast-curving surface the squares cut into each other at one end and gape at the other.
  Earlier cuts also got the frame (shortest-arc normals spun every square), the scale order
  (`board.vert` scaled the world axes, not the mesh's) and the size (the metric is not
  constant) wrong. See **ADR-0019**.
- **The board is posed on its surface:** `slideU`/`slideV` slide it *along* the surface -
  middle-drag on the shape, or `--slide`/`--slide-v` - so a1 rides to b1, round, and home
  (mirrored, on a Moebius band, after two laps). `evert` swaps the outward side - the
  squares fixed, the pieces moved to the other face, `INVERT`/`[`/`]`/`--evert`.
  Presentation only; neither enters `VariantId`.
- `Settings::geometryView`, `geometrySlideU`/`geometrySlideV` and `geometryEvert`, a
  `SHAPE` button and the `G` key, offered only where `render::hasPlaySurface` holds.
- Two surfaces were corrected in the shared catalogue: the Klein bottle's figure-eight is
  now walked at constant speed (8 files had been landing in pairs), and the Moebius
  ribbon's stretch is a pose field so the play board can keep less of it than the library
  screen does.
- Coordinate labels moved to the board's near edges in the machine face (flat/3-D only),
  and changing the palette now re-applies the ImGui style (console had been keeping
  manifold's dark type). Settings drop-downs became press-to-cycle buttons.

**Deferred** (see the M17 status section): the move animation is not warped, the surface
has no coordinate labels or seam rails yet, and the eversion passes through genuinely
degenerate embeddings (the spindle torus, the cylinder's own fold), drawn honestly.

**Next, and specified**: M17.7-M17.12 in the same plan.

**M17.7-M17.11 are built (2026-09-30).** The drag axes are swapped (left/right along the
ranks, up/down along the files, M17.8); the slide wraps after two laps - `fmod(s, 2*nx)` in
cells, in `PlaySurface` alone (M17.9); the shape is framed on its own centre with
`headroom = 0` (M17.11); an `INVERT` button and `]`/`[` set a target the front end eases,
and the outward normal is chosen away from the shape's axis so pieces stay outside after the
eversion (M17.7); a `GHOST` button and `--ghost` blend the board through a second pipeline
with depth writes off (M17.10). Tests and `gui-invert`/`gui-ghost` captures are in.

**Remaining: M17.12, the shapes above two dimensions.** First pass done (2026-09-30):
`torus3d` (nested shells) and `hyper4` (tesseract) play on their own shapes via an authored,
name-keyed `playShapePosition` and a stacked branch of `PlaySurface`. Limits and the `t6`
verdict are in [`M17.12-shapes-above-two-dimensions.md`](M17.12-shapes-above-two-dimensions.md):
the level/aeon axes are not adjacent-in-the-drawing, slide/invert are not offered for these
shapes yet, and **a faithful playable quintic is impossible** (the 6-torus projects ~10
lattice cells per drawn tile), so `t6` stays on the lattice and any playable quintic must be
a sliced view. Finish the 3-D shapes (slide/invert, hidden-sheet handling, adjacency tests)
before the sliced quintic.

### 5. Found 2026-10-03, playing it: the chase camera goes into the board, and jitters

**Fix this before anything else below.** Reported during a manual playtest pass: a followed
move's camera on a shape (`shapeFollow = "chase"`) sometimes clips into the board and
sometimes jitters. Reproduced and root-caused: `alignSlideU`'s anti-clip search
(`src/render/play_surface.cpp`) scores a *different* eye direction than the one
`surfaceChaseCamera` actually draws (the search uses the raw surface normal; the real
camera first removes its component along the travel direction), so a rotation the search
calls "clear" can still be looking straight into the shape - and the existing property
tests cannot catch it, because they reimplement the same (wrong) formula instead of calling
the real one. A second, compounding bug: when nothing in the search grid is actually clear
at the nominal eye distance, it silently falls back to the best-*facing* (still occluded)
candidate rather than ever pulling the camera closer. Full root-cause writeup, the fix (a
shared `chaseEyeDirection`/`clearEyeDistance` primitive used by both the search and the
camera, so they cannot diverge again), and the regression tests that would have caught it:
**[`M17-geometry-view.md`](M17-geometry-view.md), §M17.20.**

### 6. Then the rest of Wave 1, fully spec'd and ready to build, in this order

Spec'd 2026-10-03 for a handoff to opencode (see each file for the detailed build/tests/
acceptance - this is only the index and the order):

1. **M17.20** above - fix first; M12.6 below records through this camera, so a clip of it
   is only worth having once this is fixed.
2. **M17.21** - seam rails and coordinate labels on the shape (the M17.5 remainder):
   [`M17-geometry-view.md`](M17-geometry-view.md), §M17.21.
3. **M17.22** - slide/invert/ghost-picking for the D>=3 stacked shapes (`torus3d`,
   `hyper4`), pinned adjacency tests, and a new twisted-3-torus variant (`flip` on one
   `torus3d` axis, same trick `klein.toml` already uses in 2-D) with the shape view
   extended to show the twist:
   [`M17.12-shapes-above-two-dimensions.md`](M17.12-shapes-above-two-dimensions.md).
4. **M12.6** - the marketing notation runner (`--play FILE [--clip DIR]`), now including
   making the M17.19 shape choreography a pure, deterministic function of elapsed time (it
   currently only runs in interactive play - captures use the plain steady-state follow on
   purpose, and that default must not change) and `--width`/`--height` capture flags
   (default bumped to **1920x1080**, Steam's trailer minimum; was a hardcoded 1440x900):
   [`M12-broadcast.md`](M12-broadcast.md), "M12.6 implementation spec (2026-10-03)".

Trailer-priority variants, from a planning conversation the same day: `standard`,
`torus`/`klein`/`mobius`, `cube5`/`hyper4`, plus `5d` (time travel, flat board, no shape-
view work needed), `atomic_torus` (explosion + wrap) and `torus3d` (the twisted-variant's
untwisted sibling) - see §M17.22 for why `torus3d` specifically rounds out the D>=3 set,
and `t6` stays lattice-only (no shape view planned; see the Verdict in
`M17.12-shapes-above-two-dimensions.md`).

### 7. Then Wave 2

M14 onboarding, M16.3 the demo (M16.2 is done), M7's board and gamemode editors (its piece
editor moved up to Wave 1 on 2026-10-16). See the release sequence in the roadmap.

## Verifying a derived overture (no shipped variant)

The derived path only triggers for a variant whose name is in no C++ table. To see one:

1. Write `variants/_demo.toml` (any name, e.g. a 2-D torus or a 4×4×4 box). A worked example
   is in `tests/render/test_overture_scene.cpp` ("a data-only variant gets a derived
   overture") and the two structures are in `variants/standard.toml`.
2. `./build/dev/src/gui/endspielzero_gui _demo --shot out.ppm --screen newgame --t 1.0`. The
   capture forces the library picker to the named variant, so the derived scene is what is
   drawn.
3. **Delete the file before committing** - `tests/golden/test_shipped_variants.cpp` scans
   `variants/`, so a stray temp variant becomes a shipped one and breaks the goldens.

## Commands (from `AGENTS.md`)

- `tools/test.sh --build <tag...>` — only the areas a change can touch (tags: `view`,
  `render`, `app`, `io`, `game`, `rules`, `variant`, `pieces`, `golden`, `arch`).
- `tools/test.sh quick` — every fast ctest label (`unit render golden arch` + gui).
- `ctest --preset dev` — the whole fast suite (adds the property sweep and perft).
- `tools/package.sh` — self-contained package + clean-directory smoke test.
- Captures: `--shot`, `--clip`, `--cinema`, `--follow off|piece|route`, `--move-t 0..1`,
  `--bench-frame N`.

## Gotchas that will bite on a fresh machine

- **A build directory remembers its shell.** Anything touching the renderer, the UI or the
  GUI must be configured and built under `nix develop .#gfx`; the plain shell has no
  SDL/Vulkan headers.
- The `--shot`/`--clip` path is settled by **two thrown-away frames** before the real ones
  (a screen-change resets the pane transition after the tick; the second advances it). One
  frame and the library draws at alpha zero.
- Temp variant files must be deleted before committing (see above).

## 8. After Wave 1: M18, production polish

Spec'd 2026-10-04, once all of Wave 1 (items 6 above) is done. Not a release-sequence item -
see [`M18-production-polish.md`](M18-production-polish.md)'s own header for why. Prompted by
a direct playtest note: the engine and the camera work are solid, but a capture vanishes with
no acknowledgement, pieces have no contact shadow, the game is completely silent, the menu
background is disconnected rainbow confetti, and `--cinema` output is unframed. Five
independent pieces, cheapest/highest-impact first: M18.1 capture/move juice, M18.2 contact
shadows, M18.3 a cinema vignette/grade, M18.4 the ambient background redone (the game's own
geometries - torus, Klein, Möbius, cube, hypercube; explicitly not the quintic - drawn as
quiet wireframes instead of filled confetti, brought to the game screen too), M18.5 audio v1
(SDL3's own audio API, procedurally synthesised SFX, no licensed assets needed, no music yet).
Full build/test/acceptance detail is in the spec file. **Built 2026-10-04**, all six items
plus the M18.4/M18.6 revision round below - see each section's own "Status" note in the
spec file for what was built and any deviations.

### M18.4/M18.6 revision round (2026-10-04)

Found by directly playing with the shipped M18 work, not by review: the SHAPE morph
(M18.6) was off-centre at the end of its roll - which turned out to be a genuine camera bug
one layer below M18.6 itself, fixed as **M17.24** in `M17-geometry-view.md` (the camera was
framed on the shape once, at the toggle instant, and never revisited as the shape's own
bounds kept changing through the roll) - plus a request for a morph-speed setting, and three
pieces of feedback on M18.4's background (the quintic wanted back in as one shape among the
six rather than staying fully retired, the clutter's own breathing motion could carry it back
toward the centre it was supposed to avoid, and every body traced one fixed rotation path
instead of a real tumble). All four are built, independently re-verified and committed; see
`M17-geometry-view.md`'s M17.24 section and the two "Revision" subsections in
`M18-production-polish.md` (under M18.4 and M18.6) for the detail.

**One verification gap worth knowing about:** M17.24 and the morph-speed setting only
affect the live interactive loop, not `--shot`/`--clip` captures - there is no
synthetic-input tool in this environment to script a SHAPE toggle in a running window and
capture it over time, so both were verified by code review and by tests built against the
underlying pure functions, not by a watched recording. Flagged explicitly in both commits;
worth a human confirming the live feel next time SHAPE is used in play.

## Next

**Reprioritised 2026-10-16 (the user's direction):** the **piece editor of M7 is moved into
Wave 1**, ahead of M16.1's store page. The shipped piece models are placeholder primitive
archetypes and are not good enough to ship; only a human can redraw them, and they appear in
every clip, overture and store-page frame. So the next build is M7's **piece editor only** -
the shared authoring document/serializer (M7.0), then the 3-D profile and 2-D icon editors
(M7.3) that let a person replace the models. The board and gamemode editors (M7.5) are not
part of Wave 1 and stay in Wave 2. See `00-roadmap.md`'s release sequence and
`M7-editors.md`'s own "Scheduled (2026-10-16)" note and Status.

Beyond that, Wave 1 (M11, M12 incl. the M12.6/M12.8 runner and watch, M13, M17) and M18 are
done; the rest of the next real work is Wave 2 (M14 onboarding, M10.2-M10.3 the baseline
opponent, M16.2-M16.3 the demo, M7's game editor) per the roadmap.
