<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# M25 — The neural handoff: one interface, weight, flexible skin, and VR where the room runs out

Status: **M25.1–M25.4 built 2026-11-21; M25.5 built and verified 2026-11-25; M25.6 (readable VR) built 2026-11-28** from play-testing after M24. Implementers: Codex
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

## M25.3b — Measured gantry speed and continuous phases (2026-11-26)

This follow-up supersedes M25.3's old 2.5–3.5 s slab / 5 s overall targets.
The load must stay at or below **0.8 m/s**, vertical speed **0.5 m/s**, and
acceleration magnitude **1.5 m/s²**, measured at 60 Hz through deploy and stow
for every shipped variant using the physical rig. Expect roughly 5–7 s for one
slab; the entire deploy, including its trailing second, must finish within 9 s.
Departures still overlap. Keep flat-in-transit, skip-and-consume, calm endpoints,
the camera clearance and the existing debug/capture entry points.

**Investigation / red evidence.** `out/m25.3b/red.log`, from the new
`[gantryspeed]` case in `tests/render/test_rig_weight.cpp`, measures standard at
3.801 m/s, 3.143 m/s vertically and 29.659 m/s²; hyper4_8x8 reaches about
4.68 m/s and 36.59 m/s². `view::slabDeploy` serialises four rest-to-rest moves
into fixed 0.5, 0.55, 1.0 and 0.5 s windows, regardless of their metre distances.
Zero velocity at joins prevents jumps but does not make the intervening movement
slow. Existing weight tests checked continuity with a 700 cells/s² threshold,
timings and swing, not physical speed limits. Even the 7 mm drop in 0.12 s alone
exceeds the new acceleration bound. `slabRelayout` has the same compressed
lift/lower windows and a damped sine settle with nonzero initial velocity.

**Implementation, extending the existing pure explicit-time path.** Keep a
single fixed clock and existing `SlabPathSpec`, duration consumers and reverse
stow mechanism. In `src/view/rig_deploy.*`, replace the mutually exclusive centre
branches with overlapping C2 displacement ramps. Use
`S(x)=6x^5-15x^4+10x³` for vertical and orientation movement. The old horizontal
`H(x)=x^4(15-24x+10x²)` has peak velocity 2.0736 times its mean: tuning its
overlapping windows to a 7.05 s movement end still measured about 0.927 m/s,
so merely moving its phase boundaries does not meet the cap. Replace H with
the integral of this broader late-peaking velocity profile, for progress x:
`v=.9*S(x/.2)` on [0,.2], `v=.9+.1*S((x-.2)/.5)` on [.2,.7], and
`v=1-S((x-.7)/.3)` on [.7,1]. Divide its cumulative integral by **0.715**;
use `Q(x)=x^6-3x^5+2.5x^4` as the antiderivative of S, carrying each previous
segment's integral into the next. Clamp outside [0,1]. This H starts slowly,
continues gaining speed until x=.7, then brakes; its peak/mean speed is 1.39860.
Velocity, acceleration and jerk join continuously. Its normalized acceleration
maximum is 11.8007, so a 0.92 m slide alone needs at least 2.69 s to stay below
1.5 m/s²; allow additional margin for simultaneous axes and the pivot offset.
Starting from the
bay, add `(pick-bay)*H(slide)`, the vertical rise
`(liftZ-pick.z+drop)*S(lift)`, horizontal `(hang-pick)*H(travel)`, and vertical
`(hang.z-liftZ)*S(lower)`, then subtract the latch drop and settle envelopes.
Horizontal means x/y only. Every ramp clamps outside its own time window;
their sum therefore has exact original endpoints without a stop at each join.

Start tuning with slide 0.60–3.50 s, lift 1.00–4.80 s, travel 2.10–7.10 s,
lower 3.90–7.10 s, and lock 7.30 s. These are candidate coefficients, not
claimed passing measurements: use the recorded shipped path distances to tune
the overlapping windows before production changes, then prove the real poses
meet the limits. Limit departure span to 0.50 s (keep the 0.12 s per-slot upper
bound), giving `0.2 + 0.5 + 7.3 + 1 = 9 s` even at the largest slot count.
Prefer shortening the common lock time if the physical measurements allow it.
Rise and lowering overlap deliberately: the slab need not touch the old rail-height
waypoint, but must retain the magazine clearance and adequate mast/yoke clearance.
Do not rotate a slab through the rack: delay orientation change until the slab has
cleared its bay, independently of when its centre starts rising. Check this on
standard and the stacked/rack carriers in the real captures.

