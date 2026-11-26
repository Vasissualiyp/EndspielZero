<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# M25 — The neural handoff: one interface, weight, flexible skin, and VR where the room runs out

Status: **M25.1–M25.4 built 2026-11-21; M25.5 built and verified 2026-11-25** from play-testing after M24. Implementers: Codex
**gpt-6.1-sol** (M25.1, M25.2) and **gpt-6-astra** (M25.3, M25.4, M25.5). All on `main`,
one slice after another, in the order below. Each slice adds its own dated Status entry
at the end of this file.

Read first:
- `docs/plan/M24-the-rig.md`, "Rules for every M24 slice". They still hold: lab-off
  frames stay byte-identical, captures default to lab off, and every visible change gets
  a contact sheet you have looked at;
- `docs/plan/M24-bci-look.md`, which is binding for anything drawn by the implant;
- the Status entries of M24.4b (implant layer), M24.5/M24.5b/M24.6 (deploy, skin,
  gantry), M24.7 (waking up: the eyelid and BCI handshake animations), M24.9.

The play-test feedback this answers, in the user's words where it matters:
- the slab is fine for a flat board, but not for **shape** mode;
- the structure models (frames, the wires between squares when a shape is applied) are
  very low-poly. "I liked the previous stretching squares better";
- only the main menu is neural. "The whole UI should be neural, otherwise
  inconsistent": the pause menu, the editor, and the rest;
- the boards leave the rack too fast: "I don't feel weight underneath the boards";
- T6 "lags tremendously because of extra wiring". When the board count is unphysical
  for the room, switch to a holographic VR projected by the implant;
- after AXES on a 3-D initial view (T6), there is no way back to that view;
- the board must **never** leave the rack in shape form, only as a flat screen;
- the heartbeat is invisible.

User decisions (2026-11-21):
- heartbeat: a **visible glow pulse** on every neural screen, whose rate **reacts to
  the game**;
- VR switch: games with **more slabs than the rack physically holds** go to VR;
- VR space: **"lucid horizon"**, an endless soft-lit floor fading to a horizon with a
  faint grid, like a calm dream, with the boards floating above it;
- shape mode on a physical board: a **physical flexible skin**. Drop the slab, ribs and
  wires; the stretching squares bend into the shape themselves, high-poly and smooth;
