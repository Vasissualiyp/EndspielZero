<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Manifold visual ideas — choose what to add

The intended look is a physical LCD/LED board carrying holographic chess pieces in
cyberspace. The board supplies structure; the pieces supply light. Effects should
support a game that remains readable for an entire session, not just a trailer.

**Already requested:** rare tiny panel-interference strips and a
barely visible LCD settling response when a square's gameplay colour changes.
Both effects are implemented.
See `docs/plan/M23.8-led-ambient-and-response.md`. These are not extra choices below.
The existing LED matrix, hologram scan, moving ground and hover-glitch buttons are
also already part of the design.

**Approved (2026-10-31):** the section "Approved direction" below, A–F. It absorbs
optional items 2, 4, 6 and the far half of 10. All other optional items remain
unapproved.

## Approved direction

### Problem statement (review of `out/m24/*-manifold.png` and `board.frag`)

1. **Dark board on a dark background.** Board and background are both low-value navy/teal;
   the only separation is the thin plinth rim. The frame has no light source except the
   pieces.
2. **The LEDs do not emit.** `board.frag` (matId 7) computes
   `fragColor * (0.82 + 0.22 * pixels)`: a cell is never brighter than its dark theme
   colour, so the panel reads as a dimly painted reflector. The holograms (matId 4) do
   read as light-emitting; the squares must reach the same "this is light" quality.
3. **The squares are not alive.** The ambient interference is clamped to ±4 %
   (`clamp(..., -0.04, 0.04)`) and sparse, which is effectively invisible. Squares are
   "just there": no flicker, refresh, glitches or sense of a powered display.
4. **The background is empty and does not respond to the camera.** `backdrop.frag` is
   screen-space (uv only, fixed glow centre), so orbiting changes nothing behind the board
   except the moving ground.

The target is still a **dim** atmosphere. The fix is value and hue *separation* and real
emission, not brightening the whole frame: the board should be the brightest large thing
on screen, the way a monitor is in a dark room.

### A. LED emission (squares look like light, not paint)

- Light squares gain an **emission term** concentrated in the existing emitter centres
  (the `pixels` matrix); the black gaps between emitters stay black. Dark squares stay
  dark. Result: strong checker value contrast, overall frame still dim.
- **Sub-pixel bleed:** a soft falloff around each emitter centre inside the cell, so
  emitters glow rather than being flat dots. Must go through the existing
  `fwidth`/`undersampled` filtering so far ranks do not moiré (this is optional item 4).
- **Emission-only soft glow:** a restrained, low-radius glow from emissive board
  fragments (an emissive mask plus the existing `blur` pass, or an in-shader
  approximation if a pass is too costly). Never a whole-screen bloom; pieces, portal
  rails and text must not be washed out.
- Gameplay colours (selection, legal-move, last-move, check, seams/portals) keep their
  hue and must remain at least as distinguishable as today. Emission scales the theme's
  cell colour; it does not replace it.

### B. Board as a light source in the scene

- **Backdrop glow follows the board:** drive `backdrop.frag`'s glow centre and falloff
  from the board's projected screen centre and size each frame, tinted with the board's
  emission colour. This is the light the board spills into the dark room.
- **Ground spill:** the moving ground/grid brightens slightly near the board's footprint.
- **Hue separation:** shift the manifold background toward neutral near-black (or a
  slight violet) and keep the board cyan, so they separate even at equal low value.
- **Lit bezel:** a thin continuous emissive line around the board silhouette — a stronger
  version of the plinth rim, never competing with portal rail hues.

### C. LED aliveness (bounded)

The panels must look powered and slightly imperfect, while the board stays readable for
a whole session. All of these are cell-local, sparse and bounded:

- **Per-cell breathing:** a very slow, small, per-cell decorrelated emission variation
  (a few percent), so the field is never perfectly uniform or static.
- **Refresh sweep:** an occasional faint scanline sweep across the panel field (rows
  refreshing), slow and low-contrast.
- **Cell glitches:** rarely, one cell (never several in a rank at once) shows a brief
  defect for ~80–250 ms: a row of emitters shifts sideways, a short horizontal tear, a
  momentary channel offset confined to that cell, or a brief dropout to dark followed by
  the LCD settling response. At most a few per board per ~10 s.
- **Stuck/flickering emitters:** a tiny number of individual emitters that flicker or
  sit slightly off-brightness, stable over time (seeded per cell).
- **Dead/flickering pixels wander (revised 2026-10-31):** no emitter is permanently
  defective. The set of faulty emitters is re-seeded over time (each fault lives a few
  seconds to tens of seconds, fades in and out rather than popping, then a new one
  appears elsewhere). Flicker rate is slow — roughly 1–3 irregular blinks per second, not
  a fast buzz.
- **Pixel flicker is a first-class effect, not a rare accident:** at any moment a handful
  of emitters across the board are visibly flickering (fast, irregular, per-emitter
  seeded), clearly visible at normal zoom but each confined to one emitter. Cell glitches
  must also be visible at normal play distance — tune for "noticed occasionally while
  playing", not "only found by searching a still".
