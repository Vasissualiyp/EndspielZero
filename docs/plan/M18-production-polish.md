# M18 — Production polish: the game stops reading as a tech demo

Spec'd 2026-10-04, after Wave 1's visuals (M11/M12/M13/M17) landed. Prompted by a direct
note during a playtest: the engine is sound and the shape view is a genuine trailer moment,
but the moment-to-moment feel - a capture that vanishes with no acknowledgement, pieces with
no contact shadow, total silence, a flat-colour void behind the board, raw unframed footage
out of the M12.6 runner - reads as a tech demo rather than a shipped game. None of this is
new engine work: it is finishing the presentation layer around engine work that is already
done.

**Not a release-sequence wave.** This sits beside Wave 1/2 rather than inside them - every
item here is presentation, addressable independently, and ships whenever it is ready. Several
items revisit M4.7's deliberate M4-time scope boundary ("no shadows, no post-processing");
that boundary was correct for M4 (a renderer not yet proven should not carry speculative
weight) and is being revisited now because the goal has changed - M4 needed a board that
worked, M18 needs footage that sells.

**Order, cheapest/highest-impact first:**

1. **M18.1 Capture/move juice** - a capture currently just disappears.
2. **M18.2 Contact shadows** - pieces read as floating.
3. **M18.3 Cinema vignette and grade** - exported clips are unframed and flat.
4. **M18.4 The ambient background, redone** - the existing decoration is colourful confetti
   with no connection to the game; replace it with the game's own geometry, drawn quietly,
   and bring a toned-down version to the board itself.
5. **M18.5 Audio v1** - the game is silent. `Settings` already has volume sliders stored
   "for when it does" (`src/app/settings.hpp`). Biggest single perception change, biggest
   lift; scoped so a v1 needs no licensed assets at all.
6. **M18.6 The flat/shape morph, and view controls that cannot contradict each other** -
   SHAPE currently snaps instead of morphing (the overture already does this exact morph,
   one layer over - `PlaySurface` just never asks for it), and 2D/SHAPE can be set to states
   that make no sense together.

## M18.1 Capture/move juice

**Want.** A capture is the single most dramatic moment chess has, and right now it is
invisible: the captured piece's instance simply stops being drawn once the position updates,
with no transition and no cell-level acknowledgement. A marketing clip of a capture should
look like one.

**What it is.** A short, local flourish at the captured cell, timed against the existing move
animation rather than a new clock.

**Build.**

- **Locate the hook.** `view::MoveAnimation` already distinguishes `Move::captureCell` from
  `Move::to` (see AGENTS.md's gotcha: "`Move::captureCell` is not `Move::to`" - en passant is
  the one case they differ). Find where `BoardRenderer::buildInstances` currently decides
  whether to include the captured piece's instance during the animation (it is presumably
  drawn normally until the move resolves, then dropped abruptly) and where the capturing
  piece's own arrival is timed.
- **The captured piece fades, it does not vanish.** Scale and fade its instance out over a
  short window timed to the move's own progress (reuse the easing helpers already used for
  the move camera's shot envelope, e.g. `view::shotEnvelope`-style smoothstep, not a new curve
  vocabulary), ending at or just before the capturing piece's arrival.
- **A flash ring at the cell.** One short-lived instance - reuse `Archetype::Cell` (the flat
  slab already used for the board itself) scaled and coloured, or add a new thin-ring
  archetype if `Cell` cannot be made to read as a ring - that brightens at the capture instant
  and fades over a few hundred milliseconds. Colour: `Theme::blood` already exists
  ("semantic, deliberately separate from the accent" - used today only for the check
  vignette) is the natural choice; a capture and a check sharing one "something happened"
  hue is consistent rather than introducing a second meaning for red.
- **Determinism.** Like every other piece of M11/M12/M13, this must be a pure function of
  the move's own progress `t`, not of wall-clock time or frame count, so `--clip` stays
  reproducible. No new per-frame state beyond what `MoveAnimation` already carries.

**Tests.**

- A unit/property test that the captured piece's instance alpha is 1 before the capture
  window and 0 after it, monotonic in between, as a pure function of `t`.
- A validation-clean `--shot`/`--clip` of a capturing move on `standard` at a `t` inside the
  flourish window, and a differential check that the same `t` twice produces the same frame
  (determinism).
- A test that a *non*-capturing move triggers no flourish instance at all.

**Acceptance.** A capture, watched in `--cinema` at normal pace, reads as an event - the
captured piece visibly leaves rather than disappearing between frames - and a `--clip` of it
is still byte-identical on re-export.

**Risks and non-goals.** Not a combat system or a damage number; one flourish, tuned for
legibility at trailer resolution, not a cosmetics system with options. Must not change
`Move`/`MoveAnimation`'s timing contract that the move camera (M11) already depends on - this
is a draw-time decoration over an unchanged animation, not a longer move.

### Status: M18.1 built (2026-10-04, opencode)

A capture now leaves a flourish: the captured piece shrinks and fades out over the move,
and a blood flash pulses at the square it stood on as the pieces meet.

- **Where it hooks.** The spec guessed the captured piece "is drawn normally until the move
  resolves, then dropped". It is not: `Session::playChecked` applies the move and calls
  `refreshSnapshot` *before* `MoveAnimation::start`, so the captured piece is already absent
  from the `PositionView` the renderer is handed. `view::tracePath` (which already runs
  against the pre-move position) now records `MovePath::captureCell` and a copy of the
  captured `Piece`; `MoveAnimation::start` keeps them and exposes `captures()`,
  `captureCell()`, `capturedPiece()`. `BoardRenderer::buildInstances` rebuilds the piece
  from that copy. `captureCell` is used, never `to`, so en passant fades the pawn on d5, not
  the empty square the capturer lands on (AGENTS.md's gotcha).
- **Pure function of `t`.** `view::captureFade(progress)` returns 1 through the first fifth
  of the move and falls smoothly to 0 by four fifths of the way in (before the capturing
  piece arrives); `view::captureFlash(progress)` is a smoothstep pulse peaking at `t = 0.8`.
  Both live in `src/view/move_anim.cpp` and reuse the `shotEnvelope`-style smoothstep, with
  no new curve vocabulary and no state beyond `MoveAnimation`'s own progress.
- **The blended pass.** The opaque pipeline writes alpha but does not blend, so the flourish
  is kept in a new `InstanceSet::flourish`/`flourishBatches` and drawn as one extra group
  with the existing GHOST `surfaceBlendPipeline_` (blend on, depth write off, depth test on)
  - no new pipeline, no second buffer; the flourish instances ride the instance buffer
  immediately after the opaque ones. The flash is `Archetype::Cell` scaled and grown as it
  fades, coloured `Theme::blood` (the check-vignette hue), as the spec allows. Both the flat
  board and the geometry view emit the flourish; flat (`options_.flat`, UI-drawn pieces) is
  skipped.
- **Tests.** `[view]` unit tests pin `captureFade`/`captureFlash` boundaries and
  monotonicity and that the trace/animation carry the captured piece (including en passant);
  a no-GPU `[render]` test builds instances for a real `standard` capture and checks one
  opaque captured piece at `t = 0`, a translucent piece plus a blood flash mid-window,
  byte-identical `flourish` on a repeat build, an empty flourish at `t = 1`, and an empty
  flourish for a non-capturing move; a second no-GPU test does the same for `torus` with
  `options.surface` on, so the geometry view's own copy of the flourish is covered.
- **Deviations.** (1) The captured piece had to be carried on `MovePath`/`MoveAnimation`
  rather than read from a snapshot that no longer has it. (2) The fade is drawn through the
  existing GHOST blend pipeline rather than by adding blending to the opaque one or a new
  pipeline. (3) The flash is the reusable cell slab, not a new ring archetype (the spec's
  cheaper option).
- **Verification.** `tools/test.sh --build view render` green; `tools/precommit.sh` green
  (format, build, arch, unit, property). Visually validated with
  `standard --cinema --move-t T --script` (e2-e4, d7-d5, exd5) rendered to
  `build/m181_check/`: at `t = 0` the black pawn sits whole on d5, at `t = 0.45` it is
  shrunk and translucent against the arriving white pawn, at `t = 0.8` a red flash covers
  the square, at `t = 0.95` both are spent. Two captures at the same `t` are byte-identical
  (`cmp`), and every capture is validation-clean.

## M18.2 Contact shadows

**Want.** Every capture taken during Wave 1's QA pass shows pieces that read as floating a
few millimetres above their square - there is no cue tying a piece to the cell under it. A
contact shadow is one of the cheapest "looks finished" tricks there is.

**What it is.** A soft, dark, roughly circular decal under each piece, drawn before the piece
in the same instanced pass the board already uses - not a shadow-mapped light, which M4.7
correctly scoped out and which this does not need.

**Build.**

- **One instance per occupied cell**, positioned at the cell's own surface point (the flat
  board's cell centre, or - on the geometry view - the seat's `centre` from `PlaySurface`,
  nudged along the normal by a small fraction of a cell so it never z-fights the board),
  scaled smaller than the piece's own footprint, coloured dark and translucent
  (`Theme::soot`/`ink` at low alpha - check both themes render it correctly, the way
  `tests/unit/view/test_theme.cpp` already pins piece-on-square contrast).
- **Shape.** `Archetype::Cell` is a flat slab, not a disc with a soft radial falloff; a hard-
  edged square shadow will look wrong. Either add a small fragment-shader radial falloff to
  the existing flat-slab path gated by a new per-instance flag (cheapest, reuses the existing
  pipeline and shader), or add a new `Archetype` (a low-poly disc) if a shader branch is
  judged worse than one more mesh. Decide by trying the shader-flag route first - it is less
  code and this project's own convention is "one material, no shadows, no post-processing"
  stays mostly true if this rides the existing pipeline rather than opening a second one
  (contrast with GHOST and M17.21's rails, which both deliberately stayed inside the existing
  per-vertex-colour / existing-pipeline discipline rather than adding passes).
- **The shape view.** A piece's shadow there should sit on the surface under it, using the
  seat's own normal - reuse exactly the placement math `kSurfaceHoverCells`/the piece's own
  seat placement already computes, so the shadow is "one cell's worth of offset along the
  normal" the same way the GHOST/rail work already thinks about placement on a curved board.
- **Does not apply during a glide's hover.** A piece mid-glide is already hovered off the
  surface (`kSurfaceHoverCells`) by design - its shadow should stay pinned to the board at
  the position directly below the hovering piece (projected along the normal), not follow the
  piece up, or the "contact" the shadow exists to show becomes a lie.

**Tests.**

- A render test that an occupied cell gets exactly one shadow instance and an empty cell
  gets none.
- `tests/unit/view/test_theme.cpp`-style contrast check: the shadow must not make a dark
  piece unreadable on a dark square in either theme (measure, do not eyeball).
- Validation-clean `--shot` on `standard` (flat) and `torus` (geometry view) with shadows on.

**Acceptance.** Every occupied cell, flat or on a shape, shows a soft dark decal under its
piece; `standard --shot` before/after is visibly different only by the addition of shadows
(no other instance, colour or layout change).

**Risks and non-goals.** Not real shadow-mapping, not self-shadowing between pieces, not
affected by the (nonexistent) light's position - a flat, cheap, always-on ground contact cue,
nothing more. If the shader-flag route turns out to need real branching complexity, fall back
to a second thin disc mesh rather than fighting the single-material constraint - record
whichever is chosen and why, the way GHOST's status note already records its own pipeline
decision.

### Status: M18.2 built (2026-10-04, opencode)

Every occupied cell now carries a soft, dark contact shadow under its piece; empty cells
have none. It reads on the ordinary board and on the geometry view.

- **Route: a new disc mesh, not the shader flag.** The spec's preferred route - a new
  per-instance flag adding a radial falloff to the flat-slab `Cell` path - was rejected on
  inspection: it needs either a new field on the 84-byte hot `Instance` struct (and its
  vertex-attribute layout) or an overloaded meaning for an existing field, and it puts a
  branch into the fragment shader every draw executes. A new `Archetype::Disc` instead bakes
  the falloff into the mesh's *vertex alpha* - the exact channel the geometry view's ghost
  board already rides, which the shader already multiplies into the instance colour - so the
  soft edge is data: no shader change, no new pipeline, no new attribute. A disc is also
  genuinely round, where a falloff on the box slab still shows square sides at a grazing
  angle. This is the same "one more translucent draw group through the existing blend
  pipeline" pattern M18.1's flourish established.
- **Where it hooks.** `BoardRenderer::buildInstances` emits one shadow per occupied
  placement, plus one for the travelling piece at its current ground position (not its
  hovered one); `options_.flat` emits none, the same cut the flourish takes, because the
  interface draws those pieces. They live in a new `InstanceSet::shadows`/`shadowBatches`
  and are drawn through `surfaceBlendPipeline_` (blend on, depth write off, depth test on)
  immediately after the opaque board and before the flourish. The geometry view builds its
  own from `PlaySurface` seats, turned to the seat's frame and lifted `kShadowLift` above
  the tile so it cannot z-fight.
- **Hover.** `SurfaceMoveSample` gained `ground`, the pre-hover point on the surface, so a
  gliding piece's shadow stays pinned under it instead of riding up with the hover; a leap's
  ground is its chord. `position` is unchanged, so the camera is untouched.
