<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

> **Binding** for M24.4c and M24.7 (design by Fable 5.2; approved by the user 2026-11-12,
> including the two departures: positive persistence instead of the negative afterimage,
> and phosphene mapping instead of the ring wave). Where a slice spec gives other values,
> this brief wins. "The orchestrator" in it means the spec author.

# BCI vision: design brief (implant layer, M24.4b / M24.4c / M24.7)

Sources: M24-the-rig, ADR-0026, M24.4b (built), M24.4c and M24.7 (planned), `theme.hpp`, and the sheets in `out/m24.4b/`, `out/m24.3/`, `out/m24.4a/`. All px values are at 1080p and scale with `pixelScale`.

## 1. Diagnosis: why it reads as a flat overlay plus noise

Seen in the sheets:

- **The settled panel is a window, not a percept.** `organic-flight.png` t=0.8–1.2 and `m24.3/screens.png`: an opaque navy rectangle with a 1 px frame, corner brackets, filled row bars and a saturated green selection bar. It hides the lamp and bench behind it, and nothing in the room reacts to it. All the "organic" work lasts 0.35 s; the other 99 % of the time it is a box.
- **The transition carries no shape.** `panel-closeups.png` "form-0.3": about 120 identical hard white dots in a uniform field, plus one isolated green block. No row, title or outline of the menu is legible. The dots are uniform in size, brightness and density, so they read as a starfield.
- **The dots are not tied to the panel.** `organic-flight.png` t=0.2 and t=0.6: dots cover the right half of the frame, over the lamp and the wall, when the panel content is already gone or not yet there.
- **Order is wrong.** "form-0.6": text is nearly sharp while dots still lie on top of it. The noise outlives the signal, so it reads as dirt on the image.
- **LED vocabulary is still inside the implant.** "form-0.6" and "dissolve": horizontal raster lines in the fill, crisp frame and brackets while the text blurs. The box survives and its contents wobble, which is a screen with a filter on it.
- **The parts that worked:** the curl fringe on "ENDSPIELZERO" in the dissolve crop is good (coherent, soft, coloured). The calm cross-fade is clean. Heartbeat and afterimage are invisible in the sheets.

Root cause: the effects are per-pixel noise applied to a rectangle. Nothing is driven by the eye (where you look) or by the content (what is being written).

## 2. The visual language, settled

Three facts from real neuroprosthetics set the rules:

- **A cortical implant can only add light.** Phosphenes are luminous; nobody can stimulate "black". So there are no dark fills, no frames, no boxes.
- **Percepts are coarse first, and finest where you look.** Phosphene size grows with eccentricity (cortical magnification). Form is perceived when stimulation is swept in order (dynamic current steering), and as blobs when it is flashed all at once.
- **The image is gaze-locked.** It does not sit in the room, so it never takes the room's light, shadow or motion blur. It does take the state of the visual system: focus, attention, eccentricity.

Rules:

- **Text is the panel.** Glyph core in `implant` (#D9D2FF), secondary in `implantDim`, tertiary in `implantFaint`. No row bars, no frame, no brackets, no raster, on the implant path always (not only with `livingVision`).
- **Two glow scales, both from pre-blurred copies of the UI layer** (see 3.0):
  - tight: `uiSoft` × 0.35, hugging the glyphs;
  - broad: `uiHalo` × 0.12, tinted toward `implantDeep` (#7E6FD6). This is the "body" of the panel: a faint violet cloud shaped by the text rows.
- **Selection is brightness, not a bar.** The selected row: core at full `implant`, tight glow × 2, plus a soft `implantDeep` wash behind it (alpha 0.30, feathered 10 px on all sides, no hard edge). Unselected rows are `implantDim`. Hover: glow × 1.5 over 0.12 s.
- **Game-meaning colours stay but shrink.** Difficulty ticks stay as 3 px marks with their hue. The green selected fill goes; the tick keeps the green.
- **Controls:** slider = a 1 px `implantFaint` line with a soft 8 px `implant` knob (the solid lilac block in the library sheet is the brightest thing on screen; remove it). Checkbox = a 1 px `implantFaint` ring, filled dot when on.
- **Legibility without a box: attentional veil.** Behind the panel the room is dimmed, never painted. Premultiplied alpha with zero colour, peak 0.45 at the panel's interior, falling to 0 over 90 px outside the rect with a plain smoothstep and 40 px rounded corners. No noise on its edge. The room stays visible through it (the lamp must still be seen behind the main menu).
- **Why it is not a HUD:** no geometry of its own (no lines, corners, plates), nothing that implies a display surface, sharpness and brightness depend on where the eye is, and it appears coarse-to-fine from the fixation point.
- **Contrast with hardware:** LEDs are cyan, gridded, stepped. The implant is violet-white, continuous, radial. Warm (`implantWarm`) appears only as a transient fringe.

## 3. Transitions

### 3.0 Prerequisite: two pre-blurred layers

Replace every 8-tap Poisson blur (it gives eight ghost copies, visible in "form-0.6") with two Gaussian copies of the UI layer, made each frame with the existing separable blur pattern:

- `uiSoft`: half resolution, sigma 4 px;
- `uiHalo`: quarter resolution, sigma 14 px.

Every "blur" below is a mix between sharp, `uiSoft` and `uiHalo`.

### 3.1 Verdict on Step 4.2a

Keep: one connected front, no sparks, effects confined to the grown rect. Change:

- **Origin** at the title's corner is arbitrary. Use the gaze point clamped into the rect (the click point when there was a click).
- **The fbm wobble of ±0.35 and the luminous rim** together are the "burning paper" dissolve shader. Cut the rim. Reduce the wobble to ±0.05 at 320 px scale, static in time.
- **The front reveals content at full sharpness behind a blur band.** Replace with coarse-to-fine: the whole panel's shape arrives first as soft light, then sharpens outward from the gaze. This is what makes it "assemble into a menu".
- **The 14 px curl smear on leaving** looks like heat haze. Cut to 5 px.

### 3.2 Appearing: "lock-in" (0.45 s; replaces `kImplantFormSeconds` 0.35)

One scalar per pixel, `s` in 0..1, drives everything.

- `d` = distance from origin / panel diagonal (0..1), plus wobble ±0.05.
- Front: retinotopic, slow at the fovea and accelerating outward. Reached time `t0(d) = 0.26 s × ln(1 + 6d) / ln(7)`.
- `s = smoothstep(0, 1, (age − t0) / 0.19 s)`.

Stages, by `s`:

| `s` | What the pixel shows |
|---|---|
| before the front | nothing, except `uiHalo` at alpha `0.5 × smoothstep(0, 0.12 s, age)` over the whole panel: the menu's rows exist as soft violet bands from the first frames |
| 0 – 0.45 | `uiHalo` → `uiSoft`, alpha 0.5 → 0.8 |
| 0.45 – 1 | `uiSoft` → sharp, alpha → 1; colour fringe decays |

- **Fringe:** R and B displaced ±2.5 px × (1 − s) along the M24.4b curl field, at 1/3-screen scale so whole words shift together. Warm tint (`implantWarm` × 0.4) only where R exceeds B. This is the one M24.4b effect that looked right; keep it, smaller.
- **Veil** rises with the mean of `s` over 0.3 s.
- Input opens at age 0.25 s.

### 3.3 Leaving: "let go" (0.22 s)

- Whole panel at once: sharp → `uiSoft` over 0.10 s, alpha falling 1 → 0 over 0.22 s.
- Far-from-origin first: the same `t0(d)` reversed, scaled to 0.10 s. The clicked item goes last and brightens × 1.3 for its first 0.06 s (the click registers).
- Smear: sample offset 5 px × (1 − alpha) along the curl field.
- Veil falls over 0.22 s.
- **Persistence instead of the negative afterimage:** `uiHalo` in `implantDeep` at 6 %, fading over 0.35 s. A cortically written image leaves no retinal negative, and the built one is invisible in the sheet anyway.

### 3.4 Panel to panel, same station (no camera move)

Never empty the field: old content → `uiSoft` and alpha 0.4 over 0.10 s, new content lock-in at 0.6× duration (0.27 s), origin at the click. The veil holds throughout.

### 3.5 During camera flights

- Old panel lets go in the first 0.22 s. The field is empty mid-flight (this is the saccadic suppression; no separate effect needed for panels).
- New panel starts lock-in at **0.70 T** (was 0.55 T), so it sharpens as the camera settles. It never slides or motion-blurs: gaze-locked content forming while the room still glides is the cue that it is in the head.
- The breadcrumb (bottom left) stays and takes the dip: opacity −40 % at peak angular speed.
- Fix the stray-dot bug (t=0.2, t=0.6): deleting sparks removes the symptom, but also assert no record affects pixels outside its grown rect.

### 3.6 Launch handshake (1.8 s, starts at wake 3.0 s)

Cut the `cos(rho·18)` rings: concentric ripples read as sonar. Replace with what real implants do at first use, phosphene mapping, then the normal lock-in played slowly over the whole field. It teaches the vocabulary used afterwards.

- **0 – 0.75 s, mapping.** 24 soft blobs on a log-polar lattice centred on the viewport: 3 rings at eccentricity 0.05 H, 0.16 H, 0.42 H, 8 per ring, each ring rotated 22.5° from the last, jitter ±10 %. Lit **in spiral order**, 28 ms apart, each a Gaussian in time (0.20 s wide). Radius 5 / 14 / 36 px by ring (cortical magnification). Peak alpha 0.32 / 0.22 / 0.13. Colour `implant`, with a 1.5 px R/B split. It reads as one process travelling outward, never as random points.
- **0.5 – 1.2 s, the front.** One front (no rings) with the 3.2 law, origin at the centre, `d` over the screen diagonal, sweep time 0.7 s. At the front: +4 % brightness on the room, in `implantDeep`, width 0.12 in `ln(1+6d)` units, so it is thin at the centre and broad at the edges.
- **0.6 – 1.8 s, resolve.** The main menu locks in behind the front with a resolve time of 0.5 s per pixel and fringe starting at 4 px.
- Skipped or calm: plain 0.2 s cross-fade.

## 4. Integration with the scene

- **Focus is the coupling.** `uiFocus = 1` (reading the menu): veil 0.45, room depth of field as specced (max 2.4 px). `uiFocus = 0` (looking at the room): veil × 0.35, glows × 0.35, core alpha 0.75, core softened by mixing 40 % `uiSoft`. The room sharpens at the gazed depth. Menu and room trade clarity; this is what stops the panel looking pasted on.
- **Mouse is a weak gaze proxy.** `uiFocus` drops only after the pointer has dwelt 0.6 s outside every panel, and returns in 0.15 s. Core text never goes below alpha 0.75.
- **No global exposure change** when a panel is up: the room is already near black in every sheet. The local veil is the adaptation.
- **Lens warp:** keep, centred on the viewport, `kLensWarpK` 0.045 → **0.030**. Compensate the resampling softness with `core += 0.25 × (sharp − uiSoft)`.
- **Tremor:** not on the glyph core (sub-pixel resampling makes text shimmer). Apply 0.8 px peak to `uiSoft`, `uiHalo` and the veil only. The glow breathes around stable text.
- **Heartbeat:** same split. 0 on the core, 6 % on `uiHalo`. At 2 % on everything it is invisible (`heartbeat.png`).
- **Periphery:** keyed to gaze as specced. Blur by mixing toward `uiSoft` (max 50 %), desaturate 0.25, fringe 0.8 px.
- **Gaze wander:** keep as specced (0.8° cap). A wander saccade dims the layer by 25 % for 45 ms, not 60 %.
- **Salience:** hovering a row that refers to an object lifts that object's rim in `implant` over 0.3 s. This is the only way the implant touches the room, and it should be visibly present on the projector at the main menu.
- **No light from the panel onto the room, no occlusion by room objects, no bloom from the post chain on the layer.**

## 5. Cut or tone down

| Spec | Item | Decision |
|---|---|---|
| M24.4b step 7 | sparks on an 18 px grid | cut |
| M24.4b step 3 | per-pixel fbm progress | cut, replaced by 3.2 |
| M24.4b step 5 | 8-tap Poisson blur | cut, replaced by `uiSoft` / `uiHalo` |
| M24.4b step 8 | negative afterimage | replaced by 6 % persistence |
| M24.4b | 6 px curl separation | 2.5 px, 1/3-screen scale |
| M24.4b | `hudPanel` raster, frame, brackets, fills | cut on the implant path, with or without `livingVision` |
| M24.4b | saccadic dip 60 % | 40 % on the breadcrumb; 25 % on wander saccades |
| M24.4c 4.2e | fbm cloud edge (±16 px) | cut; smooth 90 px falloff |
| M24.4c 4.2d | halo 0.30 at 5 px | two scales (0.35 tight, 0.12 broad) |
| M24.4c 4.2a | origin at title, rim, ±0.35 wobble, 14 px smear | gaze origin, no rim, ±0.05, 5 px |
| M24.4c | `kLensWarpK` 0.045, tremor on text, `kUiAwayAlpha` 0.82 | 0.030, tremor on glow only, values in section 4 |
| M24.4c step 3 | `panelHi` / selected fills at 0.22 / 0.38 | soft wash 0.30 feathered 10 px, selected only |
| M24.7 | ring wave `cos(rho·18)`, 5–8 random phosphenes, global curl unsync | mapping spiral, one front, slow lock-in (3.6) |
| M24.7 | lens flare disc 0 → 3 → 1 | 0 → 1.6 → 1, alpha 0.5 → 0 |
| M24.7 | lids, blinks, hesitation, lid glow | keep unchanged |

## 6. Risks and check-by-eye criteria (lavapipe sheets)

| Item | Looks cheap if | Check on the sheet |
|---|---|---|
| Frameless text | text floats unreadably over the lamp or hologram | every menu screen at 1080p: each label readable at 50 % zoom; no straight line longer than 40 px that is not a glyph, tick or slider |
| Veil | it shows as a grey rounded rectangle | 2× crop of the veil's edge: no visible boundary; the lamp is still recognisable behind the main menu |
| Glow | it is a uniform fog, or text looks out of focus | 2× crop: glyph stems are as sharp as with `--living 0`; the halo is visibly violet, not white |
| Selection | it reads as a bar again | selected row at 2×: no edge on the wash; it is still found at a glance in a 50 % thumbnail |
| Lock-in | "burn-through" edge, or ghost copies | `condense.png`, age 0 … 0.45 s in 0.05 steps at 2×: at 0.10 s the rows of the menu are countable as soft bands; no isolated bright pixel; sharpness spreads from the origin; no visible front line |
| Fringe | rainbow text | at age 0.2 s, colour separation visible only at 2×, and whole words shift the same way |
| Let go | a plain fade, or smeared mush | `melt.png`, 0 … 0.22 s: the clicked row is the last readable thing; no content outside the grown rect |
| Persistence | invisible, or a ghost panel | frame at 0.30 s after leaving: just detectable at 2×, gone at 0.60 s |
| Flight | panel pops at the end | flight sheet in 0.1 s steps: field empty mid-flight (no dots anywhere), bands appear before the camera stops, sharp within 0.15 s after |
| Handshake mapping | random flashes again | `handshake.png` in 0.05 s steps: lit blobs form a spiral order; outer blobs are visibly larger and dimmer; never more than about 8 lit at once |
| Handshake front | a sonar ring | 2× crop: no ring line can be traced; only a broad faint lift |
| Focus trade | the menu looks broken when looking away | `focus.png`: with `--gaze-ui 0` the menu is still readable at 100 %, and the room behind the panel is visibly brighter and sharper than with `--gaze-ui 1` |
| Lens warp | text softness, bent rows | `warp.png` at 2×: row baselines bow ≤ 2 px across the panel; stems as sharp as warp 0 |
| Tremor / heartbeat | text shimmer, pumping | clip of 25 frames: the difference image of consecutive frames is zero on glyph cores, non-zero only in the glow |
| Periphery | a vignette blur | menu with gaze pinned on the hologram vs on the panel: the difference is visible only at 2× |
| Salience | outline-shader look | menu sheet: projector rim visible but softer and dimmer than its own cyan LEDs |
| Contrast rule | any LED motion in the layer | every sheet: no scan line, grid, square step or cyan in the implant layer |
