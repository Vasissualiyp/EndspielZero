# M13 — Generic Variant Overtures

**Goal.** Every variant animates on the library screen — including one that did not exist
when the build was made. Today the New Game animation is a closed compile-time set:
`app::Overture` is an enum (`src/app/overture.hpp:15`) and `overtureScene` is a `switch`
over it into hand-built per-variant scene functions (`src/render/overture_scene.cpp`,
75 KB). That is the right way to make *fourteen* scenes stunning and the wrong way to make
the *thousandth*. M13 adds a **derived** overture, built from the variant's own
`VariantSpec`, and keeps the hand-authored scenes as overrides for the marquee variants.
It is the content-scale answer that makes M9 (Workshop) work: a user-authored variant
cannot be added to a compile-time enum.

**Exit condition.** A variant that exists only as a data file — never named in the C++ —
animates on the library screen: it opens on the flat board, forms the surface its geometry
implies, shows one of its own pieces making one of its own wrapped moves, and unwinds. No
scene function is written for it, and its seam closure is asserted by the same property
test the hand-authored surfaces pass.

---

## The one idea: the overture is a function of the spec, the way the camera is a function of the move

M11 generalised the camera by observing that it only needs `MovePath` (topology as tags),
`layout` (dimension as world positions) and a policy record. M13 does the same for the
overture: a scene is three derived things and one authored narrative.

1. **The surface is derived from the identifications.** The variant already declares
   which axes are periodic, which are mirrored, and where the twists are (ARCH §4.1). A
   *small, finite catalogue* of closed-surface builders — tube, torus, band, Klein
   figure-eight, mirror box — is selected from that pattern. `kleinSurf`'s pinch is not
   lost: it becomes the `Klein` builder, reached by every variant whose gluing is a rank
   flip, instead of by one variant's name.
2. **A D ≥ 3 overture is the layout, extruded.** Above 2-D there is no faithful embedding
   to draw, and there never was — `cube5`/`hyper4`/`t6` are already stylised. The honest
   generalisation is to animate the board into the *very grid `view::layout` already
   computes* (`src/view/layout.hpp:79`): flat, then spaced along the depth axis, then
   fanned into a lattice of slices. That is derived from `DimSpec`/`ViewConfig`, needs no
   new shape, and is the same picture the game will show.
3. **The demo move is the variant's own.** Pick a signature piece and a signature
   destination by running the real `expandAtom` and the real `tracePath` — the same call
   M7.1.4 makes for the editor's move preview and M11 consumes for the camera. A torus
   variant shows a rook wrapping; a Klein variant shows it come back reversed; an atomic
   variant shows its capture. Nothing about the move is authored per scene.
4. **The arc is authored once, for all of them.** Open flat → dwell → form → show the
   move → unwind. The timings are the shared `OverturePlayer` cycle
   (`src/app/overture.hpp:65`), unchanged.

The single invariant that keeps this honest: **derived surfaces obey the same closure
arithmetic as the hand-authored ones, and the same test checks both.** `overtureSurfaceAt`
(`src/render/overture_scene.hpp:118`) exists precisely because `klein` joining its rank
edges with the file reversed was got wrong silently once. A derived surface is not allowed
to re-open that hole. **[INVARIANT]**

---

## M13.0 Preconditions and placement

Depends on M11 (the shared camera-shot planning; the derived arc reuses it) and on the
engine's `expandAtom`/`tracePath` (M4/M5, done) for the demo move — **not** on M7's editor;
when M7 lands it shares the same move-preview logic, but M13 does not wait for it. Feeds
M9: it is the reason a Workshop variant animates at all. It is **Wave 1, third in the
roadmap's release sequence** (after M11 and the M12 export half), because every variant
animating is both polish and the prerequisite for M9's user content. Were M13 absent, a
Workshop variant already falls back to the standard board (`overtureFor`,
`src/app/overture.hpp:38`), which is acceptable, not broken — so M13 buys quality, not
correctness.

---

## M13.1 The derived signature

One new value type turns a resolved variant into the few facts an overture needs:

```cpp
namespace cb::app {

/// What the geometry looks like to an overture. Derived, never authored.
struct OvertureSignature {
  SurfaceKind surface{SurfaceKind::Flat};  // Tube|Torus|Band|Klein|MirrorBox|FlatGrid
  std::uint8_t dims{2};
  bool temporal{false};       // a turn or multiverse axis exists
  bool hasForcedCapture{false};
  bool hasRadiusEffect{false};  // a rule the burst can illustrate
};

OvertureSignature overtureSignature(const VariantSpec& v) noexcept;
}
```

`SurfaceKind` is derived purely from the identification pattern: no gluing → `FlatGrid`;
one periodic axis → `Tube`; two → `Torus`; a periodic axis with a sign flip → `Band`;
a flip plus a periodic axis → `Klein`; a mirror identification → `MirrorBox`. The
mapping is a pure function with a table test over every shipped variant.

**`Overture` the enum stays** — it becomes the *hand-authored override* key, and
`overtureFor(name)` keeps returning it for the marquee variants. What changes is the
selection: a variant with a bespoke scene uses it; everything else is built from its
signature. `overtureScene` gains a sibling:

```cpp
/// The scene for a variant with no hand-authored one. Pure in `t`, like its sibling.
OvertureScene derivedOvertureScene(const VariantSpec& v, float t, const view::Theme& theme);
```

---

## M13.2 The surface catalogue (the sacred part)

Six builders, each a pure `(u, v) -> OvVec3`, generalising the `app::Overture` surface
functions of the same name. The seam-closure property is the acceptance test, and it is
stated as arithmetic per surface, not as an image:

- `Torus` — a point on the ring of rings; closure `(0, v) == (1, v)` and `(u, 0) == (u, 1)`.
- `Band` — a Möbius band; closure `(u, 0)` lands on `(1, u)` reversed in the normal.
- `Klein` — the figure-eight cross-section; `(u, 0)` lands on `(1 - u, 1)`, and the test
  asserts the file reversal is *real* rather than a shifted circle. **This is the test
  that caught the original bug; it must catch the same bug in the derived path.**
- `Tube`, `MirrorBox` — the trivial cases.
- `FlatGrid` — not a closed surface; above 2-D the "surface" is the extruded `layout`, and
  the closure property does not apply (the current code returns the flat board here).

`tests/render/test_overture_scene.cpp` already states each variant's gluing as arithmetic;
M13 extends it so that the *derived* surface for a variant equals the hand-authored one
where both exist. That differential test is what makes "derive it" safe: the shipped
scenes become the oracle for the generated ones.

---

## M13.3 The derived demo move

From the resolved variant:

- **Choose the piece.** The first non-royal mover with an atom that wraps or travels an
  extra axis; fall back to the first piece. Deterministic and documented.
- **Choose the move.** Enumerate that piece's moves from the start position, prefer one
  that crosses an identification (`PathStep::kind == Portal`) or lands on a captured
  piece — a move that *shows the variant*. Reuse `expandAtom` and `tracePath` unchanged.
- **Draw it.** Tokens along the route (an `OvToken` at each run start), a `dashed`
  `OvTrail` for a leap and a solid one for a slide, and an `OvBurst` when the move is a
  capture the signature flags (`hasRadiusEffect` → the atomic ring; a plain capture → a
  smaller burst). All existing `OvertureScene` elements — **no new output types.**

This is the same code path the editor's preview and the M11 camera use, so "what does this
piece do on this board" has one answer everywhere.

---

## M13.4 The camera arc

The overture's `OvCamera {yaw, elev, reach, persp}` (a mini-camera, not `view::OrbitCamera`)
is driven by a short `CameraShot` sequence from M11: settle to the flat board, rise to
reveal the surface, hold while the piece moves, pull back for the formed shape. Because
`reach` is a function of `t` (the header says so at `overture_scene.hpp:80`), the zoom
cannot drift out of step with the shape. The arc is authored once and shared by every
derived scene; only the surface and the move differ.

---

## M13.5 Selection and overrides

- `OverturePlayer::select(std::string_view)` (`src/app/overture.hpp:93`) becomes
  spec-aware: it takes the variant, computes the signature, and prefers a bespoke
  `Overture` if `overtureFor(name)` names one, else the derived scene. The player's
  "never cut off" rule (`app/overture.hpp:45`) is unchanged.
- The fourteen hand-authored scenes are kept and remain the reference look. A variant is
  never *worse off* for having a derived scene — the fallback discipline is the same as
  `archetypeFor`: an unknown variant animates rather than showing a blank pane.
- **Cosmetic, like every asset.** The signature is derived from the spec and the scene
  enters nothing; an overture change must not touch `VariantId` (the M7.0.2 rule).

---

## M13.6 Rule-flavoured scenes (best effort, explicitly partial)

Variants whose *point* is a rule rather than a shape — `atomic`, `mustcapture`,
`charged` — are the hardest to derive, because the drama is in the effect. M13 does not
promise to generate them. It promises only that their signature flags (`hasForcedCapture`,
`hasRadiusEffect`) can drive a burst, and that where that is not enough the bespoke scene
is used. Stated as a non-goal so the milestone cannot be judged on it.

---

## M13.7 Tests and acceptance

- **Signature table:** every shipped variant maps to the expected `SurfaceKind`; a
  variant with no gluing maps to `FlatGrid`.
- **Derived == authored:** for `cylinder`, `torus`, `mobius`, `klein`, `mirrorbox`, the
  derived surface equals the hand-authored one pointwise (differential; the shipped
  scenes are the oracle).
- **Closure arithmetic:** the derived `Klein` reversal and `Band` half-twist are asserted
  as the existing test asserts them — the same facts, on the general path.
- **Determinism:** `derivedOvertureScene(v, t, theme)` twice at the same `t` is
  byte-identical; it reads no clock (the purity rule the overture already has).
- **A Workshop-shaped variant:** a variant built only as a `variant.toml` — its name in no
  C++ table — produces a non-empty scene at `t = 0, .5, 1` and passes the `--shot` gate.
- **No regression:** every bespoke overture's `--shot` frame is unchanged.

### Acceptance facts

1. A variant introduced purely as data animates on the library screen with no scene code
   written for it.
2. The derived surface for each 2-D topology satisfies that topology's closure arithmetic,
   checked by the same test the hand-authored surfaces pass.
3. A D ≥ 3 variant's overture is the extruded `view::layout` grid, derived from
   `DimSpec`/`ViewConfig` — no per-variant geometry.
4. The demo move is a real move of a real piece, traced by `tracePath`, not a scripted
   path.
5. Every hand-authored overture is unchanged, and a variant using one is never downgraded
   to the derived scene.
6. Overture selection changes nothing in `VariantId`.

---

## M13.8 Docs and skills

- `docs/overtures.md` — the signature table, the surface catalogue and its closure
  arithmetic, and the derived arc.
- Update `docs/ARCHITECTURE.md` §10: overture surfaces as a derived, catalogue-keyed
  projection, like the board's.
- Update `AGENTS.md`: the overture section gains "a variant with no bespoke scene gets the
  derived one", and the gotcha list keeps the Klein closure note with a pointer to the
  general test.
- Skill after the third repetition: `cb-new-overture-surface`.

---

## Dependencies and ordering

1. **M13.1** the signature and `SurfaceKind` (table test) — everything keys off it.
2. **M13.2** the surface catalogue + derived==authored differential — the correctness core.
3. **M13.3** the derived demo move (reuse `expandAtom`/`tracePath`).
4. **M13.4** the camera arc (reuse M11 shots).
5. **M13.5** selection and overrides; **M13.6** the partial rule-flavour work.
6. Tests and docs land with each step (`cb-tdd-step` loop).

## Risks and non-goals

- **Procedural beauty is not bespoke beauty.** Derived scenes will be *consistent and
  pleasant*, not as striking as the tuned ones. That is the accepted trade: the marquee
  variants keep their hand-authored scenes; the tail gets something good. The milestone is
  judged on "the thousandth variant animates and is correct", not on matching the first
  fourteen.
- **The closure tests are the whole risk.** A generated surface that is subtly wrong is
  invisible in a still and wrong in motion. The differential against the shipped scenes is
  not optional.
- **Rule-drama variants are explicitly partial** (M13.6). Do not claim them.
- **Non-goals:** no scene scripting, no authoring UI for overtures, no change to the
  `OverturePlayer` cycle, no new `OvertureScene` element types, no engine change. The
  milestone lives in `render`/`app` and consumes `VariantSpec` read-only.

## Status

Planned, not started. The generic path is new, but it rests on existing, tested pieces:
the identification model (M3), `view::layout` (M4), `expandAtom`/`tracePath` (M5/M4), and
M11's camera shots. The hand-authored scenes become the oracle for the generated ones, so
the risky part — surface closure — is checked against something already correct.

### As built (2026-09-30)

`app::overtureSignature` derives a `SurfaceKind` from the identifications (never the name);
`render::derivedSurfaceAt` maps it to the same warps the authored scenes use, asserted
pointwise-equal to them; and `Ui` uses the authored scene when the name has one, else
`derivedOvertureScene`, so a variant that exists only as TOML animates with no scene code.
The derived scene opens on the variant's own start position and plays a real traced move
(M13.3) for a 2-D 8×8 board. Tests: a signature table over every shipped variant, the
differential surface, and a data-only TOML torus. Docs: `docs/overtures.md`.

**Also built:** the D ≥ 3 extruded-`layout` overture (`derivedGridOverture`), so a data-only
3-D/4-D variant animates into its own lattice; it compiles and the render/view tests pass but
has not been eyeballed on a machine with a display.

**Deferred:** rule-flavoured scenes (M13.6, a stated partial). `--shot` of a derived variant
forces the library picker to it.

## M13.9 The main-menu attract sequence: a played game that changes shape