- **Living colour change (per-emitter transition):** when a cell's gameplay colour
  changes (selection, legal-move cue appearing/clearing, last-move, check, move arrival),
  the cell does not switch as one block. Each emitter has a seeded delay and duration:
  most emitters (~80 %) complete within ~120 ms so the cue reads immediately; a
  long tail (~10–20 %) lags 2–4× longer, and a very few "lazy" emitters drift into the
  new colour over ~0.4–0.6 s. The change therefore sweeps/sparkles across the cell and a
  few pixels linger in the old colour before catching up. Optionally a slight overshoot
  in brightness as an emitter lands. This extends the existing LCD settling response
  (`panel_response`); it needs the cell's previous colour and change time per cell.
  Pure in explicit time; settled captures show the final colour.
- Replace the ±4 % clamp with explicit per-effect bounds; keep seeded, pure-in-time
  behaviour so a capture at `--t` is reproducible.
- Never: whole-rank flashes, glitches on squares carrying a gameplay cue (selected,
  legal destination, check) while that cue is shown, or anything resembling a move cue.

### D. Hologram moves as projector handoff (replaces the glide; absorbs items 2 and 6)

Rationale: a piece is light projected by a square. Gliding across gaps and seams means it
exists where no projector is. Instead the piece is handed from panel to panel.

- **Path:** use `view::MovePath` (`src/view/move_anim.hpp`). A slider hands off along
  `steps` cell by cell, portal steps included (the far-side cell simply takes over —
  exactly what a glued edge means). A leap (`leap == true`, e.g. knight) is one handoff
  `from → to`. `unexplained` moves (castling, rule displacements) are a direct handoff
  per piece.
- **Solid head, ghosts behind (revised 2026-10-31, supersedes the crossfade bullet):**
  at every moment exactly one copy is solid — the *head*, on the newest cell reached. When
  the head advances, the new cell's copy comes up to full opacity almost at once (a very
  short rise, well under a hop), and the copy it left drops to ghost level and decays.
  There is never a faint "arriving" copy in front of the head; faintness only exists
  behind it. The destination is therefore solid the moment the head lands — no late
  resolve. Heat-up ahead of the head stays (the base warms, then the solid piece appears).
- **Crossfade, not on/off (superseded by the bullet above for the leading copy; still
  applies to the trail):** each hop's copy starts appearing before the previous one is
  gone; old copies fade over ~2 hops, leaving a short trail. Intermediate copies are
  dim and scan-dominated (partial projections); only the destination resolves to a solid
  figure.
- **Heat-up:** a cell's hologram base warms up 1–2 hops *ahead* of the leading copy; the
  piece resolves once its base is lit. After the piece leaves, the base cools with a short
  afterglow. Only cells just ahead warm up — never the whole path at once (that would look
  like legal-move highlighting). Neutral warm-white, clearly distinct from selection and
  move cues.
- **Standing footprint:** every standing piece keeps a faint lit base on its cell
  (item 2). Much dimmer and smaller than any move cue; both armies' identities preserved.
  Occupied squares become small light sources, which also helps problem 1.
- **Capture on the LED theme is an LCD colour change, not a slab:** the existing capture
  flash (a scaled cell slab drawn over the board, `captureFlash` in `board_renderer.cpp`)
  is replaced, for LED cells, by the captured cell's own emitters switching to the
  capture colour through the per-emitter living transition and then settling back to the
  cell's colour. Nothing is drawn above the panel. Other themes keep the slab.
- **Timing:** total move duration capped near the current duration; per-hop time
  shrinks on long moves. Captures: the captured piece fades as the leading copy reaches
  its square (as `captureFade` does now). Pure in `progress`, so `--move-t` captures
  reproduce.
- **Camera:** `move_camera` and shape-follow keep reading a *continuous* virtual
  position along the path, so the camera does not jerk when the piece hops.
- **Surface view:** the hop replaces the continuous ride and the contact shadow pinned
  to `SurfaceMoveSample::ground` (M18.2); the footprint per hop takes its place.
- Heat-up/footprint need their **own per-cell channel**; the existing
  `fragPanelSignal.y` response is clamped far too small. Reuse the per-cell signal
  plumbing from `panel_response`.

### E. Background depth (far layer first)

- **Far layer, direction-only:** sparse dim points on a sphere at infinity (reconstruct
  the view ray in a fullscreen pass, hash direction cells). They rotate with the camera
  orbit but do not translate. Cool neutral colours only, never portal or army hues;
  fade near the board silhouette.
- **Mid layer (only if the far layer is not enough):** a few slow faint shapes at 2–3
  finite depths for real parallax. Low count.

### F. One presentation intensity

Every new animated effect (C, D's trail, E's motion) reads one shared intensity scalar,
so the calm presentation (item 11) can later be one setting rather than scattered
switches. In that mode: no trail (plain `from → to` crossfade), no glitches, static
emitters and footprints kept.

### Order

1. A + B (emission, board-centred backdrop glow, hue separation, bezel). Changes how
   every later effect looks, so later levels are tuned against it.
2. C (aliveness).
3. D (handoff moves with heat-up and footprints).
4. E (far background layer).