- the rack stays where it is (M24.9 section 6's fallback is accepted).

---

## M25.1 — One neural interface (Sol)

Every screen and panel the player sees in the lab goes through the implant layer the
main menu uses (M24.4b, `M24-bci-look.md`). Nothing stays as a plain ImGui window over
the room.

1. **Inventory first.** List every screen and overlay: menu, tutorial, Library, Watch
   (chooser and the watching HUD), Settings, Quit, **Editor** (and its sub-screens),
   the in-game HUD (title card, move panel, the dock with UNDO/RESET/AXES/3D/SHAPE/MENU,
   promotion picker, game-over), **Paused** with Save/Load, the debug console (exempt:
   leave it plain), and any modal. Record which path each draws through now. Put the
   table in the Status entry.
2. **Route them all** through the implant composite: the same form/dissolve, afterimage,
   salience, focus coupling, tremor and heartbeat split (never on glyph cores), the same
   typography and synaptic hue. The in-game HUD may be dimmer (a lower focus floor), so
   it does not fight the board.
3. **Editor:** the editor screens were never moved onto the lab background. In the lab,
   they get a station (standing rule from M24.4e: eye 1.45–1.75 m, ≥ 0.25 m from every
   footprint) looking at the bench or the wall projector, and the editor's panels are
   neural. Its own board previews use the hologram material. The menu ↔ Editor flight
   follows the existing station flights and their clearance test.
4. **Heartbeat you can see.**
   - Visible: an obvious "lub-dub" on panel glows (`uiHalo`) and a faint one at the
     edges of the view. A reasonable target is 15–25 % on the halo, 3–5 % on a soft
     peripheral band. Never on glyph cores. It runs on every neural screen and in
     M25.5's VR.
   - Reacts to the game: a resting rate of 60 bpm that rises with tension and settles
     back over ~10 s. Tension comes from check against the player (+20), a capture in the
     last move (+10, decays), the player's clock under 30 s (+25), and the game's end
     (a peak, then rest). Cap at 110 bpm. Keep the phase continuous when the rate
     changes (integrate the phase; never recompute it from the time).
   - At calm intensity (and with reduced motion), the heartbeat stays off, as in M24.4b.
   - Pure function `heartbeatRate(tension)` plus a phase integrator, both unit-tested.
5. **Sheets** in `out/m25.1/`: every screen in the inventory, before and after, at both
   intensities; `heartbeat.png`, a 2-second strip at rest and under check, with a
   brightness-over-time plot of the halo.
6. Commit: `render: one neural interface - every lab screen through the implant, the
   editor in the lab, a heartbeat you can see (M25.1)`.

## M25.2 — AXES can always go back (Sol)

On a board with D axes, the AXES button cycles through `1 + 2·D` views (`ui.cpp`, the
`axisRotation_` cycle). On T6 that is 13 clicks, and the initial 3-D view only comes back
after the 12th.

1. Make the initial view always one action away:
   - the AXES button opens a small neural picker. Its first row is **"Initial view"**,
     the representation the game started in; the rows below are the arrangements the
     cycle offers now. Clicking the button again closes it;
   - or, if that cannot fit in the dock, keep the cycle and add a "back to initial"
     button. Record the choice.
2. "Initial view" must restore exactly the `ViewConfig` the session started with
   (`chosenCfg_` from `ViewConfig::forBoard`), including the 3-D depth arrangement and
   the flat/solid mode. Test: for every shipped variant with D > 2, apply each
   arrangement and then "Initial view"; the effective `ViewConfig` and the camera
   framing equal the starting ones.
3. Add a session action for it (for example `axes initial`), so scripts, the console and
   tests can use it.
4. In the lab, choosing an arrangement moves the hardware as M24.6 does, and so does
   going back.
5. Commit: `app: AXES can always return to the initial view (M25.2)`.

## M25.3 — Boards with weight, flat out of the rack (Astra)

1. **Always flat out of the rack.** A slab leaves and enters the rack only as a flat
   screen. If the game (or a resumed game) is in shape view, the deploy runs flat, and the
   shape forms only after the slab has locked at its position. A stow first unforms the
   shape to flat and then lifts. The same applies to relayouts. Test: at every sample of
   every deploy, stow and relayout, `formed == 0` for any slab that is not locked.
2. **Weight.** The current departure is too fast and floaty. Retime and reshape it so the
   slab reads as heavy:
   - the carriage takes up the load: a short, visible hesitation (~0.15 s) while the
     slab breaks free of the bay, a tiny drop (5–10 mm) as the latch lets go, then a slow
     start;
   - acceleration and deceleration with an S-curve: no instant velocity changes, with
     peak speed reached late;
   - inertia: the slab lags the carriage slightly through accelerations (a pendulum
     on a stiff hanger, a few degrees at most, critically damped), and a small settle
     (1–3 mm overshoot, one cycle) when it locks;
   - optionally, a mechanical sound cue hook (no audio assets needed);
   - total deploy for one slab about 2.5–3.5 s, for the slab phase; multi-slab departures
     overlap as M24.6 does, with the whole deploy ≤ 5 s. Any key or click still skips it.
3. Keep the M24.9 camera clearance (≥ 0.30 m from the carried slab) through the new
   timings; extend its test.
4. **Sheets** in `out/m25.3/`: deploy and stow for standard and cube5, sampled every
   0.1 s, plus a plot of the slab's height and speed over time.
5. Commit: `render: boards with weight - they leave the rack flat, break free, and
   settle (M25.3)`.

## M25.4 — The flexible skin (Astra)

In shape view, a single-board shape (cylinder, torus, Möbius, Klein, and any 2-D glued
board) is no longer a slab with ribs and wires underneath.

1. **Drop the slab** for the shape: once the board is deployed and locked flat (M25.3),
   the slab's playing surface becomes a **flexible skin** of the squares themselves. The
   squares stretch, bend and close into the shape (the earlier "stretching squares" look
   from before M24.5b), held at one point by the gantry's yoke. The slab body, ribs and
   the wires between squares are not drawn in shape view. Find the earlier stretching
   look in git history (before M24.5b) and start from it.
2. **High-poly and smooth.** The skin is tessellated finely enough that no facet or
   silhouette kink shows at 1280×720 from the game camera or a 2× crop (aim for ≤ 2 px
   chord error). Normals are smooth across tile borders; tile borders are a material
   seam (a thin line in the material), not a geometric gap.
3. **Structure models:** frames, yoke, carriage and any remaining hardware visible in
   play get smooth, higher-poly versions (bevelled edges, round sections at ≥ 24
   segments). Check the authored OBJs and the procedural pieces. Keep the M24 model
   provenance rules (CC0 or public domain only, recorded).
4. Pieces keep standing on the skin's surface normal, as now.
5. The flat state stays the slab (M24.5b); the transition flat → shape starts with the
   tiles lifting off the slab's surface into the skin, and shape → flat reverses it.
6. **Perf:** the skin must not cost more than the M24.5b ribs did. Measure torus formed,
   before and after, with the M24.8 bench method.
7. **Sheets** in `out/m25.4/`: cylinder, torus, Möbius and Klein at formed 0, 0.5 and 1,
   with 2× crops of a silhouette and a tile seam; the structure models before and after.
8. Commit: `render: the flexible skin - squares bend into the shape, no slab or wires,
   smooth hardware (M25.4)`.

## M25.5 — The neural VR (Astra)

When a game needs more slabs than the rack physically holds (count the rack's bays in
the authored model; M24.6 reuses 8 bays), the room cannot hold it. The game moves into a
virtual space the implant generates. T6 (256 slabs) must no longer lag.

1. **Which games.** `needsVr(variant, viewConfig)` is true when the deployed slab count
   exceeds the rack's bay count. Unit-test it for every shipped variant and record the
   list (expected: standard, cylinder, torus, Möbius, Klein, cube5 physical; hyper4,
   hyper4_8x8, t6, torus3d* checked by count). For 5D, crossing the limit mid-game (new
   timelines) triggers the handoff then.
2. **Entering, after the player starts a VR game:**
   1. one slab deploys to the centre exactly as for a standard game (M25.3, with
      weight), and boots;
   2. its LEDs show a **neural code**: an organic, QR-like pattern (think of a
      reaction-diffusion or a cell pattern, structured but alive) that the implant reads.
      It pulses for ~1.5 s;
   3. the implant handshake (M24.7's BCI animation) answers it, then **the eyes close**
      (M24.7's eyelids, run in reverse);
   4. darkness, then the **lucid horizon** opens: an endless soft-lit floor that fades to
      a horizon, with a faint grid, a calm dream sky, and the implant's synaptic hue as
      the light. The boards float above the floor as **hologram light** (no slabs, masts,
      cables or wires), laid out by the current `ViewConfig`;
   5. the game HUD is neural (M25.1).
3. **In VR:** board layout, AXES, SHAPE, 3D/2D and relayouts all work, as light. Pieces
   are hologram pieces. Light bridges (M24.6) become streaks of light across the space.
   A relayout moves the boards smoothly; nothing teleports.
4. **Leaving** (menu, game over then menu, quit to menu): the eyes open (M24.7), the
   implant activates again, the room is back with the slab in the centre showing the
   neural code, which fades, the board turns off, and the slab goes back into the rack
   (with weight). Then the menu flight runs as usual.
5. **2-D view, physical or VR.** Choosing 2D closes the eyes (the same short eyelid
   close, without the code step when already in VR), and the board is shown as the
   implant's own picture: a front-on flat projection, filling the view, with no room
   or perspective. Going back to 3D opens the eyes (physical) or returns to the floating
   boards (VR).
6. **Perf.** No rig hardware is built or drawn while in VR. Target with the M24.8 bench
   method: T6 in VR ≤ 1.5× the standard game in the lab, and hyper4 in VR ≤ the hyper4
   lab case recorded in M24.8 (102.5 ms/frame on lavapipe at 1280×720). Record the
   numbers.
7. Debug: console `vr on|off|status`, CLI `--vr-t <s>` to pin the handoff, `--vr 1` to
   start inside. Document them in `docs/debug-console.md`.
8. **Sheets** in `out/m25.5/`: the handoff (enter and leave) for hyper4 and t6, sampled
   every 0.25 s; the lucid horizon with t6, hyper4 and torus3d; the 2-D implant view for
   standard and hyper4; the neural code close up.
9. Commit: `render: the neural VR - when the room runs out, the implant takes over
   (M25.5)`.

## Status

### M25.1 — built and verified (2026-11-21)

Inventory before implementation (all drawing is ImGui unless indicated):

| Screen / overlay | Existing builder and route |
|---|---|
| Main menu, Tutorial, Library, Watch chooser, Settings (scrolling Display/Looks/Board/Camera/Animation/Gameplay/Audio/Controls sections), Quit | `ui_menus.cpp` screen panes; implant only on lab menu path |
| Watch playing HUD / trailer controls | `Ui::buildGameHud`, direct overlay |
| Editor home, movement help, Board designer | `Ui::buildEditor`, plain `beginPane`, no lab |
| Piece designer: Movement, Body, Flat, Identity, Castling, Marks, Fields, Rules | `buildEditor` / `ui_designer.cpp`, plain shared pane, no lab |
| Editor unsaved-change confirmation | `buildEditor` replaces shared pane content, direct |
| Editor number-entry popups and outline tools | nested children / popups in shared editor pane, direct |
| Editor movement-board preview | `drawEditorPreview`, background ImDrawList software projection |
| Editor body/icon previews | `ui_designer.cpp`, authoring canvas inside shared pane |
| Game title / variant, move ledger, captured trays, dock (UNDO/RESET/AXES/3D/SHAPE/MENU) | `buildGameHud`, framed windows and text overlays, direct |
| Game-over result / collapsed RESULT button / review dock | `buildGameHud`, direct |
| Promotion, move confirmation | `buildGameHud`, modal popups, direct |
| Paused with Save/Load and saved-game rows, pause Quit | `buildPause` / `buildPauseQuitConfirm`, screen panes over board, direct |
| Game information / piece reference and move diagrams | `buildGameInfo` / `buildPieceMoves`, panes plus diagram window, direct over board or implant on menu path |
| Debug console | `drawDebugConsole`, tool-rectangle bypass (exempt) |
| Development FPS overlay | `buildGameHud`, tool-rectangle bypass |

Continued the interrupted working tree after reviewing its diff and the saved failing
test logs. Implemented:

- `configureImplantUi` now enables the shared composite on every lab screen, including
  the watching HUD, pause, results, promotion and move confirmation. The renderer keeps
  its hardware palette; `implantTheme` supplies the interface palette. Completed ImGui
  windows and popups are recorded by `Ui::recordImplantWindows`; children inherit their
  parent's record. Existing form/dissolve, persistence, focus, tremor and gaze machinery
  is reused. Console and development FPS rectangles remain tool bypasses.
- Editor has a standing `editor` station and uses the existing station flights in both
  directions. Its movement-board preview feeds `overtureHologram` at the bench, tipped
  35 degrees toward the standing eye; projected quads provide nearest-visible-cell
  picking. Body/icon outline canvases remain editable content inside the neural pane.
  Tests extend the existing station clearance sweep and check preview normals, picking
  and the unicorn glyph. Hardware corner brackets and heading rules are suppressed on
  the neural path; board-designer semantic colours survive as small ticks.
- `view::heartbeatRate` clamps the additive tension demand to 60–110 BPM.
  `HeartbeatPhase` integrates the phase with an exact constant-demand exponential and
  a three-second rate time constant (approximately ten seconds to rest).
  `app::NeuralHeartbeat` observes player check, the last move's capture and game end;
  transient demands decay. A screen/session change preserves phase. The shader pulses
  the broad halo by 20% and adds a soft 4% peripheral band, leaving glyph cores and tools
  stable. Calm intensity suppresses both. Explicit capture time drives the same phase
  integrator; capture heartbeat still defaults off.
- Debug help, capture help, the console reference and AGENTS.md navigation reflect the
  shared interface and editor station. Regression coverage includes every screen,
  editor sub-screen, modal records, phase continuity, tension inputs, the glow-only
  split, calm suppression and deterministic headless formation.

Deviations and limitations:

- Countdown player clocks do not exist in the current game/session API (only the chess
  halfmove draw counter does). The controller accepts and tests optional remaining
  seconds; connecting it awaits an actual time-control feature.
- `implantTheme` remains inline in the existing header-only theme module. No new theme
  `.cpp` was introduced. The existing calm presentation setting is the reduced-motion
  path; there is no separate reduced-motion setting.
- Two old assertions expecting no settled implant records now require records: every
  neural HUD/window needs settled bounds for focus and glow. The heartbeat bounds were
  updated from 2% to the specified 20%. No golden or pixel tolerance was changed.
- No staging or commit is performed in this sandbox; the requested message and file
  manifests are the handoff, once all gates pass.

Visual review (all under `out/m25.1/`): `menus-{0,1}.png`, `game-{0,1}.png`,
`pause-and-modals-{0,1}.png`, `editor-a-{0,1}.png`, `editor-b-{0,1}.png` and
`tools-{0,1}.png` compare the inventory before/after at calm/full intensity, including
the editor's open number-entry popup. `editor-popup-and-picking.png` also shows a
piece placed on the projected preview; these final sheets were reviewed.
`lab-off.png` records four byte-identical before/after pairs (game, menu, editor body,
formed torus). `heartbeat.png` contains the two-second rest/check strip and measured
halo/peripheral brightness plot; `heartbeat-contact.png` covers all 164 frames in 82
on/off pairs at 0.05-second intervals. The peak measured blue lift is 4.39/4.95 code values in the
halo region and 8.38/9.13 at the periphery (rest/check). The check rhythm accelerates
without a phase jump. Also reviewed the inherited diagnostic sheets (`inventory-before`,
`overlays-before`, `first-pass-{0,1}`, `second-pass-{0,1}`,
`implant-identity-investigation`, `heartbeat-initial-contact`) and the designer detail.

Final review found one additional BCI mismatch: `widgets::heading` still draws a
horizontal rule in Settings and reference panels. The existing neural-heading test
covered `eyebrow` and `sectionHead` only, which is why it missed this older helper.
Adding `heading` reproduced eight unwanted vertices (34 versus 26) in
`out/m25.1/legacy-heading-red.log`. The same `implantFrameless` guard now covers this
helper, preserving its text, spacing and lab-off path. The expanded regression passes;
the Settings/reference sheets were refreshed and reviewed, and all gates were rerun
against that final binary.

Final verification, through the required `g.sh`, with at most seven build jobs and
serial GUI captures:

- `tools/precommit.sh` printed **`precommit: OK`**: architecture, unit and property
  gates passed (`out/m25.1/precommit-accepted.log`).
- `[render],[app],[view]`: **730/730 cases, 18,350,437 assertions** passed
  (`out/m25.1/render-app-view-accepted.log`).
- `ctest -L gui --output-on-failure`: **58/58 passed**, serially, in 873.06 seconds
  (`out/m25.1/gui-all-accepted.log`), including `gui-neural-interface`, both editor
  station flights and the unchanged implant/direct ±2 comparison.
- Explicit whole-file `clang-format --dry-run --Werror` checks passed for all changed
  C++ files, including the new unstaged files; the final heading/test pair was checked
  again. `git diff --check` passed. No golden was updated.

### M25.2 — built and verified (2026-11-21)

- Choice: the dock **fallback**, not the picker. The AXES cycle is unchanged; a new
  **INITIAL** dock button (cold, beside AXES) appears on boards with more than two axes
  only while the chosen arrangement differs from the starting one, and restores it in one
  click. The dock is a measured layout, so the button count is sampled once per frame
  (`offInitialView`) before the AXES click can change it. A popup was not attempted: it
  would need its own implant window record and focus handling inside a dock that is
  already full on T6.
- `Session` keeps `initialCfg_` (the `ViewConfig::forBoard` result at creation) and
  exposes `atInitialView()`. Session action `axes initial` restores exactly that
  `ViewConfig` (axes, grid, vertical flag, gap, depth spacing) and re-frames the camera;
  `axes default` still means the board's own default. The button resets the cycle counter
  so the next AXES click starts from the first arrangement.
- Lab: the lab already follows any view change through the M24.6 relayout, so choosing an
  arrangement and returning both move the hardware; no lab code changed.
- Test: `"axes initial" returns to the view the session started in (M25.2)` runs every
  shipped variant with D > 2, applies every pair of axes both across and down (skipping
  pairs a temporal board refuses), then `axes initial`, and checks the `ViewConfig` and
  camera distance/target equal the start; also through flat mode. Debug help gains a
  `script axes` line (checked in `test_debug_console.cpp`) and `docs/debug-console.md` a
  row. The test was written alongside the implementation in one build, so no separate red
  run was recorded.
- Captures `out/m25.2/a.png` (T6 in a non-initial view, INITIAL shown) and `b.png` (after
  `axes initial`, button gone, start view back) were looked at.
- Gates: `precommit: OK`; `[render],[app],[view]` 731 cases, 18,351,479 assertions;
  `ctest -L gui` 58/58 passed, serially (`out/m25.2/gui.log`).

### M25.3 — built and verified (2026-11-21)

- **Weight** (`view::slabDeploy`, `src/view/rig_deploy.*`). The new phase table, in seconds
  after a slab's departure: the carriage arrives over the pick-up and takes up the load
  (`kSlabTakeUpEnd` 0.40; a smootherstep from its park); a 0.15 s hesitation in which the
  slab drops `kSlabDrop` = 7 mm as the latch lets go (0.43–0.55); slide out (to 1.05),
  tilt face-up while rising (to 1.60), travel (to 2.60), lower (to 3.10), lock at
  `kSlabLockSeconds` = **3.30**. Horizontal moves use a late-peaking S-curve (velocity
  ∝ x³(1−x)², peak at 0.6, zero velocity and acceleration at both ends); vertical
  moves and the tilt use smootherstep, so no phase join changes velocity. The settle is
  one 2 mm dip below the hang and back, starting 0.1 s before the lowering ends with
  zero velocity (the old damped sine started with a velocity jump). Inertia: the slab
  rocks on its yoke against the travel's acceleration, at most `kSlabSwingDegrees`
  2.5° before damping, through a critically damped kernel (ω = 15 rad/s) sampled
  in explicit time; it is causal in the stow's own time (`SlabPathSpec::reversed`), so
  a stow lags its own motion. One slab: 3.5 s with its 0.2 s departure; 256 slabs:
  4.7 s. `appendRig` still draws the yoke while the slab is within ~5° of level.
  No sound hook was added (the spec marks it optional).
- **Flat out of the rack.** `deployFormGate`, `stowFormGate`, `relayoutFormGate` and
  `stowLead` are pure gates on `geometryFormed`: 0 whenever any slab is neither clamped
  nor latched in its bay. The shape forms over `kRigFormSeconds` 0.8 s after the last
  lock (inside the deploy's trailing second) and unforms over `kRigUnformSeconds`
  0.8 s before anything leaves: a stow waits `max(shutdown, 0.8 s)` from the shape view
  (also for a finished game, whose fx shutdown stays 0: `gStowFxShutdown`); a relayout
  that starts formed gets `Relayout::lead` = 0.8 s, during which `relayoutFrame` holds
  every slab clamped at its start pose (arrival boots wait for it too). `main.cpp`
  applies the gate to `Settings::geometryFormed` for the drawing part of each frame
  (`FormGate`, restored after; capture and interactive paths), so the surface, the
  stacked parking, ribs and seam latches all see it, while `frameBoard` and the morph
  framing keep the live value. A closing seam therefore latches as the shape forms on
  the locked slab.
- **The sheet rides its slab.** While flat in transit the shape view's surface mesh is
  carried vertex by vertex (`carryBoard` / `carryRelayout`, normals turned). Found in the
  torus stow sheet: the mesh's identity instance was also being carried by the instance
  pass, which moved the sheet twice (`sheet-carry-fixed.png`); those instances are now
  skipped. Ribs are drawn only on a still rig. Before this slice the formed shape simply
  hung at its destination during a deploy (M24.5 decision 3).
- Still slabs in a relayout (`slabRelayout` with an unchanged pose) now report
  `settled`, so they count as clamped.
- **Tests:** `[unit][view][rig][weight]` (5 cases: durations, break-free and drop, no
  velocity jump (< 700 cells/s², a 1 cell/s jump in a millisecond would be 1000) with a
  late peak, the swing's bound/sign/one sign change and the stow's causal lean, the
  gates); `tests/render/test_rig_weight.cpp` (`[render][rig][weight]`, 3 cases: the
  spec's invariant at 60 Hz through every deploy and stow (with and without shutdown)
  for standard, torus, Klein, cube5, hyper4 and every relayout sample for hyper4, cube5
  and standard; the sheet carry including the identity instance; plus a hidden
  `[.weightplot]` data writer). The camera clearance test now also covers the shape-view
  stow leads. Updated timing-pinned assertions in `test_rig_deploy.cpp`,
  `test_lab_rig.cpp` and `tests/check_lab_deploy.cmake` (settled at 5.6 s instead of
  3.6 s; a torus shape-view deploy is deterministic and moves). Red first: the new tests
  failed to compile against the old API. The identity-instance regression check was
  added together with its fix, so it has no separate red run.
- **Sheets** in `out/m25.3/`, all viewed: `deploy-standard.png`, `deploy-cube5.png`,
  `stow-standard.png`, `stow-cube5.png`, `deploy-torus.png`, `stow-torus.png` (0–5 s at
  0.1 s), `weight-plot.png` (first slab's height, speed and tilt; peak speeds ≤ 4 m/s,
  swing 1.7° accelerating / 2.1° braking), `sheet-carry-fixed.png`.
- Deviations: the peak speed is still up to 4 m/s: the slab covers ~4 m (slide out,
  rise, travel, lower) and the spec caps one slab at 3.5 s. The phases were rebalanced
  once after the first plot (vertical peaks were 5 m/s). `geometryFormed` is gated by
  a scoped override in `main.cpp` rather than a new options field.
- Gates (through `g.sh`): `tools/precommit.sh` printed **`precommit: OK`**;
  `[render],[app],[view]` **739 cases, 18,399,777 assertions** passed; `ctest -L gui`
  **58/58 passed**, serially, in 494.64 s (`out/m25.3/gui.log`). `clang-format --dry-run --Werror` clean on every changed C++ file.

### M25.4 — built and verified (2026-11-21)

Continued the interrupted working tree after reviewing `git status`, the complete diff,
the saved red run and all seven inherited contact sheets. The earlier stretching-square
implementation is the `PlaySurface::build` patch loop at `38a51fa^` (before M24.5b):
it samples each cell's lattice footprint directly, without inverse-stretch shrinking.
The flexible skin extends that same sampler and the existing piece seats/picking.

- `SurfacePose::flex`, `FlexTile` and `PlaySurface::flexTiles` produce a finely sampled
  sheet for the 2-D glued shapes. Ten subdivisions per cell plus two seam grid lines
  give 13 × 13 samples per tile. Analytic finite-difference normals follow the existing
  walked normal field's side. Adjacent tiles share positions and tangent planes;
  the non-orientable gluing can exchange the two drawn faces. A thin tinted material
  border replaces the geometric gaps, retaining the LED cell coordinates, capture
  pulse, colour response, gameplay cues, ghosting and seam-latch draw ranges.
- `flexBlend` closes the tile gaps smoothly over formed 0–0.15; `slabBackFade` removes
  the backing over the same interval using the existing screen-door fade. At formed 0
  the M24.5b slab remains. On the formed sheet, `appendSkinFrame` draws one short round
  support from the yoke boss to a seat's back face (`skinHold`), with a normal-aligned
  foot; no ribs, hoops or inter-square wires are drawn. M25.3's existing form gates
  keep deployment, stow and relayout flat whenever the slab is travelling.
- Procedural rig rails, bridges and cable tray now use bevelled bars; the yoke and rack
  T-bars use the existing smooth 32-segment cylinder, turned along their length.
  Reviewed the authored carriage/mast OBJs and their generators: the carriage already
  has bevelled plates, 24-segment wheels and a 40-segment socket; the masts/clamp use
  48-segment round sections with bevelled rims. Their recorded original procedural
  provenance remains in `assets/lab/models/README.md`; no imported asset or model file
  was added or changed in this slice.
- Existing debug access (`skin`, `stretch`, `latch`, `--geometry`, `--formed`,
  `--skin`, `--latch-t`) reaches the feature. Console help, the console reference and
  AGENTS.md navigation now describe flexible skin; `stretch` includes its blend.

Test-first and review:
- The inherited `out/m25.4/red.log` records missing skin API compile failures before
  implementation. Its initial nine tests passed on resumption (107,682 assertions).
- Extended the chord-error check to the actual triangles' diagonals. This reproduced
  a Klein error of **0.02351361 cells**, exceeding the unchanged 0.02-cell bound:
  grid-edge-only sampling had missed the diagonal's combined curvature. Increasing
  the subdivision count from eight to ten fixes it. Reversed Möbius/Klein seam checks
  also pass. The console-help assertion failed before its wording was updated.
  `review-red.log` and `review-green.log` retain both runs; the latter passes **11
  cases / 219,516 assertions**. No existing golden or tolerance was loosened.

Decisions and deviations:
- The Klein intersection band still uses the existing hologram material: a physical
  sheet cannot occupy its own crossing. All other skin tiles are physical; no old
  membrane or ribs remain behind a formed shape.
- Hardware OBJs already meeting the smoothing target were retained, not regenerated;
  this slice changes only procedural hardware selections/orientation. No new module
  or header-only-to-`.cpp` deviation was needed. Whole-file formatting also rewrapped
  one pre-existing comment in `test_debug_console.cpp`.
- The backing disappears through the existing screen-door mechanism, rather than a
  new deformation of its authored mesh. The sheet follows the original continuous
  shape morph. Lab-off and `--skin 0` use their existing geometry paths.

Review finding (2026-11-21), before the correction: `drawLabGame` applied the new
backing fade whenever `BoardOptions::slabSkin && surface`, which also includes the
M24.6 stacked/projected shapes. The new `gui-slab-skin` comparison reproduces this:
`torus3d --geometry --formed 1 --skin 1` loses its parked slab backs, while `--skin 0`
keeps them (`out/m25.4/stacked-red.log`, `stacked-red.png`, viewed). The unit test
checked that stacked surfaces had no flexible tiles, but did not exercise the GUI's
hardware fade. Restrict that fade with the existing `!stackedShapeView(shell)` guard;
leave the projected shape and its parked hardware on the M24.6 path. Acceptance:
the two stacked captures must be byte-identical, and the existing 2-D morph/latch
checks must still pass.
Fixed as stated: `drawLabGame` now fades the backing only for `!stackedShapeView(shell)`;
`gui-slab-skin` compares the two `torus3d` captures byte for byte and passes
(`stacked-green.log`, `stacked-fix.png`, viewed).

Performance (M24.8 bench method, `--bench-frame 30`, torus formed in the lab,
1280x720, lavapipe, three runs each): before **153.97–156.29 ms** per frame (repeated:
154.12–155.86 ms), after **132.93–136.13 ms**: the flexible skin costs about 13% less
than the M24.5b membrane, tiles and ribs it replaces (`perf-before.log`,
`perf-after.log`). Lab-off frames: ten sampled captures byte-identical before and after
(`laboff.txt`).

Sheets in `out/m25.4/`, all viewed: `before-sheet.png` (M24.5b) and `final-sheet.png`
(this slice, the final binary): cylinder, torus, Möbius and Klein at formed 0, 0.5, 1;
`final-crops.png` and `crops-tk.png` / `crops-cm.png`, 2x crops of silhouettes and
tile seams (no facet or kink visible; seams are material lines, normals continuous);
`hw-sheet.png` / `hw-crops.png`, the structure before and after (round yoke and stem,
bevelled rails); `transition.png`, the tiles lifting off the slab (formed 0 → 0.15);
`stacked-red.png` / `stacked-fix.png`. The early `crops-cm.png` Möbius frame still shows
the stem's foot poking through the sheet before the normal-aligned foot; the refreshed
`final-crops.png` and a 2560x1440 check (`check/`) show it gone.

Gates (through `g.sh`, after the last source change): `tools/precommit.sh` printed
**`precommit: OK`**; `[render],[app],[view]` **749 cases, 18,619,276 assertions** passed
(`render-app-view-final.log`; the 10 new `[render][skin][flex]` cases are in it);
`ctest -L gui` **58/58 passed**, serially, in 842.32 s (`gui-final.log`). No staging or
commit was performed in this sandbox.

### M25.5 — continuation review (2026-11-21)

Reviewed the interrupted diff and its saved test-first logs (`out/m25.5/red.log`,
`red-render.log`); all 15 inherited VR cases pass (15,017 assertions). Reviewed all
nine inherited sheets. Before completing the slice, extend its existing implementation:

- `carryVr` currently skips surface identity instances but never moves the mesh vertices.
  Carry each vertex by its encoded cell, preserving normals and leaving the identity
  draws fixed. Extend `BoardRenderer::buildInstances`' existing light range to every
  VR surface, including forced VR on a 2-D glued board. Existing tests cover lattice
  cells only; a torus regression reproduces the missing light range.
- VR layout interpolation currently has no input guard. Reuse the physical relayout's
  skip-and-consume policy before ImGui receives input, so picking cannot act at a cell's
  destination while its picture is moving. Retarget from interpolated placements and
  carry bridge endpoints with the same offsets.
- Replace the per-frame `FlatGate` session mutations with an explicit-time mode switch:
  keep the old picture until closed, apply the new mode once, then reopen. Frame the
  implant's flat picture front-on across the available view, and keep the same camera
  for rendering and picking.
- The scene's eyelid pass precedes the neural composite, so T6's check overlay survives
  darkness. Fade the implant with the handoff too; add a full-black capture assertion.
  Do not change lab-off shader output or the launch handshake's settled behaviour.

Continuation measurement (2026-11-21): the first M24.8-method sample is standard lab
88.808 ms, hyper4 VR 55.578 ms, T6 VR **2634.053 ms/frame**. A separate CPU probe
measures slab counting 1.546 ms, bridge topology 11.529 ms, board instances 4.881 ms,
and bridge instances 2.195 ms. The 256 bridges produce 8,192 smooth cylinder instances
(two 32-sided tubes per each of 16 segments), over a million triangles for thin light.
Extend the existing `appendBridges` sampling and `cylinderBetween` placement, but draw
VR streaks with one emissive box per segment using the existing box batch. Physical
bridges retain their smooth tubes. Reuse the session's cached bridge topology when
settled; only a moving layout rebuilds endpoints. Test the light-only batch and zero
power, then repeat the same benchmark and review the actual streaks. This changes no
hardware mesh and does not omit bridges.

### M25.5 — light-tile performance investigation (2026-11-24)

After the bridge change, the same M24.8-method sample reports standard lab
85.224 ms, hyper4 VR 52.646 ms, and T6 VR 232.529 ms/frame. T6 therefore still
misses its 127.836 ms target. A diagnostic debugger run suppressing bridges reaches
197.946 ms; this isolates remaining cost, and is not permission to omit bridges.

Reading the actual draw path finds 8,198 board instances, including 4,096 cells
and their seam rails, sharing `Archetype::Cell`. `buildArchetype` builds that shape
with `addBox`: six separate faces, 24 vertices and 36 indices. The board pipeline
uses `VK_CULL_MODE_NONE`. VR changes materials but still submits all six faces of
every thin slab. Existing tests check light materials, instance coverage and carry,
not the geometry submitted by the draw; they cannot detect this wasted work.

Extend the alternate-mesh-range pattern already used by `MeshLibrary::cylinderRanges`
and the board draw loop, with a VR light-cell range. Reuse the Cell mesh's first
face (the +Z face emitted first by `addBox`), six indices referencing its existing
four vertices. Keep its original first index and vertex offset. The range must
cover x/y = +/-0.46 and z = +0.055, preserving the exact visible cell-top and rail-top
positions under their existing instance transforms. Keep the ordinary range at
36 indices. Select this alternate range only for the opaque Cell batch when
`BoardOptions::vrLight` is true and `set.surfaceIndices` is empty. Do not change
instance ordering/count, colours, effects, picking, piece meshes, flourishes,
physical hardware, bridge geometry or sampled surface meshes. The existing
no-cull pipeline makes this sheet visible from below too; a floating light tile
has no solid sidewall. Use a named, testable range selector in the actual draw
path, rather than a test-only reconstruction of its condition.

Test first: verify the selected VR range's two triangles, four unique vertices,
normal and bounds; verify ordinary Cell and every non-Cell range are unchanged,
and sampled surfaces keep their existing mesh path. Preserve all T6 cell and
seam instances and their gameplay cues. Then run the affected render tests and
repeat the benchmark without diagnostic suppression. Review actual T6 and hyper4
captures, including an underside/oblique orbit and seam hover, for light tiles and
readable portals; repeat lab-off byte comparisons. This is a measured-cost hypothesis,
not a claim that reducing triangles guarantees the target. If the result remains
above budget, record the number and profile the remaining full render before
choosing another optimization; do not remove cells, seams or bridges to meet it.

### M25.5 — calm handoff clock review (2026-11-24)

Static continuation review found a clock mismatch requiring a red integration test
before correction. `Ui::currentMoveSeconds` uses `app::moveSeconds` at calm intensity,
so Deploy and Stow end after 0.3 s. `Ui::rigMove` supplies that age to `vrFrame`, but
`view::vrEnter` and `vrLeave` require 0.6 s. At 0.3 s the view function specifies
full darkness; the UI instead reports a finished move and `vrFrame` selects settled
VR. The pure view test samples its own 0.6-second clock and never exercises this
caller, which is why it misses the truncated reopening. Calm leave has another
integration gap: `app::moveFx` returns `holdsBoard == false`, so the menu fails
`labGameAny` and bypasses the VR leave picture entirely.

Extend the existing `Ui::setRigMoves` configuration with an explicit optional calm
handoff timing override (separate deploy/stow seconds), absent for physical moves.
The VR branch of `configureRigMoves` supplies `kVrEnterCalmSeconds` and
`kVrLeaveCalmSeconds`; physical configuration clears the override. Snapshot the
applicable override when the screen move starts, alongside `rigTotal_`, so later
configuration cannot change a running move. `currentMoveSeconds` uses that value
only for calm Deploy/Stow. Keep the existing physical 0.3-second fade and full
intensity timing unchanged. For an explicitly overridden calm Stow, `Ui::moveFx`
also holds the outgoing board until the move finishes, allowing `vrFrame` to render
both halves of `vrLeave`. Reuse the existing alpha calculation with the supplied
total. No change to the pure VR envelope or global calm duration is needed.

Test first using the existing headless Ui fixture pattern in
`tests/render/test_rig_stations.cpp`: configure the VR override, preview entering
and leaving, and pin 0.3, 0.45 and 0.6 s. Require a 0.6-second RigMove, unfinished
at 0.45 s with age 0.45, and finished at 0.6. Feeding that age to the existing
view functions must give darkness 1 at midpoint and 0.5 at 0.45. Stow must hold
the board before completion and release it afterwards. Cover ordinary physical
calm moves still ending at 0.3, full intensity unchanged, and skip completing the
move. Record the failing result before production edits. Then capture calm enter
and leave at those times, review the dark midpoint and visible reopening, and
retain a full-black midpoint assertion. This review ran no tests or captures;
artifact reproduction and verification remain the continuation's responsibility.

### M25.5 — calm black-frame reproduction (2026-11-24)

The new Ui integration test reproduces the truncated calm clock (18 failed assertions).
After applying the specified timing override, all 23 VR cases pass, but the GUI
midpoint assertion finds RGB values of 0 or 1 instead of pure black. The final
post-composite dither can add one code value after `applyVrLids` darkens the scene
with exposure -24. Full-intensity closure already uses the wake's opaque eyelids
and passes the same assertion; calm keeps its lids open, so exposure alone is
insufficient. At exactly full darkness, reuse the fully closed wake mask in
`applyVrLids`; on every other calm frame retain open lids and the existing fade.
This changes no post shader or lab-off output. The unchanged full-black GUI
assertion is the regression; review calm enter/leave at 0.3, 0.45 and 0.6 s.

### M25.5 — acceptance continuation (2026-11-25)

Continued the interrupted working tree after reviewing `git status`, the complete
tracked diff, new modules/tests, and the saved failing runs. Retained the implementation
and the test-first corrections described above; reviewed the completed acceptance
captures and restarted the interrupted gates. No production rewrite was needed in this continuation.

What was built:

- `view::needsVr` counts deployed slabs for the current layout against the authored
  rack's eight numbered bays (`render::rackBayCount`). The live session placements
  drive temporal games, so the ninth visible 5D board triggers the handoff. Once a
  game enters VR it stays there until leaving or explicitly forcing `vr off`.
- `vrEnter`, `vrLeave` and `vrBlink` use explicit seconds. One standard stand-in slab
  follows M25.3's weighted deploy, powers up an animated organic QR-like code, receives
  the existing BCI handshake, and hands the picture over behind M24.7's eyelids.
  Leaving restores the coded slab, fades its code, stows it with weight and returns
  to the menu. Calm handoffs retain both halves of their 0.6-second fade; their UI
  clock is snapshotted, and calm stow holds the outgoing picture until completion.
- The lucid horizon is a soft violet floor/grid and sky in the existing backdrop
  pass. VR cells and pieces use hologram light, with no rig hardware built or drawn
  in the settled virtual scene. The HUD uses M25.1's neural composite and heartbeat.
  `VrSwitch` commits 2D/3D changes once behind closed eyes. The 2-D implant picture
  uses a front-on orthographic camera shared by rendering and picking.
- AXES/relayout moves carry instances, sampled surface vertices and bridge endpoints
  from their current positions. Input uses the physical relayout's skip-and-consume
  policy while pictures move. SHAPE retains the existing surface sampler and picking.
- VR bridges retain every connection as emissive box segments instead of smooth
  tubes. VR lattice cells retain the Cell mesh's exact original top face (two
  triangles), selected by the tested `opaqueBoardRange` used by the real draw loop.
  Ordinary cells, sampled surfaces, pieces and flourishes keep their meshes. T6
  retains all 4,096 cells and 4,096 seam instances plus its six piece instances.
- The final opaque eyelid mask and implant fade eliminate HUD/dither leakage at full
  darkness, including T6's initial check overlay. Console `vr on|off|auto|status`,
  CLI `--vr 0|1` / `--vr-t`, existing `--stow-t`, debug documentation and AGENTS.md
  expose the paths. ADR-0027 records the explicit virtual-space exception to ADR-0026.

Default-view counts (all shipped variants are covered by the threshold tests):

| Variants | Slabs | Default |
|---|---:|---|
| standard, cylinder, torus, mobius, klein, mirrorbox, atomic, atomic_torus, mustcapture, charged | 1 | physical |
| cube5 | 5 | physical |
| hyper4 | 16 | VR |
| hyper4_8x8 | 8 | physical |
| t6 | 256 | VR |
| torus3d, torus3d_8x8, torus3d_twist | 4 | physical |
| 5d | 1 initially; live visible boards thereafter | physical through 8, VR from 9 |

Test-first evidence: `red.log` / `red-render.log` predate the new modules;
`continue-red.log`, `controller-red.log`, `picture-red.log`, `gui-dark-red.log`,
`streak-red.log`, `palette-red.log`, `skip-red.log` and `light-mesh-red.log` record
subsequent regressions before their corrections. `calm-clock-red.log` reproduces
18 failed integration assertions; `calm-gui-red.log` reproduces the remaining
one-code-value pixels before the opaque-mask fix. The 23 VR cases are included in
the final combined gate. No golden or assertion tolerance was relaxed.

Visual acceptance, all viewed under `out/m25.5/`: `accepted-enter-hyper4.png`,
`accepted-leave-hyper4.png`, `accepted-enter-t6.png` and `accepted-leave-t6.png`
cover entry through 9 s and leave through 7 s in 0.25-second samples at 1280x720.
`accepted-calm.png` covers both directions for both games at 0, 0.15, 0.3, 0.45 and
0.6 s; each midpoint is checked as exactly black. `accepted-scenes.png` covers T6,
Hyper4 and forced-VR torus3d, standard/Hyper4 2-D pictures, code, T6 seam hover,
T6/Hyper4 underside views, torus/Hyper4 shapes and a Hyper4 relayout.
`accepted-code-close.png` is the 2x code crop. `accepted-lab-off.png` records ten
byte-identical comparisons against the saved pre-slice captures (`laboff-final.log`).
Also reviewed `return-repro.png`, `return-exact.png`, `return-sheet-detail.png`
and `calm-black-red.png`. The capture audit rechecked all 132 handoff frames, all
four exactly black calm midpoints, and all ten saved lab-off pairs. The current
build reports no work to do, confirming these captures follow the last source build.

Decisions and deviations:

- The performance investigation above justified light sheets and box streaks; no
  cells, seams or bridges were removed to meet the budget. Physical bridges retain
  their smooth tubes. Sampled shape meshes retain the existing M24 light treatment.
- Calm uses the required short fade instead of the full deploy/code/handshake; at
  exactly full darkness it uses the opaque wake mask to suppress final-pass dither.
- The existing `gui-lab-multislab` cases are explicitly pinned with `--vr 0` because
  they test physical hardware. `gui-neural-vr` covers the new automatic path.
- No header-only module required a new `.cpp`. Whole-file formatting rewrapped a few
  existing wake helpers/comments. No authored model file was changed or included.
- Per the requested sandbox handoff, no staging or commit was performed. The message
  and repo-relative file manifest are written only after all gates pass.

Performance (the retained light-mesh run, M24.8 method, lavapipe, dev build,
1280x720, manifold, lab/post/UI-auto on, intensity 1; three warmed 30-frame runs
in alternating order, `light-mesh-perf.json`):

| Case | Samples, ms/frame | Median, ms/frame |
|---|---|---:|
| Standard physical lab | 83.472, 83.041, 84.989 | 83.472 |
| Hyper4 VR | 48.872, 49.825, 50.125 | 49.825 |
| T6 VR | 109.121, 110.159, 109.194 | 109.194 |

T6 is **1.308x** standard, below the 1.5x limit (125.208 ms); Hyper4 is below
M24.8's 102.5 ms target. The measured optimization retains every bridge and tile.

Fresh gates (through the required `g.sh`, seven build jobs, tests/captures serial):
- Explicit `clang-format --dry-run --Werror` passed for every changed/new C++ file.
- `tools/precommit.sh` printed **`precommit: OK`**; architecture, 632 unit cases
  (389,054 assertions), and 14 property cases (1,230,006 assertions) passed.
- `[render],[app],[view]` passed **772 cases / 18,955,351 assertions**.
- The serial `ctest -L gui` gate is still in progress.

The first nonterminal precommit run ended with SIGTERM during property tests,
without an assertion failure; the terminal-session retry completed the same
unmodified gate. The successful fresh logs are `precommit-retry-2026-11-25.log`
and `render-app-view-2026-11-25.log`. No message or file manifest has been issued
while the final GUI gate is pending.

### M25.5 — final gate and handoff complete (2026-11-25)

Resumed the interrupted acceptance above. Read the instructions and prerequisite
Status entries, reviewed the tracked diff and all new source/test files, and retained
the implementation. No further production change was needed. The current wrapped
build reports `ninja: no work to do`; both successful precommit and combined-test logs
postdate the latest source/test edit.

The completed slice provides rack-capacity selection, weighted stand-in deployment,
animated neural code and handshake, eyelid entry/return, the lucid horizon, hologram
boards/pieces/bridges, the front-on 2-D picture, guarded layout glides, and console/CLI
access described in the acceptance entry above. Reviewed all eight final acceptance
sheets again: `accepted-enter-hyper4.png`, `accepted-leave-hyper4.png`,
`accepted-enter-t6.png`, `accepted-leave-t6.png`, `accepted-calm.png`,
`accepted-scenes.png`, `accepted-code-close.png`, and `accepted-lab-off.png`, all under
`out/m25.5/`. No new contact sheet was made in this continuation.

All required gates now pass:

- `tools/precommit.sh`: **`precommit: OK`**, retained successful run
  `precommit-retry-2026-11-25.log`; 632 unit cases / 389,054 assertions and
  14 property cases / 1,230,006 assertions, plus the architecture gate.
- `[render],[app],[view]`: **772 cases / 18,955,351 assertions**, retained successful
  run `render-app-view-2026-11-25.log`, including all 23 VR cases.
- Fresh serial `ctest -L gui --output-on-failure`: **59/59 passed** in **954.27 s**,
  including `gui-neural-vr`; `gui-complete-2026-11-25.log`.

The fresh build and GUI gate used the required `g.sh`, with the seven-job build limit
and no concurrent captures or parallel ctest. The retained performance and artifact
results above remain applicable: T6 **109.194 ms/frame** (1.308x standard), Hyper4
**49.825 ms/frame**, ten byte-identical lab-off pairs, and four exactly black calm
midpoints.

Deviations are unchanged: VR uses top-face light sheets and box-segment bridge streaks
to meet the measured budget without removing cells, seams or connections; calm full
darkness uses the opaque wake mask; existing physical multi-slab tests are pinned with
`--vr 0`. No header-only module needed a new `.cpp`, and no golden or tolerance changed.
The requested `m25.5.msg` and 30-path `m25.5.files` handoff includes this spec and
excludes `out/` and `assets/lab/models/`. No staging or commit was performed.