- **Colour.** The spec suggested `Theme::soot`/`ink`, but those are *background* colours that
  flip with the theme: on the light `manifold` theme they are the palest values in the
  palette, so a shadow from them would read as a glow. Added `Theme::shadow` instead - a
  deep slate on `manifold` (alpha 0.34) and a near-black on `console` (alpha 0.17), each
  alpha tuned so the decal darkens the square without eroding a black piece on a dark
  square. The disc's vertex-alpha profile holds near-solid to 0.55 of the radius then eases
  to zero, so the visible rim is soft rather than a cone.
- **Tests.** A no-GPU `[render]` test pins one shadow per occupied cell, none on an empty
  FEN, translucency, and a byte-identical rebuild; another pins the disc mesh's radial alpha
  ramp (solid core, zero rim); a third does the same for `torus` with `options.surface` on.
  `test_theme`'s new contrast case measures the shadow composited over both squares in both
  themes: it must darken the square and leave both piece colours above 1.8:1.
- **Verification.** `tools/test.sh --build view render` green (100 render cases including
  the validation-layer GPU runs, 68 view cases); `tools/precommit.sh` green (format, build,
  arch, unit, property). Visually validated to `build/m182_check/`: `standard --shot`
  before/after differs only by the shadows under the pieces, `torus --geometry --shot`
  shows them lying on the surface, and two captures of the same state are byte-identical
  (`cmp`).
- **Deviations.** (1) The disc mesh rather than the spec's preferred shader flag, for the
  reasons above. (2) `Theme::shadow` rather than `soot`/`ink`, which are background colours
  that would *lighten* the light theme. (3) The shadow rides the blended pass after the
  opaque pieces (depth test hides it under them) rather than literally "before the piece";
  a translucent decal cannot go through the opaque, depth-writing pipeline, and this is the
  order M18.1's flourish already set.

## M18.3 Cinema vignette and grade

**Want.** `--cinema` strips the interface so the board is the whole frame, but the frame
itself is undressed - flat lighting, square corners, no sense that a camera is "shooting" it.
A subtle vignette and a small contrast/saturation push, applied only in cinema mode, costs
nothing structurally and reads immediately as "graded footage" rather than "a screenshot."

**What it is.** A 2-D overlay, exactly like the existing check-warning vignette
(`Ui::drawCheckEdges`, `src/render/ui.cpp`: "a soft red vignette at the frame's edge: a few
nested outlines, strongest at the very edge and fading inward") - reuse that technique
directly, neutral-toned instead of red, gated on cinema mode instead of check.

**Build.**

- **Vignette.** Copy `drawCheckEdges`'s nested-outline technique (same ring count, same
  falloff shape is a fine starting point) with `theme.ink`/`theme.soot` instead of
  `theme.blood`, gated on a new `cinema` flag reaching `Ui::build` (cinema mode is already a
  capture-path and interactive concept - `--cinema` on the CLI, and presumably a settings or
  session flag for the interactive "watch" use case M12.6 adds; thread whichever already
  carries that intent, do not add a second one).
- **Grade.** A small, fixed contrast/saturation adjustment is harder to do as a 2-D overlay
  trick (multiplying over colour does not raise contrast the way a real curve does) - the
  cheapest honest version is a very low-alpha black overlay for lift/crush plus a very
  low-alpha saturated-colour overlay (e.g. a faint warm or cool wash, picked from the theme,
  not an arbitrary new colour) for a touch of colour grading, both 2-D overlay tricks like the
  vignette - no shader, no post-processing pass. If this reads as too subtle or too crude in
  practice, the more correct version is a real colour-curve post-pass, which is a materially
  bigger lift (a second render target and a fullscreen pass) - try the overlay version first
  and only escalate if it visibly fails.
- **Off by default outside cinema.** Ordinary play must look exactly as it does today; this
  is additive and gated, never a change to the default theme.

**Tests.**

- A pixel-level test: a cinema-mode capture's corner pixels are measurably darker than its
  centre pixels (the vignette), and a non-cinema capture of the same position is unaffected.
- Validation-clean `--shot --cinema` on a couple of variants.

**Acceptance.** `--cinema` output visibly reads as graded footage next to an ungraded
capture of the same frame; interactive play and non-cinema captures are pixel-identical to
before this lands.

**Risks and non-goals.** Not colour management, not HDR, not a settings-exposed grading
system - one fixed look, tuned once, for the one purpose (marketing capture) cinema mode
exists for. If it fights `--ghost`'s own alpha blending or M17.21's rails anywhere, that is a
compositing-order bug to fix, not a reason to change either feature.

### Status: M18.3 built (2026-10-04, opencode)

Cinema captures now carry a soft neutral vignette and a low-alpha grade; every other frame -
captures and interactive play - is byte-for-byte unchanged.

- **Route: a 2-D draw-list overlay, not a post pass.** The board renderer's overlay slot
  already draws the interface after the board (and after any blur), so the look is two
  full-frame rectangles plus the vignette's rings in `ImGui::GetBackgroundDrawList()` - no
  second render target, no fullscreen shader. The spec's cheap option, and it reads, so the
  real colour-curve pass was not needed.
- **Where it hooks.** Cinema used to pass `noUi`, so the interface was never recorded. It now
  records the pass (`main.cpp`) and a new `Ui::setCinema` reaches `Ui::build`, which in cinema
  returns before building any screen and draws only `drawCinemaLook()`. That is what keeps the
  furniture out while still compositing the look over the board; the non-cinema path is
  otherwise untouched, and `setCinema` defaults off.
- **Vignette.** `drawCheckEdges`'s nested-outline technique, neutral instead of red. Not its
  seven fat rings, though: at cinema scale those read as a picture frame, so it is 80 thin
  rings (1.5 px) reaching ~120 px in with a quadratic falloff - soft at 1080p and at capture
  size.
- **Grade.** A 6% crush plus a 4% wash of `theme.ember` - the one accent, so the wash is cool
  on manifold and warm on console with no cinema-only colour.
- **Deviations.** (1) `theme.shadow` (its RGB, alpha forced to 1) rather than the spec's
  `theme.ink`/`theme.soot`, which are *page* colours and would lighten the light theme - the
  same trap M18.2 recorded. (2) Ring count and band tuned away from `drawCheckEdges`'s values
  for softness, which the spec allowed ("a fine starting point").
- **Tests.** `gui-cinema-grade` (new `tests/check_cinema_grade.cmake`) renders `torus
  --geometry` with and without `--cinema` at 640x360 and reads the PPM: the ring leaves the
  frame centre as page, so the cinema corners must be measurably darker than its centre, while
  the ungraded frame stays flat (corner vs centre within 6/1000). `gui-cinema-standard` and
  `gui-cinema-torus` are validation-clean `--shot --cinema` captures.
- **Verification.** `tools/test.sh --build render` green (100 cases, 4.6M assertions);
  `ctest --preset dev -L render` 30/30; `tools/precommit.sh` green (format, build, arch, unit,
  property). Pixel-identity was checked with `cmp`, not argued: `standard --script "click e2"
  --shot` and `torus --geometry --shot` are byte-identical before and after
  (`build/m183_check/`), while the same frames with `--cinema` differ. Visually confirmed at
  640x360 and 1920x1080: cinema reads framed and graded, the ungraded frame is the old picture.

## M18.4 The ambient background, redone

**Want.** The existing menu background (`render::DepthField`, `src/render/deco.cpp`) is 120
bodies of randomly-coloured pastel polygons plus a few piece icons, cycling hue continuously -
confetti, disconnected from what the game actually is. Direction from the same playtest
conversation: replace the abstract polygons with the game's own geometries (torus, Klein
bottle, Möbius strip, cube, hypercube - **not** the quintic/`t6`, which is "too much" visually
busy for a background), drawn as thin wireframes rather than filled shapes, in a muted
"near-future, string-theory" palette rather than rainbow pastels, and bring a toned-down
version to the actual game screen, which currently has no background decoration at all.