## Next: presentation polish (specified)

Dev overlay toggle, post-processing (HDR, emissive bloom, identity-preserving tone map,
grain/dither), board boot sequence, check ripple, game-end power-down, stars-only game
background and HUD chrome are specified in
[`docs/plan/M23.10-presentation-polish.md`](docs/plan/M23.10-presentation-polish.md).

## Optional choices

| ID | Idea and visible treatment | What it contributes | Readability/intensity tradeoff | Implementation weight |
|---|---|---|---|---|
| 1 | **Recessed display modules.** Give each square a thin dark gasket and a restrained inner bevel highlight; corners read as manufactured panels instead of painted checks. | A tangible screen assembly beneath the holograms, visible even in a still. | Static and quiet; border must not shrink the playable-looking area or compete with portal rails. | Small: refine existing cell-local LED border shading; inspect curved/inverted surfaces. |
| 2 | **Hologram projector contact.** *Approved as part of D.* A very faint neutral light footprint immediately under each standing piece, roughly the shape of its base, never a tall beam. | Makes the piece look projected *by* its square and visually seats it on the display. | Can resemble selection if too bright; keep ordinary footprints much weaker and smaller than any move cue, preserving both armies' identity. | Medium: existing base/disc geometry or local panel emission; geometry surfaces and moving pieces need careful anchoring. |
| 3 | **Off-axis LCD response.** Very gently lower panel brightness when seen at a grazing angle, while the front view retains current contrast. | Gives the board a screen surface rather than self-lit paint. | Too much makes far ranks disappear; strong lower bound and portal/piece exemptions essential. No rainbow shift. Not recommended: curved boards are read at grazing angles. | Small: one bounded view-normal term in the LED shader, tested on rotated/inside-out boards. |
| 4 | **Emission restricted to the pixel grid.** *Approved as part of A.* On a close zoom, bright highlights strengthen existing emitter centres while their tiny black gaps stay black. | More convincing display colour changes and close-up LCD physicality. | Little normal-play cost; can produce moiré if it bypasses current derivative filtering. | Small–medium: extend the existing matrix shading and close/far sampling tests. |
| 5 | **Quiet display edge hardware.** A few low-contrast ventilation slots or connector marks on the plinth's side, plus one tiny neutral power light. | Turns the whole board into a deliberate device without filling it with decoration. | Keep detail off the squares and away from seam hues; useful in oblique views, mostly invisible from above. | Small–medium: procedural glass/plinth treatment, distinct from LED cells. |
| 6 | **Arrival-only hologram reacquisition.** *Subsumed by D* (the final handoff resolving). Once a moved piece reaches its legal destination, one short, faint scan stabilizes its silhouette, then the ordinary scan resumes. | Suggests projection being re-established at the receiving panel. | Must not delay or obscure the actual move or mimic a capture; no repeated flicker, no disappearing figure. | Medium: explicit move-event timing and object-local material control; arbitrary still captures need defined event age. |
| 7 | **Very faint directional halo on piece silhouettes.** A narrow cool-neutral halo strengthens only along selected viewing angles; solid base silhouette and army colour remain clear. | A stronger holographic light impression without making pieces glassy again. | Broad glow washes out nearby pieces and portal hues; avoid whole-screen bloom and large halos. | Medium: shader rim refinement first (a rim term already exists); a genuine outside-silhouette halo would need extra geometry and more testing. |
| 8 | **Topology links as data conduits.** Existing matching portal rails carry an occasional very dim brightness packet, consistently related at their two ends. | Makes non-Euclidean connections feel like active circuitry rather than arbitrary coloured markings. | The portal's original hue and continuous identity must dominate; moving packets must not be mistaken for legal routes. | Medium–large: use stable portal identity and seam parameterization, including twists and geometry view; not a per-cell random stripe. |
| 9 | **Layer brackets for higher-dimensional boards.** Quiet neutral corner marks or coordinate ticks frame each visible board layer, shown mainly near the focused layer. | Makes multiple boards feel like organized display surfaces in a shared space. | Avoid dense labels or implying a new move relationship; keep unrelated layers subdued and picking unchanged. | Medium: layout-aware presentation geometry using existing layer information. |
| 10 | **Soft spatial depth cues behind the board.** *Far layer approved as E.* A few slow, dim points at different depths drift behind the existing moving ground, fading near the board silhouette. | Strengthens cyberspace depth without changing board materials. | Can compete with the existing ground and chess terminals. | Medium: extend the existing backdrop, no separate particle framework. |
| 11 | **A calm accessibility presentation.** Optional reduction of ambient glitches/scan motion while retaining static LED pixels and hologram silhouettes. Recommended next; F prepares for it. | Keeps the aesthetic usable for players who find continuous motion distracting. | Less animated, equally legible. This is a preference, not a new visual effect forced on everyone. | Medium: one coherent presentation setting propagated to shader/UI effects and captures, rather than many scattered switches. |

Avoid a full-screen CRT filter, permanent RGB splitting, strong bloom, constantly
corrupted pieces or random flashes across entire ranks. Those would obscure the
LCD/hologram distinction and compete with gameplay signals.