Requested directly, for trailer production: "do the game with changing geometries [in
the main menu]... program piece moves with 'shape' morphs, start on the regular board."
Unlike M13.1-M13.8 above, this is not the New Game screen's per-variant preview (a pure
function of `(spec, t)` with no real game state) - it is a real, short game, actually
played move by move, on the **main menu**, that cuts and morphs between several
variants' own geometries while it plays. Scoped explicitly "for now": piece moves and
shape/ghost morphs only - no new camera work. The user is building the camera
choreography separately.

### A. A trailer-scoped 8x8 nested-shell variant

`cylinder`, `torus` and `klein` (`variants/*.toml`) are already 8x8 - the same file/rank
count as `standard`. `torus3d` is 4x4x4. For the sequence's "it's the same board,
gaining a dimension" beat to actually read that way, the 3-D step needs to match: a new
`variants/torus3d_8x8.toml`, file=8/rank=8/level=4 (256 cells, the same scale `hyper4`
already renders), built the same way `torus3d.toml` is - three periodic identifications,
no code - with a comment stating plainly that it is trailer-scoped, not a catalogue
variant (matching `torus3d_twist.toml`'s own precedent of a special-purpose 4x4x4
variant already in the repo).

That alone is not enough: the D >= 3 play-shape dispatch
(`src/render/overture_scene.cpp`'s `playShapeAt`, the `d.dims() == 3` branch, and
`hasPlaySurface`'s matching check) is gated by **literal variant name** -
`v.name == "torus3d"` - not by structure. A new variant under a different name gets
nothing and falls back to the ordinary flat lattice. Fix this by generalizing the gate
to a structural check - three dimensions, every axis periodic (the same boundary-kind
inspection `PlaySurface::buildStacked` already reads per axis, `src/render/
play_surface.cpp`'s `periodicU`/`periodicV`-style checks, extended to a third axis) -
rather than adding a second hardcoded name. This is the generalization the codebase's
own stated ethos already argues for (`torus3d_twist.toml`'s own comment: "the gluing
language has no dimension-specific case"), and it has a useful side effect worth noting,
not chasing: `torus3d_twist` (shipped, 4x4x4, non-orientable) currently has no play
shape for the identical name-gating reason, and would gain one for free. That is a
welcome consequence of the fix, not a second task.

**Tests.** `hasPlaySurface` true for the new variant; a `playShapeAt` position check at a
couple of coordinates; `torus3d` and `hyper4`'s own existing behaviour completely
unchanged (regression) - and `torus3d_twist` gaining a working play shape is an
acceptable, expected side effect to note in the status write-up, not a scope violation.

### B. The attract sequence: a data model, and a driver that reuses the watch machinery

**Reuse, do not reinvent.** `Session::startWatching(GameFile, dwell)` /
`advanceWatching(dt)` (`src/app/session.hpp:235-249`, impl `session.cpp:710-776`) is
already the per-frame driver for "play scripted moves over time, with animation,
interactively" - built for M12.8's "Watch a game," and already proven at that job. An
attract sequence is a **looping sequence of these**, one per geometry beat, with the
shape/ghost pose also eased per step - new orchestration layered on top, not a new
move-timing mechanism.

A plain data structure - name it for what it is, e.g. `AttractStep{ variant, moves
(a GameFile), formedTarget, ghostTarget }`, `AttractSequence = vector<AttractStep>` - and
a driver on `Shell`, `startAttract(AttractSequence)` / `advanceAttract(dt)`, built the
same shape as `startWatching`/`advanceWatching` (`src/app/shell.cpp:406-441` is the
pattern to follow, not reinvent). Per step: `startGame(step.variant)` (full variant
switch/reset, `shell.cpp:306-325`), `session_->startWatching(step.moves, watchDwell)`,
then every frame ease `settings_.geometryFormed`/`settings_.geometryGhost` toward the
step's targets (`render::advanceFormMorph` already does this for `formed`
- `src/gui/main.cpp:1390-1413` is the existing call site to read before adding a sibling
for ghost, or reusing the same helper if it is already generic enough). Advance to the
next step - wrapping back to 0, looping forever - once **both** the step's moves are
exhausted (`advanceWatching` returns false) **and** the pose has settled near its
target; never cut mid-ramp.

**Two caveats to build around, not paper over** - both already documented in this
plan's M18.6 work, not new discoveries, but easy to trip over here specifically:

1. **`hyper4` has no continuous roll - it is a snap**, per M18.6's own status note
   ("honestly a snap - there is no continuous roll to expose"). Its step's
   `formedTarget` reaching 1 will pop, not ease, no matter what this task does. Do not
   spend effort trying to smooth it - that is out of scope here (and already out of
   scope in M18.6). A hard pop reading badly is a pacing/camera problem for later.
2. **`geometryFormed = 0` is a rank-mirrored flat board**, not the ordinary flat
   lattice (M18.6, documented, deferred - the same "180 mirror" item this plan's own
   Klein/morph revision flagged as still unaddressed). The *first* step's flat ->
   shape reveal - the "disguised as a regular board" opening the user asked for - may
   therefore show visibly mirrored ranks on its very first frame, not a clean echo of
   `standard`. **Build it, then actually look at a capture of it.** If the mirror reads
   as wrong, the documented fallback is: open on the literal `standard` variant for the
   sequence's first second or two, then hard-cut to the cylinder step already mid-reveal
   (`formed > 0`) - sidestepping the open bug rather than fixing it here. State plainly
   in the status note which of the two shipped, and why.

**Variant-switch side effects.** `Shell::startGame`/`startWatching` do not themselves
push renderer options or re-frame the camera - the existing "watch a different bundled
game" request handler (`src/gui/main.cpp:1460-1477`) does that explicitly, at the call
site, after `Shell::startWatching` returns, including whatever it already forces for
`cameraMode`/`geometryAlign` on a glued variant. `advanceAttract`'s callers need the
identical treatment on **every** cut, not just the first - read that handler as the
pattern and inherit it as-is. Camera behaviour is explicitly out of scope for this task;
whatever "Watch a game" already does today is what ships here too, unchanged.

### C. Wire it into the main menu

Today `Screen::MainMenu` draws only the ambient `Deco::Manifold` field (`decoForScreen`,
`src/render/deco.cpp:572-600`), and `Shell::showsBoard()` (`shell.cpp:165-180`) returns
`false` for it - no board is ever drawn on the main menu. While an attract sequence is
running: `showsBoard()` should return `true` for `MainMenu` (so the existing board path -
the one `Screen::Game` already uses, no new renderer code - picks it up), and
`decoForScreen` should stop drawing `Deco::Manifold` for `MainMenu` in this state (the
board replaces the ambient field, not alongside it). Drive it from the same per-frame
spot `advanceWatching` already runs from (`src/gui/main.cpp:1425`): call
`shell->advanceAttract(dt)` whenever `screen() == Screen::MainMenu` and a sequence is
running. Start the sequence when the app opens on the main menu (always-on is simpler
than an idle-timeout trigger and matches "implement it in the main menu" directly -
default to it unless it conflicts with something already there).

**Do not touch input handling.** Every existing `MainMenu` button (New Game, Watch,
Settings, Quit) must keep working exactly as today, laid over the now-playing board the
same way the pause menu already composites its UI over a live board (`Screen::Paused`) -
reuse that layering, do not invent a new one.

### D. The sequence content

`cylinder` (8x8, exists) -> `torus` (8x8, exists) -> `klein` (8x8, exists, ghost target
~0.35) -> `torus3d_8x8` (new, part A, ghost target ~0.35) -> `hyper4` (exists, 4x4x4x4,
`formed` snaps per the caveat above) -> loop back to `cylinder`. Only the **first** step
(`cylinder`) ramps `formed` 0 -> 1, the reveal the user described; every later step
starts already-formed and simply cuts - repeating the flat-reveal trick at every step
would read as slow and repetitive rather than escalating, which is the pacing judgement
call for this part. Each step carries 2-4 ply of **real, legal** opening moves - author
them for real and let the engine's own legality check prove it: a `GameFile` that fails
to load or replay (`io/game_file.hpp`'s `applyGameFile`) is itself a build-time test
failure here, not something to hand-wave past. Ghost ramps (`klein`, `torus3d_8x8`) ease
the same way `formed` does, over the step's own duration, settling before that step's
"moves exhausted and pose settled" advance condition can fire.

### Tests

A pure logic test (no GPU, same shape as `tests/unit/app/test_shell.cpp`/`test_watch.cpp`)
stepping an `AttractSequence` through `advanceAttract(dt)` ticks: each step's variant
loads, its moves replay legally, and after enough ticks the sequence wraps back to step
0 and keeps going - proving the loop, not just one pass. A `showsBoard()`/
`decoForScreen()` test confirming both flip correctly while attract is active on
`MainMenu`, and are unchanged on every other screen (regression). Part A's own tests as
above. Note plainly, the way M17.12 part B's status note did: a live, looping,
many-minutes sequence cannot be exercised end-to-end headlessly, but every piece of its
*state* (which variant is loaded, the current `formed`/`ghost` targets, whether a step
has settled) is directly testable without a display - capture a `--shot` at a couple of
hand-picked points in the sequence (mid-cylinder-reveal, klein-with-ghost) for a visual
spot-check, and say plainly that is a spot-check, not full coverage.

### Acceptance

Launching the app and sitting on the main menu shows the sequence playing automatically:
a real short game, the board visibly changing shape between beats, ghost visibly
engaging on the klein and `torus3d_8x8` beats, looping back to the start without a
restart. Every existing main-menu button still works, laid over the playing board. No
other screen's behaviour changes - the New Game screen's own M13 overture system, Game,
Paused and Watch are all exactly as before.

### Risks and non-goals

Camera is explicitly out of scope ("I'll figure out camera moves later") - ship whatever
`Shell::startWatching`'s existing glued-variant defaults already do; do not add new
framing, follow, or orbit behaviour. Do not touch the New Game screen's own overture
system (M13.1-M13.8 above) - a separate, already-working code path. `torus3d_8x8.toml`
is explicitly trailer-scoped, per the user's own words ("just for this trailer") - do
not add it to any curated/"start here" list or other player-facing surface beyond what
this task needs. The `formed = 0` mirror and the `hyper4` snap are known, pre-existing,
documented limits (M18.6) that this task works around, not fixes.

### Status: M13.9 built (2026-10-05, opencode)

**Built, in the four lettered parts, each green before the next.**

**A - the trailer variant and the structural gate.** `variants/torus3d_8x8.toml` is an
8x8x4 (256 cells) all-periodic torus, `torus3d.toml`'s construction at the
sequence's 8x8 scale, commented as trailer-scoped. The D >= 3 play-shape dispatch is no
longer keyed on `v.name == "torus3d"`: a new `allAxesPeriodic` reads
`boundaryKind(axis, Side::Max) == Periodic` for all three axes, and both `playShapeAt`'s
3-D branch and (through `playShapePosition`) `hasPlaySurface` now follow structure. Because
the old formula divided the file/rank coordinates by the hardcoded `kE = 4`, each is now
normalised by the board's own extent: `torus3d` (extent 4) is bit-identical, and the
8-wide board wraps the tube once instead of ~1.9 times and folding its tiles onto each
other. `torus3d_twist` gains a working play shape for free, as the spec predicted.

**B - the sequence and its driver.** `AttractStep{variant, moves, formedTarget,
ghostTarget, formedFrom}`, `AttractSequence`, `defaultAttractSequence()`, and on `Shell`
`startAttract`/`stopAttract`/`advanceAttract`/`attracting`/`attractStep`. Each beat is a
full `startGame`/`Session::startWatching` (the M12.8 watch machinery, reused), and
`advanceAttract` eases `geometryFormed` and `geometryGhost` toward the beat's targets and
cuts only once the game has run out *and* the pose has settled. The easing helper
`advanceFormMorph`/`kFormMorphRate` moved down to a new header-only `src/view/morph.hpp`,
re-exported from `render/play_surface.hpp`, so the app layer can share the one definition
rather than duplicate it (app L95 may not include render L100).

**C - the main menu.** `Shell::showsBoard()` returns true for `MainMenu` only while
attracting; `decoForScreen(screen, boardBehind)` returns `None` for the menu when a board
is behind it (default unchanged, every other screen unchanged). The menu build now uses
`pauseFrame`'s layering - board left behind a thin scrim, menu right - instead of
`drawShellFrame`, whose opaque ground would paint over the board; `Screen::Paused` and
`Screen::Settings`-over-a-game already worked exactly this way. The interactive loop starts
the sequence on `MainMenu`, drives `advanceAttract` every frame, stops it the moment the
player navigates away, and on every cut pushes `optionsFor` and re-frames the camera with
the same `geometryView`/`cameraMode="route"`/`followStrength` the "watch a different game"
handler already forces. Input handling is untouched. One extra: `showsCheckWarning()` is
suppressed on `MainMenu`, because the attract board is ambient footage and a red check
vignette there reads as an error, not a game warning.

**D - content.** `standard` (opener) -> `cylinder` -> `torus` -> `klein` (ghost 0.35) ->
`torus3d_8x8` (ghost 0.35) -> `hyper4`, looping. Every beat's 2-4 ply are real, legal
opening moves and are proven so by an `applyGameFile` test and by the runner replaying
them; the Ruy line, a torus seam capture, a Klein rook capture and a 3-D rook/knight
exchange are the lines. `hyper4`'s `formed` snap is inherited and untouched, per the
caveat.

**The `formed = 0` mirror: the documented fallback was shipped, not the opening.** A
capture of the cylinder beat at `formed = 0` (before the fallback) shows the flat board
with black nearest the camera and white far off - the reverse of an ordinary board, the
M18.6 rank-mirror bug. Rather than ship that, the sequence opens on the literal `standard`
variant for two plies, then hard-cuts into the cylinder beat already at
`geometryFormed = 0.30`, rolling the last 0.70 in motion. The standard opener is a separate
beat (`formedFrom = 1`), so the sequence is six steps, not five, and the reveal is a cut
plus a short roll rather than a full 0 -> 1 ramp - the fallback the spec names.

**Tests.** `tools/test.sh --build render` (138 cases) and `--build app` (73) green after
Part C, plus `[attract]` (4 cases / 42 assertions): every beat's file replays legally,
the sequence visits all beats in order and wraps to step 0 while still running, `stopAttract`
restores the opaque board, and `showsBoard` flips only on `MainMenu`. `[variants]` green
with the new variant, `[arch]` green, `test_deco` pins the `decoForScreen` flip and its
non-leakage, `test_play_surface` pins the 8x8 play shape, the extent-normalised one-lap
period and the unchanged `torus3d`/`hyper4` behaviour. `ctest -R gui-(menu|settings|pause|
board|watch|newgame)` 8/8 validation-clean, and `tools/precommit.sh` green (build, arch,
unit, property). `tools/precommit.sh`'s format step only inspects staged files, so
`clang-format --dry-run --Werror` was also run by hand over every touched source and is
clean.

**What could and could not be visually checked.** This is a spot-check, not end-to-end
coverage: a live, many-minute looping sequence cannot be exercised headlessly (the dummy
SDL video driver cannot create the Vulkan window the interactive loop needs, so the real
loop was not run). The reviewable capture path (`--shot --screen menu --t S`) was extended
to run the sequence for `S` seconds at a fixed step, and captures were taken and inspected:
the standard opener (white nearest, correctly oriented), the cylinder mid-roll, the
ghosted Klein and 3-D torus (pieces visible through the shell), and `hyper4` with its
tesseract scaffold. A single captured frame does not ease the camera the way the live loop
does, so its framing is approximate and occasionally crops the shape; the interactive
framing eases home over the following frames. The `hyper4` beat was seen only as a
fully-formed cut.

**Known, unchased.** `torus3d_8x8.toml` is auto-scanned into the New Game library like any
other file (the same as `torus3d_twist`); it is in no curated list, per the spec, but it is
player-visible because the picker globs the directory. `startAttractStep` goes through
`startGame`, so each beat persists settings and rewrites `lastVariant` - harmless (the
library always opens on `standard`) but worth a lighter path if the sequence ever gets a
settings-menu interaction. Suppressing the check vignette on the menu is a small behaviour
change the spec did not name; the alternative (a red flash mid-trailer) read clearly worse.

### Revision: a button instead of an always-on takeover, a static camera, and one
### continuous game across every shape (2026-10-05, feedback)

Three corrections, after actually seeing what shipped:

1. **This should never have replaced the quintic as the menu's default.** "Implement it
   in the main menu" meant *an entry point from* the menu, not *the menu's own state*.
   The quintic manifold (`Deco::Manifold`) stays what a player sees on an ordinary visit
   to `Screen::MainMenu`; the sequence only plays when asked for, the same way "Watch a
   game" only plays a bundled game when the player picks one.
2. **The camera must not follow the pieces.** The previous build's `applyAttractLook`
   forced `cameraMode = "route"`/`followStrength = 1.0` - inherited from "watch a
   different game," which is exactly the move-chasing behaviour the camera-choreography
   work (being built separately) needs to own instead. Re-framing *at a beat's own cut*,
   so the newly-revealed shape is actually in view, is still wanted and should stay; what
   must go is any per-frame following of a piece mid-slide within a beat.
3. **One continuous game.** Every beat currently starts from its own variant's canonical
   opening and plays its own short, independent scripted line - so each "morph" is really
   a fresh game that happens to look similar, not the same game continuing. The whole
   six-beat sequence should be one running game: the position a beat ends on becomes the
   position the next beat starts from, carried forward rather than reset. Chosen
   explicitly over the alternative (continuity only within the three same-dimension 2-D
   beats, a fresh start at each dimension change): **the position is embedded on one
   slice** when a beat steps up in dimension - the running position's pieces land at the
   same file/rank/level coordinates in the bigger lattice, with every newly-introduced
   axis (level, then aeon) fixed at a single value for every carried piece, so the rest of
   that new axis starts empty. The game is still real and still provably legal at every
   step - nothing about this fakes a position or skips the engine's own legality check.

**Part E (small, do first): the button and the static camera.**

Revert the always-on trigger in `src/gui/main.cpp`'s per-frame `MainMenu` block (the
`if (!shell->attracting()) { shell->startAttract(...); ... }` that starts a sequence the
instant the app sits on the menu) - starting the sequence belongs behind a new menu
action, not the per-frame loop. Add a sibling row to "Watch a game" in
`Ui::buildMainMenu` (`src/render/ui_menus.cpp:138-197`; the existing row is built with
`menuEntry("Watch a game", 2, t, display, width, true, scale_)` at line ~169, routing to
`shell.go(app::Screen::Watch)` - `menuEntry`'s signature is
`src/render/ui_widgets.hpp:124-125`). A new row immediately after it (shift every later
row's printed index by one) that calls `shell->startAttract(defaultAttractSequence())`
directly - this sequence has no picker step the way bundled "Watch" games do, so it does
not need its own screen, just a button that starts it in place. Reuse the existing
`decoForScreen`/`showsBoard` plumbing exactly as built (it already does the right thing:
the board replaces the manifold *while a sequence is actually running*, which after this
fix only happens after the button is pressed).

Give it an exit, matching the house precedent rather than inventing a new one: while
`shell.watching()` on `Screen::Watch`, `src/render/ui.cpp:1468-1479` shows a single
`"STOP WATCHING"` button plus an `Esc leaves the game` caption, wired to
`Shell::stopWatching()` (`shell.cpp:437-440`) and to `Esc` at `src/gui/main.cpp:1332`.
Do the same for attract: a `"STOP"`-style control (visible only while
`shell->attracting()`) calling `shell->stopAttract()`, and the equivalent `Esc` wiring.

For the camera: in `applyAttractLook` (`src/gui/main.cpp`, near the function added for
M13.9), stop forcing `cameraMode = "route"`/`followStrength = 1.0f` - set
`cameraMode = "off"` and `followStrength` to `0.0f` instead (`Session::setCameraMode`/
`setFollowStrength`, `src/app/session.hpp:108-113`; `"off"` means no per-frame following
at all - confirmed at `src/gui/main.cpp:363`, `if (st.cameraMode == "off" || ... ) { ...
skip the follow update ... }`). Keep whatever re-frames the camera *on a cut*
(`frameBoard`, already called every time `advanceAttract`/`startAttractStep` reports a
new beat) - that is not "following," it is making sure the new shape is visible at all,
and removing it would leave the camera pointed at nothing after every cut.

Build and verify this part alone first - it is small, low-risk, and does not depend on
part F at all.

**Part F (the continuous game): position embedding across variant switches.**

A new helper - a free function, pure, independently testable, living wherever it reads
most naturally alongside `Position`/`DimSpec` (the `app` layer already sees both; keep it
out of `render`) - call it something like
`Position embedOnto(const Position& from, const VariantSpec& fromVariant, const
VariantSpec& toVariant)`. For each occupied cell in `from` (`Position::isEmpty`/`at`,
`src/position/position.hpp:28-31` - there is no ready-made "for each occupied cell"
iterator, the idiom used throughout the renderer is a plain loop over `cellCount()`
checking `isEmpty`, e.g. `src/render/board_renderer.cpp:688`): decode its `CellId` to a
`Coord` on `fromVariant.dims` (`DimSpec::toCoord`, `src/space/dim_spec.hpp:75`), build a
`Coord` on `toVariant.dims` by **matching axes by name** (`DimSpec::axisIndex`,
`dim_spec.hpp:57` - more robust than assuming position, though every variant in this
sequence happens to declare file/rank/[level/[aeon]] in that same order) rather than
blindly copying by index, carrying every axis `from` already has and setting every axis
only `to` has to a fixed value (0 - "level 0", "aeon 0"), then re-encode with
`DimSpec::toCell` (`dim_spec.hpp:67`) and `place` the same piece type/colour into a fresh
`Position(toVariant)` (`position.hpp:22`, `Position::place`, `position.hpp:49`). Side to
move carries forward too. This mapping is injective - and therefore safe, no two source
cells ever colliding onto one target cell - exactly when every shared axis's extent in
`toVariant` is >= its extent in `fromVariant`; state this precondition in a comment where
the helper is defined, since it is what makes "embed on one slice" a safe operation here
rather than a general-purpose one.

Rework `Shell::startAttractStep` (`src/app/shell.cpp`, ~line 480 on): capture the
*current* position from the session **before** calling `startGame` (which replaces
`session_` wholesale - confirm the exact live-position accessor on `Game`,
`src/game/game.hpp`, alongside the already-known `startPosition()`/`setStartPosition()`;
it is not necessarily named `currentPosition()`, check before using it). After
`startGame(step.variant)` constructs the new beat's fresh session, build the new beat's
`GameFile` the same way as today (`toGameFile(session_->variant(), session_->game())`,
`io/game_file.hpp:31`) but then **overwrite** `file.startFen` with
`toFen(embedOnto(previousPosition, previousVariant, step variant's VariantSpec))`
(`toFen`/`fromFen`, `io/fen.hpp:18,21`) rather than leaving `toGameFile`'s own
canonical-start default - that default is exactly what currently makes every beat reset.
The very first beat (`standard`, nothing to carry forward into it) keeps today's
behaviour unchanged - there is no previous position for the sequence's own opening.
`GameFile::startFen` already is a free-form string (`io/game_file.hpp:26`) - nothing
downstream (`Session::startWatching` → `GameRunner::reset` →
`Session::loadFen`, `session.cpp:664-672,710-739`, `game_runner.cpp:13-21`) assumes it
matches the variant's own declared opening, so this is a drop-in substitution, not new
plumbing.

**The wrinkle this surfaces, not to be quietly worked around:** `hyper4` is a fixed
4x4x4x4 lattice - *smaller* in file and rank than the 8x8 the rest of the sequence now
uses, `torus3d_8x8` included. The injective-mapping precondition above fails exactly at
the last beat: an embedded file/rank coordinate from an 8-wide board has nowhere
legal to land on a 4-wide one. Mirror the fix this same revision already made for
`torus3d`: a similarly trailer-scoped `variants/hyper4_8x8.toml`, file=8/rank=8/level=4
(matching `torus3d_8x8` exactly, so that beat's embedding is a trivial identity on its
first three axes) and aeon=2 as the one genuinely new axis (512 cells total - noticeably
more than the 256 the hyper4 scaffold/rotation work was verified against; actually check
this renders and performs acceptably before treating 512 as fine, rather than assuming
it). If 512 proves to be a real problem, aeon=1 (256 cells, unchanged scale) is the
fallback, but say so plainly rather than silently shrinking it - an aeon axis of size 1
has nowhere for "the fourth dimension" to actually go, which is a real loss for this
beat's own point.

Every beat's scripted moves (`AttractStep::moves`) need to be real, legal continuations
from whatever that beat's *embedded* starting position actually is - which, past the
first beat, is no longer any variant's own canonical opening, so it cannot be guessed or
reused from the previous build. Compute each beat's true starting FEN for real (run the
sequence through the engine up to that point - a throwaway test or scratch tool is fine
for authoring, same as how every other move list in this plan has been required to be
engine-verified, never hand-guessed) before choosing that beat's next few moves.

**Tests.** A unit test for `embedOnto` alone: a small source position, embedded into a
same-dimension target (identity - every piece lands exactly where it started) and into a
higher-dimension target (every piece's new axis is 0, the rest of that axis is empty,
existing axes unchanged). The existing `[attract]` "every attract beat is a real, legal
game for its variant" test (`tests/unit/app/test_attract.cpp`) no longer makes sense
beat-by-beat against each variant's own canonical start - rewrite it to walk the whole
sequence once, computing each beat's embedded start from the previous beat's end exactly
as `startAttractStep` now does, and proving the whole chain legal end to end. A property
check across the full sequence: total piece count never increases from one beat to the
next (only captures remove pieces; embedding must never duplicate or drop one without a
capture to explain it) - a cheap, global guard against exactly the kind of bug a
coordinate-mapping mistake would produce.

**Acceptance.** Watching the sequence start to end: the same pieces, accumulating the
same move history, carry through every shape change - no beat's board reverts to a fresh
opening array. The ordinary main menu is the quintic field until the new button is
pressed; pressing it starts the sequence; a visible control plus Esc stops it and returns
to the ordinary menu. The camera never chases a sliding piece mid-beat; it only recentres
at a cut.

**Risks and non-goals.** Camera choreography beyond "stop auto-following, keep the
per-cut re-frame" is explicitly not this task - that is being built separately. Keep
building and verifying part E before part F; they are independent and E is far smaller.
`hyper4_8x8.toml` is exactly as trailer-scoped as `torus3d_8x8.toml` - same disclaiming
comment, not added to any curated list, not a catalogue variant.

### Status: M13.9 revision - part E built; part F blocked on topology legality (2026-10-05, opencode)

**Part E (button + static camera) is built and green.** It is the small diff the spec
described.

- The per-frame auto-start on `Screen::MainMenu` is gone. The sequence now starts only
  from a new main-menu row, **"Watch trailer"** (ordinal 03, immediately after "Watch a
  game"). The row raises a new `render::UiRequest::startAttract`, handled in `main.cpp`
  exactly like the bundled-watch request (`applyAttractLook` -> options -> `frameBoard` ->
  `wasSurface`), so the camera/renderer plumbing lives where it already did. Every later
  row (Continue 4, Multiplayer 5, Tutorial 6, Editor 7, Settings 8, Quit 9) is renumbered.
- The exit matches the watched-game precedent: while `shell.attracting()`, a bottom-left
  **STOP TRAILER** overlay plus the caption "Esc leaves the trailer" is drawn (only then -
  the ordinary menu has neither), wired to `Shell::stopAttract()`. Esc checks `attracting()`
  **first**: a running attract also sets `watching()`, so without that check Esc would stop
  the inner watch and leave the sequence running.
- `applyAttractLook` now sets `cameraMode = "off"` / `followStrength = 0.0f` instead of the
  inherited `"route"`/`1.0f`. The per-cut `frameBoard` is kept, exactly as specified.
- One judgement call the verification forced: the `--screen menu --t S` review path was
  gated on an explicit `--t` (new `overtureTGiven`), so a plain `--shot --screen menu` is
  now the ordinary quintic menu rather than the trailer at its default 0.78 s. `--t 0`
  still starts the trailer from the top. Without this the acceptance's "the ordinary main
  menu is the quintic field" could not be shown.

Tests after Part E: `tools/test.sh --build render` 138 cases, `--build app` 73 (before
part F's additions), `[attract]` 4, `[variants]` 5, `[arch]` 9, `io game rules variant`
43, and `ctest -R gui-(menu|pause)` 2/2 all green. Capture review: `--shot --screen menu`
shows the quintic field with rows 01-09 and no stop control; `--t 0` / `2.0` / `4.0` show
the board behind the menu with the STOP TRAILER control and a static (not piece-chasing)
camera.

**Part F (the continuous game) was attempted and stopped partway.** What is built and
left as scaffolding:

- `app::embedOnto(from, fromVariant, toVariant)` (`src/app/embed.{hpp,cpp}`): a pure
  helper that carries every piece across a variant switch, **matching axes by name** and
  fixing every axis only the target declares at 0. Unit-tested (`tests/unit/app/test_embed.cpp`):
  identity across two same-shape variants, and new-axis-is-0 (with the rest of the new
  axis empty) for the higher-dimensional case.
- `variants/hyper4_8x8.toml`: 8x8x4x2, aeon=2 (512 cells), trailer-scoped. It loads,
  has 86 legal opening moves, and renders headlessly in 0.36 s, so the size concern the
  spec raised is not a problem. It declares the full standard six piece kinds so the
  embedding is lossless.

The reworked `startAttractStep` (capture the live position before `startGame`, overwrite
`GameFile::startFen` with the embedded FEN) was written and proven mechanically in a
throwaway generator, then reverted. It cannot be activated: **the spec's central
assumption - "still real and still provably legal at every step" - is false once the
topology changes.**

- **Piece tables are per-variant, not global.** `PieceTypeId` is declaration order
  (`src/io/variant_toml.cpp`), so a raw id copy is wrong across variants. Matching by name
  fixes that, but `klein`/`mobius`/`torus3d_8x8` declare **no pawn** and `hyper4` declares
  **no bishop or queen**. Authoring `hyper4_8x8` with all six makes the last embedding
  lossless, but the pawns still have nowhere to go at the Klein cut (the spec's "embedding
  must never drop one" cannot hold). Making `klein` declare pawns would change a catalogue
  variant's identity and its documented "no forward direction" design.
- **Topology changes reject the running position outright.** Setting the ordinary standard
  army on `torus` gives **0 legal moves** (`endspielzero "load torus" "fen
  rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1" moves` -> 0; `torus`'s own,
  designed opening has 54). The pawnless army on `klein` also has 0, and `klein` cannot
  even parse a pawn FEN. A torus with the black queen removed still has only 1 legal move.
  This is the rank-wrap contact the variants' own openings are authored to avoid (the
  AGENTS gotcha "the usual one starts in check on a torus"), and it is intrinsic: carrying
  the game onto the glued 2-D boards produces a dead position, not a legal continuation.
- **The tesseract is keyed on the name `hyper4`** (`render/overture_scene.cpp`,
  `playShapeAt`, M17.12). `hyper4_8x8` therefore fails `render::hasPlaySurface`, so as a
  play shape it falls back to the ordinary 4-D lattice (confirmed by capture). Restoring
  the scaffold would mean touching M17.12, which this task excludes, and the branch's
  `axis - 2` centring is the 4-wide case and would need extent-normalising anyway.

**Why stopped, not routed around.** Full continuity with the shipped catalogue needs one
of three things, each larger than this task: (a) long cooperative lines that trade all
sixteen pawns away before the Klein cut *and* clear every wrap check before the torus cut
- a milestone-sized authoring job, not the "next few moves" the spec describes; (b) a
fallback to the target's canonical start whenever the embedded position is dead, which
directly contradicts the acceptance ("no beat's board reverts to a fresh opening array")
and would show exactly a reset at the torus and Klein cuts; or (c) editing the catalogue
variants' piece sets / touching M17.12. Per the task, this was stopped partway with the
reason recorded rather than shipping a sequence that contradicts its own acceptance.

**State left.** `embedOnto` and its tests and `variants/hyper4_8x8.toml` are in place,
building and green; `embedOnto` has no production caller yet, deliberately - the driver
that would use it is the part that cannot run. `Shell::startAttractStep` and
`defaultAttractSequence` are untouched, so the trailer behaves exactly as M13.9 shipped,
plus Part E's fixes. `tools/test.sh --build render` (138) and `--build app` (75, the two
new embed cases included) are green; `[attract]` is 6; `[variants]`, `[arch]`,
`io game rules variant` and `gui-(menu|pause)` are green. What could **not** be visually
checked: any piece carry across a cut, because the carry is not active - the sequence is
still the per-beat canonical-start one, and the captures above show that, unchanged. Like
`torus3d_8x8`, `hyper4_8x8` is auto-scanned into the New Game library (no curated list),
so it is player-visible even though nothing uses it yet.

### Revision: a proper game screen instead of a menu backdrop, 2-D pieces where the shape
### needs them, and the hypercube dropped (2026-10-05, feedback)

Three more corrections, after watching Part E run:

1. **This should be a real game environment, not a menu backdrop.** Part E composited
   the trailer board behind the main menu's own panel (`pauseFrame`, board left, menu
   right) - the wrong shape entirely. The trailer should land on the ordinary game
   screen, full board, full camera orbit - exactly what "Watch a game" already gives a
   bundled game, since it already *is* a real, running `Session`/`Game` (M12.8's watch
   machinery is what drives every beat). The player should be free to orbit and zoom
   while the pieces play themselves, the same way watching a bundled game already works.
2. **2-D icon pieces only where the shape needs them.** Switch `shapePieceCylinders` on
   for the Klein and 3-D torus beats only (both already ghost, for the same underlying
   reason: those are the two shapes where a sculptural piece, or a piece half-hidden
   behind a nearer shell, is hardest to read at a glance - M17.25's icon pucks exist
   exactly for this). Cylinder and torus keep the ordinary sculptural pieces.
3. **Drop the hypercube beat.** The sequence ends on the 3-D torus; no `hyper4` beat.
   `hyper4_8x8.toml`/`embedOnto` stay exactly where Part F left them - untouched, still
   independently tested, simply unused by the shipped sequence now. Don't remove them.

**Part G (do first): land the trailer on `Screen::Game`, not the menu.**

`Shell::startGame` already lands on `Screen::Game` by default (same place "watch a
bundled game" ends up) - `Shell::startAttractStep` (`src/app/shell.cpp`, ~line 480-501)
currently *undoes* that itself, forcing `screen_`/`previous_` back to `Screen::MainMenu`
right after `startGame` runs (the "Attract runs behind the menu: `startGame` moved us to
the game, put us back" lines). Delete those two lines - let the beat land on
`Screen::Game` like every other `startGame` call does, nothing more to do there.

Unwind the MainMenu-specific plumbing Part E added for the backdrop case, now dead
weight:
- `Shell::showsBoard()`'s `case Screen::MainMenu: return attracting() && session_ !=
  nullptr;` (`shell.cpp`, ~line 177-180) - revert to `false`, matching every screen
  `showsBoard()` doesn't list. Attract no longer runs while `screen() ==
  Screen::MainMenu`, so this case can never fire true again.
- `Ui::buildMainMenu`'s `overBoard = shell.showsBoard(); pauseFrame(...) : drawShellFrame(...)`
  branch (`src/render/ui_menus.cpp`) - revert to always `drawShellFrame`, and remove the
  bottom-left "STOP TRAILER" overlay window Part E added directly in this function (it
  drew over the menu specifically because attract used to run *on* the menu; it no
  longer does).
- `Shell::stopAttract()` (`shell.cpp`, ~line 516) doesn't change `screen_` at all today,
  because it never needed to - attract never left `Screen::MainMenu` before. Now it must:
  add the same `go(Screen::MainMenu)` `Shell::stopWatching()` already does
  (`shell.cpp:437-440` is the pattern), so leaving the trailer returns to the ordinary
  menu exactly like leaving a watched game does.

Then give the trailer its *own* exit control on the screen it now actually uses. Read
`Ui::buildGameHud`'s bottom-left controls block (`src/render/ui.cpp`, ~line 1468-1481):
while `shell.watching()`, it shows a single "STOP WATCHING" button plus a "move N of M -
Esc leaves the game" caption, calling `Shell::stopWatching()`. An attract beat also sets
`session_->watching()` true internally (it is a watch run under the hood), so this
branch will fire during a trailer beat too, and calling `stopWatching()` there would stop
only the current beat's inner watch, not the whole sequence - the wrong control for the
wrong mode. Check `shell.attracting()` **first** (the same precedence the Esc handler in
`src/gui/main.cpp` already uses) and show a sibling "STOP TRAILER" / "Esc leaves the
trailer" pair calling `Shell::stopAttract()` instead, falling through to the existing
ordinary-watch branch otherwise. `main.cpp`'s own Esc handling already checks
`attracting()` before `watching()` (Part E) and needs no change.

Camera: nothing to change here beyond what Part E already did. `cameraMode = "off"` /
`followStrength = 0` (set in `applyAttractLook`) only disables *automatic* move-chasing;
the player's own orbit/zoom input is a separate path, already live on `Screen::Game` the
same way it is for an ordinary game or a watched one - which is exactly what "camera
controls" means here, and it falls out of landing on the real game screen rather than
needing new code of its own.

**Part H: 2-D pieces on the Klein and 3-D torus beats.**

Add a field to `AttractStep` (`src/app/shell.hpp`, next to `formedTarget`/`ghostTarget`)
- `bool flatPieces{false}` (name it for what it does). In `defaultAttractSequence()`, set
it `true` on the Klein and `torus3d_8x8` steps only - the same two that already set a
`ghostTarget` below 1. In `Shell::startAttractStep`, set
`settings_.shapePieceCylinders = step.flatPieces` when a beat starts (a discrete style
switch, not something to ease the way `formed`/`ghost` are - it can simply snap at the
cut, there is no continuous "half icon, half sculpture" to animate toward).

**Part I: drop the hypercube beat.**

Remove the `hyper4` entry from `defaultAttractSequence()` (`shell.cpp`, ~line 451-479).
The sequence is `standard -> cylinder -> torus -> klein -> torus3d_8x8`, looping back to
`cylinder` (five steps, not six). Update every test that hard-codes the old step count or
step identities (`tests/unit/app/test_attract.cpp`'s `REQUIRE(seq.size() == 6)` and the
beat-by-beat checks) to match. Leave `variants/hyper4_8x8.toml` and `app::embedOnto` in
the tree untouched - independently correct, independently tested, simply not referenced
by the sequence any more.

**Tests.** Part G: a `Shell`-level test that starting the sequence lands on
`Screen::Game` (not `Screen::MainMenu`), `showsBoard()` is true there the same way it is
for an ordinary game, and `stopAttract()` returns to `Screen::MainMenu`. Part H: the
default sequence's Klein and `torus3d_8x8` steps have `flatPieces == true` and every
other step has it `false`; a `Shell`-level check that `settings().shapePieceCylinders`
actually flips as `advanceAttract` cuts between beats. Part I: the existing loop test
(`"the attract sequence plays every shape and loops back forever"`) updated to the new
five-step sequence, still proving the wrap, not just the shorter count.

**Acceptance.** Pressing "Watch trailer" moves the game onto the ordinary game screen -
full board, no menu panel beside it - and the player can freely orbit/zoom while it
plays. The Klein and 3-D torus beats show icon-puck pieces; cylinder and torus show the
ordinary sculptural ones. The sequence never visits `hyper4`. A visible control plus Esc
returns to the ordinary main menu (the quintic field) from anywhere in the sequence.

**Risks and non-goals.** Still no new camera choreography - the player's manual orbit is
not "camera motion" in the sense the user means to build later; nothing here adds
automatic framing beyond what `frameBoard` already does on a cut. The still-open
continuity question (Part F, parked) is untouched by this revision - dropping `hyper4`
removes one transition from that future problem but does not resolve the `torus`/`klein`
wrap-contact issue for the beats that remain.

### Status: M13.9 revision - parts G, H, I built (2026-10-05, opencode)

All three parts are built, tested and green. Part F's `embedOnto`/`hyper4_8x8` scaffolding
is untouched, as are M17.12 and M17.27.

**Part G (trailer on the real game screen).** The backdrop layout is gone. `Shell::
startAttractStep` no longer forces `screen_`/`previous_` back to `Screen::MainMenu` after
`startGame`, so a beat lands on `Screen::Game` like any watched game;
`showsBoard()`'s `MainMenu` special case is deleted; `stopAttract()` now `go(Screen::
MainMenu)`s exactly as `stopWatching()` does; `Ui::buildMainMenu` is back to a plain
`drawShellFrame` with the `pauseFrame`/`overBoard` branch and the `##attractstop`
overlay removed; and `Ui::buildGameHud`'s bottom-left controls check `attracting()`
**before** `watching()` and show a sibling "STOP TRAILER" / "Esc leaves the trailer"
calling `stopAttract()`. One fix beyond the spec's enumerated bullets was required: the
interactive main loop (`src/gui/main.cpp`) gated `advanceAttract` on `screen() ==
MainMenu` and stopped attract otherwise, which would have killed the run on its first
frame on `Game`; it now advances while on `Game` and stops only if the player navigates
away. Esc handling needed no change (it already checks `attracting()` first).

**Part H (flat icon pucks on two beats).** `AttractStep` gained `bool flatPieces{false}`
(placed last so the existing positional aggregates are unchanged). The Klein and
`torus3d_8x8` beats set it true - the same two that ghost; every other beat leaves it
false. `startAttractStep` snaps `settings_.shapePieceCylinders = step.flatPieces` at each
cut. `stopAttract()` also clears it (a small defensive addition the spec did not name,
matching the existing `geometryGhost` reset) so a beat's pucks cannot leak into the next
game.

**Part I (hypercube dropped).** The `hyper4` entry is removed from
`defaultAttractSequence()`; the sequence is the five beats `standard -> cylinder -> torus
-> klein -> torus3d_8x8`. The spec's "looping back to `cylinder`" is implemented
literally: the `standard` beat is a one-time cold open and the last beat cuts back to
`cylinder`, not replaying the opener (`advanceAttract`'s next index). This is a
deliberate loop-target change beyond a bare entry removal, taken because the spec states
the loop destination twice. `variants/hyper4_8x8.toml` and `app::embedOnto` are untouched
and still unit-tested.

**Tests.** `tests/unit/app/test_attract.cpp` was updated: the old
`REQUIRE(seq.size() == 6)` and the `MainMenu` screens are gone, the five expected variant
names are pinned, the loop test now proves the wrap goes to `cylinder` (index 1) at the
new length, and two new cases cover the per-beat `flatPieces` flags and the live
`shapePieceCylinders` flip (sculptural -> pucks at Klein -> back to sculptural at the
cylinder wrap). Results: `tools/test.sh --build app` 77 cases / 1062 assertions,
`--build render` 138 / 5,034,444, `--build unit` 297 / 48,802, `--build arch` 9 / 156,
`ctest -R "^property$"` passed (276 s), `gui-menu`/`gui-pause` 2/2, `clang-format
--dry-run --Werror` clean on every touched file, and `tools/precommit.sh` completed
clean (format/build/arch/unit/property, exit 0).

**Capture review.** `--shot --screen menu` (no `--t`) is the ordinary quintic-field menu,
unchanged. `--shot --screen menu --t 0` is the full game screen - label, board filling the
window, no split menu panel - with the STOP TRAILER control bottom-left. A 0-20 s sweep at
1 s steps visits `standard` (0-1 s), `cylinder` (2-4), `torus` (5-8), `klein` (9-12),
`torus3d_8x8` (13-15), then wraps to `cylinder` from 16 s: `hyper4` never appears, and the
wrap no longer replays `standard`. Full frames confirm `cylinder` and `torus` draw the
ordinary sculptures, while `klein` and `torus3d_8x8` draw the flat icon pucks (and ghost).
**What could not be checked this way:** the player's manual orbit/zoom (interactive input
on `Screen::Game`, no display here) is unexercised, though it is the same already-live
path an ordinary or watched game uses; the new interactive-loop driver branch in
`main.cpp` is bypassed by the `--capture` path (which calls `advanceAttract` directly), so
it is covered by the app/loop tests and reasoning rather than a screenshot; and the
style/flip at a cut is shown by per-beat stills plus the unit test, not as motion.

### Revision: every cut is a real, continuous geometry morph, built directly by Claude
### (2026-10-05, feedback)

Two more corrections, this time implemented directly rather than specified for opencode,
on the user's own request after opencode's attempts at the shape sequencing did not land
the way they wanted:

1. **Only the standard -> cylinder cut was actually animated.** Every later cut
   (cylinder -> torus, torus -> Klein, Klein -> the 3-D torus) was a hard swap:
   `startAttractStep` reset `geometryFormed` to a fixed value with no ramp between a
   beat's own end state and the next beat's own start state, so each shape just appeared.
   "They all should be [animated]."
2. **The specific sequence the user wants, because it is geometrically real, not just
   narratively convenient:** cylinder morphs into torus (closing an open tube's ring);
   torus morphs into Klein (folding the ring's circular cross-section into the
   figure-eight, then twisting it - the Klein bottle's own construction); Klein morphs
   back into a torus (undoing that fold and twist); and that torus visibly "copies
   itself" into the 3-D torus's four nested shells. Not a metaphor - this session's own
   math already carries it: `kleinSurf`'s circular-cross-section case (`pinch = 0,
   tau = 0`) *is* a torus, and `shellTube`'s four levels are the same ring construction at
   four radii.

**What this took, and the real bug found along the way.** Reading `tube()`/`kleinSurf()`
closely showed cylinder, torus and Klein are not three shapes - they are one family of
embeddings (`th` rolls the files, `ph` bends that roll into a ring, `pinch`/`tau` fold and
twist the ring's cross-section), already shared by `derivedSurfaceAt`'s own dispatch. The
Torus case gained a second real construction stage (`torusMorph`, mirroring the Klein
morph's own existing three-stage pattern): roll the files into an open tube first
(0 .. `kTorusRollEnd`), then bend that tube into a closed ring (`kTorusRollEnd` .. 1) -
exposed as `view::kTorusRollEnd` (moved down to `view/morph.hpp`, below both `render` and
`app`, alongside the pre-existing `kKleinRingEnd`/`kKleinFoldEnd`, for the same layering
reason `kFormMorphRate` already lives there) so the attract sequence can land a cut
exactly on it.

The first attempt at this produced a measured, real discontinuity - not a smoothness
worry, an actual seat jumping 2+ units in one 1/100th of the morph
(`tests/render/test_play_surface.cpp`'s existing "the flat-to-shape morph is continuous
and never degenerate" caught it immediately, red before anything was declared done). The
cause: `tube()`'s ring-bend branch never reads its own `open` (ring-radius) parameter
until `ph` first leaves zero, and the *limit* of the ring-bend formula as `ph -> 0` is
`open` times the flat offset, not the flat offset alone - so `open` has to be exactly 1 at
that instant for the two branches to agree. The old single combined ramp got this for
free (`open` had barely begun ramping by the time `ph` first mattered); staging `th` and
`ph` apart meant `open` was already most of the way to its shipped value by the time the
ring-bend began. Fixed by ramping `open` on the *close* stage's own progress instead of
raw `form` - and the identical bug, with an even larger absolute mismatch (2.2 vs 2.05,
the torus and Klein's own different shipped ring radii), existed at the torus -> Klein
handoff too, fixed the same way: Klein's own `open` now ramps to the *torus's* radius
(2.2) through its ring stage - since at `pinch = 0, tau = 0` it draws the same picture a
torus does - then eases to its own native 2.05 over the fold and twist, landing on the
old shipped constant at `form = 1` to the bit.

**`shellSpread` (new `SurfacePose`/`BoardOptions`/`Settings` field, `geometryShellSpread`,
threaded through `optionsFor`/`surfacePose`/`SurfaceCache::Key` the same way `formed`
already is).** For the D >= 3 nested-shell shapes: 0 collapses every level onto the
*mean* of the shipped four radii - one ring, not four nested ones, the same `shellTube`
with every level's own `d` offset blended toward that one fixed point - and 1 is the
shipped, always-spread stack. `playShapeAt`'s `torus3d`/`torus3d_8x8` branch blends
`dLevel` toward `dMid` by `1 - shellSpread`.

**The sequence, now six beats on five shapes** (`Shell::defaultAttractSequence`):
`standard` (cold open, unchanged) -> `cylinder` (the existing M18.6-mirror-workaround
reveal, unchanged) -> `torus` (`formedFrom = kTorusRollEnd`, easing the ring closed) ->
`klein` (`formedFrom = kKleinRingEnd`, easing the fold then the twist) -> a second,
move-free `klein` beat (`formedTarget = kKleinRingEnd`, easing *back down* - the
"temporary morph to torus") -> `torus3d_8x8` (`formedFrom` pinned at `1.0` explicitly,
since the stacked shape's own `formed` does not share the 2-D shapes' staged meaning;
`spreadFrom = 0.0`, easing the shells apart). `AttractStep` gained `spreadTarget`/
`spreadFrom`, eased by `advanceAttract` the same way `formed`/`ghost` already are, and
included in the beat's own settle check. `stopAttract` resets `geometryShellSpread` to
`1.0` alongside the ghost and flat-piece resets it already does, so nothing leaks into a
game started afterwards.

**Tests, all new.** A direct, no-screenshot-needed proof of the central claim: "cylinder,
torus and klein land on each other's end-of-stage waypoint"
(`tests/render/test_overture_scene.cpp`) checks `derivedSurfaceAt` for the two shapes on
either side of each cut, at the exact waypoint, across a grid of `(u, v)`, to within
`1e-4` - this is what actually caught both `open`-ramp bugs, not inspection. "The nested
shells collapse onto one ring at shellSpread = 0" pins the new parameter directly:
collapsed to one real (non-degenerate) radius at 0, the shipped per-level radii at 1,
monotonic in between. `tests/unit/app/test_attract.cpp` gained "every cut in the sequence
lands on the previous shape's own end-of-stage," pinning the six beats' own
`formedFrom`/`formedTarget`/`spreadFrom`/`spreadTarget` wiring directly, and the existing
sequence/loop/flat-piece tests were updated for six beats instead of five (Klein appears
twice; the loop wraps to `cylinder`, index 1, not `standard`).

One pre-existing test's own threshold moved: "the morph keeps the camera on the shape as
its bounds move" asserted the torus's bounding-box centre wanders more than `0.5` units
across a full flat -> formed sweep, as a "the test has teeth" sanity check. The new
two-stage torus morph wanders less (measured ~0.35, not a tracking regression - the
camera's own worst-case miss stayed small throughout) because rolling and closing
sequentially, rather than together, visits a narrower range of bounding boxes; the
threshold was lowered to `0.3` with the reasoning recorded in the test itself, not
silently.

**Results.** `tools/test.sh --build render` 140 cases (5,034,654 assertions, three new
cases included), `--build app` 78, `[arch]` 9, `ctest -R "^property$"` green,
`clang-format --dry-run --Werror` clean on every touched file. Visual spot-check via the
existing `--shot --screen menu --t S` path: cylinder (open tube, opaque, sculptural) ->
torus (closed ring, opaque, sculptural) -> Klein (ghosted, flat icon pucks, the
figure-eight self-intersection visible) -> `torus3d_8x8` (ghosted, flat icon pucks,
opening visibly collapsed toward one ring rather than four separated shells). The actual
sub-half-second transitions themselves (each stage closes in well under the 1-second
sampling grid used for review, at the shipped `geometryFormSpeed`) could not be caught
mid-motion by a still - the *handoff points* are proven exactly by the new differential
test instead, which is the stronger claim a screenshot cannot make.

**Known limits, stated plainly rather than silently worked around.** The morph's own
*speed* is fast (a full `0 .. 1` sweep is half a second at the shipped
`geometryFormSpeed`; a half-range stage is a quarter second) - real, but brief enough
that a cut can still read as a quick pop rather than a lingering motion depending on
camera framing. Speed is already the adjustable `geometryFormSpeed` knob; making it
choreographable per beat is for the camera/scene-authoring scaffold requested alongside
this, not built here. Two asks from the same round of feedback are **not** addressed by
this revision and remain open: carrying one continuous game across every shape (the
Part F question, parked on the real wrap-contact/king-adjacency wall found earlier - not
touched again here) and the camera-keyframe authoring scaffold (not started).

### Revision: wider shell spacing, and a Mobius beat between standard and cylinder
### (2026-10-05, feedback, built directly by Claude)

Two more corrections after watching the above run - confirmed working well, then two
small, well-scoped asks on top of it.

**Wider nested-shell spacing.** The 3-D torus read as "a thicker torus," not four
distinct shells - correct in principle (the handoff math is unaffected) but not legible
in practice. The per-level radial step in `playShapeAt`'s `torus3d`/`torus3d_8x8` branch
(`src/render/overture_scene.cpp`) was `0.62 + (level - 0.5) * 0.82`; the multiplier is now
`1.7`, by eye, checked against a fresh opaque and ghosted capture of `torus3d_8x8` until
four separated rings actually read as four. This is the shared D >= 3 dispatch, so it
widens `torus3d` and `torus3d_twist` too, not just the trailer's variant - a general
legibility fix, not trailer-only tuning. One existing test's own thresholds moved with
it: "picking a three- and four-dimensional shape finds the tile" measures how far a
seat's own screen projection can land from the tile `pick()` actually resolves to before
counting it as a miss - wider shells mean seats on different levels project further
apart, so the proximity bound widened (120px -> 260px) and the self-agreement ratio
loosened (`hits / 8` -> `hits / 16`), both with the reasoning recorded in the test, not
just the numbers changed.

**A Mobius beat, standard -> Mobius -> cylinder -> torus -> klein -> the 3-D torus.**
Mobius and cylinder are not the same construction family the way cylinder/torus/Klein
are - `band()`/`stripSurface()` bend the *file* axis into a loop in the board's own
plane, `tube()` rolls it into a tube's cross-section - genuinely different embeddings,
not two parameter settings of one function. The only point they share is the bare,
unrolled flat board (`th = 0`), proven directly ("the Mobius strip and the cylinder
share the bare flat board," `tests/render/test_overture_scene.cpp`), not assumed. The
first version of that test used `derivedSurfaceAt`'s own bare default pose
(`stretch = 1.0`) and failed red, with real, multi-unit distances - not rounding noise:
`derivedSurfaceAt`'s internal `lerpf(1, kMobiusStretch, clampf(pose.stretch))` reads
`stretch = 1.0` as *fully* stretched, not unstretched, because the actual "unstretched"
value the real `PlaySurface::build` produces at `formed = 0` is `pose.stretch = 0`
(`play_surface.cpp`'s own Band-specific remap, `pose.stretch = kRibbonStretch *
stageForm(form, 0, kRibbonStretchWindow)`) - a detail the test had to reproduce to be
honest, not sidestep. Fixed by setting `.stretch = 0.0f` explicitly on the Mobius side of
the comparison; the test is green and, more usefully, is what the sequence's own
`formedTarget = 0.0f` on Mobius's second beat relies on being true.

The sequence gained two beats either side of the shared waypoint it needed: Mobius's
first beat (`formedFrom = 0.30`, the same M18.6 mirror-avoidance reveal cylinder's own
first beat used to carry) plays a short, engine-verified legal line
(`a1a4 a8a5 b1c3 b8c6` - mobius has no pawns, same reasoning as Klein, so development
starts with the back-rank pieces directly); a second, move-free Mobius beat eases
`formed` all the way down to `0.0` (not a window - a single point, the one the proof
above establishes); cylinder's own `formedFrom` is now left at its default ("wherever the
previous beat left it," `-1`) rather than the old explicit `0.30`, since it now inherits
Mobius's own true-flat end state directly. Every later beat (torus onward) is unchanged.
The sequence is eight beats on six shapes now; the loop (`loopFrom = 1`) still skips only
the one-time `standard` opener and now lands on Mobius first.

**Tests.** The new differential proof above; `tests/unit/app/test_attract.cpp`'s sequence
test extended to the eight beats and their names; the loop test's wrap target updated to
`mobius`; the flat-icon-style cut test's step indices renumbered for the two new beats in
front of cylinder; "every cut... lands on the previous shape's own end-of-stage" gained
checks for both new Mobius waypoints (`formedTarget = 0.0` on the fold-back beat,
`formedFrom < 0` - inherited - on cylinder).

**Results.** `tools/test.sh --build render` 141 cases, `--build app` 78, `[arch]` 9,
`[attract]` 9, `ctest -R "^property$"` green, `clang-format --dry-run --Werror` clean.
Visual spot-check via `--shot --screen menu --t S`: the Mobius beat renders as a
recognisable coiled ribbon (opaque, sculptural, its own "glued with a twist" label), and
by `t = 7` the sequence has already cut cleanly to `CYLINDER` playing its own opening -
confirming the new beats slot into the loop without disturbing anything downstream.

### Revision: the Mobius beat reverted (2026-10-05, feedback)

The Mobius beat above shipped, then was dropped the same day. While chasing the Part F
position-continuity question (carrying one real game across every cut, rather than each
beat replaying its own short independent line), the blocker turned out to be Mobius
itself, not the wrap-contact/king-adjacency theory this doc parked on earlier - that
theory was tested directly with the CLI tool and falsified (cylinder's own unmoved
4-ply opening carries onto `torus` with 54 legal moves; the issue was specific to
checking the *unmoved* starting array for simultaneous wraps, not a structural
illegality). Mobius declares no pawn piece type (`variants/mobius.toml`, same reasoning
as Klein), and `embedOnto` can only carry pieces the target variant declares - it can
never manufacture pawns back. Since Mobius sat right after `standard`, wiring up real
continuity would drop the pawns there and leave every later beat (cylinder onward)
pawnless for the rest of the trailer, permanently, for a shape the sequence does not
need. Told this trade-off plainly, the call was to drop Mobius rather than keep
continuity pawnless or keep Mobius and give up on continuity: "Ok, no need to have
mobius then, skip it."

Reverted: `defaultAttractSequence()` back to six beats on five shapes (`standard ->
cylinder -> torus -> klein -> klein -> torus3d_8x8`), cylinder's `formedFrom` restored
to its own explicit `0.30` since it is once again `standard`'s direct successor; the
now-orphaned differential test ("the Mobius strip and the cylinder share the bare flat
board") removed from `test_overture_scene.cpp` - it stated a true geometric fact but was
no longer load-bearing for any shipped feature; `test_attract.cpp`'s five beat-indexed
tests (the beat list, the loop wrap target, the stop-at-Klein index, the flat-icon-style
cut indices, and the end-of-stage waypoint bindings) restored to the six-beat numbering.
The wider shell spacing above is untouched - that fix is independent of Mobius and
stands. Position continuity (Part F) and the camera-keyframe scaffold remain open; now
that Mobius is gone, continuity may be more tractable than this doc previously feared,
but torus -> klein and klein -> `torus3d_8x8` were not yet checked before this revert
interrupted that investigation.

### Revision: Part F resolved, partially - continuity where the geometry allows it
### (2026-10-06, feedback, built directly by Claude)

Picked back up with a direct instruction to keep going until it works. The
wrap-contact/king-adjacency wall this doc parked on earlier turned out to be a real
effect, but the original diagnosis of it was wrong twice over, each time caught by
testing against the engine rather than reasoning about it: first, the session's own
earlier "good news" (cylinder's unmoved opening carries onto `torus` with 54 legal
moves) was an artifact of the CLI's `fen` command silently leaving the old position in
place after a rejected castling token - not a real result at all. Second, once that was
fixed (a new `embed <variant>` CLI command, built for exactly this kind of check, see
below), the real effect turned out not to be simple king-on-king adjacency: `torus.toml`
own comment already names it - "rank 1 and rank 8 are *adjacent*... White is already in
an inescapable check" - and a transplanted opening's queen and rooks, still sitting near
the board's own back rank, are just as exposed to that adjacency as the kings are. The
fix was empirical, not theoretical: cylinder's own ten-ply line ends with both kings
stepped one square off their home rank (`Ke2`, `Ke7`), found by trying a line, embedding
it, and reading the legal-move count back from the engine - the same discipline as
everywhere else in this file.

**The CLI gained an `embed <variant>` command** (`src/cli/main.cpp`): carries the
loaded position onto another variant's board through `app::embedOnto`, the same function
the trailer now uses, so a line can be authored and checked against the *exact* position
the engine will actually start the next beat from, not a hand-typed FEN standing in for
it. Building it surfaced a real dangling-pointer bug, independent of the trailer:
`Position` and `MoveGen` hold a raw pointer to their `VariantSpec`, so computing the
embedding against the old `VariantSpec` and *then* moving it into the new session (the
obvious way to write it) corrupts the embedded position the instant the move happens -
caught by a crash, not a logic check. Fixed by building the new session first and
embedding against its own stable, final address; `Shell::startAttractStep` and
`test_attract.cpp`'s own continuity check both follow the same order for the same reason.
`endspielzero_cli`'s layer now depends on `endspielzero_app` (moved after it in `src/CMakeLists.txt`,
layers are added bottom-up) to reach `embedOnto` at all.

**Where continuity landed.** Cylinder and torus carry one real game between themselves -
cylinder plays its own ten-ply opening, torus's own ten plies are a verified-legal
continuation of *that exact position*. It does not reach Klein or the 3-D torus, and not
for the Mobius reason (no pawn type) but a sibling of it, found the same way: both
`klein.toml` and `torus3d_8x8.toml` field only king, rook and knight at their own start,
and both files already say why - a bishop or queen gives check from almost anywhere once
every axis is periodic, so the shipped army is deliberately stripped down. Embedding a
normal opening's bishops and queen onto either produces a forced or near-forced position,
confirmed directly against both shapes with the new `embed` command. Klein and the 3-D
torus keep their own independently authored ten-ply lines instead, played on the army
their own files actually field - the same judgement Mobius was dropped on, just reached
by hitting the wall instead of reasoning around it in advance. Klein's own two beats do
carry a position between themselves (`continuesPosition` on the fold-out beat): nothing
is cut there, it is the same variant both beats stand on, so the pieces should not
visibly jump at a fold that is not a shape cut. The loop's own wrap from the 3-D torus
back to cylinder stays a hard reset, as it already was.

**Every beat is ten plies now** (five moves a side), not the two to four beats carried
since M13.9's first cut - a separate, explicit ask alongside continuity. All six lines
were authored and checked the same way: propose a move, embed or replay to the position
it reaches, read the engine's own legal-move count back, and only keep it once mobility
stayed healthy (never dropping to the single-digit, near-forced counts a bad embed
produces).

**Mechanism.** `AttractStep` gained `continuesPosition` (`src/app/shell.hpp`):
`Shell::startAttractStep` (`src/app/shell.cpp`), when set, captures the previous beat's
own variant and finished position *before* `startGame` replaces the session, then embeds
that captured position onto the new beat's own variant and uses the embedded FEN as the
`GameFile`'s `startFen` instead of the variant's own declared start - `file.moves` for a
continuing beat therefore has to be a legal continuation of the embedded position, not of
the variant's own opening.

**Tests.** `test_attract.cpp`'s "every attract beat is a real, legal game" test now
builds the same embedded-start-position chain the real `Shell` builds, tracking the
previous beat's variant and finished `Game` across the loop (with the same
stable-address discipline the CLI fix needed); "every cut...lands on the previous
shape's own end-of-stage" gained `continuesPosition` checks for every beat. No new
render-level test was needed - continuity is a `Shell`/`GameFile` concern, not a
geometry one.

**Results.** `tools/test.sh` full suite (496 cases, all tags) and `ctest -R "^property$"`
both green, `clang-format --dry-run --Werror` clean on every touched file. Visual
spot-check via `--shot --screen menu --t <seconds>`: the torus beat's own move log reads
"d7d6 c2c3 b7b5 a4b3 c8e6 b1d2..." - the real continuation moves, played on the carried
position, not a fresh independent line - confirming the mechanism works end to end, not
just in the headless test.

The camera-keyframe authoring scaffold (the other open ask from the same round) is next.

### Revision: the camera-keyframe authoring scaffold (M13.9 Part E)
### (2026-10-06, feedback, built directly by Claude)

The other open ask from the same round, picked up right after Part F: "you code the
morphs, and the user then defines how the scene changes and how the camera moves as the
game plays out." A scaffold, deliberately - the morph machinery is coded once, here;
*where* to put a keyframe, what the camera and shape pose should look like there, and how
fast to ease into it is left entirely to whoever plays the game and presses record. There
is no in-game keyframe list or editor yet - the saved file is plain text and is meant to
be hand-edited too, the same spirit `GameFile` already has.

**Data model** (`src/app/camera_track.hpp`/`.cpp`, new, added to `endspielzero_app`).
`CameraCue` is where a keyframe is pinned: a variant name and a ply count into it - the
same two numbers `Shell` already tracks for any running game, attract beat or not.
`CameraKeyframe` is a saved `view::OrbitCamera` plus the shape pose the M13.9 attract work
already threads through `Settings` - `geometryFormed`, `geometryGhost`,
`geometryShellSpread`, and `geometrySlideU`/`geometrySlideV` (the "middle mouse button
morph" the original ask named explicitly - the control that slides the board round the
shape) - plus `flatPieces` and a per-keyframe `morphSpeed`. `CameraTrack` holds at most
one keyframe per cue, kept sorted; `cameraTrackText`/`parseCameraTrack` serialize it as
one `keyframe variant=... ply=... target=x,y,z ...` line per keyframe, line-oriented and
diffable like a game file, with a line's own bad token failing that line's parse with a
line number rather than the whole file silently losing its mind.

**A track is not a timeline sampled at an arbitrary point.** A keyframe is a waypoint the
live view eases *toward* once play reaches its own cue - `Shell::advanceCameraTrack`
detects a cue with a keyframe different from the one last eased toward, captures the
view's *current* camera and pose as the blend's start (not the previous keyframe's own
endpoint - the player may have kept orbiting since), and eases over the keyframe's own
`morphSpeed` using `view::slerpCamera` for the camera and a plain lerp for the pose
scalars. Once the blend reaches `t = 1` it stops touching anything, handing control back
to whatever was driving the camera before - the player's own orbit, or the attract
sequence's own per-beat morph, which keeps running underneath and simply gets overridden
for however long a keyframe's own blend is in flight. A cue with no keyframe is left
alone entirely. This is what lets a track be as sparse as one keyframe for an entire
sequence, or as dense as an author wants, with no keyframe forcing an answer for a frame
it does not actually care about.

**Session gained one new primitive**: `setOrbitCamera` (`src/app/session.hpp`), setting
the stored camera directly rather than through an `apply()` action - the one place
something other than a player gesture is allowed to move it, called out explicitly in its
own doc comment rather than folded into `ActionKind` as a fake player action.

**Shell's own surface**: `cameraTrack()`, `recordCameraKeyframe(speed)` (captures the
live camera and pose at the current cue, replacing any keyframe already there),
`eraseCameraKeyframe()`, `cameraTrackPath()` (a file beside the settings file, the same
convention `gamesDir()` uses), and `advanceCameraTrack(dt)`. A track loads automatically
in `Shell::create` if the file exists - a missing one is the ordinary case for every
shell that has never recorded one, not an error - and both `recordCameraKeyframe` and
`eraseCameraKeyframe` persist immediately, the same way a setting change does, so there is
no separate save step for the scaffold's one editing surface to also wire up.

**The front end's whole authoring surface is two keys**, `k` to record and `j` to erase,
wired in `src/gui/main.cpp` next to the existing `u`/`r`/`g` shortcuts, gated the same way
they are (`inGame && !twoPlayer`); `advanceCameraTrack` is called once a frame whenever a
session is running, independent of whether the attract sequence, a watched game, or an
ordinary played game is what's driving it - the scaffold works for any of them, not just
the trailer, matching the original ask's own "as the game plays out," not "as the trailer
plays out."

**Tests** (`tests/unit/app/test_camera_track.cpp`, new, headless like the rest of this
file): `CameraTrack`'s own ordering/replace/erase and the text form's round-trip, parse
failures for an unknown field, a malformed number and a missing variant; `Shell`-level
recording (captures the live, player-moved camera and pose, not just whatever `frame()`
set up, and persists to the exact path a fresh shell would read back); the blend itself
(a keyframe's own cue starts easing the camera and pose toward it, a halfway `dt` lands
provably between the start and the target, a full `morphSpeed`-second `dt` lands on it,
and further frames at the same cue do not re-trigger or drift); and a cue with no keyframe
leaving the camera untouched. Camera-endpoint checks compare `eye()` with a tolerance
rather than the raw `yaw` field, the same precedent `slerpCamera`'s own tests
(`test_camera.cpp`) already set - an angle can wrap to an equivalent value spelled
differently, so exact float equality on `yaw` is not the right question to ask of a slerp.

**Results.** `tools/test.sh` full suite (502 cases, all tags) and `ctest -R "^property$"`
both green, `clang-format --dry-run --Werror` clean on every touched and new file, a
`--shot` smoke check confirming the GUI still starts and renders with the new wiring
loaded. Both of this round's open asks are now scaffolded or resolved: continuity where
the geometry allows it (Part F), and the camera-keyframe machinery for whoever authors a
track next (Part E). What remains is authoring, not engineering: nobody has recorded a
real track yet, and the in-game list/editor the scaffold's own doc comment names as
future work - browsing, re-ordering, or nudging a keyframe without re-recording it from
scratch - is still just the hand-edited text file.

### Revision: one continuous game, standard to the 3-D torus
### (2026-10-06, feedback, built directly by Claude)

Direct feedback on the previous round's Part F: standard and cylinder were still two
independent openings, not one game, and the ask was explicit - "ALL game should be
continuous" - with a concrete way to reach it for Klein and the 3-D torus too, rather than
leaving them out: "create a game where bishops/queens are gone before we get to torus
etc." Both landed.

**The line.** One real game, authored and checked the same way as every other line in
this file - propose a move, embed or replay to the position it reaches, read the engine's
own legal-move count back - except this time as fifty plies straight through, not six
independent ten-ply fragments. `standard` plays its own ten-ply opening (unchanged);
`cylinder` is now a real continuation of it (`continuesPosition`), not a second opening;
`torus` continues `cylinder`; `klein`'s fold-in continues `torus`; its fold-out continues
that; `torus3d_8x8` continues the fold-out. The trade-down the previous round's own status
note named as the missing piece is in the line itself: both bishop pairs and both queens
come off within `cylinder`'s and `torus`'s own plies, several of them as the direct result
of the king-safety recaptures those beats already needed (`Ke1-e2`/`...Ke8-e7`-style
steps, now also serving as the king landing *on* the capturing or captured square rather
than just off the back rank). By the time `torus` ends, nothing is left but kings, rooks,
a knight and pawns - exactly Klein's and `torus3d_8x8`'s own declared roster once the
pawns are dropped at the cut (`embedOnto`'s ordinary behaviour for a piece type the target
does not declare, needing no special handling, because nothing downstream ever needs them
back). One beat's own first move is itself a capture that leaves its own king in check,
resolved by that beat's second move - a real, legal way for a beat to open, confirmed
directly rather than assumed to be too strange to try.

**The wrap stays a hard reset, for a reason stated plainly to the user rather than just
implemented quietly.** A looping sequence replays the same fixed `moves` list every lap.
Continuing `cylinder`'s position across the wrap would make its own ten-ply line a
continuation of whatever the *previous* lap's `torus3d_8x8` beat happened to end on - and
the shipped line ends that beat with white down to a lone king, against which a scripted
ten-ply line is essentially never going to stay legal. Found by building it anyway and
watching it crash (`seen.size()==1`, stuck on cylinder's first move) and then fail cleanly
(`'f3d4' is not legal`) once the first bug was fixed - not reasoned around in advance.

**Mechanism.** `AttractStep` gained `resetMoves`: the line to play, from this variant's
own fresh start, on the one beat where `continuesPosition` cannot be honoured on a given
visit - the loop's own re-entry point, every lap after the first. `cylinder`'s own
`resetMoves` is simply `standard`'s ten plies followed by `cylinder`'s own ten, replayed
fresh rather than embedded: cylinder and standard share an army and a start array, and
none of these twenty plies go near the file seam, so replaying them fresh reaches the same
board the continuation path reaches by embedding (confirmed directly) - which is what lets
every beat after `cylinder` keep its own single `moves` list regardless of which path
cylinder took to arrive. `Shell` gained `loopEntryIndex()` (`attract_.size() > 1 ? 1 : 0`,
replacing the duplicated `loopFrom` literal) and `attractLooped_`, set the instant the
sequence first wraps past its own end (`attractIndex_ + 1 >= attract_.size()`, not simply
"the next index equals the loop entry" - the sequence's very first forward step into
`cylinder` also satisfies that equality, and conflating the two was the first bug above).
`startAttractStep` reads both to decide, per visit, whether a beat marked
`continuesPosition` actually continues this time or falls back to `resetMoves`.

**A second, independent dangling-pointer bug**, found the same way the CLI's `embed`
command's own was: a crash, not a hang. `Position` holds a raw pointer to its
`VariantSpec`; the previous round's `startAttractStep` copied both the old session's
`VariantSpec` and its `Position` into locals *before* `startGame` replaced `session_`, on
the reasoning that the copies would outlive the old session - which is true of the
`VariantSpec` copy, but the copied `Position` carries its own internal pointer forward
unchanged, still aimed at the old session's (about to be destroyed) `VariantSpec`, not the
new copy sitting next to it. `embedOnto`'s own `from.cellCount()`/`from.at(cell)` calls
read through that stale pointer, not the separately-passed `fromVariant` parameter - so
the corruption was real but hadn't been exercised before, because no earlier beat in this
file had reused a `Position` copy across a `startGame` call this way. Fixed by finishing
the whole embedding - FEN string and all, the point at which no pointers remain - before
`startGame` runs, against a `VariantSpec` loaded fresh for exactly that purpose, following
the CLI fix's own resolution rather than rediscovering a different one.

**Tests.** `test_attract.cpp`'s loop test now runs all six beats plus the wrap and checks
the sequence is still playing afterward - it already existed, but the fixes above were
both found by it going from "silently stuck" to "cleanly fails" to "green" as each one
landed, not by new assertions. "Every cut...lands on the previous shape's own
end-of-stage" updated its `continuesPosition` expectations for every beat that now
continues, plus a check that `cylinder.resetMoves` is non-empty (the loop test is what
actually exercises it).

**Results.** `[app],[render],[arch],[attract]` (233 cases) and `ctest -R "^property$"`
(green, ~284s) both pass on the final build; `clang-format --dry-run --Werror` clean on
every touched file. Visual spot-check via `--shot --screen menu --t <seconds>` across the
full loop: `t=35` shows `TORUS3D_8X8` mid-sequence on the continuation line; `t=55` shows
`CYLINDER` at ply 18 with its move log reading `7. d1d4 7.. f8c5 8. d4c5...` - the
twenty-move `resetMoves` line, not the ten-move continuation one, confirming the wrap
fallback fires at runtime and not just in the headless test.

### Revision: material preservation and `resetMoves` superseded by looping to `standard`
### (2026-10-06, feedback, built directly by Claude)

Direct feedback on the round above, in two parts. First: "we in fact have fewer pieces in
the torus onwards variants compared to the standard 8x8 setup" - correct, and worth
separating into what was fixable and what was not. Second, once that distinction was laid
out and agreed on: "I do not want you to take the already-existing variants with
non-standard initial pieces' placements... I want you to JUST take geometry from them and
use the standard pieces' placements." Half right, worth being precise about which half -
`klein.toml` already declares bishop and queen as piece *types*; its restricted start
array is not what was dropping them. What *was* fixable: `torus3d_8x8.toml` is
TRAILER-SCOPED, not a catalogue variant, and its geometry - three plain periodic
identifications, no flip - is fully orientable, so excluding pawns from it was never
structural, just unexamined. It now declares `orientation_axis = "rank"` and a full pawn
type, with a full starting army (`RNBQKBNR` + 8 pawns per side, still separated along the
level axis the original K/R/N-only army was), confirmed loadable and mobile (163 legal
moves from its own fresh start) before it was ever wired back into the sequence.

**The trade-down was rebuilt to actually preserve material, not just reach zero bishops
and queens by whatever route got there first.** The previous round's line lost both of
White's knights, one of Black's, and a rook - the user's own complaint, read back against
the committed line, was accurate. The new line keeps both rooks on both sides for the
entire sequence (the original's worst single loss) and only White's two knights - down
from four pieces of uncounted-for collateral to one knight and nothing else. Every
bishop is removed by a direct sacrifice onto a king-defended square with the *king*
recapturing, never a rook; the queens go last, finishing in a rook-for-queen trade only
once nothing lighter reaches the square. All of it was re-derived and re-verified move by
move against the live engine the same way every line in this file is - not reasoned about
on paper.

**A second wrapped-diagonal hazard, independent of the one Part F already found and
documented**, turned up while verifying this new line could also be replayed as the
loop's own re-entry fallback (`resetMoves`, from the round before this one): a bishop
sacrifice onto f7 with the king recapturing is sound on the plain `standard` board, but
not on `cylinder` - `cylinder`'s own file-seam lets a queen still on d1 defend f7 the long
way round the wrap, turning the same sacrifice into a losing blunder the instant it is
replayed on the shape instead of the flat board. Found by building the `resetMoves`
fallback, testing it, and watching it fail at exactly that move - not predicted.

**Rather than patch around that hazard move by move, the loop itself was simplified to
make the whole problem disappear.** `AttractStep::resetMoves`, `Shell::loopEntryIndex()`
and `attractLooped_` - all three added in the previous round specifically to give the
loop's own re-entry beat a safe fresh-start fallback - are gone. The loop now wraps all
the way back to `standard`, not to `cylinder`: every beat but `standard` continues the
position right before it, so the only way every lap stays correct with one fixed `moves`
list per beat is for every lap to replay the exact same verified chain, cold open to 3-D
torus, start to finish. This is a strictly simpler design than the one it replaces - no
beat needs a second, wrap-only line, and there is nothing left to keep in sync if the
trade-down ever changes again - at the cost of replaying the (now fifty-plus-ply) opening
in full every lap rather than skipping straight to the shape beats. `standard`'s own
`continuesPosition` already defaulted to false (it has no predecessor, lap or not), so
this needed no new per-beat state at all, just removing the special case.

**Tests.** The loop test (`"the attract sequence plays every shape and loops back
forever"`) now runs the Shell through a complete second lap and checks it lands back on
`standard`, not `cylinder` - this is what actually caught the wrapped-diagonal bug above,
failing first with a silent hang and then, once the first issue was fixed, with a clean
`'e8f7' is not legal` before the loop-to-standard redesign made the whole class of
failure moot. The flat-icon-style cut test now runs one full lap plus the wrap back to
`standard` and on into `cylinder` again, checking the sculptural/icon-puck state at each.
No render-level test changed - continuity and the loop are `Shell` concerns, not geometry
ones, same as the previous round.

**Results.** `[app],[render],[arch],[attract]` (233 cases) and the full unfiltered suite
(502 cases, perft and the slow property suite included) both green on the final build;
`clang-format --dry-run --Werror` clean on every touched file. Visual spot-check via
`--shot --screen menu --t <seconds>`: the second lap's `t≈210` capture shows `STANDARD`,
ply 1, the full thirty-two-piece army, exactly the fresh canonical start - confirming the
loop-to-`standard` redesign works end to end, at runtime, not just in the headless test.

### Revision: a calm trailer camera, a shorter opener, and a camera-move mode
### (2026-10-16, feedback; spec written before the code)

Feedback: the trailer's camera "jitters a lot", turns to sit behind each piece, and
rotates far more than a trailer wants; the standard-board game runs too long ("by move 20
people will click off"); the trailer should offer the SHAPE-mode buttons (INVERT ...); and
the author wants to set the camera up themselves with keyframes, as a separate camera mode.

**Root cause of the swinging camera.** `applyAttractLook` (`src/gui/main.cpp`) returns
early for a variant with no play surface, so on the flat `standard` beat - the longest
beat by far - the player's own `cameraMode` (`route`) drove `Session::cameraOver`'s move
camera: yaw swung behind every piece, with `shotEnvelope` re-easing on each of ~50 plies.
On shape beats it forced `settings.cameraMode = "off"` instead, which both clobbered the
player's setting and was invisible to the flat case. The existing tests never saw it
because they drive a `Shell` with default settings, where the camera mode is already off.

**The fix: the trailer owns its camera policy, per session, never via `Settings`.**
`Shell::trailerCamera()` is one of `TrailerCamera::{Auto, Follow, Authored}`:
- `Auto` (default): move-follow is off on *every* beat (flat or shape); the only motion
  is `view::steadySway`, a pure function of the trailer clock - a slow sinusoidal yaw of
  at most ~4 degrees and a smaller pitch breathing, no per-frame state, so it cannot
  jitter, and continuous across a cut because the clock is not reset by one.
- `Follow`: the old chase (`route`), for whoever wants it.
- `Authored`: follow off and no sway; the camera holds still except where a keyframe
  eases it. Recording a keyframe switches to this mode.
The mode is applied to each beat's new `Session` in `startAttractStep`, so no `Settings`
field is touched and the player's own mode survives the trailer. `Auto`/`Follow` skip
`advanceCameraTrack` while attracting; `Authored` runs it.

**Camera edit mode** (`Shell::trailerEditing()`): pauses the trailer's clock (the pose
scalars stop following the beat's targets and follow `editPose_` instead, which the
buttons set), selects `Authored`, and enables stepping: `trailerStep(+-1)` plays/rewinds
one ply inside the beat (`Session::watchSeek`), `trailerGoto(beat, ply)` replays the
chain from beat 0 so a `continuesPosition` beat is reachable. The cue gains a `beat`
field (0 outside the trailer, `attractIndex_ + 1` inside it) because `klein` appears
twice and a trailer cue must not fire in an ordinary game; the text form writes `beat=`
only when nonzero. A keyframe also records `invert`. While a keyframe's pose has been
reached in `Authored` mode the beat's pose targets become that keyframe's pose, so the
driver does not ease the authored pose back to the beat default.

**The trailer panel** (`render/ui.cpp`, shown while `shell.attracting()`): STOP TRAILER,
CAMERA (cycles the three modes), EDIT; and in edit mode PLY -/+, BEAT -/+, REC KEY,
DEL KEY, KEY SPEED -/+, and for shapes SHAPE, INVERT, GHOST, PIECES (and SPREAD on the
3-D torus). Free orbit/zoom is unchanged. Keys `k`/`j` keep working.

**A shorter opener.** `standard` is cut to <= 20 plies and the trailer's per-ply pace is
raised (`AttractStep::pace`). The trade-down that Klein needs (no bishop/queen) can no
longer all happen on the flat board, so the Klein cut starts from Klein's own restricted
start (`continuesPosition = false`) while `standard -> cylinder -> torus` still continue
one game, and `klein -> torus3d` still continues. Every line is re-derived and replayed
through the engine by `test_attract.cpp`.

**Tests.** `view` : `steadySway` bounds, zero at zero weight, smooth (max per-frame step).
`app`: the cue/`invert` text round-trip incl. legacy lines without `beat=`; the trailer
leaves `Settings::cameraMode` untouched and the session's follow off on a flat beat even
with `cameraMode = "route"`; `Auto` does not run the track and `Authored` does; edit mode
freezes the beat clock and the cut; `trailerStep` forward/back lands on the right ply;
`trailerGoto` reproduces the chain; the standard beat is <= 20 plies.

**Status (2026-10-16): built.** `view/trailer_camera.hpp` (`steadySway`), `Session::watchStep`/
`watchSeek`, `CameraCue::beat` and `CameraKeyframe::invert`, the `Shell` trailer camera /
edit-mode / stepping API, the trailer panel, and `--trailer-edit` for a capture of it.
`applyAttractLook` no longer touches `Settings`; `applySettings` re-applies the trailer's
policy so a mid-trailer setting change cannot hand the chase back. The standard opener is
18 plies (was 54) at 0.22 s dwell; it trades both bishop pairs and both queens (Bxh6 gxh6,
Qxg4/Qxd7, Bxa6 bxa6, dxe5) so Klein still receives a legal position, and keeps every rook
and knight. Cylinder, torus, Klein and 3-D torus lines were re-found by an engine-driven
search (quiet, no shuffling, one wrapped step per shape beat) and the whole chain still
continues through `embedOnto`. Not verified by me: the live window (only `--shot` captures
were looked at); and the render suite's Vulkan offscreen tests were flaky with
`VK_ERROR_DEVICE_LOST` in this environment, independent of these changes. Open: keyframes
only ease on reaching a move (no mid-move timeline), and there is no in-app keyframe list.

### Revision: trailer look buttons while playing, layer colour swap, far-plane fix (2026-10-16)

INVERT / GHOST / PIECES are now shown while the trailer *plays*, not only in edit mode
(`Shell::setTrailerGhost`/`setTrailerFlatPieces` hold across beat cuts until the trailer
stops). `Settings::swapLayerColors` (settings screen, off by default) flips square colour on
every odd layer of a board with 3+ axes via `render::squareParity`, in both the surface and
lattice paths. `OrbitCamera::viewProj` now floors the far plane at `2 * distance + 50`: the
framed `farZ` was fixed at framing time, so zooming out past it put the whole shape beyond
the far plane and it vanished.

### Feature: each trailer move is previewed (2026-10-16)

Before a move plays, the piece about to move is selected and its legal squares light, for
`Settings::trailerMovePreview` seconds (default 0.3, slider 0-0.5 on the settings screen,
0 = off). `Session::startWatching(file, dwell, preview)` owns it: once the dwell has passed
`advanceWatching` selects the next move's `from` cell (via `GameRunner::nextMoveText`),
holds, then plays. Only the trailer passes a preview; "Watch a game" is unchanged. The first
move of each beat starts at once, and edit-mode stepping bypasses the preview. Tests:
`[preview]` in `test_watch.cpp` and `test_trailer.cpp`.

### Fix: `BEAT >` / `< BEAT` crashed the app (2026-10-16)

The trailer panel holds `app::Session& session` for the rest of its frame, and a beat jump
(`trailerGoto` -> `startAttractStep` -> `startGame`) replaces `Shell::session_`; the panel
then read the destroyed session (the "beat n of m - move k" note, and the HUD after it). The
interface now calls `Shell::requestTrailerJumpBeat`, applied at the top of the next
`advanceAttract`, before anything holds the old session. Rule: UI code must never call
something that replaces the session. Test: "a beat jump asked for by the interface waits for
the next frame". Not reproduced in a live window (no input driver here).

### Revision: a smooth camera spline, fractional keyframes, morph in keyframes, restart (2026-10-16)

**Motion.** The trailer's keyframes are no longer waypoints eased toward one at a time (a
stop-and-start leg per keyframe, which read as jagged). In the authored camera they are one
spline over the whole trailer timeline: `view::splineCamera` - Catmull-Rom for target,
distance and lens, *squad* (the quaternion spline) for orientation - which is C1 through
every keyframe, turns the short way round, and follows the keys closely rather than hitting
them exactly in between. `view::splineScalar` does the same for the pose scalars. The old
per-cue ease remains only for keyframes recorded in an ordinary (non-trailer) game.

**Timeline.** `Shell::trailerTime()` = beats laid end to end (`beatOffset`, a beat spans
`moves + 1`) + the move + how far through it (`moveElapsed_ / moveDuration_`, a move being
its animation + preview + dwell). Held at the first/last keyframe outside the range.

**Fractional keyframes.** `CameraCue::sub` is eighths of a move (text `sub=`, written only
when nonzero). Edit mode has `GRID 1 / 1/2 / 1/4 / 1/8` and `< STEP` / `STEP >` that move by
one grid unit and cross into the next/previous move; REC KEY records on that point, and the
camera previews the spline's value there.

**Morph.** `formed`, `ghost`, `shellSpread`, slide are spline channels; `flatPieces` and
`invert` step at their keyframe. While keyframes drive, the beat's own pose easing and its
settle check stand down, so an authored morph neither fights the driver nor holds a beat
open. Edit mode records the edit pose's targets, not the mid-ease value.

**RESTART** (trailer panel, always shown): deferred a frame like a beat jump; keeps the
camera and edit modes. Per-keyframe ease speed no longer applies in the trailer, so its
buttons are gone. Tests: `[spline]` in `test_camera.cpp` / `test_trailer.cpp`.

### Fix: an authored zoom-in landed on the tightest framing (2026-10-16)

`main`'s per-frame "ease the frame toward the shape while a morph is in flight" ran for the
whole trailer: the trailer's `geometryFormed` never equals the SHAPE toggle's target (`1`
on the flat opener with the toggle off, mid-value under the spline), so `morphInFlight` was
always true and `Session::easeFrameOn` pulled target and distance to the exact fit of the
board every frame, on top of the keyframe spline - a halfway zoom came out close-up. It also
ate manual zoom in edit mode. It now stands down while `Shell::trailerOwnsCamera()`. The
spline itself was right: the new Shell test records 40 / 25 / 10 and reads 25 at the middle
key. Not seen in a live window.

### Feature: a keyframe timeline (2026-10-16)

A strip along the bottom of the trailer (`Ui::buildTrailerTimeline`), shown whenever the
trailer runs: one segment per beat (labelled, the current one lit, a tick per move), a
diamond per keyframe (lit under the playhead), and the playhead at `Shell::trailerTime()`.
In edit mode a click or drag scrubs: the pointer snaps to a keyframe when within a few
pixels, otherwise to the grid in use, and asks `Shell::requestTrailerSeek` - deferred a
frame like the beat jumps, since a cross-beat seek replaces the session. After a cut the
front end calls `refreshTrailerView` so the camera shows the spline's value, not the
re-framed default. Hover shows the beat, move and fraction. Time <-> cue mapping
(`trailerCueAt`, `trailerLength`, `beatOffset`, `trailerKeyTimes`) is in `Shell` and
tested (`[timeline]`); the drawing is not. Not yet: dragging a keyframe to retime it.

### Fix + feature: the shape-to-shape morph survives keyframes; morph and move speed sliders (2026-10-16)

**Fix.** Once keyframes existed, the spline owned the pose scalars for the whole trailer and
held the first keyframe's morph value backwards, so every beat's own reveal (the cylinder
rolling up, the torus bending, Klein folding, the 3-D torus spreading) was gone. The camera is
still one global spline, but the *pose* (morph, ghost, spread, slide, piece style, invert)
now belongs to the keyframes only from the first keyframe of the current beat on
(`Shell::poseKeysFrom`); before it, and in a beat with no keyframes, the beat's own morph
plays, and it eases on from wherever the last keyframe left the pose. Test: "a beat's own
morph plays until its first keyframe takes over".

**Sliders.** `Settings::trailerMorphSpeed` and `trailerMoveSpeed` (0.25-4, default 1) scale
the trailer's shape morph and every piece's move animation on top of the player's own
settings, so tuning the trailer leaves their game alone. Live sliders sit in the trailer
panel (`MORPH SPEED`, `MOVE SPEED`) and on the settings screen. Tests: `[speed]`.

### Feature: PAUSE, and click-to-seek on the timeline while playing (2026-10-16)

`Shell::setTrailerPaused` freezes the trailer's clock, game and cuts without the edit
controls (edit mode still implies it; the PAUSE/PLAY button reads only then). The timeline
now seeks in any mode: a click or drag lands on the pointed-at beat, move and eighth (a
deferred seek like the beat jumps), and playback carries on from there - or stays paused if
it was. Playing seeks start part-way through the move (`seekPhase_`) and keep the beat's own
morph start rather than snapping the pose to its end state, which edit-mode seeks still do.
Tests: `[pause]`.

### Revision: render the trailer, find every discontinuity by diffing frames (2026-10-17)

**Method.** `--trailer-clip DIR` renders one lap of the trailer, frame by frame at a fixed
step, in cinema (no interface), through the same per-frame logic as the interactive loop;
the RENDER button runs it in a child process with the player's own settings and keyframes,
piping frames into ffmpeg (a 1080p lap as PPMs is ~11 GB), or `--trailer-frames` keeps
PPMs. A script then took the mean absolute difference between consecutive frames and
flagged any frame more than 3x its neighbours - those, and only those, were investigated.

**What the diffs found, and the fixes.** The first lap flagged every cut:
- *standard | cylinder* (diff 15 vs ~1): the opener was the flat board drawn by the flat
  renderer, the roll the shape renderer, with a different camera and a half-rolled start.
  The opener is now the **cylinder at `formed = 0`** - the shape renderer's flat board is
  the standard board turned a half turn about the vertical (not a mirror), so the camera
  is turned to put white nearest - held flat for 18 plies, then the same board rolls up.
  The opener line was re-searched on the cylinder with every wrapping move forbidden (on a
  board that still looks flat a wrap reads as a teleport); the later lines re-derived.
- *every cut re-framed the camera and reset it to the new session's default angle.* The
  camera is now carried across (`startAttractStep`), a cut no longer snaps the frame, and
  `easeTrailerFraming` follows the shape's bounds each frame (a quarter-second lag) when
  the camera is not the author's.
- *morphs started at full speed* (a linear ramp: the instant the motion "switches on").
  `view::EasedMorph` is a smoothstep with the same duration as before, and each beat has
  a `morphSpeed` (the torus's bend is the slowest - it was the one that "wound up", in a
  quarter of a second). `Settings::trailerMorphSpeed` now defaults to 0.5.
- *klein -> 3-D torus* (diff 25): the shells' collapsed state was a ring three times the
  torus's size. At `shellSpread = 0` `playShapeAt` is now the plain 2-D torus exactly and
  blends to the shipped shells; coincident levels' ghost alphas no longer multiply.
After these, no frame in the 1839-frame lap is above 1.2 mean difference (was 25).
Not fixed: Klein's coloured seam rails vanish at the cut into the 3-D torus (shells have
none), and the sculpture-to-icon piece swap on the Klein beat is still a snap.

**Also.** The authored camera is the default; with no keyframes it frames the shape itself
(`trailerAutoFrames`), with keyframes it is the author's (`trailerOwnsCamera`). Keys:
left/right between keyframes, shift+left/right by the grid (pauses into edit), Enter adds,
Del removes the one under the playhead, Space pauses/resumes. Orbiting, zooming or sliding
while the trailer would take the camera back pauses into edit mode instead of fighting the
hand (`trailerUserTookCamera`) - the likeliest cause of the "3-D mouse move discontinues"
report: the spline or the auto-framing overwrote the drag every frame. The in-game stutter
was the loop waiting for events (10 frames a second) whenever no piece was in flight;
the trailer now counts as animating.

### Revision 2026-10-17 (b): the standard opener is back, and every cut re-checked frame by frame

The cylinder opener of the previous revision was a mistake: the trailer opens on the real
`standard` board again, and the cylinder beat starts flat (`formedFrom = 0`), so the roll
is the first thing that moves. Keyframes are pinned to (variant, beat), which is why the
variant swap made the standard beat's keyframes look deleted - the track file was never
touched.

Found by rendering a lap with the author's own settings and keyframes (`--trailer-clip DIR
--user-settings --trailer-frames`) and diffing consecutive frames:

- **Keyframe targets are relative to the shape's centre** (`Shell::setTrailerShapeCentre`):
  a recorded target is measured from the standard board's centre and applied relative to
  the shape's bounds centre, so a camera authored on one shape stays on the middle of the
  next instead of staring at empty space beside it.
- **The standard beat's camera takes the shape renderer's half turn** (`flatBoardYaw`),
  and the 0.7 lift above the board is kept over the shape, so the cut is the same picture.
- **The trailer clock is continuous across a cut** (`trailerShift_`): a beat's first move
  starts at once, which skipped the timeline ~1.5 moves ahead in one frame and made the
  camera spline lurch; the shift decays at 0.8 units/s.
- **The player's thickness, width and rank slide no longer pop in** on the first rolled
  frame: thickness (Tube, Torus, and now the Klein ring) and the torus's width ramp in
  with the roll, the torus's rank slide with the bend; the keyframe slide eases in over the
  stretch before a beat's first keyframe, as a remainder modulo one lap.

Remaining by measurement (diff against a ~1.0 median): the standard-to-cylinder cut ~10
(the two renderers shade differently: slab, colours), the Klein cut ~5 (icon pucks swap in),
the 3-D torus cut ~6, and ~13-17 where the rolling cylinder fills the frame under the
author's close-up keyframes.