**What it already has to build on.** This is cheaper than it sounds: the whole decoration
system (`deco.cpp`) is 2-D ImGui draw-list math with its own small local camera/projection
helpers (`Camera`, `rotateX`/`rotateY`) - it does not touch the Vulkan renderer, the instanced
pipeline, or `BoardRenderer` at all. `drawTesseract` *already is* a muted wireframe 4-cube
(theme-toned `line` colour, edges drawn with `AddLine`, corners as small filled squares) -
the exact technique this item needs, for one of the five shapes already. `Theme` already has
an unused-for-this-purpose "cold" accent (`rift`/`riftDeep`, cyan, "reserved") and the seam
hue ramp (`seamHueBegin/End/Saturation/Value`, "a cold arc...that deliberately never reaches
the warm half of the wheel, which belongs to the game rather than to the geometry") - both
are exactly the muted, thematically-load-bearing palette this item should draw from instead
of `kPastels`/`kPastelsDark`.

**Build.**

- **Four more wireframe shape-drawers, alongside `drawTesseract`.** A torus (a ring of
  wireframe quads or just lines along its two parameter directions - reuse the parametric
  idea from `shellTube`/the overture torus, projected through the same local `Camera` struct
  `drawManifold`/`drawTesseract` already use, not the full 3-D renderer), a Klein bottle
  (reuse `kleinSurf`'s parametrisation the same way, wireframe not filled), a Möbius strip
  (a degenerate case of the same family, or its own simple parametrisation), and a cube (the
  easy one - `drawTesseract`'s 4-cube code specialised to 3 axes, or simply drawn by hand,
  it is eight points and twelve edges). Each one slowly rotating, line-only, no fills, in the
  `rift`/seam-ramp palette.
- **`DepthField`'s body vocabulary changes.** Today a body is either a piece icon or a
  random-sided filled polygon (`kPastels`-coloured). Replace the polygon kind with "one of
  the five wireframe shapes" (keep the piece icons - they are already "the game appearing in
  its own background," which is the right idea and not what was called out as a problem).
  Fewer bodies than today's 120 is likely right, since a wireframe shape reads as more
  complex than a triangle at a glance - tune by eye, but expect a smaller number with more
  visual weight each, not the same count.
- **Colour.** Replace `kPastels`/`kPastelsDark` sampling for these bodies with the seam hue
  ramp (`view::seamRampColor`-style, or `Theme::rift`/`riftDeep` directly) at low saturation/
  low alpha - "wireframe-y," glowing faintly, never competing with the board the way the
  existing rule for seam colour already states ("saturated colour is reserved for geometry
  that is not flat... the shell itself is cool and neutral"). This item is squarely an
  application of that existing rule, not an exception to it.
- **The main menu's dedicated object.** `decoForScreen` currently gives `MainMenu`/`Welcome`
  the quintic-cross-section `Deco::Manifold` object (`drawManifold`) *in addition to* the
  `DepthField` background every shell screen gets. Per the "quintic is too much" direction,
  retire `Deco::Manifold` for the main menu specifically (fold to `Deco::None` there, letting
  the redone `DepthField` alone carry the background) rather than keeping two decoration
  systems stacked. `Deco::Manifold` itself need not be deleted if the quintic is still wanted
  elsewhere (e.g. `t6`'s own overture, which is unrelated and out of scope here) - just stop
  selecting it for `MainMenu`/`Welcome`.
- **Bringing it to the game screen.** `Ui::drawShellFrame` (where `field_.draw(...)` is
  called today) is not currently invoked for `Screen::Game` - the board HUD
  (`Ui::buildGameHud`) draws the board and its labels directly with no background decoration
  at all. Add a *much* quieter call into the same `DepthField` - fewer bodies, lower alpha,
  likely wireframes only (no piece icons, which would visually compete with the real pieces
  on the board) - behind the board. This must never distract during play: start it low-alpha,
  get a second opinion (literally - show a capture to the user) before tuning it brighter.
- **Determinism.** `DepthField` already documents itself as reading no clock it cannot be
  given ("the shell has to draw the same frame twice... so nothing here may reach for a clock
  or a global random source") - keep that invariant for the new shapes; they are driven by
  the same `t`/seed the existing bodies already use.

**Tests.**

- A render test that each of the five wireframe shapes draws a non-degenerate, non-empty set
  of line segments at a few sample `t` values (a smoke test, not a pixel-perfect golden - this
  is 2-D decoration, not board state).
- A differential/consistency test that the background is identical across two calls with the
  same `t`/seed (determinism, same discipline as every other animation in this codebase).
- A validation-clean `--shot --screen menu` and a validation-clean `--shot` of an ordinary
  game screen with the new quiet background on.
- A contrast/legibility check: with the game-screen background on, the existing piece/board
  theme contrast test (`tests/unit/view/test_theme.cpp`) must still pass - the ambient
  background must not erode the thing that test protects.

**Acceptance.**

1. The main menu's background reads as "the game's own exotic geometries, quietly" rather
   than "rainbow confetti" - torus, Klein bottle, Möbius strip, cube and hypercube wireframes
   drifting through the depth field, coloured from the existing cold/seam palette.
2. The quintic is no longer the main menu's dedicated object.
3. The game screen has a subtle version of the same background, which a player can look at
   for a full game without it becoming distracting (this is a judgement call - get the user's
   sign-off on the tuning before calling it done, the same way GHOST/the chase camera needed
   several rounds of "it still looks wrong" before landing).
4. `tools/test.sh --build render` and the app-tagged suite stay green; the theme contrast
   test is unaffected.

**Risks and non-goals.** Not a new rendering subsystem - everything here stays inside
`deco.cpp`'s existing 2-D draw-list approach, deliberately, since that is what makes it cheap.
Not a per-variant background (every screen gets the same five-shape vocabulary regardless of
what is being played - a `t6` game does not get a louder background than a `standard` one).
If the game-screen version cannot be tuned subtle enough to avoid distracting during real
play, ship it cinema-mode-only (or capture-only) rather than shipping something that hurts the
moment-to-moment experience for the sake of a background.

### Status: M18.4 built (2026-10-04, opencode)

The confetti is gone. The background is now the game's own exotic geometries as thin
wireframes in the theme's cold seam palette, the quintic object is retired from the main
menu, and the board screen has a much quieter copy in its margins.

- **The shape vocabulary.** Five `WireShape` drawers live in `deco.cpp` alongside
  `drawTesseract`, using the same local `Camera`/`rotateX`/`rotateY` 2-D projection and
  adding nothing to the Vulkan pipeline: torus, Klein bottle (the figure-eight immersion),
  Möbius strip, cube, and the 4-cube. The tesseract's vertex math is now shared with the
  Settings screen's `drawTesseract` (`tesseractVertices`), so the two cannot drift apart.
  They are self-contained parametrisations rather than calls into the board's own surface
  functions: `kleinSurf`/`shellTube` are internal to `overture_scene.cpp`'s anonymous
  namespace and carry board-scale constants a background has no use for, and this item is
  explicitly ambient decoration rather than a second board renderer. The quintic/`t6` is
  deliberately not in the vocabulary. `drawWireShape` is exposed so the shapes can be
  smoke-tested without a display.
- **The field's colour.** The `kPastels`/`kPastelsDark` arrays are deleted. Bodies now
  sample `view::seamRampColor` and mix it 34% into the page's own ground (`soot` on the
  light theme, `ink` on the candlelit one), so a wireframe reads as a faint cold hue and
  sits *under* the interface instead of glowing through it. The existing "candlelit field
  is muted and dark" test is unchanged and still passes - the console field's raw vertex
  RGB stays under 0.35 luminance, which the dark-ground mix guarantees.
- **The body vocabulary and count.** Every non-piece body is now one of the five
  wireframes; the piece icons are kept (`i % 4 == 3` - the game appearing in its own
  background was never the problem). The menu field dropped from 120 bodies to 56: a
  wireframe reads as more complex than a filled triangle, so fewer, heavier bodies. The
  main-menu object is folded to `Deco::None` (`decoForScreen`), retiring the quintic
  cross-section; `Deco::Manifold`/`drawManifold` are untouched for any later use.
- **The game screen.** `Ui` gained a `boardField_` (`DepthField(true)`): 24 bodies,
  wireframes only (a second set of pieces drifting behind the real ones competes with the
  thing being played), born on a wider annulus (`radius` 1.30-2.30 rather than 0.62-1.77)
  so they stay out towards the frame, drawn at 0.80 opacity. It is advanced in `Ui::tick`
  and drawn first in `buildGameHud`, behind the labels.
- **A note on "behind".** The interface is recorded into the ImGui pass, which the renderer
  composites *after* the Vulkan board - there is no way to put a draw-list layer literally
  behind the board geometry. The wider quiet-field ring is what keeps it out of the board's
  footprint instead: a before/after difference image of the game screen (`build/m184_check/`)
  shows bodies only at the frame's edges and nothing over the board. The menu field is
  unaffected by this because no board is drawn under it.
- **Tuning choices (all by eye, all retunable).** Menu: 56 bodies, wireframe alpha
  `(0.18 + 0.24·breath)·fade`, piece icons at 0.34 fill / 0.22 outline. Board: 24 bodies,
  wider ring, 0.80 opacity. Line width 1.2 px. Shape grids: torus 14×8, Klein 16×8,
  Möbius 20×4, cube and tesseract exact. These are judgement calls - the exact body count,
  opacity and ring width can be moved without touching the mechanism.
- **Tests.** Three new `[render]` cases: each `WireShape` emits a non-empty, non-degenerate
  figure at `t` = 0, 0.5, 2.0; two fields advanced by the same clock emit byte-identical
  positions and colours (the determinism contract); and the quiet field draws wireframes
  and emits nothing at all when asked for piece bodies. The existing draw-list properties
  (alpha-zero emits nothing, turn reaches the camera, zoom scales) still pass.
- **Deviations.** (1) Self-contained parametrisations rather than reusing the board
  surfaces, for the reason above. (2) The game-screen background is a faint overlay kept
  clear of the board by geometry, not literally behind it, for the compositing reason above.
  (3) `withPolygons` was renamed `withShapes` on `DepthField::draw`, and an `opacity`
  parameter added; the menu and board fields share one code path.
- **Verification.** `tools/test.sh --build render` green (109 cases, 4.8M assertions) and
  `--build app` green (59 cases); the ten `gui-*` screen captures pass validation-clean,
  including `gui-menu` and `gui-board` with the new background; `tools/precommit.sh` green
  (format, build, arch, unit, property). Menu and game captures are byte-identical on
  re-run (`cmp`), and the visuals are in `build/m184_check/`: the menu shows cold
  torus/Klein/Möbius/cube/tesseract wireframes where the pastel polygons used to be, and
  the game screen's version is visibly subtler and stays off the board.
- **Open.** Acceptance criterion 3 asks for the user's eye on the game-screen tuning; the
  mechanism is correct and restrained, and the numbers (24 bodies, 0.80 opacity, 34% hue
  mix) are the retuning knobs if it reads as too faint or too present in motion.

### Revision: the quintic returns as a sixth shape, clutter pushed further out, real
### per-shape rotation (2026-10-04)

Three pieces of feedback from actually using the redone background, after M18.4 shipped.

**1. The quintic is back - as clutter, not as the centrepiece it was.** The original
direction ("having the quintic is too much") was about the *dedicated, dominant* object the
old `Deco::Manifold` drew - a dense checkerboard quilt occupying its own stacked pane. That
is still correct and stays retired. What's wanted now is narrower: fold the quintic in as a
**sixth `WireShape`**, subject to the exact same treatment as the other five - one body
among many, same size range, same faint alpha, same edge-biased placement (§3 below). A
quintic cross-section drawn as a sparse wireframe grid, the size of a torus or a tesseract
among the other clutter, is a different thing from the quintic as the screen's one big
object; the first was "too much," there is no reason the second is. If this reading is
wrong - if what's wanted is closer to the old centrepiece after all, just redrawn more
quietly - say so and this gets rescoped; the spec below assumes the "sixth shape" reading.

Build: add `WireShape::Quintic` to the enum in `deco.hpp`; `buildWireShape`'s new case
draws a sparse grid of the quintic's own parametrisation (`render::quintic(k1, k2, x, y)`,
already shared with the `t6` overture and the settings-screen decoration - reuse it,
do not re-derive the surface) at a coarse enough resolution to read as a handful of line
segments, not a dense mesh - this is a background body, not a second overture. Both
`DepthField` populations (menu and the quiet board field) pick uniformly from all six
shapes now, not five.

**2. Push the clutter further from the centre, and keep it there through its own motion.**
Today's placement (`DepthField::respawn`) draws a body's `radius` once, uniformly, from
`0.62..1.77` on the menu field - that is the *average* distance from centre, but the
`radiusV`-driven "breathe" (`std::sin(clock_ * b.radiusV + b.radiusPhase) * 0.10f`, added to
`radius` every frame in `advance`) lets a body swing closer to centre than its own spawn
radius for part of its cycle, and `0.62` is already not very far out. Two changes:

- Raise the floor - the menu field's `radius` range moves outward (something in the
  neighbourhood of the board field's own `1.30..2.30`, or wider; tune by eye, the mechanism
  is what matters), so a body's *resting* position is already clear of the centre.
- The breathing amplitude must not be able to carry a body back into the centre it was just
  moved out of - either shrink `radiusV`'s amplitude so the swing is small relative to the
  new floor, or clamp the breathed radius to never fall below some minimum distance from
  centre regardless of phase. Pick whichever is the smaller change against today's code.

The board (quiet) field's ring already starts further out (`1.30..2.30`) and already has to
stay clear of the board itself (not just a vague "centre") - re-check it against the same
breathing concern, since its margin for error is tighter (the board, not just empty page,
sits behind the centre).

**3. Real per-shape rotation, not one angle wearing two hats.** Every body's orientation
today is `drawWireShape(..., b.spin, b.spin * 0.55f, ...)` - `yaw` and `pitch` are both
*derived from the same single value*, so a body does not tumble through 3-D orientation
space, it traces one fixed path through it (an ellipse, in effect) no matter how long it is
watched. "4-D hypercube rotation, torus tumbling" wants each shape rotating with its own
character, independently in more than one axis:

- Give each `Body` independent yaw and pitch rates (two velocities instead of one `spinV`),
  so a body's orientation genuinely wanders through 3-D rather than retracing one path -
  this alone fixes the torus/Klein/Möbius/cube cases.
- The tesseract is a special case worth keeping special: `drawTesseract` (the Settings
  screen's own decoration) already turns a 4-cube in **two independent 4-D planes**
  (`xw` and `yz`, at different rates) rather than a single 3-D spin, which is the thing that
  makes a hypercube read as a hypercube and not just a cube. `buildWireShape`'s `Tesseract`
  case currently likely borrows the same single-angle treatment as the other five (confirm
  by reading it) - if so, give it the same two-independent-plane treatment `drawTesseract`
  already has, rather than leaving it the odd one out doing less than its own reference
  implementation.
- Determinism is unaffected by any of this: more independent rates per body is still a pure
  function of the field's own clock and seed, the same contract every other animation here
  already holds.

**Tests.** A test that two bodies of the same shape with different (seeded) yaw/pitch rates
diverge in orientation over time (today's single-`spin` coupling would make this fail - it's
the regression test for item 3). A test that a body's distance from centre, sampled across
a full breathing cycle, never drops below the new floor (item 2). A smoke test that
`WireShape::Quintic` draws a non-empty, non-degenerate set of segments (item 1), and that
the differential test against `derivedSurfaceAt`/`quintic()` the overture system already
uses still agrees - reuse that oracle rather than hand-deriving a second one.

**Acceptance.** A fresh menu capture shows a quintic-shaped wireframe among the other five,
sized and faded like the rest, not as a dominant object; bodies visibly keep clear of the
exact centre through their whole motion, not just at spawn; a short clip (or a few captures
a couple of seconds apart) of the menu shows the tesseract performing its own two-plane
turn and at least one other shape tumbling on an independent axis rather than repeating a
fixed loop.

### Status: M18.4 revision built (2026-10-04, opencode)

The quintic is back as the sixth wireframe, one body among many; the clutter starts further
from the centre and can no longer breathe back into it; and each body now tumbles on two
independent orientation rates rather than one shared angle.

- **The quintic body.** `WireShape::Quintic` is a sparse grid of the shared
  `render/quintic.hpp` formula - the same `quinticPoint` the `t6` overture collapses onto -
  walked over all 5x5 `(k1, k2)` patches at 2x2 cells each (200 segments, comparable to the
  torus's 224), so it reads as a body-sized tangle rather than a quilt. Both `DepthField`
  populations now pick uniformly from all six shapes (`nextFloat * 6 % 6`). `Deco::Manifold`
  stays retired from the main menu exactly as before.
- **Containment.** The menu field's spawn annulus moved from `0.62..1.77` to `1.30..2.50`
  (the quiet field's `1.30..2.30` is unchanged); on top of that, `advance` clamps the body's
  actual distance from centre to `kFieldMinRadius = 1.20` after the breathing offset and the
  frame's vertical squeeze, so the trough of the cycle can never carry a body back inside the
  floor it was born outside of. `1.20` is deliberately the tightest distance the quiet field
  ever shipped at its spawn minimum, so the board screen's clearance is not loosened.
- **Per-shape rotation.** `Body::spin/spinV` is replaced by independent `yaw/yawV` and
  `pitch/pitchV`; `drawWireShape` feeds both to the camera for torus/Klein/Mobius/cube. The
  tesseract is the exception the spec asked for: its two rates now drive `tesseractVertices`'
  two 4-D planes (`xw` and `yz`), with the camera parked at `drawTesseract`'s fixed
  `pitch = 0.35`, so it performs the same two-plane 4-D turn as the Settings screen's own
  object instead of merely spinning a baked projection in 3-D.
- **Tests.** Three new `[render]` cases in `tests/render/test_deco.cpp`: the quintic's
  segments are non-degenerate and every endpoint is exactly a `quinticPoint` sample on the
  body's own coarse grid (the differential against the shared oracle); two same-shape bodies
  wander off the old `pitch == 0.55 * yaw` line by different amounts (which the old single
  `spin` makes impossible); and every body in both fields stays at or outside
  `kFieldMinRadius` across 720 s (over two of the slowest breathing cycles). The existing
  "each wireframe body is a real drawing" case now covers six shapes. The containment test
  was checked against a temporarily-disabled clamp and failed as expected (closest
  `1.136 < 1.200`), so it is not vacuous.
- **Exposed for tests.** `wireShapeSegments(WireShape, yaw, pitch)` (with the public
  `WireSegment`), `DepthField::bodies()` and `kFieldMinRadius`; none is on the drawing path.
- **Verification.** `tools/test.sh --build render` green (114 cases, 4.85M assertions) and
  `--build app` green (173 cases total across both tags); `gui-menu`/`gui-welcome`/`gui-board`
  validation-clean; `tools/precommit.sh` green (format, build, arch, unit, property).
  Captures in `build/m18_rev_check/`: the menu (`--screen menu`) shows a quintic tangle among
  the torus/Klein/Mobius/cube/tesseract bodies, sized and faded like the rest, and the board
  capture keeps its quiet field at the frame edges with nothing over the board.
- **Tuning choices (all by eye, all retunable).** Menu ring `1.30..2.50`, quiet ring
  `1.30..2.30`, breathe amplitude unchanged at `0.10`, containment floor `1.20`, quintic grid
  2x2 cells per patch. The floor is the knob if a future field wants more clearance.

### Correction: wrong quintic, and it didn't work either (2026-10-04)

Two things wrong with "the quintic is back as a sixth wireframe shape," found only once a
fresh report ("still missing quintic in the main menu") forced an honest look rather than
trusting the earlier screenshot.

**Wrong reading.** "Still missing" meant the *original* dedicated quintic object
(`Deco::Manifold`, the one the main menu carried since before any of M18 - retired by the
first M18.4 pass on the direction that a big stacked centrepiece was "too much"), not a new
sixth clutter shape. The spec above explicitly flagged this as an assumption and asked to be
corrected if wrong; it was wrong. Restore `decoForScreen`'s `MainMenu`/`Welcome` mapping to
`Deco::Manifold`, exactly as it was before the first M18.4 pass retired it -
`drawManifold`/`Deco::Manifold` were never touched by any of this work, only unselected, so
this is a one-line revert of the mapping, not new code.

**It also didn't work as clutter.** Independent of the misreading, the sixth-shape build
itself does not achieve what it was asked to: zoomed into an actual rendered body, the
`WireShape::Quintic` tangle reads as noise, not as a recognisable shape, unlike the other
five. Root cause: the five original shapes are each *one continuous parametric surface*, so
even as bare line segments they stay legible as a ring, a strip, a box. The quintic is not
one surface - it is 25 separate local patches (`(k1, k2)` each 0-4), each in its own
position in 3-space, all pushed through *one* 2-D camera with no fill and no depth sorting
between patches. `drawManifold`'s own version only reads as a quintic because area fill and
light/dark checkering organise those 25 patches into a visible quilt; stripped to pure line
art at clutter scale, what is left is 200 segments from 25 unrelated little grids
superimposed with nothing showing which lines belong together. This was not caught before
landing because the differential test only checks that each segment endpoint sits on the
real surface - a true and necessary property, and not the same thing as the body reading as
a shape to a person looking at it. No test in this codebase currently checks "is this
legible," nor should one try to - this is exactly the kind of thing that needs eyes, which
is why the earlier status note's visual claim ("shows a quintic tangle... sized and faded
like the rest") should have been scrutinised harder before being written, not merely
screenshotted and described.

**Fix: remove `WireShape::Quintic` from the clutter vocabulary entirely.** Back to five
shapes (torus, Klein, Mobius, cube, tesseract), `nextFloat * 5 % 5`. Delete the `Quintic`
enum value, its `buildWireShape` case, and the differential test that exercised it (the
containment and rotation-divergence tests from this same revision are unaffected - keep
them, they tested something real and still do). The quintic's rightful place in this file is
back where it started: the one big, dedicated `Deco::Manifold` object on the main menu, not
a background shape among six.

**Tests.** A test that `decoForScreen(MainMenu)`/`decoForScreen(Welcome)` return
`Deco::Manifold` again (the inverse of whatever test, if any, pinned the `Deco::None`
retirement - update or remove it to match). A test that `DepthField`'s wire-shape selection
only ever produces the five remaining `WireShape` values (no stray `Quintic` reachable by
any seed). The containment (`kFieldMinRadius`) and rotation-divergence tests stay exactly as
they are - re-run them to confirm they still pass with `Quintic` removed, since they should
not have depended on the shape count, but confirm rather than assume.

**Acceptance.** The main menu shows the original quintic object again, in its original
place and scale (same as any commit before this M18 round touched `decoForScreen`); the
background clutter is five shapes, not six, and none of it is the quintic; `tools/test.sh
--build render` green.

### Status: quintic corrected back to the centrepiece (2026-10-04, opencode)

The dedicated quintic object is restored to the main menu and the sixth wireframe is gone
from the clutter; the field is back to five shapes.

- **The mapping.** `decoForScreen`'s `MainMenu`/`Welcome` case returns `Deco::Manifold`
  again. Nothing in `drawManifold`/`Deco::Manifold` had been touched, so this was the
  one-line revert the correction called for.
- **The clutter.** `WireShape::Quintic` is deleted from the enum (back to Torus, Klein,
  Mobius, Cube, Tesseract, `Count`), its `buildWireShape` case is gone, and `respawn`
  selects with `nextFloat * 5.0f % 5`. The test-only scaffolding that existed solely for
  the sixth shape went with it: `wireShapeSegments`, `WireSegment` and the
  `kQuinticWireCellsX/Y` grid constants are removed, and the quintic differential test in
  `tests/render/test_deco.cpp` is deleted. The containment (`kFieldMinRadius`) and
  two-orientation-rate tests are untouched and still pass, as the correction predicted.
- **Tests.** Two new `[render]` cases: `decoForScreen(MainMenu)`/`decoForScreen(Welcome)`
  return `Deco::Manifold`, and the field's wire selection stays inside the five remaining
  values across 1000 s of respawns. `each wireframe body is a real drawing` now asserts five
  shapes. Written and shown red first (the mapping returned `Deco::None`, `Count` was 6),
  then green.
- **Verification.** `tools/test.sh --build render` green (115 cases, 4.92M assertions);
  `--build app` green (68 cases); `tools/precommit.sh` green (format, build, arch, unit,
  property). `--shot --screen menu` is validation-clean and byte-identical on re-run.
- **The capture, actually looked at** (`build/m18_corr_check/menu.png`, plus crops). The
  left pane shows the original filled checkerboard quilt — the wavy-boundaried quintic
  cross-section — as one large, legible, roughly centred body with its piece icons on it;
  it is unmistakably the centrepiece, not a wireframe tangle. The surrounding clutter reads
  as coherent line drawings: a nested wireframe cube (top-left and top-right) and two
  ring/grid surfaces off to the right that stay legible as a torus/Klein/Mobius family
  figure — no 25-patch superimposed quintic noise anywhere, matching the code (no quintic
  value is reachable).
- **Open.** None; this restores the pre-M18-round `decoForScreen` behaviour with the
  five-shape field from the first M18.4 pass.

### Correction: the in-game ambient field draws in front of the board, not behind it (2026-10-05)

Reported directly: "clutter is also drawn on top of chessboard in-game." Confirmed by
reading the actual render-pass order, not just the UI code - the comment that introduced
`boardField_` in M18.4 ("A quieter field for behind the board") was never true at the
Vulkan level, and the bug has been there since that commit.

**Root cause.** `BoardRenderer::record` (`src/render/board_renderer.cpp`) does the 3-D
board in one `vkCmdBeginRendering`/`vkCmdEndRendering` block (instances, shadows,
flourish - all opaque or depth-tested), then, *after* that block ends, opens a second one
purely for `overlay(cmd)` - see the comment right above it, "The interface is drawn last
and never blurred." `overlay` is `Ui::record`, which calls
`ImGui_ImplVulkan_RenderDrawData` on the *whole* frame's `ImGui::GetDrawData()` - every
ImGui draw list built during `Ui::build`, menus and HUD and both ambient fields alike.
`ImGui::GetBackgroundDrawList()` only means "behind other ImGui windows"; it says nothing
about the already-rendered 3-D board, which sits in a wholly separate, earlier pass this
one knows nothing about and composites over unconditionally. On the main menu this is
invisible because there is no 3-D board pass to be behind - the quintic centrepiece and
the menu's own field (`field_`) are the only thing in frame. In `Game`, `boardField_` has
real board pixels under it, and whatever wireframe bodies happen to drift across the
board's on-screen footprint paint straight over it, because nothing establishes "behind"
as anything other than a comment.

**Fix shape.** Give `boardField_` an actual early position in the frame, inside the *same*
render pass the board itself uses, before the board's own draw calls - true painter's-
algorithm "behind," not a clip-rect approximation (the board's on-screen footprint moves
with camera framing and shape, so a fixed exclusion region would be chasing a moving
target and would still break on some camera angle or shape).

1. `BoardRenderer::render`/`record` (`src/render/board_renderer.hpp/.cpp`) gain one more
   callback parameter alongside the existing `overlay`, call it `background` - same
   signature, `const std::function<void(VkCommandBuffer)>& background = {}`. Invoke it
   right after the first pass's `vkCmdBeginRendering(cmd, &ri)` and *before* the board's
   own `vkCmdSetViewport`/`vkCmdSetScissor`/instance draws - with its own full-frame
   viewport/scissor set first (the same `full`/`fullScissor` the late overlay pass
   already computes further down - hoist that computation, or compute it twice, whichever
   reads cleaner), then restore the board's own viewport/scissor afterward exactly as now
   before the instance draws proceed. The board's opaque/depth-tested draws then simply
   overwrite whatever the background pass put down where the board actually sits - no new
   depth or stencil logic needed, ordinary painter's algorithm within one render pass.
   Default-constructed (empty) `background` must change nothing about existing callers
   that don't pass one.

2. The background callback has to carry *only* `boardField_`'s content - not the rest of
   the frame's ImGui draw data, which must stay on the existing late `overlay` path
   unchanged (the HUD, panels, and - importantly - the coordinate-label strokes drawn via
   `ImGui::GetBackgroundDrawList()` in `buildGameHud`, which belong *on top* of the board,
   labelling it, not behind it). `ImGui::GetBackgroundDrawList()` is shared frame-wide, so
   it cannot be the vehicle for this split: both the field and the coordinate labels write
   into it today, in that order, with no way to submit "only the first part of this list,
   early." Instead, give `Ui` a second, dedicated `ImDrawList` that exists only to carry
   `boardField_`'s strokes - built with `ImGui::GetDrawListSharedData()` (the standard way
   to own an `ImDrawList` outside a window), filled by calling `boardField_.draw(...)`
   against *that* list instead of `ImGui::GetBackgroundDrawList()` when building the Game
   screen's HUD, and cleared/rebuilt once per frame like any other per-frame draw state.
   Check whichever exact Dear ImGui version this repo has vendored/pinned (grep the Nix
   dev shell's imgui package, or search the build's include path under `nix develop .#gfx`
   - do not guess the `ImDrawData`/`ImDrawList` field names from memory) for the right way
   to wrap one standalone `ImDrawList` in a minimal `ImDrawData` and hand it to
   `ImGui_ImplVulkan_RenderDrawData` directly - this is a documented, supported pattern for
   multi-pass ImGui rendering, not a hack, but the exact struct shape is version-specific.
   Add a small `Ui::recordBackground(VkCommandBuffer cmd)` (alongside the existing
   `Ui::record`) that does this wrapping and gets passed to `BoardRenderer::render`'s new
   `background` parameter from `src/gui/main.cpp`'s `renderFrame` lambda, next to where
   `drawUi`/`overlay` is already passed.
3. `field_` (the main menu/library's own field) is **not** part of this fix and must not
   move - it stays exactly as it is, on `ImGui::GetBackgroundDrawList()`, in the existing
   late overlay path. Nothing about the menu's already-verified-correct quintic/clutter
   look should change. This correction is scoped to `boardField_` or on the `Game` screen
   only.

**Tests.** This is exactly the kind of thing the existing `[render][gpu]` tests in
`tests/render/test_offscreen_render.cpp` are built for - real `OffscreenTarget` +
`BoardRenderer::render` + `readPixels()`, no synthetic mocking of the Vulkan pass. Write a
test red first: render a frame with a normal opaque board (any variant already used
elsewhere in that file is fine) and a `background` callback that fills the *entire* frame
with one solid, saturated colour nothing else in the scene produces (e.g. pure magenta,
`AddRectFilled` over the whole viewport). Read back a pixel known to sit inside the
board's opaque footprint (reuse however nearby tests in the same file pick a known-good
board pixel) and assert it is the board's own colour, not blended with the fill - today,
before the fix, that assertion is false because there is no `background` hook at all yet
(won't compile until the parameter exists) and, once it exists but is wired to the old
late-only path, the pixel would come back magenta-tinted. Then a pixel clearly outside the
board's footprint (a frame corner) should show the fill colour, confirming the callback
did run, just in the right order. Add a second, narrower test that actually drives
`Ui`/`boardField_` (build a `Game`-screen frame through the normal `Ui::build` +
`recordBackground` + `record` path, the way the GUI really calls it) and checks the same
ordering holds for the real field, not just a synthetic fill - a unit test only on the
synthetic callback would leave the real `Ui` wiring unverified.

**Acceptance.** `--shot --screen game` (or interactively watching a game) never shows a
wireframe body painted over the board's own pixels, at any camera angle or shape, because
it no longer can - the board draws after and over the field within the same pass, not
because any body happens to miss it. `field_`/the main menu is pixel-for-pixel unchanged.
`tools/test.sh --build render` green.

### Status: in-game ambient field drawn behind the board (2026-10-05, opencode)

`boardField_` now really is behind the board: it is painted at the top of the board's own
render pass, before the board's viewport and draws, so the board's opaque geometry covers
it. The menu's `field_` and the coordinate labels are untouched - still on
`ImGui::GetBackgroundDrawList()` in the late overlay pass - and the menu is byte-identical.

- **The spec's literal step 1 does not survive MSAA.** The board's first pass targets
  `target.msaaColorView()` (4x), while Dear ImGui's one pipeline is built at
  `info.MSAASamples = VK_SAMPLE_COUNT_1_BIT` because the late overlay draws into the
  *resolved* single-sample image. Invoking the overlay in the board pass - which is the
  shape of the specified fix - fails validation with
  `VUID-vkCmdDrawIndexed-multisampledRenderToSingleSampled-07285/07286` (pipeline 1 sample,
  colour and depth attachments 4). Reproduced by temporarily moving the `overlay(cmd)`
  call up, then reverted.
- **What shipped instead: a second, multisample-matched pipeline owned by `Ui`,** so the
  field can be drawn into the 4-sample attachment using the *same shader* as ImGui.
  `src/render/shaders/imgui_field.vert/.frag` are byte-for-byte copies of the backend's
  `glsl_shader.vert/.frag`, compiled by the existing `glslc`/`EmbedSpirv` step and built
  with `rasterizationSamples = OffscreenTarget::kSampleCount`, ImGui's own `ImDrawVert`
  vertex layout and blend state, a descriptor-set layout with one combined image sampler,
  and the same 4-float vertex push-constant range. The pipeline layout is deliberately
  *compatible* with the backend's (`vkCmdBindDescriptorSets`/`vkCmdPushConstants` are
  issued by the backend against its own layout), so `ImGui_ImplVulkan_RenderDrawData` can
  drive it. `Ui::recordDrawList(cmd, list)` wraps the list and calls
  `ImGui_ImplVulkan_RenderDrawData(&data, cmd, boardFieldPipeline_)`; the overlay keeps
  passing `VK_NULL_HANDLE` and the backend's single-sample pipeline, unchanged.
- **`ImDrawData`/`ImDrawList` shape (ImGui 1.91.4, vendored via nix).** `ImDrawData::CmdLists`
  is an `ImVector<ImDrawList*>` with `CmdListsCount` kept separately; the wrapper uses the
  public `ImDrawData::AddDrawList(list)` helper, which maintains both and the totals. The
  dedicated list is `std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData())`, and each
  frame it is reset with `_ResetForNewFrame()`, `PushClipRectFullScreen()` and
  `PushTextureID(ImGui::GetIO().Fonts->TexID)` - without those it is clipped to nothing.
  `Ui::build` resets it first thing every frame, so cinema and every non-board screen leave
  it empty and `recordBackground` is a no-op there.
- **`BoardRenderer::render`/`record` gained a `background` callback** alongside `overlay`
  (default-constructed = no change), invoked immediately after the first pass's
  `vkCmdBeginRendering` with the frame's full viewport/scissor (hoisted above the pass),
  before the board's own viewport/scissor and instance draws. `src/gui/main.cpp` passes
  `Ui::recordBackground` at both call sites, next to the overlay. `info.ImageCount` went
  `2 -> 4` because the backend's vertex/index buffers round-robin over it and there are now
  two `RenderDrawData` calls per frame with two frames in flight.
- **Tests.** `tests/render/test_offscreen_render.cpp` gained two. The first renders an
  ordinary board with a `background` callback that clears the whole attachment to magenta
  with `vkCmdClearAttachments`: a pixel solidly inside the board stays the board's colour
  (and a corner turns magenta), while the same clear submitted as `overlay` paints over the
  board - no ImGui context needed, it pins the ordering itself. The second drives the real
  `Ui`/`boardField_`: builds a `Game`-screen frame through `Ui::build` and compares the real
  field submitted by `recordBackground` against the board alone and against the same field
  submitted late, over 24 field frames, requiring the field to reach the frame and the
  board to occlude strictly more of it than the late path. Both wrote their frames to
  `render-captures/field-{none,behind,over}.ppm`.
- **Results.** `tools/test.sh --build render` green (117 cases, 4.92M assertions);
  `tools/precommit.sh` green. The menu `--shot` is **byte-identical** before and after the
  change (`cmp` on the two captures), which is stronger than visually unchanged.
- **The capture, actually looked at.** `field_over.png` is the bug: light wireframe rings
  and lines lie straight across the board's dark squares and the pieces. `field_behind.png`
  is the same moment with the fix: the board is clean and the rings are cut off by its
  silhouette - the left ring disappears behind the near edge instead of crossing it. The
  raw frame has no board-crossing field body at the default `--shot` clock, which is why the
  occlusion-specific frames come from the real-`Ui` test; `field-{behind,over}` are the same
  field frame rendered both ways.
- **Open / caveat.** The field is now part of the board's 4x MSAA attachment, so its
  anti-aliasing is resolved at 4x rather than ImGui's own 1x fringe - a hair smoother than
  the menu's copy of the same field. That is the only look change, it is confined to
  `boardField_`, and it is a consequence of drawing in the board's pass rather than a
  separate decision; no ADR, because it does not constrain anything outside this correction.

## M18.5 Audio v1

**Want.** The game is completely silent. `Settings` already carries `volumeMaster`/
`volumeMusic`/`volumeEffects` with the comment "the game has no sound yet; these are kept for
when it does" (`src/app/settings.hpp`) - the intent was always there, nothing was ever built.
This is very likely the single biggest "tech demo" tell of everything in this document, and
also the biggest lift - scoped here as a v1 that needs **no licensed or sourced assets at
all**, so it can ship without anyone's input beyond this spec.

**What it is.** A small audio layer using SDL3's own audio API (already a hard dependency -
`sdl3` is already in `flake.nix`; no new library), playing a handful of procedurally
synthesised sound effects at the moments that already exist as discrete engine events (a
move lands, a capture resolves, a UI action fires) - no music in v1, since a good loop needs
an actual composed/sourced asset and licensing judgement that belongs to the user, not to a
procedural generator.

**Build.**

- **New layer, `src/audio` (new CMake target, per `cb-new-module`), sitting beside `render` in
  the layer map** (it depends on nothing below `base`, and nothing above it depends on it
  except `app`/`gui` - update `AGENTS.md`'s layer table in the same commit per the repo's own
  rule 8). Uses `SDL_OpenAudioDeviceStream`/`SDL_PutAudioStreamData` (SDL3's audio API) to
  play short PCM buffers on demand; no streaming, no file I/O, no codec.
- **Procedural SFX, generated once at startup into static buffers**, not shipped as asset
  files: a handful of short (<200 ms) envelope-shaped tones/noise bursts - a soft click for
  UI navigation, a slightly longer tone for a piece landing, a sharper/lower one for a
  capture (pairs naturally with M18.1's visual flourish - trigger both from the same event),
  a distinct one for check. Simple additive sine/noise synthesis with an ADSR-style envelope
  is enough; this is deliberately not sound design, it is "something plays instead of
  nothing," tuned for clarity over character. Keeps this v1 entirely free of licensing
  questions and asset-pipeline work, consistent with the engine's existing "no magic assets"
  discipline (`src/assets`'s own pieces are integer-vector data, never imported models).
- **Hook points**, each a one-line call into the new layer from code that already knows the
  event happened - do not invent new event plumbing:
  - A move lands / a capture resolves: `Session`/`Game`'s existing move-application path.
  - A UI navigation action (`Ui`'s button press handling) - reuse whatever single chokepoint
    already exists for "a button was activated," if one does; add one if every button handles
    its own click independently today.
  - Check: wherever `shell.showsCheckWarning()` (already driving the visual vignette,
    `drawCheckEdges`) becomes newly true, not every frame it stays true.
  - The MainMenu/NewGame deco field is wisely left **alone** here - no ambient audio loop in
    v1, since that is squarely "music," out of scope.
- **Mixing.** `volumeMaster * volumeEffects` scales every SFX; the stored `volumeMusic`
  setting is read but has nothing to multiply yet (no music in v1) - leave it wired through
  so turning it on later is a one-line change, not a second settings pass.
- **Respect the settings screen's own honesty.** `ui_menus.cpp` line ~1070 currently shows
  "the game has no sound yet; these are kept for when it does" - remove that line (or narrow
  it to "no music yet," if music genuinely stays out) once SFX actually play, so the settings
  screen does not contradict what just shipped.
- **Headless/capture safety.** `--shot`/`--clip`/any headless path must not require an audio
  device - SDL3's audio init must degrade gracefully (log and continue silent) when no device
  is available, exactly the pattern the renderer already uses for a missing font ("a missing
  font is never fatal - the interface falls back and looks plainer").

**Tests.**

- A unit test that the synthesis functions produce non-silent, non-clipping, correctly-sized
  PCM buffers for each SFX (deterministic - same inputs, same samples, pinned the way every
  other generated-content path in this codebase is).
- A test that `volumeMaster = 0` (or `volumeEffects = 0`) produces silence (every sample
  zero, or the play call is skipped outright) rather than merely a quieter buffer, since a
  player expects "off" to mean off.
- A headless-path test (or an explicit assertion in the existing `--shot`/`--clip` tests) that
  capture commands still succeed with no audio device present (CI has none).
- Manual/listening verification is unavoidably part of accepting this one - note in the
  status section whether it was actually listened to, not just unit-tested.

**Acceptance.**

1. A move, a capture, a check and a UI navigation action each produce a short, distinct sound
   in normal interactive play.
2. The volume sliders actually do something; `volumeMaster`/`volumeEffects` at 0 is silent.
3. No asset files are added; everything audible in v1 is generated in code.
4. `--shot`/`--clip`/headless tests are unaffected by audio being present.
5. The settings screen's "no sound yet" note is updated to match reality.

**Risks and non-goals.** Not music - a v1 with no loop is an accepted, explicit scope cut,
not an oversight; music is a follow-up once an actual asset is sourced or composed and its
licence is confirmed, which is the user's call, not code. Not spatial/3-D audio - stereo or
mono SFX triggered by event, no positional mixing tied to camera/board position. Not a sound
designer's pass - procedural placeholder tones are explicitly "good enough to not be silence,"
and a follow-up with authored sounds (once sourced) replacing the procedural ones is expected
and fine; record that distinction in the status note so a later pass knows which sounds are
placeholders.

### Status: M18.5 built (2026-10-04, opencode)

Sound effects are in: a move, a capture, a check and a UI navigation action each play a
short procedurally synthesised tone, scaled by the two volume sliders - and the game is
silent, never fatal, when no audio device is present.

- **New layer `src/audio` (L93).** A `cb_layer` target between `io` and `app`, depending
  only on `base`; `app`, `render` and `gui` all link it, and the layer is added to AGENTS.md's
  map in the same change (rule 8). SDL3 is linked **privately**, so no SDL header reaches
  `app`/`render` - the public surface is the `Sound` enum, the pure synthesis functions and a
  `Sink`/`play` pair. A build without SDL3 (the `nix flake check` package) compiles the layer
  with `CB_HAVE_SDL_AUDIO` undefined and `Mixer::create` then returns null: silence, not a
  build error. Verified by compiling `endspielzero_audio`/`endspielzero_app` in a no-SDL tree.
- **The sounds.** Four buffers, 40-190 ms, generated once at startup and cached: `Click` (a
  soft 1.4 kHz blip), `Move` (a 300 Hz body with a short noise knock), `Capture` (170 Hz plus
  more noise - lower and sharper than a landing), `Check` (two rising notes, 700 then
  1050 Hz). Each is normalised to a fixed intended peak (0.28-0.70 of full scale) so no sum
  of components can clip, and the noise comes from a fixed-seed `base::Rng`, so a buffer is
  the same bytes on every run and machine. These are **placeholders**; a later pass with
  sourced or composed sounds is expected to replace them.
- **Hook points, no new plumbing.** `Session::playChecked` - the one path a move is applied
  through - plays `Capture`/`Move` from `Move::isCapture()` and, if `game_->inCheck()`
  afterwards, `Check`. `render::widgets::rowControl` is the single chokepoint every menu row,
  button and cycling setting already passes through, so the navigation click is one call
  there; a disabled control reports no press and stays silent.
- **Mixing.** `audio::effectsGain(master, effects)` is the only gain. A gain of zero skips the
  buffer outright rather than scaling it to a near-zero noise floor, and `applyGain` returns
  literal zeros (the test proves it). `volumeMusic` is read by the settings screen and has
  nothing to multiply yet - the stated no-music scope cut, wired so a loop is one more factor
  later.
- **Settings screen.** The AUDIO sliders are enabled for Master and Effects; only Music stays
  greyed, and the caption now reads "sound effects play; music is not in this build yet".
  `Settings`' own audio comment is updated to match.
- **Headless/capture safety.** `audio::Mixer::create` is called by the interactive `main`
  alone; the capture branch returns before it, so `--shot`/`--clip` and every headless test
  never touch SDL audio. When there *is* a device it is opened, but `create` logs and returns
  null on any failure (no device, no stream, no resume) and `play` with no sink is a no-op -
  the state every unit test runs in. A new `gui-capture-no-audio` CTest runs a capture with
  `SDL_AUDIODRIVER=endspielzero-does-not-exist` as the regression guard.
- **Tests.** Five `[unit][audio]` cases: the four buffers are correctly sized (< 200 ms),
  non-silent and below full scale, deterministic and pairwise distinct; `effectsGain` and
  `applyGain` make either slider at zero literal silence while full gain is identity and half
  is half; `play` forwards to an installed recorder and is a safe no-op without one; and a
  `Session` driven through a move, a capture and a checking move announces `Move`, `Capture`
  and `Move`+`Check` respectively. `tools/test.sh --build render` green (109 cases);
  `--build app unit` green (279 cases); `ctest --preset dev -R gui-` 31/31 (the 30 existing
  captures plus the new one); `tools/precommit.sh` green (format, build, arch, unit,
  property).
- **Not listened to.** Nobody - and no machine - heard this output in the pass that wrote it,
  and could not: the build environment has no audio device. It ships as correct-by-construction
  and unit-tested code (deterministic samples, correct sizes, no clipping, zero means silence,
  no device means silence) whose actual audio quality is **unjudged**. The manual listening
  acceptance calls for is still outstanding.

## M18.6 A morph between the flat board and its shape, and view controls that cannot
## contradict each other

**Want.** Two things, found while playing with the SHAPE toggle (M17): switching it snaps
instantly between the flat board and the fully-formed shape, where everywhere else a
transition like this (a screen change, a variant selection in the library) morphs - that
inconsistency is exactly the kind of rough edge that reads as unfinished. Separately, the 2D/
3D button and the SHAPE button can be set to states that make no sense together: SHAPE is
offered while the board is flat 2D (a donut has no 2D rendering - there is nothing to show),
and switching to 2D while SHAPE is on silently does *something* rather than being refused.

**It already exists, one layer over.** The library/menu overture
(`render::derivedOvertureScene`/the hand-authored scenes, `src/render/overture_scene.cpp`)
already does precisely this morph: "pure in `t`...it opens on the flat board, forms the
surface the variant's identifications imply, and unwinds." The underlying surface functions
(`kleinSurf`, `shellTube`, and their siblings) take continuous roll parameters (`th`, `ph`)
where 0 is the flat board and the shipped value is fully formed - "the flat box falls out of
it as `th = ph = 0`" (`shellTube`'s own comment). `PlaySurface` - the *play* board's version
of the same shapes - calls these at the fully-formed value **always**;
`render::SurfacePose`'s own doc comment says so explicitly: "The surface functions still
*carry* a geometric eversion - the overtures use it - but the play board does not take it."
So the morph this item wants is not new math, it is **exposing a knob `PlaySurface` already
has access to but never uses**.

**Build.**

- **Add a `formed` field to `SurfacePose`** (`src/render/overture_scene.hpp`), 0..1, default
  1 (so every existing caller that does not set it keeps today's always-fully-formed
  behaviour - `nix flake check`'s goldens must not move). Thread it into `PlaySurface::build`/
  `buildStacked`'s calls to the underlying surface functions as the `th`/`ph` (or equivalent)
  scale, exactly the way `pose.openness`/`pose.twist` are already threaded through today -
  this is adding one more pose component to a mechanism that already passes several.
- **A target the front end eases toward**, exactly `Settings::geometryEvert`'s own pattern
  ("the `INVERT` button... sets a **target** the front end eases... to, so the pose stays a
  pure function of its number"): a new `Settings::geometryFormed` (default 1) is the live,
  eased value actually fed to `SurfacePose::formed`; toggling SHAPE sets a target (1 to show
  the shape, 0 to return to flat) and the interactive loop eases the live value toward it over
  a fixed duration - reuse whichever easing helper `geometryEvert`'s own transition already
  uses rather than inventing a second one. Still a pure function of a number, still
  `--shot`-able mid-transition (state `--formed 0.5` the way `--evert`/`--slide` already can
  be stated for a capture).
- **What "flat" looks like at `formed = 0`.** This needs an actual decision, not just a
  number: does the board at `formed = 0` look like the *ordinary flat board* (cells at their
  lattice positions, same as `flatView`/non-surface rendering), or like the overture's own
  "opens on the flat board" pose (which may differ slightly - check `shellTube`/`kleinSurf`'s
  `th = ph = 0` output against the ordinary flat-board layout before assuming they coincide).
  If they do not naturally coincide, the honest fix is adjusting the surface function's
  `formed = 0` case to match the real flat board exactly (a torus's `th=0` state should **be**
  the ordinary 8x8 grid, not merely resemble it), since the morph has to land exactly on the
  thing SHAPE-off already draws, or toggling twice would leave the board in a visibly
  different place than where it started - pin this with a test, not an eyeball check.
- **Mutual exclusivity (the simpler half).** `widgets::button` already takes an `enabled`
  parameter - every call site in `src/render/ui.cpp`'s view-control row currently passes a
  hardcoded `true` for it. Change two call sites (~line 1274 and ~line 1286 at the time of
  writing - grep for the `"2D"`/`"3D"` and `"SHAPE"` button calls, they sit beside each
  other): the 2D/3D button's `enabled` becomes `!shell.settings().geometryView` (greyed out
  while the shape is showing - the player sees why: the state that would conflict is right
  there, lit), and the SHAPE button's `enabled` becomes `!session.flatView()` (greyed out in
  2D, since there is nothing to show). Do **not** hide either button - a control that
  disappears is a worse surprise than one that is visibly present but refuses a click; keep
  both visible, just non-interactive, matching how `button`'s existing `enabled=false` state
  already renders elsewhere in the interface.
- **What happens to an in-flight morph if the now-disabled control is attempted anyway?**
  It cannot be, by construction, once the buttons are gated - but a scripted `--script`
  capture or a settings-file edit could still set both `flatView` and `geometryView` true at
  once. Decide and document one resolution (flat wins, since a shape has no 2D rendering to
  fall back to) rather than leaving the combination undefined; a short test should pin it.

**Tests.**

- A property test that `PlaySurface` at `SurfacePose::formed = 0` produces the same cell
  positions as the ordinary flat (non-surface) board layout, for `torus`/`klein`/`mobius` -
  the "lands exactly on what SHAPE-off already draws" guarantee above, pinned.
- A property test that `formed = 1` is pixel/position-identical to today's always-fully-
  formed output (the existing shape-view goldens must not move - this is the backward-
  compatibility check for the new default).
- A test that `--formed 0.5` (or whatever the capture flag ends up named) produces a
  validation-clean, in-between frame - the shape recognisably mid-morph, not a half-built
  mesh or a crash.
- A UI test (or a documented manual check, matching how other `widgets::button` `enabled`
  states are verified elsewhere) that the 2D/3D button is disabled exactly when
  `geometryView` is on, and SHAPE is disabled exactly when `flatView` is on.
- A test pinning the `flatView && geometryView` resolution decided above.

**Acceptance.**

1. Pressing SHAPE morphs the board from flat to its shape over a short, visible transition
   instead of snapping; pressing it again morphs back, landing exactly on the ordinary flat
   board.
2. The 2D/3D button is greyed out while SHAPE is on; the SHAPE button is greyed out in 2D -
   neither can be used to reach a contradictory state.
3. Every existing shape-view and flat-view golden/capture is unchanged (the new default of
   `formed = 1` reproduces today's output exactly).
4. `tools/test.sh --build render` and `--build app` green.

**Risks and non-goals.** Not a new animation system - this reuses the overture's own roll
math and the `geometryEvert`-style eased-target pattern, both already established. Not every
pose control needs the same morph treatment (slide/invert/ghost can stay as they are unless
a later note says otherwise) - scope this to the flat<->shape transition and the two buttons
named above. If `formed = 0` turns out not to coincide with the flat board for some shape
after investigation, fixing that is in scope (it is load-bearing for acceptance criterion 1),
  but changing the *shipped, fully-formed* shape's appearance is not - `formed = 1` must stay
  pixel-identical to today.

### Status: M18.6 built, with one sub-part deferred (2026-10-04, opencode)

SHAPE now rolls the board between flat and its shape through the overtures' own roll
parameters, and the two view controls refuse the states that contradict each other. The
morph machinery, the settings/flag plumbing and the gating are complete and tested; the
exact landing of the morph's flat end on the ordinary lattice board is deferred, and why is
recorded below.

- **The morph is a pose knob already there.** `SurfacePose::formed` (0..1, default 1) scales
  `derivedSurfaceAt`'s roll parameters: `th`/`ph` for `tube`/`torus`, `th` for the ribbon,
  and `th`/`pinch`/`ph` for the Klein cross-section, with `shellTube`'s two rolls for
  `torus3d`. `PlaySurface` reads it through the pose it already threads everywhere, and the
  front end feeds it from a new `Settings::geometryFormed` (live, transient, not written to
  disk) that the interactive loop eases toward `geometryView` with the same `dt * 2` ramp
  `geometryEvert` uses. `--formed` states it for a capture and `--shot` reproduces.
- **The frame is continuous, and forming was front-loaded.** Scaling the rolls from zero
  alone made the torus jump: `tube`/`kleinSurf`/`shellTube` apply the ring-radius cheat (2.2,
  2.05, 10.5) the instant `ph > 0`, so `ph = 0+` scales the ranks out by that factor while
  `ph = 0` does not. The overtures ramp the radius in with the roll for exactly this reason;
  the morph now ramps it too (`ramp(hi)` returns the shipped constant exactly at
  `formed = 1`). Before the ramp, `formed = 0.01` moved a seat 4.3 units; after, under 0.1.
- **Formed = 1 is bit-for-bit.** Every product is `kTau * 1.0f`, the ramp returns the
  shipped constant at 1, and the renderer's surface gate is unchanged for a formed board.
  `torus`, `klein`, `torus3d` and `hyper4` `--geometry` captures are byte-identical to a
  HEAD build, except for an 18x10-pixel box on the button row - which is the intended M18.6
  gating (the 2D/3D button greyed out), not the board. `torus --shot` is byte-identical.
- **Gating (the simpler half).** `widgets::button`'s `enabled` is now
  `!shell.settings().geometryView` for the 2D/3D button and `!session.flatView()` for SHAPE;
  both stay visible, only inert. `--flat` (new, capture only) was added to state the 2D token
  view for a `--shot`, since no script or settings path could reach it; the captures confirm
  SHAPE greyed in 2D and 3D greyed in SHAPE.
- **Flat wins when both are set.** A settings file or script can still set `flatView` and
  `geometryView` at once; the renderer's surface path is now gated on `!options.flat`, so the
  ordinary board is drawn (a shape has no 2D rendering). The `optionsFor` camera gate states
  the same, and a no-GPU test pins that the surface mesh is not built.
- **Deferred: `formed = 0` is the overture's flat pose, not the lattice board.** The surface
  functions' `th = ph = 0` embedding places cell `(f, r)` at `(f, r)` with the ranks
  *reversed* relative to `view::layout`: same files, same plane, opposite handedness. The two
  flat states cannot be joined by a continuous morph without a rank collapse (a linear blend
  of the two collapses every rank onto one line), and making them coincide means either
  collapsing the board or changing the shipped `formed = 1` shape, which the spec forbids. So
  pressing SHAPE still starts from a rank-mirrored flat and rolls to the shape; the roll is
  smooth but the very first frame is a mirror. The relationship is pinned in
  `test_play_surface.cpp` ("the morph's flat end is the flat board, reflected in rank")
  rather than hidden, and joining the two flat embeddings is left to a later change (the
  honest scope-down the spec allowed for this exact sub-part).
- **D >= 3 (torus3d) morphs, roughly.** `shellTube` carries the same two rolls, so the
  threaded `formed` does roll the nested shells; the intermediate frames are validation-clean
  but visually busy (per-cell frames are recomputed from neighbours as the box turns), so the
  torus3d transition is the least polished part of this item. `hyper4` has no roll parameter
  (a bounded 4-cube) and is honestly a snap - there is no continuous roll to expose.
- **Tests.** `[render]`: formed = 1 is bit-identical for every shape; the flat end is pinned
  as the reflected lattice board; the morph is continuous and non-degenerate over a 100-step
  sweep (`stepU`/`stepV` positive, bounded seat travel) for torus/Klein/Moebius; the
  flat-wins resolution builds no surface mesh; `surfacePose` carries `geometryFormed`. A new
  `gui-formed` CTest renders `--geometry --formed 0.5` and fails on any validation message.
- **Verification.** `tools/test.sh --build render` (106 cases, 4.76M assertions) and
  `--build app` green; `ctest --preset dev -R gui-` 30/30 (all existing shape/flat captures
  validation-clean, determinism intact). `tools/precommit.sh` green (format, build, arch,
  unit, property). Captures under `build/m186_check/`: the torus morph at formed
  0/0.01/0.1/0.25/0.5/0.75/1 is a recognisable continuous roll, the 2D and SHAPE buttons are
  each greyed in the other's state, and `torus`, `klein`, `torus3d` and `hyper4`
  `--geometry --formed 1` match a HEAD build's board bit-for-bit (only the intended 18x10-px
  button box differs), while `torus --shot` is byte-identical.

### Revision: a speed control for the morph (2026-10-04)

The morph itself (above) was accepted; the one thing missing is a way to change how fast it
runs. The rate is currently a bare constant - the interactive loop eases `Settings
::geometryFormed` toward its target with "the same `dt * 2` ramp `geometryEvert` uses,"
fixed, no setting behind it.

**Build.** Add `Settings::geometryFormSpeed` (default `1.0`, same shape as the existing
`Settings::shapeMorphSpeed` that already paces the M17.19 shape-follow choreography's own
board-morph - read how that one is wired into its settings-screen slider and its `dt`
multiply, and copy the pattern rather than inventing a second one). Multiply the `dt * 2`
ramp rate by this value wherever `geometryFormed` is eased toward `geometryView`'s target.
Expose it as a slider on the settings screen beside (or near) `shapeMorphSpeed`, since they
are the same *kind* of control for two different morphs and a player should find them
together, not in unrelated corners of the menu. Clamp to a sane range (`shapeMorphSpeed`'s
own `0.25..4.0` clamp is a reasonable default to copy) so a value of 0 can't freeze the
board mid-roll with no way back - or, if a near-zero value is wanted for dramatic slow
transitions, make sure the eased value still reaches its target eventually rather than
stalling asymptotically forever.

**Tests.** A test that doubling `geometryFormSpeed` halves the number of frames needed for
`geometryFormed` to reach a given value (or the equivalent property for whatever the actual
easing shape is - linear ramp, exponential approach, etc.) - pin the *relationship*, not one
magic number. A test that the default (`1.0`) reproduces today's pacing exactly, so this is
additive and not a silent behaviour change for anyone who never touches the new slider.

**Acceptance.** The settings screen has a working morph-speed slider; setting it away from
`1.0` visibly changes how long the SHAPE toggle takes to settle, in both directions (faster
and slower); the default is unchanged from M18.6's shipped pacing.

### Status: morph speed control built (2026-10-04, opencode)

The SHAPE toggle's flat<->shape roll is now paced by `Settings::geometryFormSpeed`, exposed
as a slider beside the shape-follow `shapeMorphSpeed` one; the default is bit-identical to
M18.6's shipped pacing.

- **The ramp is one pure function.** `render::advanceFormMorph(formed, target, dt, formSpeed)`
  (`src/render/play_surface.{hpp,cpp}`) moves `geometryFormed` toward its 0/1 target by
  `dt * kFormMorphRate * formSpeed`, snapping exactly on the target. `kFormMorphRate = 2.0`
  is the old bare `dt * 2`; the interactive loop (`src/gui/main.cpp`) now calls it instead of
  the inline `min`/`max` step. `geometryEvert` keeps its own unscaled `dt * 2`. Nothing on
  the `--shot`/`--clip` path reads the speed, so captures are untouched.
- **The M17.24 snap frame is preserved at every speed.** `wasMorphing`/`nowMorphing` bracket
  the actual step - whatever its size - so `morphSettling` is still true on the real frame
  the roll lands, and the camera snaps exactly there at any multiplier. The pure helper is
  what the loop's own comparison sees, so there is no second definition of "settled".
- **The setting mirrors `shapeMorphSpeed` end to end.** Default `1.0`, `geometry_form_speed`
  in `settings.cpp`'s load/save, `std::clamp(..., 0.25f, 4.0f)` in `sanitize`, and a new
  "SHAPE morph speed" slider in `ui_menus.cpp` added to the moved-detection list. The clamp
  up from zero is the spec's own guard: the ramp is linear and snaps on the target, so it
  cannot stall asymptotically even at the floor.
- **Slider placement (a small deviation).** It sits immediately *after* the move-camera
  `EndDisabled` block rather than inside it beside `Morph speed`, because that whole block is
  greyed when `cameraMode == "off"` (the default) and the SHAPE toggle must be paced whether
  or not a move camera is following. It is still adjacent, under the camera caption, with its
  own caption so the two "morph speed" rows are not confused.
- **Tests, against the pure step (live-loop-only caveat).** The spec's acceptance is about how
  long the live toggle takes to settle, which `--shot`/`--clip` cannot observe (the same
  caveat M17.24's verification recorded), so the tests pin the computation the live loop runs.
  A `[render]` case drives `advanceFormMorph` at a clean fixed `dt`: doubling the speed halves
  the frames to cross (2 -> 1, 0.5x -> 4), the per-frame step is exactly proportional to the
  speed below the clamp, the way back paces identically, and at `1.0` it equals the old inline
  `min(1, formed + dt * 2)` math frame for frame (bit-identical default). `[unit][app]` adds
  `geometryFormSpeed` to the settings round-trip and to the clamp test (0 -> 0.25).
- **Verification.** `build/dev/tests/endspielzero_tests '[render]'` 111 cases / 4.82M assertions
  green; `'[app]'` 59 cases green; `tools/precommit.sh` green (format, build, arch, unit,
  property). The settings screen was captured at 1000x2400 (`build/m18_form_check/`) and the
  new slider and caption render validation-clean next to `Morph speed`.
- **Deviations.** Only the slider's position relative to the camera gate, above. No behaviour
  change at the default; no capture-path change.

### Revision: four morph-quality problems, reported from actual play (2026-10-06)

Four separate issues in the flat<->shape morph, reported together from hands-on use. Two
have a confirmed root cause already (one of them a previously-deferred, documented
limitation); one is a real design problem with a concrete fix direction; one needs
investigation before a fix can be stated precisely. Each is its own numbered item below -
they touch different surface kinds and can be built and tested independently, though all
four land in the same handful of files.

#### 1. `formed = 0` is a 180-degree rank-mirror of the real flat board

Reported: "it reverses the board, rotates it 180 deg (i.e. on standard black and white
are swapped)." This is the **exact limitation M18.6 already identified and deliberately
deferred** - see "Deferred: `formed = 0` is the overture's flat pose, not the lattice
board" in that milestone's own status note above, and the test that pins it,
`the morph's flat end is the flat board, reflected in rank`
(`tests/render/test_play_surface.cpp:1953`). Restated precisely: the surface functions'
`th = ph = 0` embedding places cell `(f, r)` with the **ranks reversed** relative to
`view::layout`'s ordinary flat board - same files, opposite handedness - so the very
first frame of every SHAPE-off-to-on morph (`standard` included, since the ordinary 3-D
board shares the same flat embedding path) is a rank-mirror of the real board, which
reads exactly as "black and white swapped" before it un-mirrors over the rest of the
roll. The original spec ruled out a naive fix: a linear blend between two *flat*
embeddings of opposite handedness has to pass through a degenerate state where every
rank collapses onto one line, which is worse than the mirror it would fix.

**A fix direction, not yet a full derivation.** A linear blend collapses because it tries
to resolve two mirror-image *flat* (2-D, in-plane) states without ever leaving the plane.
A continuous **rotation through the third dimension** does not have this problem: turning
a flat rectangle 180 degrees about an axis in its own plane (think a page turning) carries
every point through a smooth arc that is never degenerate, and lands exactly on the
mirrored arrangement with no collapse anywhere in between - because the intermediate
states are briefly 3-D (edge-on), not because any two points ever coincide. The shape
morph already lifts the board into 3-D as `th`/`ph` ramp up; the proposal is to spend part
of the *early* portion of the morph on this turn - the board rotating through its own
edge-on orientation to reach the handedness the curved shape actually needs - before or
alongside the ring/cross-section forming starts, rather than trying to resolve the
handedness mismatch in-plane. The exact mechanics (which axis, how much of the `form`
range it spends, how it composes with each shape's own `th`/`ph`/`pinch` ramp, whether it
is one shared stage or per-shape) need real investigation and iteration - this is
research-grade work, the same way M17.12's `torus3d_twist` shell was flagged as such, not
a mechanical fix. Investigate the turn-through-3-D direction first since it is the one
that avoids the known collapse problem by construction; if it proves unworkable, report
back with what was tried rather than shipping a partial fix silently.

**Tests.** The existing `the morph's flat end is the flat board, reflected in rank` test
is the thing to make fail (in the good way) once a real fix lands - it currently pins the
mirror as *expected*; once fixed, invert it to assert `formed = 0` matches
`view::layout`'s handedness exactly, for every shape in `kShapes`, and keep a record of
what changed in its comment rather than deleting the history. A new test (or several)
pinning whatever intermediate mechanism is built - continuity (no seat jumps further than
its neighbour's own spacing between adjacent `formed` samples), and that `formed = 1` is
still bit-identical to the shipped shape for every kind, the one invariant every other fix
in this revision must also preserve.

**Acceptance.** Pressing SHAPE from the ordinary board never shows a rank-mirrored frame -
not at `formed = 0`, not anywhere during the roll - for `standard` and for every glued
variant. `formed = 1` unchanged, bit for bit, everywhere.

#### 2. The Mobius ribbon snaps to its full stretch instead of easing into it

Reported: "it goes from 'square' board to 'stretched' strip instantly. It should stretch
it first instead." Root cause, confirmed by reading the code: `PlaySurface::build`
(`play_surface.cpp:202`) sets `pose.stretch = 0.5f` unconditionally - never scaled by
`pose.formed` - while `derivedSurfaceAt`'s `Band` case (`overture_scene.cpp:2668-2675`)
only ramps the ring angle (`kTau * form`) with the morph. The ribbon's width-to-length
ratio is therefore already at its final, fully-stretched shape on the very first frame;
only the curling into a loop animates.

**The fix.** Stage the two instead of running only one: `pose.stretch` itself needs to
ease in as part of the morph, and - per the report - it should complete *before* the ring
starts curling, not alongside it. Split `form` into two sequential windows (exact
proportions are a tuning call, by eye, the same as every other morph-pacing constant in
this file): an early fraction ramps `pose.stretch` from 0 to its target (0.5, the play
board's own constant - read, do not hand-pick a new number) while the ring angle stays at
0, and the remaining fraction ramps the ring angle from 0 to `kTau` while `stretch` holds
at its now-complete value. `PlaySurface::build` is the natural home for the split, since
it already owns `pose.stretch = 0.5f` and already threads `pose.formed` through
unmodified; whatever staging helper is written should be reusable by item 3 below, which
needs the same "sequential sub-ranges of `form`" shape for an unrelated set of
parameters - write it once.

**Tests.** A property/unit test driving `derivedSurfaceAt`'s `Band` case (or the staged
value `PlaySurface::build` now computes) across `form = 0..1`: the ribbon's own
width:length ratio (measured the way
`the morph's flat end is the flat board, reflected in rank` already measures per-axis
step sizes) is unchanged from 1:1 at `form = 0`, strictly increasing over the stretch
window, and the ring angle stays at (or very near) 0 until the stretch window completes.
`formed = 1` bit-identical, as always.

**Acceptance.** Watching the live SHAPE toggle on `mobius`, the ribbon visibly elongates
before it starts curling into its loop, not in one cut.

#### 3. Klein's single-parameter ramp fights itself - stage it like a real construction

Reported, in detail: "square board gets its bottom row 'stretched' among all of its
length, top 7 rows disappear. Then they suddenly reappear and the winding up is happening
through the insides of a 'torus'." Root cause: `derivedSurfaceAt`'s `Klein` case
(`overture_scene.cpp:2677-2681`) currently drives `th`, `pinch` and `ph` all off the
*same* `form` value simultaneously, while `tau` (the cross-section's twist, `pose.twist`)
is **never scaled by `form` at all** - it is applied at full strength from the first
frame. A circular cross-section is rotationally symmetric, so applying the twist to it is
invisible on its own - but `th`/`pinch` ramping together means the cross-section is
*not* circular for most of the morph, it is already partway folded into the figure-eight,
and a full-strength twist applied to a partially-folded, self-intersecting shape while the
ring is also still forming is exactly the kind of fighting-itself motion that reads as
"winding through the inside of a torus."

**The fix, per the report's own proposed structure - it names the right three stages.**
Split `form` into three sequential windows (reusing the staging helper from item 2), each
owning a disjoint set of `kleinSurf`'s parameters, held at their completed value once
their own window ends:

1. **Ring, plain tube.** `th` and `ph` ramp from 0 to `kTau` (an ordinary torus-like
   ring and a circular cross-section - the shape before any pinch or twist exists, reads
   as a plain tube forming, "without ends attaching" per the report, since neither the
   fold nor the twist has started).
2. **Fold.** `pinch` ramps from 0 to 1 while `th`/`ph` hold at `kTau` - the circular
   cross-section collapses into the figure-eight, "the seam fold[ing] in."
3. **Twist.** The `tau` argument passed to `kleinSurf` ramps from 0 to `pose.twist` while
   everything else holds - only now does the chi rotation have a non-symmetric
   cross-section to act on, so this is the stage where the identification actually
   becomes visible, "ends connecting."

At `form = 1` every window has completed, so `th = ph = kTau`, `pinch = 1`,
`tau = pose.twist` - exactly the values the current, unstaged call already passes,
confirming the shipped shape does not move.

**Tests.** A property test driving the staged parameters directly across `form = 0..1`:
`pinch` and the effective `tau` are both exactly 0 while `form` is within window 1;
`pinch` is strictly increasing and the effective `tau` is exactly 0 within window 2; the
effective `tau` is strictly increasing to `pose.twist` within window 3 while `pinch` stays
at 1. A visual/continuity check analogous to item 1's: no seat moves further between
adjacent `form` samples than its own neighbour spacing, so nothing "disappears and
reappears."

**Acceptance.** Watching the live SHAPE toggle on `klein`, the board reads as: a ring
forms first (plainly, no self-intersection visible), then the ring's cross-section folds
into the figure-eight in place, then the twist settles the two sides into their final,
connected alignment - three legible stages, not one tangled motion. `formed = 1`
unchanged.

**Status, items 2 and 3 (2026-10-06, opencode).** Built both, and only those two; items 1
and 4 are untouched.

- **One shared staging helper.** `render::stageForm(form, begin, end)` (`overture_scene.hpp`,
  defined in `overture_scene.cpp`) clamps `form` into `[begin, end]` and rescales it to 0..1
  within that window: exactly 0 at or before `begin`, exactly 1 at or after `end`, linear
  like every other ramp in the morph. Both items spend disjoint slices of `formed` through
  it rather than defining their own clamps.
- **Item 2 (Mobius ribbon).** `PlaySurface::build` no longer pins `pose.stretch` at the play
  value on frame 0. For `Band` it now sets `pose.stretch = kRibbonStretch *
  stageForm(form, 0, kRibbonStretchWindow)` and `pose.formed = stageForm(form,
  kRibbonStretchWindow, 1)`; every other warp takes `kRibbonStretch` unchanged. The play
  board's own constant moved to `play_surface.hpp` as `kRibbonStretch = 0.5f` (read, not
  reinvented); `kRibbonStretchWindow = 0.45f` is the tuning constant. So the ribbon spends
  the first 45% of the morph flat and elongating with the ring angle at zero, then curls.
- **Item 3 (Klein).** `kleinMorph(form, twist)` (`overture_scene.{hpp,cpp}`) is the one
  definition of the three windows: `ring = stageForm(form, 0, 0.40)` drives `th` and `ph`,
  `fold = stageForm(form, 0.40, 0.72)` drives `pinch`, `spin = stageForm(form, 0.72, 1)`
  drives the `tau` argument passed to `kleinSurf`, and it returns `{kTau * ring, fold,
  kTau * ring, twist * spin}`. `derivedSurfaceAt`'s Klein case passes those through;
  `pose.openness` keeps its own separate ramp (it is the hole's legibility cheat, not a
  construction stage). At `form = 1` every window is 1, so the call is bit-for-bit the
  unstaged shipped one.
- **Deviations.** Only the documented tuning constants and the choice of linear ramps (the
  existing morph's own ramps are linear; smoothstep would change the intermediate pacing).
  One tidy-up: `searchSlide`'s orientable probe used a bare `0.5f` for the board's stretch;
  it now reads `kRibbonStretch`, same value, one source.
- **Tests (test-first: all three went red against the pre-staging code).**
  `the Mobius ribbon stretches before it curls` measures the ribbon's own width:length from
  the seats' steps: 1:1 at `formed = 0`, strictly increasing and flat (all seat `z = 0`)
  through `kRibbonStretchWindow`, and the ring curls (>1 unit of `z`) after it.
  `the Klein morph is three legible stages` drives `kleinMorph` directly across 1000
  samples: exact endpoints; window 1 has `pinch == 0` and `tau == 0` with `th == ph`
  strictly increasing; window 2 has `tau == 0` and `pinch` strictly increasing; window 3
  has `pinch == 1` and `tau` strictly increasing to `twist`. The boundaries are discovered
  from the parameters themselves, so retuning the windows does not invalidate the test.
  `the Klein morph never jumps more than a cell` checks no seat moves further between
  adjacent `formed` samples than its own `max(stepU, stepV)` - nothing disappears and
  reappears.
- **Results.** `tools/test.sh --build render`: 128 cases / 4.99M assertions green.
  `tools/test.sh --build app`: 68 cases green. The existing
  `formed one is the shipped surface, bit for bit` and the morph-continuity sweeps stay
  green, so `formed = 1` has not moved.
- **Visual captures (looked at, not just taken).** `--clip` sweeps only the library
  overture's own `t`, so the play board's `formed` was sampled with repeated
  `--shot --formed X` (which does reach it). `klein --geometry` at X = 0.05 .. 0.40 reads as
  a plain tube forming and closing - a fan that curls into a ring, with no fold, no twist and
  none of the old "top rows disappear into the inside of a torus"; 0.40 is the closed plain
  ring, after which the fold and twist settle it. `mobius --geometry` at X = 0.0 .. 0.45
  stays a flat board and visibly elongates to the full stretched strip before any curl
  begins, then curls into the loop. Captures are under `build/m18_rev_check/items23/`
  (gitignored).

#### 4. The Klein seam's visual connection sits half a square from where it should

Reported: "current implementation is the best one so far, but the 'connection' is half-a-
square apart from where it is supposed to be" - at the shipped default (`kleinShift = 0`,
not the slide-drift issue the previous correction in this file already fixed). Not yet
root-caused. One hypothesis checked and ruled out while writing this spec: the pinch's
own self-intersection locus (where `kleinSurf`'s figure-eight crosses itself) lands
exactly at `u = 0` in the continuous embedding - `lemniscateAngle(0) = -pi`, and the
lemniscate curve's own point at `a = -pi` is the origin, the figure-eight's crossing
point - so the continuous surface function is not offset from the seam by half a cell.
The discrepancy more likely lives in how the *discrete* lattice is read against that
continuous locus - a cell spans `u` from `f / nx` to `(f + 1) / nx`, centred at
`(f + 0.5) / nx`, so anything that uses a cell's centre where the seam math expects its
edge (or the reverse) would show up as exactly this half-cell offset. Investigate before
fixing: compare, numerically (the same differential-sweep style the slide-drift
correction above used, not a screenshot), where `SeamMap`'s seam-face cells, the rendered
seam rail (`surfaceRail`), and the pinch's actual geometric crossing each place "the
seam," at the default `kleinShift = 0`, and report which two disagree and by how much
before writing the fix - a confirmed half-cell mismatch in a specific comparison, not a
guess.

**Tests.** Whatever differential check locates the mismatch should become the permanent
regression test, in the style of
`a Klein bottle's seam stays closed under a square shift and a slide`
(`tests/render/test_play_surface.cpp`) - red before the fix, green after, at the default
pose with no shift and no slide.

**Acceptance.** On `klein --geometry` at the shipped default, the seam rail
(`showSeams`) and the shape's own visible pinch/crossing line up on the same cell
boundary, not half a cell apart - confirmed by a crop, looked at closely, not just a shot
taken.

**Status, item 4 (2026-10-06, opencode).** Investigation only - **no production code was
changed**, because the comparison the item asks for shows no half-cell mismatch: the rail
is already on the crossing. The numbers, at the shipped default (`kleinShift = 0`,
`kleinTwist = 1`, `formed = 1`, `kCoverage = 0.9`, `kSurfaceSubdiv = 4`, 8x8 `klein`):

- **`SeamMap`** marks the file seam on files 0/7 (`Min`/`Max`, glued) and the rank seam on
  ranks 0/7. **`surfaceRail`** tints the patch corner at `u/v = 0.00625` (file-0 `i=0`,
  rank-0 `j=0`) and `0.99375` (file-7 `i=4`, rank-7 `j=4`) - `0.05` of a cell inside the
  seam, exactly `kCoverage`'s own inset, and the *patch corner*, not the cell centre. The
  figure-eight passes through its own origin (the pinch) at `u = 0`, `0.5`, `1`; `u = 0`
  is the file-7/file-0 lattice boundary - the same corner.
- **Offsets.** Rail corner to the crossing at the same rank: `0.0788` world = `0.086`
  cell = `4.5 px` = `0.14` square at 1920x1080. The two rims of one seam sit `0.17`-`0.28`
  cell apart - the `kCoverage` groove between adjacent patches. Nothing is near `0.5`
  cell. The spec's ruled-out hypothesis (the continuous pinch at `u = 0`) was right, and
  the discrete reading is right too.
- **A real half-cell bug, but not this one.** Locating the three did find a genuine
  cell-centre/edge confusion in `PlaySurface::nearestBoundary` (the M17.19 glide
  waypoint): it returns `pointAt(a.file + 0.5*step, ...)` where `pointAt`'s coordinate is
  an edge at integers and a centre at `f + 0.5`, so the waypoint lands on the *source
  seat's centre* instead of the shared edge. Measured on `torus` for a file step: `1.45`
  world from the true edge (ratio to source `0.75`; the true edge is exactly equidistant,
  `1.00`); its projection on the chord collapses onto the source centre (`-0.17`..`0.0`
  on `klein`, where the shared edge projects to `0.5`). The one-line `+ 0.5f` fix went
  red-then-green. But it also stops the torus rank glide in the M17.19 look-ahead test
  from clipping - because the buggy waypoint was putting the gliding piece *inside* the
  tube - so it perturbs the M17.20 anti-clip choreography and wants its own spec, not an
  item-4 drive-by. It is not read by the rail, so it cannot be the reported symptom.
- **Visual.** Captures under `build/m18_item4_check/` (gitignored): default, `--ghost
  0.55`, top-down, and an axis-isolation pass (file rail red, rank rail green) diffed
  against `showSeams` off. The file rail runs along the visible self-intersection neck and
  the rank rail's figure-eight crosses it exactly there - the rail and the pinch are one
  locus.
- **Tests.** No test changed on the green tree: `tools/test.sh --build render` 128 cases,
  `--build app` 68 cases, `tools/precommit.sh` all pass. The candidate regression test for
  the glide bug (waypoint projected into the middle half of the a->b chord, exactly
  equidistant on a torus) is recorded here rather than committed, since it pins the
  unlanded `nearestBoundary` fix.

### Correction: item 4's own methodology was wrong, twice, before the real check (2026-10-06, Claude)

Reported directly, after the status above: the user's own worked example ("edge `1234...`
meeting edge `8765...`, `1` connects to `7.5`, `2` to `6.5`") pins the half-cell offset
precisely to the rank seam's cell-to-cell correspondence - not what item 4's investigation
actually checked. Re-investigated directly, by hand, not dispatched.

**First wrong turn.** A nearest-3D-neighbour search among *seat centres* found what looked
like a real, large mismatch at every file, even at the already-verified-correct default
(`kleinShift = 0`, no slide) - until realised that on a self-intersecting immersion,
nearest-3D-distance among centres is not a reliable proxy for topological partnership:
unrelated parts of the surface pass close to each other near the pinch. Centres are also
simply the wrong thing to compare - they sit half a cell inset from the seam on both
sides, which is not where two cells actually touch.

**Second wrong turn, closer but still wrong.** Comparing patch *corners* instead (the
actual wrap-boundary locus) still showed the same ~1.3-1.6 world-unit "mismatch," even
checked against `derivedSurfaceAt` directly with no `PlaySurface` involved at all - until
realised the comparison itself was corner-mismatched: cell `f`'s low file corner is the
*same shared point* as cell `(f - 1)`'s high file corner, not cell `f`'s own partner's low
corner - a reflection reverses orientation within each cell. Comparing low-to-low instead
of the correct low-to-high gave a meaningless large distance at *any* pose, including the
default, which is exactly the false alarm both this investigation and the original item-4
status note above ran into from different angles.

**The real check, corrected.** Cell `f`'s low corner against its true partner's (read
from `SeamMap`, not assumed) *high* corner: exhaustive nearest-neighbour search across
every file, at `kleinShift` 0/1/3 and slide 0/1/1.75 laps - every single file's nearest
corner is its exact topological partner, at every combination tried, with only a small
(~0.1-0.33 world unit) residual consistent with the deliberate `kCoverage` inter-square
gap near the pinch's own curvature. The slide-drift correction's `su` compensation holds
up under this much closer scrutiny. No code change - the geometry is sound - but the
weak-tolerance gap this exposed in the existing regression coverage is real, so
`a Klein seat's low corner is nearest its true partner's high corner, under any shift and
slide` (`test_play_surface.cpp`) replaces the ad-hoc investigation with a permanent,
properly-tight check exercising exactly this.

**What this does not explain.** The user's reported "half a square," and worse after the
slide-drift fix landed ("3.5 squares"), is not reproduced by this check at the geometry
level, at any pose tried. Candidates not yet ruled out: something specific to the live
interactive session (mouse-drag accumulation, timing against when the fix actually landed
locally) rather than a fresh `PlaySurface::build`; the camera, which the user flagged
separately as a possible issue of its own; or a visual-only artifact around the pinch that
a corner-coincidence check would not catch even if the geometry is technically correct.
Needs a specific repro from the live session, not another round of offline investigation,
before going further.

**Also done:** `Settings::kleinShift` ("the stride") is now signed, `-7..7` instead of
`0..16`/slider `0..7`, so a counter-clockwise square is set directly (e.g. `-1`) rather
than winding through the positive range to reach the same square the long way
(`+7` on an 8-wide board) - reported directly alongside the above. `su`'s compensation
formula is unchanged by sign; verified in the corrected check above, which includes a
negative-adjacent case implicitly (shift 1 and 3 both exercise the same arithmetic a
negative value would).

Verified: `tools/test.sh --build render app` green (199 cases, 5.03M assertions, Vulkan
validation on); `tools/precommit.sh` green.

**Scope note for all four items.** These are independent; land and verify each on its own
rather than batching them into one large, hard-to-review change. Items 2 and 3 share a
staging helper and are the most mechanical; item 1 is explicitly research-grade and may
take more than one round; item 4 needs its investigation done and reported before any
code changes, the same discipline the slide-drift correction above used.

## Dependencies and ordering

M18.1-M18.3 and M18.6 touch only `src/render`/`src/view`/`src/gui` and are independent of
each other and of M18.4/M18.5 - any order, any subset. M18.4 is independent of everything
else in this file. M18.5 is a new layer and the most self-contained of the six (new module,
event hooks, no shared state with the others) but is also the largest - do it last, or in
parallel with the others if capacity allows, since nothing here blocks on it.

## Risks and non-goals (the whole milestone)

- **This is not a reopening of M4.7's scope boundary in general** - "no shadows, no post-
  processing" stays the right default for the renderer's hot path; every item here is either
  a 2-D overlay trick (M18.3), an extension of the existing instanced/vertex-colour mechanism
  GHOST and M17.21 already proved out (M18.1/M18.2), pure 2-D draw-list decoration that never
  touches the Vulkan pipeline (M18.4), or a new, separate, optional layer (M18.5). None of it
  is "add a deferred renderer" or "add PBR materials."
- **Subjective tuning is expected to take rounds.** Several items here (shadow strength, the
  cinema grade, the ambient background's opacity and body count, the SFX envelopes) are
  judgement calls that will need the user's eye, not a test assertion, to call "right" - plan
  for iteration, the way the chase camera's elevation/width/twist settings each needed several
  passes in M17.
- **Do not let polish regress determinism.** Every item above restates it, deliberately:
  `--shot`/`--clip` byte-identical reproduction is the thing the whole Wave 1 marketing
  pipeline depends on, and it is far easier to break by accident while adding "juice" than
  while adding a camera.