Keep the carriage take-up and visible break-free; spread the 7 mm latch drop over
at least 0.20 s (for example 0.40–0.60), and use a C2 one-dip 1–3 mm settle
envelope such as sin⁴ over its support. Recompute carriage/mast position from
the combined centre. Drive the existing causal critically damped swing kernel
from the new horizontal acceleration, including stow's own time direction;
retune the gain so a visible 1–2° lean survives the slower movement. Include the
yoke-pivot centre displacement in cap measurements, rather than checking only
the nominal centre. Avoid a final orientation or position snap at lock.

**Relayout.** Extend `slabRelayout` and `relayoutTravelSeconds` in the same file:
distance-dependent horizontal duration, overlapping smooth lift/travel/lower,
and a zero-velocity, zero-acceleration settle in place of the damped sine.
Use lab-world distances divided by 20; deploy/stow board-space distances divide
by `20 * layout.xform.labScale`. `render::relayoutOf` already takes the maximum
of per-slab durations; retain that pattern and the still-slab fast path. Its
arrivals and departures automatically use the retimed rack path. Check any lab
transform easing against actual lab-space poses, including arrivals/leavers.

The measured implementation passes default physical deploy/stow at maxima
0.76246 m/s, 0.45467 m/s vertically and 1.23148 m/s², but a
`hyper4_8x8` relayout to `axes file,aeon vertical` produces longer arriving rack
routes and reaches 0.9839 m/s and 1.7658 m/s². Matched-slab relayout already
passes. This is a distinct route-length case: retain the fixed default deploy
clock and add a cached `Relayout::timeScale` (default 1), computed once by
`render::relayoutOf`. Sample actual arriving/departing `slabDeploy` paths at
240 Hz in lab metres, including their causal swing directions and samples
before departure and after lock. Compute maxima v, vz and a, then use
`max(1, v/.75, vz/.45, sqrt(a/1.3))`; the margins allow sampling and floating
point error. Use double arithmetic for finite differences. Multiply the
existing maximum `out.seconds` by this scale. The 9 s deploy budget does not
apply to relayouts; never clamp this safety scale to fit that budget.

In `relayoutFrame`, compare wall elapsed `max(0,sinceStart-lead)` against
scaled `move.seconds` for completion, and use elapsed divided by `timeScale`
for every motion evaluator and the lab-transform easing. Keep the unform lead
in wall seconds. `relayoutBootAge` scales the arrival lock timestamp only;
the boot's age since lock and duration remain wall seconds. Preserve calm,
unchanged-slab and lead-hold behaviour. Endpoint path calibration alone is not
a proof for paths rebuilt while the lab scale changes: acceptance must sample
the actual `relayoutFrame` poses converted through each frame's xform, with
both count-changing and scale-changing cases. If those exceed the caps,
calibrate the live lab-space arrival paths as well, rather than weakening the
test or increasing an arbitrary universal duration.

**Early-hint artifact correction (2026-11-26).** The first rendered deploy frame
has no readable skip hint although its ImGui vertices and settled implant record
exist. `implant.frag` chooses the strongest transition record at each pixel; the
booting `##controls` window overlaps the hint and its form-zero record wins. The
structural test never checked that competition. `hint-overlap-red.log` reproduces
seven failures before correction. Draw the hint after the current or ghost HUD has
been measured, above the current frame's controls window by 28 scaled pixels (the
composite's 24-pixel expansion plus a gap). Read a dock only when its
`LastFrameActive` is the current frame; otherwise use the existing bottom anchor.
Keep the held-stow early return covered, the record settled, and the normal implant
composite. Test competing expanded records, including the departing HUD. Do not
change the shader, game framing, or use a tool-overlay bypass.

**Full-hint artifact correction (2026-11-26).** The resumed `early-detail.png`
shows the centred hint overlapping the departing menu's Settings row at zero,
and losing its rightmost words to that menu's dissolve at 0.25 s. The first
regression checked only the hint's centre, missing partial overlap. Extend it
to the entire hint rectangle against every other expanded implant record,
including settled records that can contain competing text. Keep the dock's
vertical clearance and existing draw order, but anchor the hint's left edge
at the viewport's existing 16-scaled-pixel margin (pivot 0,1). This keeps it
clear of the right-side departing menu without changing the composite or
the game framing. Rebuild and review the first frames and full sheets again.

**Acceptance.** First extend the red tests: all physical shipped variants in
flat and shape framing, both travel directions, negative/predeparture and
postlock samples, all slabs, and relayouts of standard, cube5 and hyper4 plus a
case with changed slab count. Use successive finite differences at 60 Hz on the
actual `rigPoses`/`relayoutFrame` centres in metres; assert all three limits,
and last lock plus trailing second ≤9 s. Preserve break-free/drop, late speed
peak, visible causal lean, exact endpoints, flat gates, no intermediate full
stops, and ≥0.30 m camera clearance. Update timing-pinned tests and capture
times from named duration constants. Run the existing view/app/render and GUI
checks through `g.sh`. Review standard/cube5 deploy contact sheets at 0.25 s,
and height/speed/acceleration/tilt plots in
`out/m25.3b/`; verify lab-off frames remain identical. Record final coefficients,
measured maxima and any departures from the candidate table in Status.

If the fixed overlapping curve cannot satisfy both caps and the 9 s envelope
with the existing route clearances, record the failing path and change the
design explicitly before implementing distance-dependent clocks or changing
the route. Do not weaken the physical limits or silently truncate a movement.

## M25.6 - readable VR (2026-11-28)

A follow-up to M25.5 from its own acceptance sheet (`out/m25.5/accepted-scenes.png`),
which shows three VR scenes that do not read. Lab-off frames stay byte-identical,
physical (non-VR) rendering is unchanged, and T6 in VR stays within M25.5's budget
(<= 1.5x the standard lab game, M24.8 method).

1. **T6: the bridges bury the board.** `appendVrBridges` gives each streak
   `min(1, seam * 1.4)` of Glow (material 5), opaque, as wide as the physical glow core
   (`kBridgeCoreMetres`). 256 bridges x 16 segments at full emission cover the cells,
   seams and pieces and bloom over them. The M25.5 test checked only the material and
   the count, never the emission against the cells. In VR the streaks become faint
   guides; every bridge and segment is still drawn, and cells and seams are untouched:
   - half the physical core across (`kVrBridgeWidth = 0.5`);
   - the seam colour normalised to its peak, then scaled to `kVrBridgeShare = 0.4` of the
     dimmest light tile's peak, `vrLightColor(theme.boardDark, theme)`;
   - given the eye (`drawVrSpace` passes `session.camera().eye()`), a linear fade from
     1 at the nearest streak to `kVrBridgeFar = 0.3` at the farthest. Without an eye no
     fade; the cap still holds.
   `render::vrLightColor(fill, theme)` is the existing lift of the lattice VR tile
   (`fill * 2 + ember * 0.10`, alpha `kVrLightOpacity`), moved out of
   `buildInstances` unchanged so the cap and item 2 share it.
2. **Hyper4 SHAPE: a faint, stretched, see-through wedge.** Two causes, neither the
   M25.4 flexible skin (VR draws through `labGameAny && !labGame`, so `slabSkin` is off
   and the plain bent patches are used):
   - the surface light pass sets only the fill's alpha. VR lattice tiles are lifted
     (item 1's `vrLightColor`); the shape's patches keep the raw LED fill, which is dark
     until it emits, so the light reads at about half the flat layout's luminance. In
     `emitPatch`/`emitFlex`, when `lightPass && options_.vrLight`, use
     `vrLightColor(fill)`. Rails, rims, ghost alpha and every non-VR path unchanged;
   - the "stretch" in a settled frame is the tesseract immersion's own default pose:
     lab off and physical draw the same wedge (`out/m25.6/diag.png`). In VR it is made
     worse by the VR carry: `drawVrSpace` glides every instance and surface vertex by
     the *lattice* layout's offsets after AXES, but a shape is sampled by
     `PlaySurface::build(v, pose)`, independent of the layout, so mid-glide its patches
     scatter across the view (`out/m25.6/diag-glide.png`). M25.5's carry test used one
     uniform offset, under which any mesh moves rigidly. Add
     `render::vrCarriesLayout(options, v)`, false when a surface is drawn
     (`surface && !flat && hasPlaySurface(v)`); `drawVrSpace` then skips the carry and
     the lattice bridges, as the physical rig draws no bridges while parked for a
     shape view.
3. **Forced-VR torus: pieces off the ring?** Pieces carry by their own cell
   (`Instance::fxCell`), the mesh by the cell in its vertex tag, so a per-cell carry
   moves both identically; a settled frame has no carry, and picking samples the same
   `PlaySurface`. The floating look is item 2's dark light tiles: the tube vanishes and
   only the pieces read. Prove it with a non-uniform per-cell carry test and captures
   of the fixed ring from the default, a low side and a rotated side angle.

Tests first, in `tests/render/test_neural_vr.cpp` (`[render][vr][quiet]`): T6 streaks
keep their count, have a cross-section <= a quarter of the physical core's, peak <= half
the dimmest VR tile, and the farthest streak's gain is < half the nearest's; torus and
hyper4 shape faces have the flat layout's median luminance (0.9-1.1x) at
>= `kVrLightOpacity`; `vrCarriesLayout` per variant and mode; the torus piece/mesh
carry agreement. Sheets in `out/m25.6/`: before/after for each scene, the T6 orbit
(underside, oblique, hover) and lab-off. Re-measure T6 against standard.
Commit: `render: readable neural VR - quiet bridges, shape and torus in VR (M25.6)`.

## Status

### M25.6 - built and verified (2026-11-28)

Reproduced all three scenes from the M25.5 sheet with the HEAD build
(`out/m25.6/before.png`, `diag.png`, `diag-glide.png`), wrote the section above, then
the four `[quiet]` cases. Red (`red.log`, `red-detail.log`, `red-detail2.log`): streak
peak 1.0 against a 0.238 cap and a cross-section 4x too large; shape faces at median
luma 0.160 against the flat tiles' 0.356; `vrCarriesLayout` true on a drawn shape. The
piece-carry case passed on arrival: it is the item 3 proof, not a regression.

Built: quiet streaks (`kVrBridgeWidth`/`kVrBridgeShare`/`kVrBridgeFar`, eye-distance
fade), `vrLightColor` shared by lattice tiles, shape patches and the bridge cap, and
`vrCarriesLayout` gating the VR glide and lattice bridges in `drawVrSpace`. All 27 VR
cases pass (`green.log`). AGENTS.md's neural-VR row names the new functions.

Visual acceptance, all viewed: `t6-before-after.png` (default, seam hover x1 and a 3x /
2x-capture crop of the piece corner), `t6-orbit.png` (underside, oblique, an underside
3x crop and underside with hover), `hyper4-shape-before-after.png` (default, orbit,
an AXES glide at 0.35 s, flat VR unchanged), `torus-forced-before-after.png` (default,
low side and rotated side, top, physical), `lab-off.png`, plus `after.png`. T6's cells,
4x4 boards, seams, hover and CHECK frame read; the bridges remain as faint arcs (visible
in the underside crop). The pieces are no longer covered, but at the default framing of
256 boards a piece is only a few pixels (true before M25.6 as well); no framing change
was made. Hyper4 SHAPE is a lit tile surface with its pieces and scaffold and stays whole
through an AXES glide. The torus ring is visible and every piece stands on it from all
three angles: the pieces were never misplaced.

Constraints: ten lab-off pairs byte-identical against HEAD captures; physical standard,
cube5 and formed torus lab games byte-identical; hyper4 flat VR (no shape, no bridges)
byte-identical, confirming the `vrLightColor` move is exact (`laboff.log`).
Performance (M24.8 method, lavapipe, dev, 1280x720, manifold, lab/post/UI-auto,
intensity 1, one warm-up then three 30-frame runs each, interleaved; `perf.log`):

| Case | Samples, ms/frame | Median |
|---|---|---:|
| Standard physical lab | 88.276, 96.085, 87.907 | 88.276 |
| T6 VR | 113.927, 113.613, 117.105 | 113.927 |
| Hyper4 VR | 53.326, 53.282, 51.549 | 53.282 |

T6 is **1.291x** standard (limit 1.5x); hyper4 stays below M24.8's 102.5 ms.

Gates (through `g.sh`, seven build jobs, tests and captures serial, after the last
source edit): `clang-format --dry-run --Werror` clean on every changed C++ file
(`format.log`); `tools/precommit.sh` printed **`precommit: OK`** (`precommit.log`);
`[render],[app],[view]` **781 cases / 19,110,510 assertions** passed
(`render-app-view.log`); `ctest -L gui` **59/59 passed** in 542.57 s (`gui.log`).
No staging or commit was performed.

Deviations: none in code placement (no header-only module touched). The `zoom` script
is reframed away by captures, so the close piece views are crops of a 2560x1440 capture.
The T6 piece size is recorded above rather than changed.

### M25.3b — built and verified (2026-11-27)

Completed the interrupted acceptance without restarting the slice. Reviewed the full
working diff, the red evidence, the implementation and timing consumers, and all seven
PNG artifacts in `out/m25.3b/`. The final standard/cube5 deploy sheets cover 0–9.5 s
at 0.25-second intervals; `early-detail.png` shows the complete skip hint from the
first frame and the slabs turning only after extraction. `weight-plot.png` shows
continuous travel between the latch and settle, with the speed and acceleration caps
intact. The two earlier first-frame diagnostics are superseded by the final sheets.

Retained the overlapping deploy/stow curves, distance-dependent relayout, cached
arrival/return time scaling, causal damped lean, and early neural skip hint described
below. Default physical variants in flat and shape framing peak at **0.76246 m/s**,
**0.45467 m/s vertically**, and **1.23148 m/s²** across deploy/stow. Relayout peaks
are **0.74993 m/s**, **0.30302 m/s vertically**, and **1.24120 m/s²**; the separate
scale-changing arrival/return case also passes. Flat-in-transit, input skip, and
M24.9 camera-clearance regressions remain green.

Verified the completed gate logs from the interrupted continuation, all newer than
the final code and test changes:

- `precommit-complete.log`: **`precommit: OK`**, including architecture, **632 unit
  cases / 429,367 assertions** and **14 property cases / 1,230,006 assertions**.
- `render-app-view-complete.log`: **777 cases / 19,074,767 assertions**, all passed.
- `gui-complete.log`: **59/59 GUI ctests passed**, serially.
- `resume-focused.log`: **5 cases / 7,372 assertions**, including the physical caps
  and complete hint-rectangle regression. Ten lab-off captures compare byte-identical
  (`laboff.log`).

A final `g.sh` build reports `ninja: no work to do`; explicit
`clang-format --dry-run --Werror` passes for all nine changed C++ files
(`handoff-freshness.log`). No production or test edits followed the completed gates.
Builds use at most seven jobs; tests/captures use `g.sh`, with no parallel ctest or
concurrent GUI captures.

Deviations remain those recorded below: the tuned windows give a **7.30 s local
slab path** (7.50 s to the first lock), slightly beyond the approximate 5–7 s target,
while the full deploy including its trailing second is **at most 9.00 s**. Long
relayouts may exceed 9 s (the changed-count case is 10.312 s). The physical-cap sweep
excludes default VR layouts above eight slabs; existing forced-physical/VR tests pass.
No header-only exception, authored-model change, or additional design deviation.

Handoff: the requested `m25.3b.msg` and 14-path `m25.3b.files` are written to the
specified scratchpad, excluding `out/` and authored models. No staging or commit was
attempted.

### M25.3b — heavy gantry motion, acceptance in progress (2026-11-26)

Continued the interrupted slice after reviewing the working-tree diff, its written
design and red runs (`red.log`, `red-more.log`, `hint-overlap-red.log`). Retained the
implementation, formatted the final hint correction, and rebuilt before verification.

- Deploy and reverse stow use overlapping C2 displacement ramps and the broad,
  late-peaking horizontal velocity profile specified above. Final local seconds:
  take-up ends 0.40; 7 mm latch drop 0.40–0.60; slide 0.60–3.90; lift 0.70–5.20;
  rail travel 2.90–7.00; face-up rotation 2.90–4.60; lower 3.10–7.05; 2 mm settle
  6.95–7.30; lock 7.30. Departures start at 0.20 with at most a 0.50-second span.
  One slab locks at 7.50 s; the entire deploy including its trailing second is at
  most 9.00 s. The phase overlap eliminates intermediate rest-to-rest joins.
- The critically damped swing uses omega 20 rad/s and a demand of 2 degrees per
  1.25 m/s², causal in each direction. Standard peaks at 1.3582 degrees and cube5
  at 1.4387 degrees; the yoke-pivot centre displacement is included in measurements.
- Relayouts overlap lift/travel/lower, use distance-dependent travel and a smooth
  settle. Arriving/departing rack paths cache a measured `Relayout::timeScale`;
  motion and lab easing share it while boot ages remain wall seconds. The
  hyper4_8x8 changed-count route takes 10.312 s (scale 1.312); relayouts are not
  subject to the deploy's 9-second limit.
- The neural skip hint is drawn after the current/ghost HUD is measured, remains
  settled during boot/dissolve, and sits above the controls' expanded composite
  record at the left margin. The full-rectangle regression reproduced three
  failures before the left-anchor correction (`hint-edge-red.log`), then passed
  69 assertions (`hint-edge-green.log`). It also covers the held-stow return.
  Existing key/click skip handling,
  flat-in-transit form gates and camera clearance are retained.
- GUI samples derive their lock/boot/VR phase ages from shared timing constants
  through `tests/rig_timing.cmake`; no golden or pixel tolerance changed.

Fresh focused verification: **5 cases / 7,372 assertions** passed. Sampling every
shipped physical variant in flat and shape framing, deploy and stow, at 60 Hz gives
maxima **0.76246 m/s**, **0.45467 m/s vertically**, **1.23148 m/s²**. Relayout cases
reach at most **0.74993 m/s**, **0.30302 m/s vertically**, **1.24120 m/s²**.
The additional scale-changing arrival/return case reaches **0.53363 m/s**,
**0.44985 m/s vertically**, **0.83195 m/s²**. Tests cover predeparture/postlock,
every carried slab, continuous motion, visible lean, and scaled arrival boot time.

Deviations from the candidate table are the tuned overlapping windows recorded
above. The slab's 7.30-second local path slightly exceeds the requested approximate
5–7 seconds to preserve the hard caps and route clearance; the hard whole-deploy
limit still holds. Physical-limit coverage excludes the >8-slab default VR layouts;
the existing forced-physical and VR regressions remain in the broader gates.
No header-only module needed a new `.cpp`, and no authored model was changed.

Visual acceptance under `out/m25.3b/`, all viewed: `deploy-standard.png` and
`deploy-cube5.png` (0–9.5 s at 0.25-second intervals, 960×540 originals),
`early-detail.png` (full-size early hint/rack details), `weight-plot.png` (height,
speed, face-up tilt and acceleration for deploy/stow), and `lab-off.png`.
The final early frames show the complete skip hint clear of the departing menu.
The slabs stay flat while extracting and turn face-up clear of the rack; cube5's
departures overlap. Ten lab-off captures remain byte-identical to the accepted
pre-slice captures (`laboff.log`). Also inspected the diagnostic `first-frame.png`
and `first-frame-fixed.png`; the latter predates the final left-anchor correction,
which is shown in `early-detail.png`.

Final gates are pending; no commit message or manifest has been issued. All
builds/tests/captures use the required `g.sh`, builds at seven jobs, with serial
GUI captures and no parallel ctest.

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
