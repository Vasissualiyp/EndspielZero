# M17 — Play on the shape: the geometry view

**Goal.** The board stops being a *representation* of a topology on a square grid and
becomes the topology. Press a button and the flat 8×8 becomes the object the variant is
actually played on - the files roll into a cylinder, the cylinder closes into a torus, the
rank edges pinch into a Möbius band or a Klein bottle, a mirror box grows its silvered
walls - and you keep playing on it. Turn it off and it lies flat again. The shapes already
exist for the New Game screen (`render/overture_scene.cpp`); this makes the *play* board
be the same shape, so "the a-file and the h-file are the same edge" is not a cyan line you
have to believe but a place you can point at.

**Exit condition.** On any shipped glued variant, a button (and a key) morphs the play
board into its own embedding, keeps the position and the move valid on it, lets a move be
made by picking a cell on the surface, and morphs back to the flat board; `standard` and
every non-glued variant are unchanged.

---

## M17.0 Why this is not the overture

The overture animates a fixed, authored scene: a separate 8×8 built for the menu, no
`Session`, no legal moves, no picking. M17 draws the *live* position on the warped surface.
The two share the warps and nothing else, and the separation is what keeps the overture
cheap and the game honest: M17 must never invent a move or a cell, and the overture must
never read the engine.

Depends only on M3 (the identification model), M4 (the renderer and `view::layout`) and the
M13.2 warps, all done. It is **Wave 1, before M16.1** in the release sequence, because it is
the most clippable thing the game can do with a board - a trailer that shows chess being
played *on a donut* is the store page's whole argument, and it needs no opponent and no
campaign.

---

## M17.1 The surface, from the variant

`app::overtureSignature` (M13.1) already turns a resolved spec into a `SurfaceKind`. M17
reuses it: the play board's embedding is `render::derivedSurfaceAt(kind, u, v)` (M13.2),
the exact surfaces the library screen already draws, asserted pointwise-equal to the
hand-authored ones. No new geometry; a variant that glues two axes is a torus whether it is
in a menu or a game.

## M17.2 The cell on the surface

- Each cell's centre is placed by the surface at its lattice `(u, v)`, exactly as the
  overture places its cells; its four corners come from `(u, v)` at the cell's own corners,
  so a cell bends with the surface instead of floating over it.
- Pieces ride their cell's normal (the finite-difference normal the overture uses), so a
  rook on the far side of a torus stands up outwards, not into the surface.
- The flat board is `t → 0`; the morph is the same blend the overture uses, driven by a
  UI clock rather than by the overture player.

## M17.3 Playing on it

This is the part that is engine work, and the part to get right:

- **Picking.** `Session::clickPixel` currently ray-casts against the axis-aligned cell boxes
  of the flat layout (`view::pickBox`). On the surface the cells are warped, so picking
  either ray-casts against the *curved* cell quads, or inverts the warp per cell and casts
  in lattice space. Whichever is chosen, the invariant is the one ADR-0011 already set:
  **the ray follows what was drawn**, or a click selects a different cell than the one under
  the cursor. The move camera (M11) already reads the trace and not the topology, so the
  route a move takes is drawn on the surface for free once the cells are.
- **Labels and marks.** Coordinates, the selection ring, legal-move highlights and the last-
  move mark are drawn on the surface cells, not in the flat layout.
- **The seams.** The edges the geometry glued keep their portal hue (as `view::seams` already
  gives them), so the surface and the seam colour agree.

## M17.4 Higher dimensions

For D ≥ 3 the "shape" is not an embedding but the lattice itself. M17 reuses the derived
D≥3 grid (M13.4) and the `view::layout` the game already draws: the toggle extrudes the
board along its depth and grid axes rather than warping it. This is a smaller step than the
2-D surfaces and mostly already exists.

**Superseded by M17.12**, which asks for the real shapes above two dimensions rather than
the lattice: `torus3d` as nested shells, `hyper4` as a hypercube, `t6` as the quintic.

## M17.5 The control and the setting

- A `Settings::geometryView` (`flat` | `surface`) beside `flatView`, and a button and a key.
  The identity is `flat`, so the shipped look is a strict no-op and every existing golden
  still holds.
- The morph is a pure function of a progress value, like the overture, so it is
  `--shot`-able and reversible; the front end owns the clock.
- `standard` and every non-glued variant stay flat: there is no surface to become, and a
  setting that did nothing there would be a lie.

## Tests and acceptance

- **The surface is the one the overture draws:** the play board at `t = 1` for `cylinder`,
  `torus`, `mobius` and `klein` uses `derivedSurfaceAt`, and a test asserts the cell centres
  land exactly where the overture's do for the same `(u, v)`.
- **Picking agrees with drawing:** for every cell, project its centre with the *effective*
  camera and pick that pixel back; it selects the same cell. On a glued board too.
- **`flat` is a strict no-op:** with `geometryView = flat`, every existing render golden and
  the flat-layout picking tests are unchanged.
- **The position is not touched:** toggling the view changes nothing in `VariantId`, the
  hash, the move list, or the FEN; it is presentation, like `shape`/`height`.
- **`--shot`:** `endspielzero_gui torus --shot t.ppm --geometry surface` renders the board as a
  torus, validation-clean, and `--clip` morphs it.

### Acceptance facts

1. A button turns the play board into its variant's shape and back, and a move is playable
   on the shape.
2. Picking on the surface selects the cell under the cursor, on a glued board as on a flat
   one.
3. `standard` is bit-identical; a glued variant's true colour/edge semantics still hold.
4. The morph is a pure function of `t`, so a still is reproducible and the reverse is free.

## Dependencies and ordering

1. **M17.1/M17.2** the surface and the cells (reuse M13) — the picture.
2. **M17.3** picking and marks — the part that makes it a *game* and not a diorama.
3. **M17.4** the D≥3 lattice.
4. **M17.5** the setting, the button and the key.
5. Tests land with each step.

## Risks and non-goals

- **Picking on a curved, possibly non-orientable surface is the whole risk.** A wrong
  inverse picks the wrong cell and the view becomes untrustworthy; the test above is the
  gate. If an exact inverse is out of reach, ray-cast the drawn quads, which cannot disagree
  with the drawing because it is the drawing.
- **Readability.** A warped board is harder to play on than a diagram, which is why the flat
  view stays the default and the surface is a deliberate mode, not a replacement.
- **Non-goals:** no change to movegen, geometry or the temporal model; the toggle is
  presentation and must not enter `VariantId`; no free-form reshaping of authored variants;
  the surface is derived from the identifications, never drawn by hand.

---

## M17.7 - M17.18 The next increment: a shape you can handle

M17 shipped a board you can play on its own surface, and playing on it turned up six
things the first pass does not do. They are specified here, smallest first; each names what
is wrong, what is known about why, what to build and what pins it. M17.13 and M17.14 were
added after M17.7-M17.12 shipped, diagnosing two regressions the M17.12 first pass
introduced - see each section's own **Have** for the evidence. M17.15-M17.17 are a third
batch, asked for once a move could be made on the shape at all: the piece's own travel, the
move camera following it there, and keeping that camera from looking through the shape's
own geometry to find it. M17.18 is a fourth, a defect found by playing with the slide on
`klein` specifically.

### M17.7 An INVERT control, and pieces that stay on the outside

**Want.** A button in the game rail, beside `SHAPE`, that turns the shape inside out and
back: what was inside the hole is outside, and what was outside is in.

**Have.** `SurfacePose::evert` already is that turn, and it is already continuous, pure and
capturable (`--evert`, `[`/`]`). What is missing is the control and - the part that makes
it *play* rather than just look - what happens to the pieces.

**Build.**

- A `INVERT` button in `src/render/ui.cpp`'s game rail, gated exactly as `SHAPE` is
  (`hasPlaySurface`), lit while inverted. It sets a *target* of 0 or 1; the front end
  (`src/gui/main.cpp`, beside `steppedBack`) eases `Settings::geometryEvert` towards it
  over about half a second. The ramp lives in the front end and not in `PlaySurface`,
  because a pose that read a clock would stop being reproducible - the same split as
  `app::OverturePlayer` and `overtureScene`.
- **The outward side has to follow the turn.** `PlaySurface` takes its normal from
  `cross(dv, du)`, which is outward at rest; sweeping the ring radius through zero to its
  negative reverses the parametrisation's handedness, so past the halfway point that vector
  points *into* the shape and the pieces are left standing inside it, hidden and unplayable.
  The normal has to be chosen by which side is out of the shape now, not by the formula's
  sign - and the piece, the seat and the patch's thickness all read it.
- Esc/`G` still leave the view; inverting is not a mode of its own.

**Tests.** No piece's foot is inside the surface at any `evert` in [0, 1] (sample it: the
seat plus half a thickness along the normal is further from the local tube axis than the
seat is). The button's two states capture: `--evert 0` and `--evert 1` both render
validation-clean with every piece visible from outside.

**Acceptance.** Press INVERT on `torus`: the hole closes, reopens inside out, and the game
is still playable with every piece on the outside. Press it again and the board comes back.

### M17.8 The drag axes are the wrong way round

**Want.** Middle-drag left and right should move the board the way the *other* axis does
now, and up and down likewise: the two are swapped.

**Build.** One swap in `src/gui/main.cpp`'s motion handler - `xrel` drives
`geometrySlideV` and `yrel` drives `geometrySlideU` - and the `slidesAlongRanks` gate moves
with it, so the axis that is not glued is the one that stays still.

**Acceptance.** On `torus`, dragging left and right carries a1 along the ranks; on
`cylinder`, which glues only the files, left and right does nothing and up and down slides.

### M17.9 A full lap, and not a two-cell one

**Want.** Sliding the board should carry a1 to b1, to c1, all the way round and home.

**Have.** It comes home after *two cells* and snaps. The cause is known and is one line:
`wrapSlide` in `src/render/play_surface.cpp` is `fmod(s, 2.0f)` applied to a slide measured
in **cells**, while the warps repeat after two *laps* - `2 * nx` cells along the files and
`2 * nz` along the ranks. (One lap for the orientable surfaces; two where a seam reverses a
coordinate, which is why the window is two.) `Settings` normalises with `fmod(…, 16.0f)`,
which is two laps only for an eight-wide board; the wrap belongs in one place, in cells,
derived from the board's own extent.

**Tests.** The existing "a whole lap brings the board home" case passes today *for the
wrong reason* - `fmod(16, 2)` is 0, so it compares the board against itself - and must be
rewritten: a lap of `2 * nx` cells reproduces the unslid board, **and** a half lap does
not, and every intermediate slide moves every seat by less than a cell.

**Acceptance.** Hold the middle button and drag: a1 walks the whole file and arrives back
at a1, having crossed the seam once (twice, mirrored, on `mobius`).

### M17.10 Semi-transparent squares

**Want.** A button that makes the board translucent, so the far side of the shape and the
pieces standing on it can be seen through the near side.

**Build.**

- A `Settings::geometryGhost` (an alpha, 0.35 to 1) and a `GHOST` button beside `SHAPE`.
  The alpha rides in `MeshVertex::color.a`, which already exists and costs nothing.
- **Blending is off in the board pipeline today** (`blendEnable` is left false in all three
  pipelines in `src/render/board_renderer.cpp`). The surface mesh needs a second pipeline -
  same shaders, same vertex input - with `blendEnable`, src-alpha / one-minus-src-alpha,
  and `depthWriteEnable = false`; the opaque pass is unchanged.
- Order: pieces and everything else opaque first, then the translucent board with the depth
  test on and depth writes off. No per-triangle sort: the error that leaves is between two
  sheets of the same translucent board, which reads as what it is.
- The pieces stay opaque. The point is to see them.

**Tests.** A validation-clean GPU render at a ghosted alpha whose luminance variance is
*higher* than the opaque one (the far side now contributes). `geometryGhost = 1` is
bit-identical to today. The flat board never takes the blended pipeline.

**Acceptance.** On `torus` with GHOST on, the pieces on the far side of the ring are
visible through the near side, and a click still selects the near cell.

### M17.11 Centre the shape

**Want.** `torus` and `klein` sit off-centre in the window.

**Have.** Unknown which of three it is, and the first job is to measure rather than guess -
project `PlaySurface::bounds()`'s centre with the effective camera and compare it with the
board rectangle's centre. The suspects, in order:

1. `Session::setBoardAspect` re-frames on `view::boundsOf(placements_)` - the **flat**
   layout - and only keeps the previous target because `framedOnce_` is set. A target
   computed for one box and a distance computed for another do not centre anything.
2. In the capture path `frameBoard` runs before the real board aspect is known, so the
   framing it does is for a square window.
3. `PlaySurface::bounds()` pads by a constant 0.9 on every side, which is symmetric in the
   world but not on screen once perspective is applied.

**Build.** While the geometry view is on, the surface's own bounds are the only thing the
camera is ever framed on - on entry, on resize, and on a variant change - and the pad is
applied where it belongs, which is to the shape's extent and not to the camera's target.

**Tests.** For each of the four shapes, project the bounds' centre through the effective
camera and assert it lands within 2% of the board rectangle's centre; and the same after a
resize.

**Acceptance.** Every shape opens centred, and stays centred when the window is resized.

### M17.12 The shapes above two dimensions

**Want.** `torus3d`, `hyper4` and `t6` get shapes of their own: nested shells, a hypercube,
and the quintic.

**Have.** `hasPlaySurface` is true only for a glued *two*-dimensional board, and M17.4
deferred the rest on the argument that above two dimensions the extruded lattice already is
the shape. That argument holds for a board with no gluing; it does not hold for these
three, and the library screen already draws all of them:

- **`torus3d`** - the overture's `tube` with its `shell` parameter: two of the three
  gluings give T², the level axis becomes the radial direction, and the four levels come
  out as four nested shells about one core circle. The third gluing has nowhere in space to
  go, which the overture says out loud and the play board must too.
- **`hyper4`** - a tesseract: a 4-cube projected into three dimensions as two nested cubes
  joined corner to corner, the fourth axis being the nesting.
- **`t6`** - the Calabi-Yau quintic already in `src/render/quintic.hpp`, which the T6
  overture collapses onto. Whether a 6-torus's *adjacency* survives that embedding is the
  open question, and it is the question the work has to answer first: if the quintic cannot
  carry the lattice so that neighbouring cells are neighbours on it, the honest answer is
  to say so in the view rather than to draw a pretty lie.

**Build.** This is the increment that needs a structure rather than a patch.
`PlaySurface` becomes the two-dimensional case of a `PlayShape`: the same three products -
a patch per cell, a seat per cell, and a gapless grid to pick against - produced for any
dimension, keyed off `app::SurfaceKind` extended with `Torus3d`, `Hypercube` and `Quintic`.
Everything above it - the mesh builder, the pose, the picker, the camera framing - is
already written against those three products and should not need to know.

**The invariant that decides whether any of it is honest**: two cells adjacent in the
lattice are adjacent in the drawing, and two cells that are not, are not. A projection that
breaks it is a picture, not a board, and must be labelled as one.

**Tests.** Per shape: every cell has a patch, the patches do not cross, picking returns the
cell under the cursor, and the adjacency invariant above holds for every pair the geometry
glues. A `--geometry` capture per variant, validation-clean.

**Acceptance.** `endspielzero_gui torus3d --geometry` plays on four nested shells;
`hyper4 --geometry` on a hypercube; `t6 --geometry` either on the quintic, or on the
extruded lattice with the reason stated in the interface.

### M17.13 `torus3d`/`hyper4` read as a scatter of fish scales, not a shape

The M17.12 first pass shipped and immediately showed two bugs, both diagnosed below by
reading `PlaySurface::buildStacked` (`src/render/play_surface.cpp:254-345`) and confirmed
by printing real seat data (see **Evidence**). `endspielzero_gui torus3d --geometry` draws a
fan of thin, wildly-angled slabs with no visible tube or ring; `hyper4 --geometry` draws a
chaotic cloud of small squares with no visible cube-in-a-cube.

**Want.** `torus3d --geometry` reads as four smooth, nested rings; `hyper4 --geometry`
reads as two nested cube-shaped clusters, the way the M17.12 plan intended.

**Have - the bug.** `buildStacked`'s local helper `uprightSite` (lines 261-269) builds the
tangent frame for every cell from its immediate file/rank neighbours, found like this:

```cpp
c.c[0] = static_cast<std::int16_t>(std::clamp(base.c[0] + df, 0, nx - 1));
c.c[1] = static_cast<std::int16_t>(std::clamp(base.c[1] + dr, 0, nz - 1));
```

This **clamps** the neighbour's coordinate at the axis's two ends. That is correct only
for a *bounded* axis (no cell past the edge). `torus3d`'s file and rank axes are declared
`kind = "periodic"` in `variants/torus3d.toml` - there is no edge, a cell at file 0's
"previous" neighbour is file 3 wrapped around, not file 0 again. Clamping instead of
wrapping makes `uprightSite` return the **cell's own position** as its neighbour at every
boundary index, which for a 4-wide periodic axis is every index touching 0 or 3 - most of
the board. The resulting tangent is then a one-sided, half-length, wrongly-based estimate,
and the surface normal (`cross(tangentV, tangentU)`) inherits the error: each affected
cell's local frame tilts in whatever direction that bad tangent happens to point, which is
not continuous from one cell to the next. That is the "fish scales"/fan-of-slabs picture.

`hyper4` has no periodic axes at all (a plain 4-D box - "no identifications", per
`docs/plan/M17.12-shapes-above-two-dimensions.md`), so clamping there is the *correct*
choice of neighbour - there genuinely is no cell past the edge. But the same formula still
mis-sizes the boundary: `cellU = 0.5f * view::length(tangentU)` (line 305) assumes a
**centred, two-sided** difference spanning two cells; at a true boundary only one side is
real and `tangentU` spans one cell, so halving it produces a tile **half the width** it
should be. That is the discontinuity in `hyper4`'s scattered look - every cell on a
boundary face of the tesseract is drawn at roughly half the size of its interior
neighbours, which breaks the fan of squares apart.

**Evidence.** A diagnostic dump of `PlaySurface::build(torus3d).seats()` at level 0, every
`(file, rank)`, shows exactly the signature of the clamp bug - a mirror-symmetric pattern
around the middle of each 4-wide axis instead of the uniform values a fully periodic 4x4
torus must have (every cell is equivalent by the identification group; `torus3d.toml`'s
own comment says so: *"no cell is special"*):

```
f0 r0: stepU=0.889 stepV=5.355      f1 r0: stepU=1.257 stepV=5.355
f0 r1: stepU=0.889 stepV=7.573      f1 r1: stepU=1.257 stepV=7.573
f0 r2: stepU=0.889 stepV=7.573      f1 r2: stepU=1.257 stepV=7.573
f0 r3: stepU=0.889 stepV=5.355      f1 r3: stepU=1.257 stepV=5.355
f2 r*: stepU=1.257 (matches f1)     f3 r*: stepU=0.889 (matches f0)
```

`stepU` takes exactly two values (0.889 at file 0/3, 1.257 at file 1/2) and `stepV` takes
exactly two values (5.355 at rank 0/3, 7.573 at rank 1/2) - a clean f↔(3-f), r↔(3-r)
mirror, which is the clamp substituting the opposite-parity neighbour's own position. A
correctly wrapped periodic axis has no such mirror: every file value is interchangeable
with every other by the torus's own symmetry, so `stepU` must come out the same (within
floating-point/metric tolerance) for all four, and likewise `stepV`.

**Build.** Replace `uprightSite`'s per-axis neighbour lookup with one that treats a
periodic axis as periodic and a bounded axis as bounded, independently for axis 0 (U) and
axis 1 (V):

- **Is the axis periodic?** `v.geom.boundaryKind(axis, Side::Max) == BoundaryKind::Periodic`
  - the same check `app::overtureSignature` already uses (`src/app/overture.cpp:71`,
  `g.boundaryKind(a, Side::Max) == BoundaryKind::Periodic`). `Side` and `BoundaryKind` are
  declared directly in namespace `cb` (`src/geometry/geometry.hpp`).
- **Periodic axis:** wrap, never clamp - `(idx + offset + extent) % extent` for both
  `offset = +1` and `offset = -1`. The existing central-difference formula
  (`tangent = p_plus - p_minus`, `cellSize = 0.5f * length(tangent)`,
  `ex = normalize(tangent)`) is otherwise unchanged and is now correct for every cell,
  boundary or not, because there is no boundary.
- **Bounded axis, interior cell** (`0 < idx < extent - 1`): unchanged - the existing
  central difference is already correct here.
- **Bounded axis, boundary cell** (`idx == 0` or `idx == extent - 1`): only one real
  neighbour exists. Use a one-sided difference **scaled to one cell**, not a clamped
  two-sided one scaled to a (fictitious) two cells:
  ```cpp
  // idx == 0: only the +1 side is real.
  tangent = p_plus - centre;       // NOT p_plus - centre_clamped_to_self
  cellSize = view::length(tangent);  // NOT 0.5f * length(...)
  exDir = view::normalize(tangent);
  // idx == extent - 1: only the -1 side is real, symmetric construction.
  tangent = centre - p_minus;
  cellSize = view::length(tangent);
  exDir = view::normalize(tangent);
  ```
  `centre` is already computed in `buildStacked`'s outer loop (line 294) before
  `uprightSite` is called, so it is available to pass in or capture.
- Apply this choice **independently per axis** (U from axis 0, V from axis 1): a future
  authored shape could glue one in-plane axis and not the other, and the code should not
  assume both behave the same way. `torus3d` exercises the periodic branch on both axes;
  `hyper4` exercises the bounded-boundary branch on both axes; nothing today exercises a
  mix, but the code must not rule it out.
- The `normal`, `ex`, `ey`, `quat`, and the patch/quad construction that follow are
  unchanged - they already consume `tangentU`/`tangentV`/`cellU`/`cellV` generically.

**Tests.**

- `tests/render/test_play_surface.cpp`, strengthen the existing
  `"a three- and four-dimensional variant plays on its own shape"` case (currently only
  checks `stepU/stepV > 0.01f`, which is why this bug shipped): for `torus3d`, assert every
  seat's `stepU` is within a small tolerance (a few percent) of every other seat's `stepU`,
  and likewise for `stepV` - citing the variant's own "no cell is special" claim as the
  justification. This directly fails today (0.889 vs 1.257, a 41% spread) and must pass
  after the fix.
- New case for `hyper4`: for a cell one step in from a true boundary (e.g. file index 1)
  and the boundary cell next to it (file index 0), `stepU` must be within roughly 20% of
  each other - not the ~40% systematic drop the halved one-sided formula produces today.
  Check this on all four axes (file, rank, level, aeon are all bounded on `hyper4`; pick
  one representative boundary pair per axis).
- Keep the existing patch/picking tests passing unchanged - the patch and pick-ray
  construction do not change, only the tangent inputs they are built from.

**Acceptance.** `endspielzero_gui torus3d --shot t.ppm --geometry` shows four continuous,
smoothly nested rings with no isolated radiating slabs; `endspielzero_gui hyper4 --shot h.ppm
--geometry` shows two legible, roughly cube-shaped clusters of squares rather than a
scattered fan. Both stay validation-clean.

### M17.14 The camera does not follow a variant switch while already in shape mode

**Want.** Loading a different variant - or the same one again, "start over" from the
library screen - while the geometry view is already on must centre the camera on the *new*
variant's shape. It must not leave the camera framed on the previous variant's shape, or
on the new variant's flat board.

**Have - the bug.** `Settings::geometryView` (and `geometrySlideU/V`, `geometryEvert`,
`geometryInvert`) live on `Shell::settings_`, a single member that `Shell::startGame`
(`src/app/shell.cpp:208-`) never resets - it only copies specific fields
(`flatView`, `hotSeat`, `confirmMoves`, `cameraMode`, `followStrength`, `theme`) onto the
freshly-built `Session`. `geometryView` itself is not one of them, so it is **sticky**
across a variant change: if it was on for variant A, it is still on for variant B.

`Session::create` (`src/app/session.cpp`) frames its initial camera from the **flat**
layout's bounds unconditionally - `Session` does not know `PlaySurface` exists.

The only place that ever frames the camera on the *shape's* bounds is `frameBoard`
(`src/gui/main.cpp:198-217`), called from exactly one place in the main loop
(`src/gui/main.cpp:941-953`):

```cpp
const bool nowSurface = optionsFor(*shell).surface;
if (nowSurface != wasSurface) {
  renderer->setOptions(optionsFor(*shell));
  frameBoard(*shell);
  wasSurface = nowSurface;
}
```

This fires only when `nowSurface` (purely a function of `geometryView` and the *current*
variant's `hasPlaySurface`) differs from last frame's value. It says nothing about which
variant is loaded. So: player turns SHAPE on for `torus` (`nowSurface` flips false→true,
`frameBoard` fires, correctly centred); player then opens New Game and picks `torus3d`
without turning SHAPE off. `request.loadVariant` fires
(`src/gui/main.cpp:928-931`), `startGame` builds a brand-new `Session` for `torus3d`,
freshly flat-framed by its constructor - but `geometryView` was already `true` and still
is, so `nowSurface` is `true` both before and after this reload. The toggle check sees no
change and never calls `frameBoard`. The camera stays on `torus3d`'s flat-board framing:
"the window is centred at the centre of the flat board, not the centre of the shape."

**Build.**

- Extract the policy `frameBoard` currently implements into a plain, headless-testable
  function in the render layer (which already depends on `app`, see `render/ui.hpp`'s
  `#include "app/session.hpp"` - this is not a new layer dependency), alongside
  `PlaySurface` since it is the thing being framed on:
  ```cpp
  // src/render/play_surface.hpp (or a small new pair if preferred - this is one
  // function and does not need its own module)
  namespace cb::render {
  /// Frame `session`'s camera on whatever the geometry view would show right now: the
  /// play surface's own bounds, headroom 0 (M17.11), while `options.surface` is set;
  /// the flat layout's bounds otherwise. Reads nothing from `session` but its variant
  /// and placements, so it is correct - and cheap - to call unconditionally every time
  /// anything that could change what is drawn has changed: the surface toggle, the
  /// pose, or the variant itself. The caller never has to work out which of the three
  /// actually happened (M17.14).
  void frameGeometryCamera(app::Session& session, const BoardOptions& options,
                           SurfacePose pose);
  }
  ```
  Its body is `frameBoard`'s current one, verbatim (the `surf.empty()` fallback to flat
  framing included).
- `main.cpp`'s `frameBoard` becomes a thin wrapper: `render::frameGeometryCamera(*session,
  optionsFor(shell), poseFrom(shell.settings()))`. No behaviour change there.
- In the `request.loadVariant` block (`src/gui/main.cpp:928-931`), call `frameBoard(*shell)`
  **unconditionally** right after `renderer->setOptions(optionsFor(*shell))`, and set
  `wasSurface = optionsFor(*shell).surface` there too, so the toggle check later in the
  same tick does not redundantly re-fire. This is always correct (per the function's own
  doc comment above) and costs one extra `PlaySurface::build` on a variant load only - the
  same cost `frameBoard` already pays every time the toggle flips, which the project has
  already measured as microseconds (ADR-0019's "Costs" section).

**Tests.** This is the first part of the surface-view policy that can be headless-tested
directly, because it no longer lives only in `main()`:

```cpp
// tests/render/test_play_surface.cpp
TEST_CASE("the camera reframes on the shape when the variant changes under it",
          "[render]") {
  // The regression this pins: geometryView is sticky across Shell::startGame (never
  // reset), so a one-shot "did the toggle flip" check misses a variant switch that
  // happens while it was already on - the camera is left on the wrong shape, or on the
  // new variant's flat board.
  auto open = [](const char* name) {
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s.has_value());
    return std::move(*s);
  };
  BoardOptions opts;
  opts.surface = true;

  std::unique_ptr<app::Session> session = open("torus");
  frameGeometryCamera(*session, opts, SurfacePose{});

  session = open("torus3d");  // switched variant; opts.surface is still true throughout
  frameGeometryCamera(*session, opts, SurfacePose{});

  const view::Bounds want = PlaySurface::build(test::loadVariant("torus3d")).bounds();
  const view::Vec3 target = session->camera().target;
  CHECK_THAT(target.x, WithinAbs(want.centerX(), 0.05f));
  CHECK_THAT(target.y, WithinAbs(want.centerY(), 0.05f));
  CHECK_THAT(target.z, WithinAbs(want.centerZ(), 0.05f));
}
```

This fails today for the obvious reason (nothing in this sequence ever calls the framing
function for the second session) and passes once `frameGeometryCamera` exists and the
`loadVariant` call site calls it unconditionally, matching what the real fix does.

**Acceptance.** Interactively (no capture flag can reach this - it needs two loads in one
process; note this limitation next to ADR-0018's similar one for the present path): launch
the game, load `torus`, press SHAPE (correctly centres, unchanged). Open the pause menu's
New Game screen and pick `torus3d` *without* turning SHAPE off first - the moment it loads,
`torus3d`'s own shape is centred in the window. Repeat starting from `klein` into `hyper4`,
and picking the *same* shaped variant again (the "start over" case). None of them should
ever show the previous variant's framing or the new variant's flat-board framing.

### M17.15 A move on the shape animates instead of teleporting

**Want.** A piece crossing the shape travels there - a knight arcs directly from its
square to its landing square; everything else (rook, bishop, queen, king, pawn) visibly
passes through every square the engine's own route says it passes through, the way M11
already draws a move on the flat board.

**Have.** `BoardRenderer::buildInstances`'s surface branch draws every piece at its
*destination* seat every frame, unconditionally - `board_renderer.cpp:772-799` loops
`surf.seats()` and places each occupied cell's piece there from `p.at(seat.cell)`, which
the engine has already updated the instant the move was applied. The animation parameter
is received and discarded: `(void)anim;  // the warped route is M17's remaining piece; a
mover shows at its seat` (`board_renderer.cpp:800`). There is no skip for the cell the
piece is travelling *to* either (the flat path has one -
`if (anim != nullptr && anim->active() && pl.cell == anim->travellingTo()) continue;`,
`board_renderer.cpp:939` - the surface loop has no equivalent), so once a mover is drawn
it would simply sit correctly at its landing square with nothing animating towards it:
a teleport.

The flat board's existing machinery - `view::MoveAnimation`, `view::tracePath`,
`view::routeRuns` - already carries everything needed to know the route (`MovePath.steps`,
each tagged `Interior`/`Portal`/`Bounce`; `MovePath.leap` for a knight/hop) and the timing
(`MoveAnimation::advance`/`progress`/`setProgress`, already driven by `Session` and already
reaching `--move-t` in captures). None of it assumes a flat board in its *logic* - M11's
whole premise (AGENTS.md: "the move camera reads the trace, never the topology") - but
`MoveAnimation::sample()` sites its `lift` along world **Z** (right for a flat board,
wrong on a curved one, where "up" is the local surface normal and world Z is often not
that), and its portal visuals assume two glued edges are drawn far apart on screen, which
is never true on the surface - a glued seam there is drawn as one continuous place, so
there is nothing to open a doorway between.

**Build.**

- **Keep `MoveAnimation`'s timing, drop its flat-specific sampling for this path.**
  `Session` already owns one `view::MoveAnimation` and drives it the same way regardless of
  view mode - nothing here changes that. Add one small, safe accessor so the renderer can
  read what move is actually in flight, since `MoveAnimation::sample()`'s own x/y/z/lift
  stay correct (and unchanged) for the flat board:
  ```cpp
  // src/view/move_anim.hpp, inside class MoveAnimation
  /// The route this animation was last started with. Needed by anything that samples a
  /// *different* placement of the same move - the geometry view's surface sampler, which
  /// cannot reconstruct the route itself (board_renderer.cpp has no access to
  /// `Session::lastPath_`, only to this animation) (M17.15).
  [[nodiscard]] const MovePath& path() const noexcept { return path_; }
  private: MovePath path_;  // set in start(), alongside the existing leap_/to_/etc.
  ```
- **A pure sampler, parallel to `moveCamera`, in the render layer** (`PlaySurface` already
  lives there and already depends on `app`; this needs nothing new in the dependency
  graph). Add to `src/render/play_surface.hpp`/`.cpp`:
  ```cpp
  struct SurfaceMoveSample {
    view::Vec3 position{};
    view::Vec3 normal{0.0f, 0.0f, 1.0f};
    std::array<float, 4> quat{{0.0f, 0.0f, 0.0f, 1.0f}};
    float fit{1.0f};  ///< the piece scale factor seats already carry (board_renderer.cpp:789)
  };
  /// Where the travelling piece sits on `surf` at progress `t`, and which way it stands.
  /// Pure in `t`, like `moveCamera` and like an overture - nothing here reads a clock.
  [[nodiscard]] SurfaceMoveSample surfaceMoveSample(const view::MovePath& path,
                                                     const PlaySurface& surf, float t);
  ```
  Its body:
  - Look up each `SurfaceSeat` the route touches by cell (same linear scan
    `view::routeRuns` already does over a handful of cells - a move touches a handful
    regardless of board size).
  - **Leap** (`path.leap`): arc directly from `path.from`'s seat to `path.to`'s seat -
    `position = mix(fromSeat.centre, toSeat.centre, t)` lifted outward along
    `normalize(mix(fromSeat.normal, toSeat.normal, t))` by the same
    `sin(t * pi) * 0.65f` the flat board's leap already uses
    (`move_anim.cpp:488`) - peaking mid-flight, zero at both ends. `normal`/`quat` blend
    (`quatOf`-style, reusing the private helper already in `play_surface.cpp`) between the
    two seats' frames.
  - **Glide** (not a leap): walk `path.from` then every `PathStep.to` in order - this *is*
    "every square between start and end", directly off the route the engine already
    computed, with no reinterpretation. Build the ordered list of seat centres, then use
    the same arc-length parametrisation `move_camera.cpp`'s file-local `pointAlong` already
    implements (move it out of that file's anonymous namespace and export it from
    `move_camera.hpp` as `view::pointAlong(points, s)`, generalised to a plain point list
    rather than a `RouteRun`, so both call sites share one definition rather than drifting
    apart - the camera and the piece disagreeing about where "the move" currently is would
    be exactly the class of bug ADR-0011's invariant exists to rule out). **A `Portal` or
    `Bounce` step is not a special case here** - unlike the flat board, there is no gap to
    open a doorway across: the seams the step crosses are already drawn as one continuous
    region of the surface, so the walk simply continues through the point sequence with no
    cut. (`Bounce` cannot occur in practice - `hasPlaySurface` already excludes
    `MirrorBox`.) Orientation snaps to the nearest route cell's seat quat as the piece
    passes it; a slerp between consecutive quats would read more smoothly and is a
    reasonable follow-up, not required here.
  - `fit`: blend the two relevant seats' `stepU*stepV`-derived fit the same way position
    blends, so a piece does not visibly resize in a jump at each cell boundary.
- **`BoardRenderer::buildInstances`'s surface branch** (around `board_renderer.cpp:772`):
  add the destination skip the flat path already has -
  `if (anim != nullptr && anim->active() && seat.cell == anim->travellingTo()) continue;`
  inside the `surf.seats()` loop - and after it, where `(void)anim;` is today, emit the
  travelling piece: `const Piece moving = p.at(anim->travellingTo());` (the engine has
  already moved it there, exactly as the flat path's equivalent line reads
  `board_renderer.cpp:1236`), and if not empty, `const SurfaceMoveSample s =
  surfaceMoveSample(anim->path(), surf, anim->progress());`, then build an `Instance` the
  same way the static loop does (`body.center`/`body.quat`/`body.scale`) but sourced from
  `s` instead of a `SurfaceSeat`.

**Tests.** New cases in `tests/render/test_play_surface.cpp` (pure geometry, no GPU):

- A leap's sample at `t = 0` equals `fromSeat.centre` and at `t = 1` equals `toSeat.centre`
  (within float tolerance); at `t = 0.5` it is displaced *outward* along the blended
  normal relative to the straight chord between the two - i.e. it visibly arcs rather than
  cutting through the shape.
- A glide's sample at `t = 0` and `t = 1` match the route's first and last seat; sampled
  across a dense sweep of `t`, the position visits a neighbourhood of every intermediate
  `PathStep.to` seat in order (monotonically increasing arc-length), and never departs the
  surface by more than the leap's own lift bound (it should hug the surface, not arc).
- A glide across a seam (pick a scripted rook move on `torus` that crosses the glued file
  edge) produces no discontinuity in the sampled position around the step that crosses it
  - the point just before and just after the crossing are close together (within one
  cell's `stepU`), unlike the flat board's portal cut.
- `BoardRenderer::buildInstances`, GPU: a validation-clean render mid-move
  (`anim.setProgress(0.5f)`) on `torus` shows the moving piece displaced from both its
  start and end seats, and neither the start nor end seat double-draws it.

**Acceptance.** `endspielzero_gui torus --script "click a1\nclick a2" --geometry --move-t 0.5
--shot mid.ppm` shows the pawn part-way along the surface between a1 and a2, not sitting
statically at a2. A scripted knight move shows a visible arc clear of the surface at
`--move-t 0.5`. `--geometry` renders without the flag are unaffected (progress defaults to
1, the sampler's own end-state, identical to today's static placement).

### M17.16 The move camera follows on the shape

**Want.** The existing camera-follow setting (off / follow the piece / follow the route)
works in the geometry view too: "off" is the player's own orbit, unchanged - the ordinary
third-person view, as now; "piece"/"route" put the camera behind the moving piece as it
crosses the shape, the same way it already does on the flat board.

**Have.** `Settings::cameraMode` (`"off"`/`"piece"`/`"route"`) already drives
`Session::setCameraMode`, which sets `cameraPolicy_.follow` to a `view::FollowMode`; the
cycle button for it already lives in the settings menu (`ui_menus.cpp:858-867`) and is not
gated on `geometryView` - it is simply **inert** there, because `Session::camera()`
(`session.cpp:253-276`) blends the move-camera shot using `placements_`/`viewCfg_` - the
*flat* layout, always, regardless of what `BoardOptions::surface` the renderer is actually
drawing. `view::moveCamera`/`view::routeRuns` themselves need no change for this: they are
already geometry-agnostic, reading only a `MovePath` and a `std::vector<Placement>`
(AGENTS.md's own description of M11). The gap is entirely that nothing ever builds a
*surface* `Placement` list and asks for the blend against it.

Two more things follow from that gap once it is closed: `moveCamera`'s "look along travel"
direction (`move_camera.cpp:188-191`, `worldDir(cfg, run.dir)`) projects a lattice
direction through `ViewConfig::screenAxes` - a flat-layout concept with no surface
equivalent - but `moveCamera` already falls back to the run's own 3-D chord
(`move_camera.cpp:192`, `dir = normalize(last - first)`) whenever `worldDir` returns
near-zero, which is exactly what happens when `cfg.screenAxes` is empty. And picking on the
surface (`src/gui/main.cpp`, the `optionsFor(*shell).surface` click branch) currently calls
`session->camera()` directly with no `shotInFlight()` guard - harmless while nothing ever
animated a surface move, not harmless once M17.15 and this both exist: a click mid-shot
would now pick against a camera that is about to move, which is exactly the desync
ADR-0011's invariant exists to prevent, and which `Session::clickPixel` already guards
against for the flat board (`session.cpp:636`, `if (shotInFlight()) return kInvalidCell;`).

**Build.**

- **One function, not two copies of the blend.** `Session::camera()`'s body (past the
  `pullBack_` line) is the move-camera blend, parametrised internally on `placements_` and
  `view::boundsOf(placements_)`. Split it:
  ```cpp
  // session.hpp
  /// `camera()`'s own blend, against an arbitrary placement set and its bounds instead of
  /// the session's flat layout - what the geometry view passes, built from
  /// `render::PlaySurface::seats()` (M17.16). One definition either way, so the move
  /// camera cannot say something different from what `camera()` already promises: see the
  /// note on `camera()` for why the blend is folded into one accessor at all.
  [[nodiscard]] view::OrbitCamera cameraOver(const std::vector<view::Placement>& placements,
                                              const view::Bounds& scene) const noexcept;
  ```
  `camera()` becomes `return cameraOver(placements_, view::boundsOf(placements_));` after
  its existing `pullBack_`/early-return lines (those stay exactly where they are - this is
  a pure extraction, not a behaviour change, and the existing camera/move-camera tests must
  keep passing unchanged).
- **Build the surface placements once per frame**, in `src/gui/main.cpp`, next to where
  `optionsFor`/`poseFrom` already live:
  ```cpp
  std::vector<view::Placement> surfacePlacements(const PlaySurface& surf) {
    std::vector<view::Placement> out;
    out.reserve(surf.seats().size());
    for (const SurfaceSeat& s : surf.seats())
      out.push_back({s.cell, s.centre.x, s.centre.y, s.centre.z, 0});
    return out;
  }
  ```
  Wherever the loop currently calls `session->camera()` while
  `optionsFor(*shell).surface` is true (the render call and the picking call both need the
  *same* one, per ADR-0011), call `session->cameraOver(surfacePlacements(surf),
  surf.bounds())` instead, with `surf` the same `PlaySurface::build(variant, pose)` already
  being built this frame for drawing - do not build it twice.
- **`ViewConfig` for the surface blend is the default, empty one** (`view::ViewConfig{}`)
  wherever `cameraOver`/`moveCamera`'s `cfg` parameter is threaded through - its only
  consumers (`worldDir`, `travelsGridAxis`) both degrade to their already-correct
  fallbacks on an empty config (chord direction; never "grid axis", since the surface view
  has no grid-axis concept). No change to `move_camera.cpp` is needed for this.
- **Gate surface picking on `shotInFlight()`**, matching `clickPixel`'s own rule: in the
  surface click branch in `main.cpp`, skip (or no-op) the pick when
  `shell->session()->shotInFlight()` is true, exactly as the flat board already refuses a
  click mid-shot.
- `Settings::cameraMode` needs no new value and no new UI - the existing three-state cycle
  already reaches `Session::setCameraMode`, which this reuses unmodified. `FollowMode::Off`
  is already "the player's own orbit, unaffected" (`moveCamera` returns the settled framing
  immediately); that is literally "3rd person, as now" and needs no code here at all.

**Tests.**

- `tests/render/test_play_surface.cpp` (or a sibling), headless: construct a `Session` on
  `torus`, start a move, call `cameraOver(surfacePlacements, surf.bounds())` at a few
  values of `anim.progress()` with `cameraPolicy_` set to `Piece`/`Route` - note this needs
  the same small accessor path M17.14 already asked for (or `cameraPolicy_`/`lastPath_`
  exposed some other way; either spec's refactor satisfies the other) - and assert the
  returned camera's `target` tracks the route (moves monotonically along it) rather than
  sitting fixed at the settled framing `FollowMode::Off` would give.
- A regression test that `FollowMode::Off` against surface placements is bit-identical to
  `frameGeometryCamera`'s own settled framing (M17.14) - the strict no-op the whole feature
  promises when the player has not opted in.
- A picking test: with a shot in flight (`anim.setProgress(0.4f)`, `cameraPolicy_` set to
  follow), the surface pick call returns `kInvalidCell` rather than resolving against a
  stale camera.

**Acceptance.** Set "follow the route" in settings, make a long sliding move on `torus` in
the geometry view: the camera leads the piece around the ring, pulling back as it travels,
and settles back to the player's own orbit once the move completes - the same character the
flat board's follow already has, now on the shape. Set it back to "off": nothing about the
geometry view's camera changes from how it behaves today.

### M17.17 Align the shape so the moving piece is never on the far side

**Want.** An option that, while the camera is following a move, keeps the piece on the
side of the shape the camera can actually see without looking through the shape's own
geometry to find it - concretely, if a piece travels along a torus's inner ring (the side
facing the hole), the torus turns so that ring becomes the outer one before or as the
camera gets there, rather than the camera ending up inside the donut looking out through
the tube wall.

**Have.** Nothing - this is new. But the mechanism already exists in a different guise:
`SurfacePose::slideU`/`slideV` (ADR-0019) already rotate *which part of the surface's own
parametrisation* sits where - the middle-mouse-button slide is a player manually doing
exactly this. "Align" is the same operation, chosen automatically rather than dragged, and
only for the file axis (`slideU`) in the first cut: on every shape in the catalogue, `u`
is the angle that wraps the ring a torus's inner/outer side sits on (`tube`'s `th`
parameter in `overture_scene.cpp`), while `v` (the ones that would need `slidesAlongRanks`)
governs the cross-section twist, not which side faces the camera. Scoping to `slideU` on
the surfaces that already support sliding (`PlaySurface::slidesAlongRanks` names which
variants slide at all - `torus`/`klein`; `cylinder`/`mobius` have no inner/outer ring to
begin with, since they do not close into one) keeps this from needing new theory about the
D >= 3 stacked shapes, which do not support slide or invert yet (M17.12's stated limits).

**Build.**

- **A facing measure already sits on every seat.** `SurfaceSeat::normal` is the outward
  direction `PlaySurface` already computes; "does the camera clip through the shape to see
  this cell" is well approximated by whether that normal points *away* from the camera:
  `facing(seat, camera) = dot(seat.normal, normalize(camera.eye() - seat.centre))`. A
  seat with `facing <= 0` is on the far side of the shape as the camera currently sits -
  not necessarily occluded by a precise raycast, but a cheap, honest proxy consistent with
  what the renderer already uses for shading and picking.
- **The search.** Given the cell the camera is about to follow (the destination of the
  move about to start, or the currently-nearest route cell while one is in flight) and the
  *settled* camera (`Session::cameraOver` with `FollowMode::Off` - the framing the shot
  would otherwise lead from), sample `facing` at a handful of candidate `slideU` offsets
  (e.g. 16, evenly spaced across the shape's own period -
  `PlaySurface`'s existing `wrapSlide(s, period)` helper and its `2 * nx` period
  (`play_surface.cpp:84`, used at `play_surface.cpp:131-132`) - by
  rebuilding just that one seat's position at each candidate (no need to rebuild the whole
  `PlaySurface` per candidate - add a small helper that samples a single seat's
  centre/normal at a given `(cell, pose)`, factored out of `PlaySurface::build`'s per-cell
  body) and keep the offset with the largest `facing`. This is a search, not a closed
  form - the embeddings have no general inverse for "which slide makes this point face
  outward," and a 16-point sweep is cheap (the same order of cost ADR-0019 already
  accepted for rebuilding the whole surface every frame).
- **Applying it.** `Settings::geometryAlign` (bool, default `false` - another strict
  no-op by default, consistent with every other M17 setting). An `ALIGN` button in the
  rail, shown next to `SHAPE` only when `cameraMode != "off"` (aligning has nothing to do
  while the player is not following a move - same reasoning `INVERT`/`GHOST` already use
  for when they are offered). While it is on **and** a shot is in flight
  (`shotInFlight()`), ease an *additional* offset into `Settings::geometrySlideU` - on top
  of whatever the player has manually slid to, not replacing it - towards the searched
  value, over the same kind of ramp `INVERT`'s target already uses
  (`main.cpp:904-913`, `dt * 2.0f`-ish), and ease it back to zero once the shot ends. The
  search itself only needs to run once per move (when the shot starts, or when the
  followed cell changes - a slide search every frame would be wasted work for a value that
  only needs to change when the target cell does).
- **Scope.** Offered only where `PlaySurface::slidesAlongRanks` or the plain
  `hasPlaySurface` 2-D case with a closed `u` axis applies - concretely, `torus` and
  `klein` (closed rings with an inner/outer side); `cylinder` and `mobius` never show the
  button (nothing to align - a `u`-slide there just spins an open tube/strip with no side
  that is more "inside" than another). `torus3d`/`hyper4`/`t6` are out of scope for this
  pass, matching M17.12's own stated limits on slide/invert there.

**Tests.**

- `tests/render/test_play_surface.cpp`: for a torus, pick a cell known to sit on the inner
  ring at `slideU = 0` (one exists - the torus's `open` parameter in `tube()` pulls the
  hole open specifically so an inner ring exists to test against) and a settled camera
  framed on the whole shape; assert `facing(seat, camera) < 0` there (the premise the
  feature exists to fix), then assert the search finds a `slideU` for which
  `facing(seat', camera) > facing(seat, camera)` and is positive - the align genuinely
  improves the measure it targets, for the specific case the feature was asked for.
- A continuity check: the chosen offset as a function of a sweep of candidate followed
  cells does not jump discontinuously between adjacent cells (the search should prefer the
  candidate nearest the previous frame's choice when two offsets tie on `facing`, or the
  shape would visibly snap between two rotations for a route that crosses the tie-break
  boundary).

**Acceptance.** Turn ALIGN on, set "follow the route", script a rook's slide along a
torus's inner ring. Without ALIGN, the camera at some point along that route ends up
looking through the tube wall (self-occlusion visible in a capture). With ALIGN on, the
torus itself turns so the travelled ring presents its outer face to the camera and the
piece stays visible throughout.

### M17.18 Klein's normal field flips mid-board, not at its seam

**Want.** The Klein bottle reads as one continuous shape while sliding through it (the
middle-drag that moves `slideU`), the way `torus`/`cylinder`/`mobius` already do. Today it
looks like two shapes glued together: on one side of a line through the middle of the
board the squares (and the pieces standing on them) read as facing/leaning one way, on the
other side the opposite way, and that line sweeps across the board as the slide changes -
confirmed below, it is not an impression, the normal field genuinely reverses there.

**Have - confirmed by measurement, not just by eye.** `PlaySurface::build`'s Klein branch
computes "outward" in two places, both independently, both from the *same* unstable test,
and both away from the pinch where it is actually unstable:

- **The fine mesh shading.** `nrm(u, vv)` (`play_surface.cpp:171-176`) takes the raw
  `normalAt(...)` and flips it to point away from `axisRef(vv)` - the per-rank
  cross-section centroid, averaged over the full gapless grid (`play_surface.cpp:148-161`,
  comment at `148-154`: *"'Outward' at a point is the side away from it"*). This feeds
  every `patch.normal[k]` (`play_surface.cpp:193`) and every `seat.normal`
  (`play_surface.cpp:203`).
- **The per-cell piece orientation.** `ex` (`play_surface.cpp:216-220`) comes from a
  *separate*, coarser tangent - the chord across the whole cell,
  `along = at(u + 0.5/fnx, vv) - at(u - 0.5/fnx, vv)` - projected flat against
  `seat.normal` and normalised. It inherits whatever `seat.normal` already decided, and
  adds its own instability on top (below).

A probe of `PlaySurface::build(klein).patches()`, dumping `patch.normal[]` across one rank
at fine (`kSubdiv = 4`) resolution, shows the fault precisely - two places where
*consecutive fine samples, a quarter-cell apart, inside what should be one smooth square*,
are nearly exact opposites (`slideU = 2.0`, rank 0, patch for file 2, its own four
corners):

```
file 2, corner i=2: n=(0.208, 0.311, -0.927)
file 2, corner i=3: n=(-0.227, -0.472, 0.852)   <- dot with i=2 is -0.98: ~168 degrees
...
file 5, corner i=1: n=(0.607, 0.425, 0.672)
file 5, corner i=2: n=(-0.556, -0.361, -0.748)  <- dot with i=1 is -0.99: ~174 degrees
```

Both loci sit at a *fixed* `u`, independent of rank, and both move together as `slideU`
changes (confirmed: at `slideU = 0` the same two flips sit at files 0 and 4; at
`slideU = 2` they sit at files 2 and 6 - a clean two-cell shift, matching the slide
exactly). That `u` is where `kleinSurf`'s lemniscate cross-section passes through its own
centre - `cr = rho*sin(a(u))` and `ca = rho*sin(2a(u))` are both zero at `u ≈ 0` and
`u ≈ 0.5` (AGENTS.md's own gotcha already names this point: *"on a Klein bottle's crossing
the tangent at the seat and the direction of the next square are most of a right angle
apart"* - this is that crossing, and it is worse than a right angle apart). Exactly there,
the sampled point sits almost *on* `axisRef`'s centroid (the centroid of a figure-eight is
near its own crossing), so `dot(n, out)` in `nrm()`'s flip test is dividing by
almost nothing - the sign it returns is numerical noise, and it comes out different sides
of zero for two points a quarter of a cell apart. The coarser `ex` computation
(`along` spanning a *whole* cell) is the same instability at a coarser scale - its
`seat.normal` is the one that just flipped, and `along` itself is a wide secant straight
across the same crossing.

**Why this cannot simply be "fixed" to zero seams.** The Klein bottle is non-orientable -
that is the entire content of the variant (its own description: *"a bishop that finally
escapes its colour"* because no global two-colouring, equivalently no global consistent
"outward", exists). A continuous outward-pointing normal field, walked all the way around
the closed `u` loop, is mathematically guaranteed to come back reversed from where it
started - this is not a bug to eliminate, it is the shape. The bug is only that the one
unavoidable discontinuity a non-orientable field must have is landing **twice, in the
middle of the board, at a numerically unstable point** instead of **once, at the board's
own declared seam** - the file edge the variant already glues (`klein.toml`'s own
`[[geometry.identify]] axis = "file"`), which is where a player already expects something
to join up, not in the middle of a square.

**Build.** Replace the per-point "test against the centroid" with a **continuity walk**:
seed one known-good orientation, then choose each neighbouring sample's sign to agree with
its neighbour rather than with a global reference. This is stable everywhere (it never
divides by a near-zero quantity) and it places the one unavoidable flip exactly where the
walk stops propagating - which can be chosen to be the file axis's own wrap, by simply
*not* closing the loop.

- **The fine field.** Build a `normals_` grid parallel to `corners_` (same
  `cu = nx * kSubdiv + 1` by `cv = nz * kSubdiv + 1` indexing, `play_surface.cpp:140-148`).
  For each rank row `j` in `[0, cv)`:
  - Seed `i = 0` using today's test (`dot(normalAt(...), corner - axisAt[j]) `, flip if
    negative) - this is stable at `i = 0` precisely when `i = 0` is not itself a pinch
    (true for every `slideU` except the rare case the slide lands a pinch exactly on the
    lattice's own file-0 edge, where seeding from `i = 1` instead is a safe fallback).
  - For `i` from `1` to `cu - 1`: take the raw `normalAt(...)` at that corner, and keep its
    sign if `dot(candidate, normals_[i-1][j]) >= 0`, else negate it. **Do not wrap `i = 0`
    back to `i = cu - 1`** - that seam is where the one unavoidable flip is meant to land,
    and it is already the board's own glued file edge.
  - `nrm(u, vv)` becomes a bilinear lookup into `normals_` (the same four-corner
    interpolation `axisRef` already does for one axis, extended to both) instead of a
    fresh `normalAt` + centroid test - patch corners sample at `kCoverage`-inset points
    that do not land exactly on grid lines, so this is an interpolation of already-
    continuous values, not a re-derivation.
  - The `inverted` flip (`play_surface.cpp:174`, the INVERT feature) still applies once,
    uniformly, to every entry after the walk - it is a global swap keyed to the `evert`
    parameter, not a spatial choice, and does not interact with this.
- **The per-cell tangent.** Compute `ex` for every `(file, rank)` with the same walk, at
  cell resolution, *before* populating `seats_`: for each rank, walk `f` from `0` to
  `nx - 1`, seed `f = 0`'s `ex` as today (`normalize(flat)`, whatever sign that naturally
  gives), and for `f >= 1` negate the candidate `ex` (and recompute
  `ey = cross(normal, ex)` to match) when it disagrees in sign with the previous file's
  `ex`. Store the result in a temporary `nx`-by-`nz` array and have the existing per-cell
  loop (`play_surface.cpp:177-221`) read `ex`/`ey` from it instead of computing them
  inline - the loop's own nesting and the order `seats_`/`patches_` are populated in does
  not need to change, only where `ex` comes from.
- Both walks are **independent per rank** (a `v`-row's own `u`-walk does not depend on any
  other row), so this is no more expensive than today's per-sample work, done once instead
  of redundantly at every patch corner.

**Tests.** `tests/render/test_play_surface.cpp`:

- For `klein`, across every pair of **adjacent fine samples within the same rank** -
  i.e. `patch.normal[k]` and its immediate neighbour one `kSubdiv` step over, including
  across a cell boundary (e.g. file 2's last corner and file 3's first corner at the same
  rank) - assert `dot` of the two is positive and close to 1 (a few degrees of turn at
  most, never the ~170 degree flips measured above). This directly fails today and must
  pass after the fix; it is the test that would have caught this the way M17.13's
  uniformity test would have caught the clamp bug.
- The same adjacency check for `seat.quat`'s reconstructed `ex` across consecutive files
  at a fixed rank.
- **The one remaining discontinuity is exactly at the wrap**, not eliminated and not
  moved: `dot` of file `nx - 1`'s last fine sample and file `0`'s first fine sample, at
  the same rank, is allowed to be negative - assert this *can* happen (so a future change
  that "fixes" it by closing the loop and reintroducing a mid-board flip is itself
  caught), while every other adjacent pair in the same sweep is positive.
- Re-run with `slideU` at a few values spanning more than one cell, to confirm the two
  measured loci above are gone at every slide position, not just the ones probed by hand.
- `torus`/`cylinder`/`mobius` are unaffected (they have no pinch - `klein` is the only
  shape in the catalogue whose cross-section is a self-crossing curve) - the existing
  continuity/frame tests for those three must keep passing unchanged.

**Acceptance.** `endspielzero_gui klein --geometry --shot k.ppm` at several `--slide` values
shows one continuous shape at every value - no captured frame shows a lit/shaded band that
reads as a second, differently-oriented object glued to the first. Dragging the middle
button through a full slide shows the squares and the pieces on them turning smoothly;
the Klein-specific comment in AGENTS.md about the crossing being "most of a right angle"
off should be revisited once this lands, since the continuity walk is the fix that comment
was asking for.

### Ordering

M17.13, M17.14 and M17.18 are regressions/defects found while playing with what shipped
and should be fixed before anything new is judged against them - M17.13 makes
`torus3d`/`hyper4` unusable to look at, M17.14 affects every shaped variant, and M17.18 is
specific to `klein` but makes it look broken rather than merely non-orientable. M17.8 and
M17.9 are one sitting each. M17.11 is a measurement and then a small fix. M17.7 and M17.10
are a button each plus one real piece of work (the outward side; a blended pipeline).
M17.15 is the dependency M17.16 and M17.17 both need - M17.16's camera has nothing honest
to follow before a move actually travels, and M17.17 only matters once the camera is close
enough to clip. Build them in that order: M17.15, then M17.16, then M17.17. [M17.12's
remaining limits](M17.12-shapes-above-two-dimensions.md) (level/aeon adjacency, no
slide/invert on the stacked shapes) are their own, larger step.

---

## Status: built for the glued 2-D case (2026-09-30)

Built on top of the M13 warps, with nothing new in the engine. The first cut drew the
surface as screen-space quads in the interface over the still-rendered flat board; that was
wrong - no camera control, and the flat board's labels showed through - so it was reworked
to draw the surface as real instanced geometry. That second cut placed every cell correctly
and still did not look like a board, for three separate reasons, and the third pass is what
is described here. See **ADR-0019**.

- **M17.1/M17.2** `render::PlaySurface` (`src/render/play_surface.cpp`) owns the placement.
  One sampling of `derivedSurfaceAt` gives each square the **patch of surface it is** - a
  grid of corners lying on the surface across the cell's own lattice footprint, with a
  little thickness - and each cell a *seat* for its piece, with a frame as a quaternion
  and a size read from its neighbours. `BoardRenderer::buildInstances` turns the patches
  into **one mesh**, rebuilt each frame with its colours in its vertices, and leaves the
  pieces instanced; both go through the session's own `OrbitCamera`, depth buffer and
  MSAA, so orbit and zoom work with no special path. The surfaces are rotated a quarter
  turn out of the overtures' Y-up world into the board's Z-up one - without that a donut
  stands on its rim like a wheel.

  Three earlier cuts of this drew the squares as *rectangles* and none of them read as a
  board. A rectangle has to be oriented, and a shortest-arc turn onto the normal left
  every square spun to its own angle; it has to be scaled, and the scale was being applied
  after the orientation in `board.vert`, which scales the world axes rather than the
  mesh's; and it has to be sized, which the surface's metric makes a moving target. Fixing
  all three left squares that are tangent at one point and therefore cut into each other
  where the surface turns fast - a Klein bottle's figure-eight turns most of a right angle
  per square - which is the "fish scales" the patch mesh exists to end. The scale-order fix
  stayed: the pieces still carry an orientation.
- **M17.3** `PlaySurface::pick` ray-tests a gapless grid of the same surface points the
  tiles were built from, with the session camera, so a click resolves to the cell under the
  cursor. The front end uses it instead of `Session::clickPixel` while the surface is on,
  and `Session::frameOn` puts the camera round the shape the moment the view is switched
  on - the flat layout's bounds are a different size in a different place, and framing on
  them left the board off in a corner.
- **M17.5** `Settings::geometryView`, a `SHAPE` button in the game rail, and the `G` key,
  offered only when `render::hasPlaySurface(v)` holds - a glued 2-D variant, never
  `standard`, `mirrorbox` or anything above two dimensions. `--geometry` captures it.
- **M17.6, new: the board is posed on its surface.** `SurfacePose` carries two poses, both
  pure functions of their numbers and both presentation only - they never enter
  `VariantId`.
  - `slideU`/`slideV` slide the board *along* the surface, in cells. It is the **middle
    mouse button** on the shape (the view centres the shape itself, so there is nothing
    left to pan) and `--slide`/`--slide-v` for a capture. Sliding one cell along the files
    is sampling at `u + 1/nx`, so a1 lands *exactly* where b1 was; keep going and the board
    comes all the way round, and a Moebius band comes home mirrored and needs a second lap.
    That is the gluing itself rather than a cyan line about it. Offered along the ranks
    only where the ranks are glued.
  - `evert` is the source of the INVERT control. As first built it geometrically everted
    the shape (the closed surfaces sweep their ring radius through zero to its negative; a
    cylinder rolls back over itself like a sock); **play found that a mirror, not an
    invert** - it moves every cell about the origin - so M17.7 below revises it to a swap
    of the outward side, with the squares fixed. `[` and `]` for a keyboard, `--evert` for
    a capture.

Two surfaces were also corrected while the board was being drawn on them, and both fixes
land in the overtures as well, since there is one catalogue:

- The Klein bottle's figure-eight cross-section is now walked at **constant speed**
  (`lemniscateAngle`). Equally spaced values of the lemniscate's angle land in pairs, so
  every other file came out twice the width of its neighbour. The reparametrisation is odd
  in the angle, which is exactly what keeps the rank seam's file reversal closing.
- The Moebius ribbon's stretch is a `SurfacePose` field. The library screen spends all of
  it on the twist being unmistakable, which leaves each cell nine times longer than it is
  wide; the play board keeps half, which is about as little as the shape will take before
  the ribbon is wider than the loop it goes round.

Coordinates for the flat/3-D board were moved off the cells to the board's near edges
(extrapolated one cell outward) in the machine face, and are skipped in the geometry view.
Changing the palette now re-applies the ImGui style, so console no longer leaves manifold's
dark type on its dark page.

**Deferred / limitations.**

- **The move animation is not warped.** A moving piece appears on the surface at its new
  cell rather than travelling the warped route; the route trail the overture draws is not
  yet emitted for a live move. The next increment: map the animation sample through the
  surface.
- **No surface coordinates or seam rails yet.** The surface's own labels and the portal
  rails along its glued edges are not drawn; on the shape themselves the seams are mostly
  self-evident, which is why this is the last piece rather than the first.
- **The eversion passes through degenerate embeddings** - the spindle torus is
  self-intersecting and the cylinder's fold has the surface doubled back against itself.
  They are drawn honestly rather than hidden: the degenerate moment is the one a player is
  watching for.
- **The D >= 3 case (M17.4) stays a no-op**: above two dimensions the ordinary extruded
  view already *is* the lattice, so there is no second shape to become and no toggle is
  offered.
- The surface mode is the game screen only; pause keeps the ordinary board.

Tests: `tests/render/test_play_surface.cpp` (the patches tiling the surface without
crossing and without being flat, the seat's frame and size, seam closure, the shape
standing up in the board's world, the slide putting a1 where b1 was and bringing the board
home after a lap, the eversion's identity at rest and its turn at full, continuity, and
picking following the drawing), `tests/render/test_offscreen_render.cpp` (a
validation-clean GPU render of `torus` on its surface) and
`ctest -R "gui-geometry|gui-evert|gui-slide"` (the capture flags).

---

## Status: M17.7 - M17.11 built (2026-09-30)

The next increment, done in the plan's order.

- **M17.8 - the drag axes are the right way round.** Middle-drag left/right slides along
  the ranks and up/down along the files; the `slidesAlongRanks` gate moved with it, so on
  a cylinder (ranks free) left/right does nothing and up/down carries the board.
- **M17.9 - a full lap, not a two-cell one.** The wrap lives in one place, in
  `PlaySurface`, in cells, as `fmod(s, 2 * nx)` / `fmod(s, 2 * nz)` - the board's own
  extent. `Settings::sanitize` no longer guesses `fmod(..., 16)`. The test now asserts the
  period reproduces the board, two cells does not, and a tenth of a cell moves no seat by
  half a cell.
- **M17.11 - the shape is centred.** Measured first: the board's centre was ~30 px below
  the window centre because `OrbitCamera::frame` lifts the look-at by its `headroom` for
  piece crowns, and `PlaySurface::bounds()` already pads for the pieces, so the target was
  double-counted. `Session::frameOn` takes a `headroom` and the geometry view passes 0.
- **M17.7 - INVERT, and pieces that stay outside.** A `INVERT` button beside `SHAPE`
  (shown while the shape is showing) sets `Settings::geometryInvert`, a *target* the front
  end eases `geometryEvert` to over ~half a second; `]`/`[` set the same target. **Revised
  after play:** the invert *swaps the outward side* rather than geometrically everting the
  shape - the surface functions' eversion mirrors the whole board about the origin and
  moves every cell, which is not what a board wants. `PlaySurface` keeps the geometry and
  negates the normal past the halfway point, so the squares stay put and the pieces move to
  the other face. The base outward normal is still chosen away from the shape's axis (the
  cross-section centroid, precomputed per rank), which is what makes even that negation
  land on a face rather than inside the material.
- **M17.10 - GHOST.** `Settings::geometryGhost` (0.35 to 1) with a `GHOST` button and a
  `--ghost` capture flag. The board mesh's alpha rides in `MeshVertex::color.a`; a second
  pipeline - same shaders and vertex input, `blendEnable` with src-alpha/one-minus-src-
  alpha, `depthWriteEnable = false` - draws it after the opaque pieces. The pieces stay
  opaque. Measured correction to the plan: a ghosted board's luminance *variance* is
  lower, not higher (blending averages the two sheets); the test asserts the picture
  changed, is validation-clean, and that `ghost = 1` is bit-identical.

Tests: the M17.7 pieces-outside-the-axis sweep over `evert` 0..1, the M17.11 framing test,
the M17.10 GPU render (`ghost` differs, `1` is a no-op), and `gui-invert` / `gui-ghost`
captures. `tests/render/test_play_surface.cpp`, `tests/render/test_offscreen_render.cpp`.

**Remaining: M17.12, the shapes above two dimensions** - `torus3d` as nested shells,
`hyper4` as a hypercube, `t6` as the quintic, with the adjacency invariant deciding whether
each embedding is a board or merely a picture. It is its own milestone-sized step and is
not started.

**M17.12, first pass (2026-09-30):** `torus3d` and `hyper4` now play on their own shapes.
The shape is *authored*, keyed by the variant's name (`playShapePosition`), because above
two dimensions it is not read off the gluing - `hyper4` is a plain 4-D box. `PlaySurface`
grows a stacked branch: one flat tile per cell from those positions, sized by neighbours,
with a per-tile picker. See [`M17.12-shapes-above-two-dimensions.md`](M17.12-shapes-above-two-dimensions.md)
for the limits (the level/aeon axes are not adjacent-in-the-drawing; slide/invert are not
offered yet) and for the `t6` verdict: **a faithful playable quintic is impossible** - the
6-torus projects ~10 lattice cells onto each drawn tile - so `t6` stays on the lattice, and
a playable quintic, if wanted, must be a *sliced* view.

---

## Status: M17.13 - M17.17 built (2026-09-30)

The second increment, in the plan's order.

- **M17.13 - the D >= 3 shapes are no longer fish scales.** `PlaySurface::buildStacked`
  took clamped file/rank neighbours, so a *periodic* `torus3d` axis returned every boundary
  cell its own position and the frames scattered; a *bounded* `hyper4` boundary took a
  halved one-sided step. The neighbour lookup now wraps a periodic axis and one-sides a
  bounded boundary, independently per axis. Tests: `stepU` uniform within a `torus3d` shell;
  a `hyper4` boundary cell no longer half the interior's.
- **M17.14 - the camera reframes on a variant switch.** `geometryView` is sticky across
  `Shell::startGame`, so a switch while it was on never reframed. The framing policy is now
  `render::frameGeometryCamera`, called unconditionally on a variant load.
- **M17.15 - a move on the shape animates.** `MoveAnimation::path()` exposes the route,
  `view::pointAlong` is shared with the camera, and `render::surfaceMoveSample` drives the
  travelling piece: a leap arcs outward, a glide walks the route's own seats, and a seam
  step is not a cut because on the surface the seam is one place.
- **M17.16 - the move camera follows on the shape.** `Session::cameraOver` runs the one
  blend against any placement set; the geometry view passes
  `surfacePlacements(PlaySurface::build(...))`. A click mid-shot is refused, as on the flat
  board.
- **M17.17 - ALIGN.** A facing search (`render::alignSlideU`, 16 slide candidates) picks the
  offset that turns the followed cell most toward the camera; `Settings::geometryAlign` (an
  `ALIGN` button, shown while following on a closed ring) eases a transient
  `geometryAlignOffset` into the pose. `torus`/`klein` only - an open tube or ribbon has no
  inner/outer side.

Tests: the M17.13 uniformity/boundary cases, the M17.14 reframe-on-load case, the M17.15
sampler (leap arc, glide visits, seam continuity) and a validation-clean in-flight render,
the M17.16 `cameraOver` no-op and follow cases, and the M17.17 facing search. All in
`tests/render/test_play_surface.cpp` and `tests/render/test_offscreen_render.cpp`.

**Remaining:** M17.12's larger limits (level/aeon adjacency on the stacked shapes, and any
sliced-`t6` view) and the M17.5 "no surface coordinates or seam rails yet" note.

---

## Status: M17.18 and the chase camera (2026-09-30)

- **M17.18 - Klein's normal field is continuous except at its seam.** `PlaySurface::build`
  replaced the per-point "flip against the cross-section centroid" with two **continuity
  walks**: a `normals` grid over the gapless corners (seed file 0 against the centroid, then
  each next sample agrees with the last, never closing the loop), and a per-cell `ex` walk.
  `nrm` is now a bilinear lookup into the continuous field. This moves the one unavoidable
  flip of a non-orientable shape off the lemniscate's numerically unstable pinch (where the
  centroid test divided by ~zero and returned noise two samples apart) and onto the seam the
  walk does not close. Tests: every adjacent fine normal sample and every adjacent seat frame
  is non-opposed, at several `slideU` values; the flanking file cells keep agreeing.
- **The chase camera (M17.16, revised).** Following a move on the shape is now **one
  continuous chase** rather than M11's lead/pull/cut: `render::surfaceChaseCamera` centres
  the piece, puts the camera behind it along the route's travel, and **rolls** the frame
  (`OrbitCamera::roll`, applied in `viewProj` and `pickRay`) so the piece's own up - its
  surface normal - is the view's up. So the piece stands vertically in the middle of the
  view, the way a third-person follow reads. `main::boardCamera` uses it whenever a shot is
  in flight on the surface and the settled framing otherwise. Test: every seat, projected
  through the chase, is centred with its normal pointing up the screen.

---

## Status: anti-clip by default, and two follow camera modes (2026-09-30)

Play then asked for the camera work to be the default, not opt-in, and for a choice of two
follow styles.

- **Anti-clip is on by default.** `Settings::geometryAlign` now defaults true and is the
  anti-clip: while following, `render::alignSlideU` searches slide offsets and scores each by
  (a) whether `PlaySurface::blocked` finds anything of the shape between the camera and the
  piece, then (b) the piece's `facing` - so a piece on a torus's inner ring is turned to the
  outer side rather than the camera clipping through the tube. The `NOCLIP` rail button
  toggles it, and the camera settings carry the same checkbox. `alignSlideU` now takes the
  route's travel direction and builds the camera its search is scored against.
- **Two follow modes.** `Settings::followUpright` (default true) makes the chase roll the
  frame so the piece's surface normal is the view's up - the piece stands vertically in the
  middle of the view. With it off the frame is not rolled, so the piece tilts and can flip
  with the shape. The `UPRIGHT`/`TILT` rail button and a settings checkbox choose between
  them. Both centre the piece and sit the camera behind it.

Tests: the anti-clip search now checks `blocked` returns false for the chosen offset and the
facing is positive; the chase test covers both the upright roll and the tilt mode's centred,
unrolled framing. `tests/render/test_play_surface.cpp`.

---

## Status: shape follow repaired, and a turntable mode (2026-10-01)

Play asked for the shape follow to be smooth, to keep the piece upright, and to stop the
camera clipping by **turning the board**; a strong GUI-less test was wanted for each.

- **Upright was inverted.** `surfaceChaseCamera` computed the roll as the turn from the
  unrolled up to the normal, but `OrbitCamera`'s `cameraBasis` applies `roll` in the
  opposite sense, so a piece's screen lean came out at twice its true tilt. The
  roll is now negated, and it is derived from the camera that was *actually built* after
  the pitch clamp and with `OrbitCamera::upHint()`, so it is exact near steep angles.
  `tests/render/test_play_surface.cpp` ("the chase camera's up really is the piece's up")
  checks the view's up axis against the piece's normal projected perpendicular to the
  view, at every seat and for both surface tangents; the old loose test did not catch the
  inversion.
- **Smoothness.** The chase eye is `-forward + normal*lift` (`forward` the route's travel,
  `lift` the elevation tangent), and the new `render::surfaceFollowCamera` samples the
  route through a window in `t` (a leap follows its chord) so the direction is continuous
  through a cell corner. `main::boardCamera` blends the follow onto the player's camera
  with `view::shotEnvelope * followStrength` (the same envelope the flat move camera uses),
  so a move eases in and out instead of snapping. Tested by "the follow camera is smooth
  along a move" - a fine sweep turns the view by a small angle each step.
- **Two selectable modes.** `Settings::shapeFollow`: `"chase"` (default and shipped) is
  the repaired third-person camera that follows behind the piece; `"turntable"` keeps the
  player's camera angle and turns the **board** - the shape-slide the middle-drag uses -
  so the followed piece comes round to the near side. The settings screen's `Follow shape`
  row toggles them, and `endspielzero_gui ... --shape-follow chase|turntable` states it for a
  capture. `main::chaseEyeDistance` (0.7 * span) is used for both the chase camera and its
  anti-clip search. `render::alignSlideToFace` is the turntable anti-clip (a fixed eye
  direction rather than the chase's travel), built on the same offset search as
  `alignSlideU`. Both are tested: the turntable test brings every cell unoccluded on torus
  and Klein, and the chase's anti-clip keeps its existing facing/`blocked` test.
- **Anti-clip in captures.** `main::alignOffsetFor` is the deterministic form of the
  ALIGN search; the interactive loop eases towards it and a capture sets it outright, so
  a `--clip` of a followed move is anti-clipped like play.
- **The align offset reaches the renderer (the real bug).** `poseFrom` folded the transient
  `geometryAlignOffset` into the surface the camera and picker sampled, but `optionsFrom` -
  which the renderer builds the drawn board from - did not. So the camera aimed at the
  aligned surface while `BoardRenderer` drew the board unaligned: the align moved the
  camera but did not visibly turn the board. `optionsFrom` now adds the offset too, and a
  capture refreshes the renderer's options after setting it.
- **The chase searches occlusion at its own distance.** `alignSlideU`/`alignSlideToFace`
  take the eye distance the camera will actually use; before, the search tested at
  `0.6 * span` while a close follow sat inside that, so a seat it called clear could still
  be behind the tube. `main::chaseEyeDistance` (0.7 * span) is used for both the camera and
  its search. `tests/render/test_play_surface.cpp` pins it ("the chase align clears the
  shape at the camera's own distance").

---

## Status: two-axis anti-clip, non-orientable slides, and the follow elevation (2026-10-01)

Play asked for the piece to stay visible while the board turns, for a Klein bottle's
tiling not to tear when it is turned, and for the follow camera to sit above the piece by
an amount that can be set.

- **The anti-clip searches both surface axes.** `alignSlideU`/`alignSlideToFace` now
  return a `SlideOffset` (U *and* V) and sweep a 12x12 grid instead of a U-only sweep.
  `Settings::geometryAlignOffsetV` carries the V offset into `poseFrom`/`optionsFrom`, and
  the loop eases it like the U one. In the turntable test both remain unoccluded on the
  torus and the Klein bottle.
- **A non-orientable board is slid on the V axis only.** Measured on the Klein bottle's
  rank seam, the tile-corner gap while sliding U rose to about ten times an ordinary gap,
  while sliding V kept it near one; the U value was therefore removed from the middle-mouse
  turn (both drag axes drive V) and from the ALIGN search. Two tests pin this: "a V slide
  keeps the Klein rank seam whole" (V keeps the gap small) and "the Klein surface
  closes its gluing" (the immersion's rank join reverses the file coordinate).
- **The normal flip is hidden by two-sided shading.** The board push constant carries the
  eye position (`board.frag`), and a fragment turns its normal toward the viewer before
  lighting.
- **The follow camera's elevation is a setting.** The eye is behind the piece and above its
  tangent plane by `Settings::followElevationDeg` (default 30, slider 5-80); `surfaceChase
  Camera`/`surfaceFollowCamera`/`alignSlideU` take the tangent (`main::followLift`), so the
  camera and its anti-clip search use the same angle.
- **The Klein twist is a half-turn count.** `Settings::kleinTwist` (default 1) is how many
  half-turns the ring makes over a lap, carried straight into `SurfacePose::twist`. The
  figure-eight cross-section has one rotational symmetry (a half-turn), so only whole
  half-turns close the join; an arbitrary cell count would leave the ends open.
- **Shape width.** `Settings::geometryWidth` (slider, default 1) scales the ring or loop
  radius via `SurfacePose::openness`, leaving the cross-section - the material's
  thickness - unchanged. It opens a tight torus/Klein bottle's hole and lengthens the
  Moebius loop, which are otherwise hard to read at the shipped ratios.
- **Tube width.** `Settings::geometryThickness` (slider, default 1) scales the
  cross-section size - the tube's own radius, its outer-to-inner radius - leaving the ring
  radius alone, via `SurfacePose::thickness`. The files still roll through a full 2pi, so
  the cells stretch around a fatter tube rather than the tube failing to close.
- **Slower moves.** The animation-speed floor dropped from 0.25x to 0.05x, so a piece can
  be followed across the surface at about 1.7 s per cell.
- **Klein square shift.** `Settings::kleinShift` (slider, default 0) rotates one rank end
  of the cylinder relative to the other *before* the figure-eight collapse, one square =
  360/nx degrees, via `SurfacePose::collapsePhase` and `kleinSurf`'s phase argument. The
  rotation ramps along the rank, so the square lines spiral round the tube instead of the
  whole cylinder turning rigidly. It is not the half-turn twist.

---

## Status: M17.19 - the shape-follow choreography, an intrinsic camera frame, and the align hot spot (2026-10-01)

Play asked for the shape follow to be a sequence - morph clear, fly to the piece, travel, fly
home - for the camera to rotate with the piece to each new direction of motion, and reported
that a followed move ran at a few frames per second.

- **The choreography.** A followed move on a shape is four stages
  (`render::shapeBeat`): **Align** (the camera stays, the board morphs clear of the player's
  line to the start square), **Approach** (the camera flies to the piece; the piece is held
  still), **Travel** (the piece moves, the camera chases, the board keeps morphing), **Return**
  (the camera flies home, the board morphs back). `ShapeMoveSequence` in `src/gui/main.cpp`
  owns the clock and pins the move animation's progress through the lead-in and the return.
  Settings: **Morph speed** (`Settings::shapeMorphSpeed`).
- **The frame is the piece's, built explicitly.** `surfaceChaseCamera` takes `a` (the travel),
  `b` (the piece's upright = its surface normal), `n = a x b` and
  `c = -a cos(theta) + b sin(theta)` (the piece-to-eye vector, `-a` tilted up by the
  elevation). The camera's **right is `n`**, its **up is `c x n`**. `(a x b) x c` is the
  negative and hangs the piece upside down. `blendShapeCamera` interpolates the eye direction
  and the roll (not yaw/pitch component-wise) and adopts the frame **fully** during Travel -
  a half-applied frame was leaving the piece half-upright.
- **The align search no longer dominates the frame.** It had built a whole `PlaySurface` for
  each of ~100 candidate slides (measured: a followed move at ~5 fps in Debug). On an
  orientable ring it now ranks candidates with a single-cell normal probe and builds the full
  surface only until one is unoccluded - the identical argmax, a couple of builds instead of
  100+. A non-orientable shape keeps the exact per-candidate build.
- **Tests.** The pure `shapeBeat` timeline; `surfaceChaseCamera`'s right `= n`, up `= c x n`
  read from the matrix the renderer uses; the existing follow/anti-clip tests still pass.

**Morphing (follow-up, same day).** The morph is now **look-ahead and rate-bounded**, and the
camera moves are quaternion blends:

- `render::followClips(path, surf, t, lookahead, distance, lift)` asks whether the chase
  camera's own next path (its eye segment, or its line to the piece) would cross a square.
  Travel morphs toward the clear pose **only while that is true**; every stage moves the slide
  by at most `kShapeMorphRate` cells/s (`Settings::shapeMorphSpeed`), so no stage can spin the
  board a half-period in a frame - which was the flip.
- The stages are strictly sequential: Align (morph, camera still) → Approach (camera to piece,
  board still) → Travel (piece follows, board morphs) → Return (camera back, board still) →
  Done (board morphs home, camera still, sequence does not end until home).
- Stages 1→2 and 3→4 use `view::slerpCamera`: the eye travels a straight line and the
  orientation is a quaternion slerp on the shortest arc (`tests/unit/view/test_camera.cpp`),
  replacing a blend that lerped the eye direction through the origin and produced 720-degree
  spins.

**Piece motion (same day).** A gliding piece no longer travels a straight chord from one
seat centre to the next. `render::surfaceMoveSample` inserts a **boundary waypoint** between
each pair of squares - the shared edge for an orthogonal move, the shared corner for a
diagonal one - so the piece goes centre → boundary → centre. The waypoint is the *genuine
surface point* at the boundary lattice coordinate, via `PlaySurface::pointAt` /
`nearestBoundary` (which carry the warp, the folded pose and the slide, and wrap a glued
axis the short way), **not** the chord midpoint of the two seat centres: on a fast-curving
board - a torus's inner ring - the chord point sits well inside the surface, which is what
clipped no matter how small the lift. The moving piece is also **hovered a full cell**
(`kSurfaceHoverCells`) off the surface, eased in and out over the move, so the base clears
the squares everywhere. A `D >= 3` stacked shape has no parametrisation and falls back to
the chord midpoint. Orientation still snaps to the nearer route *cell* (the waypoint is not
a cell).

**Still open:** the M17.5 surface coordinates and seam rails; M17.12's larger limits.

---

## M17.20 Bug fix: the chase anti-clip mismatch (the camera goes into the board, and jitters)

Found 2026-10-03, playing a followed move on `torus`: for most of the move's length the
camera is a tight, disorienting close-up - a slab of the board fills the frame and the
pieces sit scattered at its edges - clearing only right at the start and the end. The same
move under `shapeFollow = "turntable"` stays clean throughout. Separately reported: the
chase camera is jittery, and a move is sometimes shot from behind a square that is between
the camera and the piece - i.e. the anti-clip (M17.16/17) is not actually anti-clipping.

**Repro (no display needed):**

```
endspielzero_gui torus --script $'click a1\nclick a5' --follow route --shape-follow chase \
  --cinema --geometry --clip DIR --frames 12 --t0 0 --t1 1
```

Frames 1-10 of 12 are the clipped close-up; frames 0 and 11 (where `shotInFlight()` is
false, so the settled player camera is drawn instead) are fine. The same script with
`--shape-follow turntable` is clean at every frame. a1-a5 is an ordinary rook slide
(`torus`'s opening array), nothing exotic.

### Root cause 1: the anti-clip search scores a *different* camera than the one drawn

`alignSlideU` (`src/render/play_surface.cpp`) searches board rotations (`SlideOffset`) to
keep the followed cell clear of the shape, by testing where the chase camera's eye would
land for each candidate and checking `PlaySurface::blocked` against it. Its `toEyeFor`
lambda (passed into `searchSlide`):

```cpp
return fwd * -1.0f + normal * lift;
```

uses the candidate seat's **raw** surface normal. The camera that is actually drawn,
`surfaceChaseCamera` (same file), does not use the raw normal - it first removes the
normal's component *along the travel direction*:

```cpp
view::Vec3 b = piece.normal - a * view::dot(piece.normal, a);   // orthogonalised
b = view::normalize(b);
const view::Vec3 c = view::normalize(a * -cosT + b * sinT);     // the real eye direction
```

Whenever the local normal is not already exactly perpendicular to the travel direction -
the ordinary case anywhere a surface curves, and especially while a piece crosses a
torus's or Klein bottle's tube cross-section, which is most of an a-file slide - the
search's direction and the camera's real direction (`c`) diverge. The search can then
report a rotation "clear" for a camera angle that is not the one rendered, and reject a
rotation that would actually have been fine. This is root cause 1, and it is sufficient by
itself to explain the symptom: the search is simply answering the wrong question.

**Why the existing tests did not catch it.** `tests/render/test_play_surface.cpp` has two
property tests that call `alignSlideU` and then independently recompute "the eye direction"
to check the result - but both reimplement the *search's* formula (`travel * -1.0f +
normal * kDefaultFollowLift`, see lines ~797 and ~1186), not the camera's. They are
self-consistent with the bug, not a check against `surfaceChaseCamera`. Worse, the first of
the two (`"the align search improves on a torus's inner ring"`, ~line 780) feeds the search
a `travel` vector read straight off the seat's own frame (`byQuat(seat.quat, {1,0,0})`) -
which is *by construction* already perpendicular to that seat's normal, the one case where
the search's formula and the camera's formula happen to agree. The real `travel` the front
end passes in (`ahead.position - here.position` from two nearby `surfaceMoveSample` calls,
including the hover arc) is not constructed that way and routinely has a normal component.
**Any new regression test must build `travel` the same way the front end does - from
`surfaceMoveSample`'s finite difference along a real traced path - not from a seat's own
frame**, or it will have the same blind spot.

### Root cause 2: no candidate is accepted "good enough" - or rejected as impossible

In `searchSlide`'s orientable branch (`src/render/play_surface.cpp`, ~line 662), candidates
are ranked by facing and tested in order; the loop returns the first whose eye (at the
*fixed, nominal* `eyeDistance`) is unblocked. If every one of the 144 candidates is
blocked at that distance - which is expected on a torus whenever the tube's hole is
narrower than `chaseEyeDistance = 0.7 * span` (a single global constant, independent of
local curvature) - the function silently falls through to `cands.front()`, the
best-*facing* candidate, **without ever checking whether it is blocked**. The camera that
gets built from it then sits at a distance chosen for the shape's overall span, in a
direction that has nowhere clear to put it, and the result is an eye placed past or through
the tube wall: exactly the "inside the board" look. This is root cause 2, and it compounds
root cause 1 - even a *correctly* computed direction can still have no clear distance on a
tightly-curved shape.

### Root cause 3 (contributing, lower confidence): no hysteresis between frames

`searchSlide` is a stateless per-call grid argmax (12x12 candidates, re-ranked from
scratch every time). During the Travel stage, `updateShapeSequence` (`src/gui/main.cpp`)
only re-invokes it when `currentFollowedCell` changes to a new nearest seat, and then
eases toward the new result at a bounded rate (`kShapeMorphRate`) - so a jump is smoothed,
not instant. But nothing stops the *target* the easing chases from being a very different
grid cell than the previous target, if the raw argmax over two geometrically close cells
happens to favour two distant candidates (plausible with a facing-only tie-break and no
continuity term, and made worse by root cause 1 making the scores noisier than the real
geometry). The visible effect is a board that keeps turning to catch up with a moving goal
instead of settling - read as jitter. Lower confidence than roots 1-2 because it was not
isolated with an independent repro; verify it is still present after fixing 1-2 before
spending effort on it, since a correct direction formula alone may remove most of the
instability.

### The fix

1. **Share one function for "the eye direction," so the search and the camera cannot
   diverge again.** Pull the `a`/`b`/`c` construction out of `surfaceChaseCamera`
   (`src/render/play_surface.cpp`, ~lines 500-509) into:

   ```cpp
   /// The chase camera's eye direction from the piece: behind the travel direction,
   /// lifted toward the surface normal's component perpendicular to that travel, by
   /// `lift` (a tangent). Shared by `surfaceChaseCamera` and the anti-clip search
   /// (`alignSlideU`) so the search can never test a different camera than the one drawn
   /// (M17.20).
   [[nodiscard]] view::Vec3 chaseEyeDirection(const view::Vec3& normal,
                                              const view::Vec3& travel, float lift);
   ```

   `surfaceChaseCamera` calls it instead of inlining the computation. Declare it in
   `play_surface.hpp` next to `kDefaultFollowLift`.

2. **Make the achievable distance part of what "the camera" means, not a separate
   fixed input.** Add:

   ```cpp
   /// The largest distance along `direction` from `target` (toward the eye), up to
   /// `maxDistance`, at which the eye is not blocked from `target` by `surf` itself -
   /// found by bisection against `PlaySurface::blocked`. Never returns less than
   /// `minDistance`, so a degenerate position still has a defined place to put the
   /// camera rather than one that is found by trusting a fixed distance that happens to
   /// reach past the shape (M17.20).
   [[nodiscard]] float clearEyeDistance(const PlaySurface& surf, const view::Vec3& target,
                                        const view::Vec3& direction, float maxDistance,
                                        float minDistance = 0.5f);
   ```

   Implementation: if `!surf.blocked(target + direction * maxDistance, target, eps)`,
   return `maxDistance` outright (common case, no bisection needed). Otherwise bisect
   between `[minDistance, maxDistance]` (8 iterations is plenty - a cell's worth of
   precision) for the largest distance that is still unblocked; if even `minDistance` is
   blocked, return it anyway (nothing closer makes sense, and the caller must not loop
   forever).

   In `surfaceFollowCamera` (which already has `surf`), after computing `travel` and
   before calling `surfaceChaseCamera`, clamp the caller-given `distance`:

   ```cpp
   const view::Vec3 dir = chaseEyeDirection(here.normal, travel, lift);
   const float clamped = clearEyeDistance(surf, here.position, dir, distance);
   return surfaceChaseCamera(here, travel, clamped, upright, lift);
   ```

   `surfaceChaseCamera` itself stays pure and mesh-free (it is still useful, and tested,
   without a surface at hand); the clamp lives in the one place that already has the mesh.

3. **Make the search rank by the *same* shared primitives, not a reimplementation.** In
   `searchSlide`'s orientable branch, replace the custom `toEyeFor`-based scoring with:
   for each candidate pose, build the seat, compute `dir = chaseEyeDirection(seat.normal,
   travel, lift)` (or `toEyeFor(normal)` unchanged for the turntable's fixed-direction
   case - `alignSlideToFace` keeps its own simpler lambda, which has no travel/normal
   mismatch to begin with since the direction does not depend on the candidate), then
   `achieved = clearEyeDistance(candidate_surf, seat.centre, dir, eyeDistance)`. Rank by
   `achieved` first (closer to `eyeDistance` is better - i.e. the shot that needed the
   least clamping), facing as the tie-break. This both fixes root cause 1 (same formula,
   literally) and turns root cause 2's binary "blocked/not" into a graded score, so a
   shape where nothing is perfectly clear still picks the *least bad* rotation instead of
   falling through to an unchecked default.

   `alignSlideU`'s own signature does not need to change. `alignSlideToFace`'s direction
   does not depend on the candidate normal, so it is already immune to root cause 1 - no
   change needed there beyond also scoring by `clearEyeDistance` for consistency with
   root cause 2, if convenient.

4. **Hysteresis (root cause 3 - do this after 1-2 are in and re-measured).** Give
   `searchSlide` an optional previous-offset hint:

   ```cpp
   template <typename ToEye>
   SlideOffset searchSlide(const VariantSpec& v, CellId target, float eyeDistance,
                           ToEye toEyeFor, const SlideOffset* hint = nullptr);
   ```

   When `hint` is given, build its candidate first (no bisection over the grid); accept it
   outright (skip the ranked search entirely) when its `achieved` distance is at least,
   say, 90% of `eyeDistance` - "still basically clear" - so a piece's cell-to-cell motion
   does not re-target a different rotation purely because the global argmax ticked to a
   marginally better one. Fall through to the full ranked search only when the hint has
   degraded past that threshold. Thread the hint from the two call sites that track a
   live offset: `alignOffsetFor` (`src/gui/main.cpp`) passes `{st.geometryAlignOffset,
   st.geometryAlignOffsetV}`; `updateShapeSequence`'s Travel re-aim passes `seq.offset`.
   `alignSlideU`/`alignSlideToFace` gain a matching optional `const SlideOffset* hint =
   nullptr` parameter forwarded straight through. Re-measure whether jitter is still
   visible before adding this - if roots 1-2 alone settle it, this step can be skipped
   and the finding recorded as resolved by 1-2.

### Tests

- **Fix the two self-consistent tests to use the shared primitives.** Replace their
  hand-rolled `toEye`/`eye` computation (`travel * -1.0f + normal * kDefaultFollowLift`,
  then `centre + toEye * eyeDistance`) with calls to `chaseEyeDirection` and
  `clearEyeDistance`. This is not optional polish: as written, these tests cannot catch a
  future regression of the same kind, because they assert the search agrees with itself.
- **New regression test, built from a real traced path, not a seat's own frame.** Load
  `torus`, play a multi-cell slide (e.g. the a1-a5 rook move used in the repro - get it
  from `Game::legalMoves()`/`moveText`, not hand-typed), build its `MovePath`, and for a
  dense sweep of `t` in `[0, 1]`: compute `surfaceMoveSample` at `t`, the `travel` the
  front end would compute (the windowed finite difference `surfaceFollowCamera` already
  uses - reuse it or its own logic directly, not a seat-frame shortcut), and assert
  `clearEyeDistance(surf, position, chaseEyeDirection(normal, travel, lift),
  chaseEyeDistance(surf)) >= 0.9 * chaseEyeDistance(surf)` at every sampled `t` once the
  anti-clip has picked its best rotation for that `t` (i.e. run the real search, not the
  identity rotation). This must fail on the current code (confirm it does, before the fix
  lands) and pass after. Repeat for `klein` with the same move shape where legal.
- **Distance-clamp unit test.** `clearEyeDistance` on a small synthetic `PlaySurface` (or
  directly against `blocked` with a hand-built shape) returns `maxDistance` when clearly
  unblocked, something strictly between `minDistance` and `maxDistance` when partially
  blocked, and `minDistance` when nothing is clear - three cases, three assertions.
- **If hysteresis (step 4) is implemented:** a test that feeds `searchSlide` a hint equal
  to the true best candidate's close neighbour and confirms it is accepted without the
  full grid re-ranking changing the result by more than one grid step, versus a hint that
  has become genuinely blocked, which must still trigger a full re-search.

### Acceptance

1. The repro clip (`torus`, a1-a5, `--shape-follow chase`) shows the whole shape or a
   reasonably-framed close subset of it at every frame - never a slab filling the screen
   with pieces scattered off-frame.
2. The two existing anti-clip property tests pass using the shared `chaseEyeDirection`/
   `clearEyeDistance` primitives instead of a reimplementation.
3. The new dense-sweep regression test (above) passes on `torus` and `klein`.
4. `tools/test.sh --build render` is green; no change to the flat-board or turntable
   camera paths (this is a `chase`-path and search-only fix).

### Risks and non-goals

- This does not touch `ShapeMoveSequence`'s choreography timing (M17.19) or the morph-rate
  limiter - only what the chase camera and the search agree the eye distance and direction
  *are*. The choreography's Align/Approach/Return stages are unaffected except that
  Travel's camera is now the corrected one.
- `clearEyeDistance`'s bisection adds up to 8 extra `blocked` calls per candidate per
  search; `blocked` is already called once per candidate today, so this is a constant-
  factor cost on a search that is already bounded (a couple of builds on the common path,
  per M17.19's own fix for the ~5 fps regression) - re-run that frame-time bench
  (`--bench-frame`) on a followed `torus`/`klein` move after this lands to confirm it is
  still cheap.

### Status: M17.20 built (2026-10-03, opencode)

Implemented 1-3, plus the per-candidate-travel half of root cause 1 that steps 1-3 as
written missed, plus step 4 (it *was* still needed - see below).

- **`chaseEyeDirection` / `clearEyeDistance` (steps 1-2).** `surfaceChaseCamera` now calls
  the shared eye function; `surfaceFollowCamera` clamps the requested distance to the last
  `clearEyeDistance` along it. `chaseTravel` was extracted (file-local in `play_surface.cpp`)
  so `surfaceFollowCamera` has one travel computation.
- **The search (step 3), with one deviation.** Scoring is now `achieved` first (the
  `clearEyeDistance` at that candidate), facing as the tie-break, via a passed-in per-
  candidate `score` callable; the orientable probe still orders the grid and the first
  fully-clear candidate wins, so the common path stays a couple of builds. **Deviation:**
  sharing only the *direction formula* is not enough. For a chase, the camera's travel
  depends on the candidate pose, and it draws the moving `surfaceMoveSample`, not the target
  seat's centre. A single travel vector (or the seat) left the repro clipping: a
  per-candidate ideal of `6.96` (the nominal distance) collapsed to `0.5` when measured on
  the drawn sample. So the front end now uses a new `alignSlideU(v, target, path, t, ...)`
  overload that rebuilds each candidate and scores `surfaceMoveSample(path, candidate, t)`
  with `chaseTravel(path, candidate, t)`. The old travel-taking overload is kept for the
  seat-level tests and the turntable path.
- **Hysteresis (step 4) - implemented, because it was still visible.** After 1-3 + the
  per-candidate score, the repro was stable except frame 9 of 12, a tube slab: the capture
  carries the offset frame to frame and the stateless argmax jumped. `searchSlide` now takes
  an optional `const SlideOffset* hint`; when its score is still `>= 0.9 * eyeDistance` it
  is accepted outright. `alignOffsetFor` feeds back `geometryAlignOffset(V)`. With it the
  repro is a settled, well-framed chase at every frame.
- **Tests.** The two self-consistent property tests now recompute with `chaseEyeDirection` /
  `clearEyeDistance` instead of the old inline formula. New: three-case `clearEyeDistance`
  clamp test; a dense 49-sample traced-path sweep on `torus` (a1-a5 from `legalMoves()`,
  `view::tracePath`, one real search per `t`) asserting the drawn sample stays at
  `>= 0.9 * eyeDistance`. It is red on both the old raw-normal formula (12 failures) and on
  a fixed-travel search (13 failures), and green now.
- **Klein, deviation from the acceptance.** The dense sweep is `torus`-only. Measured: on
  the self-intersecting Klein bottle *no* rotation has any clear line at the nominal distance
  for the rank-seam a1-a5 move - the best achievable over the whole grid is the clamp floor -
  so `>= 0.9 * eyeDistance` is unachievable there for any implementation, not just this one.
  Klein is covered per-cell by "the chase align clears the shape at the camera's own
  distance" (its 9/10 threshold) and by the clamp. This is recorded in the test comment.
- **Cost.** `--bench-frame 30` on a followed torus chase: 42.9 ms/frame at Debug -O0 versus
  38.4 ms for the same capture with no follow (turntable 42.8 ms), i.e. ~4.5 ms of search,
  not the M17.19 ~5 fps. The hysteresis makes the common path the hint (one build + one
  score); the whole-grid fallback only runs while nothing is clear.
- **`tools/test.sh render`, the 17 `gui-*` ctest captures and `tools/precommit.sh` are
  green.** The turntable capture is unchanged (whole torus at every frame).

---

## M17.21 Seam rails and coordinate labels on the shape (the M17.5 remainder)

The flat board already does both of these things and the shape view does neither: the flat
glued board draws a coloured rim at a seam (`src/view/seams.cpp`'s `SeamMap`/
`seamRampColor` - "both ends of one identification share a colour off a hue ramp") and
file/rank letters at its near edges (`src/render/ui.cpp`'s `drawLabel` block, ~line 984,
which explicitly skips when `surfaceView` is true). On the shape, a player sees the donut
or the figure-eight but not *which loop is which axis*, and has no way to read "this is
rank 5" off the board itself - only the off-board text legend (`seamLegend`, already drawn
in the corner: "the file edges are the same edge" in green, "the rank edges are the same
edge" in blue, visible in every shape-view capture today) says so in words.

**Why a torus/Klein shape has no "edge" to label the way the flat board does.** Both axes
are periodic, so there is no boundary cell to put a label outside of - the flat board's
"project the edge cell, extrapolate one cell outward" trick has nothing to extrapolate
from. The right analogue is different for the two features:

- **Seam rails**: not a rim at a boundary (there is none, physically) but a marker at the
  *wrap locus* - the one ring of the shape where the lattice coordinate wraps from its
  last value back to its first. For the gluing on axis 0 (file) that locus is the ring of
  cells where `file == 0` (equivalently `file == nx - 1`, the two are one ring on the
  shape); for axis 1 (rank) it is the `rank == 0` ring. On `klein`, the rank ring is where
  the half-twist happens - the figure-eight's pinch, already visible geometrically - so
  colouring it is exactly the thing that turns "huh, a pinch" into "ah, *here* is the
  twisted seam."
- **Coordinate labels**: with no edge, label individual seats directly, the way a piece is
  placed - small text billboarded above a seat's surface point. Labelling every one of 64
  cells would be clutter; label one reference ring per axis (e.g. every file letter along
  the `rank == 0` ring, every rank number along the `file == 0` ring - the same two rings
  the seam rails mark, so the colour and the text reinforce each other) rather than the
  whole lattice.

### Build

**Seam rails.** `PlaySurface` already builds its board as **one mesh with per-vertex
colour** (`MeshVertex::color`, white today - see ADR-0019 and the M17 status above), the
same mechanism `GHOST` extends with alpha. Tint, do not add a pipeline:

- In `PlaySurface::build` (and `buildStacked` for the D>=3 case, if in scope - see
  M17.22), after the ordinary white/checker vertex colours are assigned, identify the
  patches whose cell lies on each periodic axis's wrap ring (`cell.file == 0` for the file
  axis, `cell.rank == 0` for the rank axis - use whichever boundary convention
  `slidesAlongRanks`/the existing identification code already treats as canonical, so this
  agrees with where `SeamMap` draws the flat rim for the same variant).
- Colour: reuse `view::seamRampColor`/`SeamMap`'s existing per-axis colour exactly (build
  a `SeamMap` for the variant, read the colour of a face on that axis) so a player who has
  seen the flat view's rim or the legend recognises the same colour on the shape - do not
  invent a second palette.
- Width: tint only the row of corners nearest the wrap edge of the patches on that ring
  (not the whole cell), so the rail reads as a thin line along the seam rather than a
  fully recoloured rank/file of squares. A reasonable first cut: blend the ramp colour
  into the vertex colour at full strength on the corners exactly on the wrap boundary,
  fading to the ordinary board colour over one subdivision step inward (`kSurfaceSubdiv`
  already gives each square a grid of corners to blend across).
- `klein`/`mobius` (non-orientable, `flip` on an axis): the flipped axis's rail should
  still read as one coherent ring even though the identification reverses the *other*
  coordinate - colour is a per-vertex property keyed to "which axis's wrap this corner is
  on," not to the direction of travel across it, so no special case should be needed
  beyond correctly finding the ring.
- Mirrors (`mirrorbox`): excluded, exactly as `SeamMap` excludes them from the flat rim
  ("nothing is on the other side, so a hue that promised a destination would be a lie") -
  `hasPlaySurface` likely already excludes `mirrorbox` from the shape view entirely (it
  has no glued axis to become a shape from); confirm, do not add rails there if so.

**Coordinate labels.** Extend `src/render/ui.cpp`'s existing `drawLabel` block
(~line 984) rather than writing a second label system:

- Replace the `!surfaceView` early-out with a branch: flat board keeps today's edge-label
  code unchanged; surface view uses `PlaySurface::seats()` (already built for rendering -
  reuse `shell`/`session`'s own surface, do not rebuild a second one) to find, for each
  file `f`, the seat with `rank == 0`, and for each rank `r`, the seat with `file == 0`.
  Project `seat.centre` through the session's effective camera (the same `cam.project`
  call the flat path already uses), and draw the label *outward along the seat's own
  normal* projected to screen space (analogous to the flat path's "extrapolate one cell
  outward using the inward neighbour," but there is no flat inward/outward pair on a
  surface - use `seat.normal` directly: project both `seat.centre` and `seat.centre +
  seat.normal * (one cell's worth of stepU/stepV)` and offset the label the same way the
  flat code offsets from `at`/`inward`).
- Respect `shell.settings().showCoordinates` exactly as the flat path does - same
  toggle, one setting for both views.
- Do not label every seat: only the two reference rings (`rank == 0` for file letters,
  `file == 0` for rank numbers) - 8 + 8 labels on a standard-sized glued board, matching
  the flat view's count.
- Skip a label whose seat is on the far side of the shape from the camera (behind the
  silhouette) rather than drawing it through the mesh: skip a seat whose normal faces away
  from the eye (`dot(seat.normal, eye - seat.centre) <= 0`), so a label never appears to
  float in front of the far side of the donut.

### Tests

- A `--shot` of `torus --geometry` and `klein --geometry` with `showCoordinates` on shows
  rails (pixel-level: sample a few corners known to lie on the `file==0`/`rank==0` rings
  and assert their colour matches `seamRampColor`'s output for that axis, not the plain
  board colour) and some non-zero count of label glyphs are recorded by the UI draw list
  (reuse whatever assertion style the existing flat-label tests use, if any exist, or add
  one alongside this change).
- A render/view unit test that the identified wrap ring (`cell.file == 0` or `cell.rank ==
  0`, per axis) used for rail placement is the *same* set of cells `SeamMap` marks as
  seam faces for that variant on the flat view - a differential test, so the shape and the
  flat view can never silently disagree about where the seam is (this is exactly the
  pattern M13's "derived must equal hand-authored" test already uses elsewhere).
- `standard` and every non-glued variant: no rails, no change in output (`hasPlaySurface`
  is already the gate; confirm the existing "`standard --geometry` is a strict no-op"
  golden still holds).
- Validation-clean captures (`--shot`/`--clip`) on `torus`, `klein`, `mobius`, `cylinder`.

### Acceptance

1. `torus --geometry` and `klein --geometry` show two differently-coloured rings on the
   shape matching the flat view's legend colours for the same variant, and the file/rank
   labels for one reference ring each, toggled by the same `showCoordinates` setting the
   flat board uses.
2. The rail location and the flat `SeamMap`'s seam faces agree, by the differential test
   above.
3. No visible change to `standard`, `cube5`, `hyper4` or any other non-surface variant.
4. `tools/test.sh --build render` and `tools/test.sh --build app` green; `nix flake check`
   clean on the new captures.

### Risks and non-goals

- This is presentation only - rails and labels must not enter `VariantId`, must not
  change `PlaySurface::blocked`'s occlusion geometry (the tint is colour, not shape), and
  must not interact with GHOST's alpha pipeline beyond both reading the same vertex colour
  array (GHOST already carries alpha in `MeshVertex::color.a`; the rail tint only touches
  `.rgb`).
- D>=3 stacked shapes (`torus3d`, `hyper4`) are explicitly out of scope here unless folded
  into M17.22, since `buildStacked` is a different code path with no continuous
  parametrisation to key a "ring" off in the same way - decide there, not here.

### Status: M17.21 built (2026-10-04, opencode)

Both halves of the M17.5 remainder are in.

- **Seam rails.** New `render::surfaceRail(seams, cell, axis, i, j)` (`play_surface.cpp`)
  returns the flat view's `SeamMap` colour at the cell for a patch corner, weight 1 on the
  wrap edge and 0 elsewhere. `BoardRenderer::buildInstances` blends it into the board
  mesh's own vertex colours - the same per-vertex channel GHOST already uses - on the patch's
  face, underside and the rim band on the wrap edge, so no pipeline was added. It reads the
  cell's own `SeamMap` face (Min end, glued only), so the shape and the flat view cannot
  disagree about where the seam is or what colour it is; a mirror and a non-ring cell get
  nothing. `showSeams` gates it, exactly as the flat rim. The tint is colour only: it never
  touches the picker or `blocked`, and `VariantId` is untouched.
- **Coordinate labels.** `Ui::buildGameHud` no longer early-outs the surface view: the flat
  block is unchanged, and the surface branch labels the two reference rings the rails colour
  - file letters along `rank == 0`, rank numbers along `file == 0` - reusing the same
  `drawLabel` lambda, camera, machine face and `showCoordinates` toggle. Each label sits on
  its seat and is pushed out along the seat's own normal (the flat code's `at`/`inward`
  offset, with `inward` the seat reflected across the outward point). A seat whose normal
  faces away from the eye is skipped, so no label floats in front of the far side.
- **Tests.** New differential test ("the shape's seam rails sit on the flat view's seams")
  checks, per `cylinder`/`torus`/`mobius`/`klein` and per corner, that a rail exists exactly
  where the cell is on the coordinate-0 row *and* `SeamMap` has a glued Min face there, and
  that its colour is that face's. A companion test pins the two label rings at `nx` + `nz`
  seats. An integration test ("the shape tints its seam rails into the board mesh") checks
  `buildInstances` carries the seam colour in the torus surface vertices, for the file ring
  at `(0,3)`, and that `showSeams = false` removes it.
- **Deviations.** (1) The spec placed the rail tint in `PlaySurface::build`, but the vertex
  colours are assigned in `BoardRenderer::buildInstances`; the tint lives there and only the
  reusable ring/colour lookup is in `play_surface`. (2) The spec's "reuse the session's own
  surface" has nothing to reuse - no `PlaySurface` is cached on the session or the renderer -
  so the UI builds one per frame in the surface branch only (gated on `showCoordinates`).
  Recording it in case the cost ever matters; the draw already builds its own.
- **D>=3.** Skipped via a new `PlaySurface::stacked()` accessor, per the non-goal. The
  `torus3d`/`hyper4` GUI captures are unchanged and validation-clean.
- **Verification.** `tools/test.sh render` green (90 cases, including the three new ones and
  the `gui-*` captures run separately); `ctest --preset dev -R gui` green (17/17);
  `tools/precommit.sh` green (format, build, arch, unit, property). Captures of `torus`,
  `klein`, `cylinder` and `mobius` with `--geometry` are validation-clean and show the two
  hue-ramped rings (matching the legend's file/rank colours) plus the reference-ring labels;
  `standard --geometry` remains a strict no-op (`hasPlaySurface` is the gate).

### Revision: coordinate labels disabled in 3-D, pending a real redesign (2026-10-04)

Playtest verdict on the labels specifically (the seam rails are unaffected and stay): the
camera-projected file/rank text reads badly wherever the board is shown in a 3-D
perspective - both the shape view's new reference-ring labels above and the **pre-existing**
flat-board labels in ordinary 3-D play (the same `drawLabel` mechanism, the non-surface
branch of `Ui::buildGameHud` that already existed before M17.21). Projecting flat text
through a steep, turning perspective camera was never going to read as clean lettering, and
it does not - at a grazing angle a letter skews, overlaps its neighbour, or goes
nearly edge-on. This is a legibility/typography problem, not a logic bug, and it is not
worth iterating on in-place the way the camera and the rails were.

**Disabled for now, in both places:** `showCoordinates`'s labels stop being drawn whenever
the board is *not* in the flat 2-D (`flatView`) projection - i.e. gate the whole label
block (both the existing flat-3D branch and M17.21's new surface branch) on `session
.flatView()` in addition to `v.dims.dims() == 2`, rather than drawing camera-projected text
in any 3-D context. The 2-D orthographic-ish view's labels are unaffected and keep working
exactly as before - this was never complained about. The seam rails (the coloured rings) are
presentation, not text, and are not affected by this change at all; `showSeams` still gates
them independently of `showCoordinates`.

**Deferred to closer to release:** a 3-D-native way to show a cell's coordinate - a billboard
that always faces the camera rather than flat world-space text, a per-cell tooltip on
hover/selection instead of an always-on label field, or something else - is real design work,
not a quick fix, and belongs to a later pass once the rest of the visual language (M18) has
settled. Do not attempt a redesign as part of disabling the current one; this entry is the
placeholder to pick back up.

### Status: coordinate labels disabled in 3-D built (2026-10-04, opencode)

The 3-D labels are off; the flat 2-D labels and the seam rails are untouched.

- **What changed.** `Ui::buildGameHud`'s label block was gated on `showCoordinates && dims
  == 2` alone, so it drew camera-projected text on the ordinary solid board and, since
  M17.21, on the shape. The gate is now the pure predicate
  `render::coordinateLabelsShown(settings, session)` (`src/render/ui.cpp`, declared in
  `ui.hpp`), which adds `session.flatView()`, so the block runs only in the flat 2-D
  projection. Both the pre-existing flat-3D branch and M17.21's surface branch live inside
  it and so are dropped together. The seam rails are the separate `showSeams` feature and
  were not touched. (The condition was extracted into a named predicate rather than grown
  in place, so the decision is testable without a device - a small deviation for
  testability, no behaviour beyond the gate.)
- **Tests.** New `tests/render/test_ui_labels.cpp` pins the predicate: shown on a flat 2-D
  board, hidden once `flatView` is off (the solid board and the shape alike), hidden with
  `showCoordinates` off, and hidden on a 3-D variant. `tools/test.sh --build render` green
  (101 cases, 4.6M assertions).
- **Verification.** `--shot` captures under `build/m17labels_check/` (repo-local, ignored):
  the flat solid board (`standard`) and the shape (`torus --geometry`) are **byte-identical
  in the board region** with `showCoordinates` on and off - the labels are gone - while the
  flat 2-D board differs between on and off, i.e. the `a-h`/`1-8` labels are still drawn
  there. The shape capture still shows the two hue-ramped seam rings. `tools/precommit.sh`
  green (format, build, arch, unit, property).

### Revision: both sides of a seam ring, and the Klein slide's premature reset (2026-10-05)

Two bugs reported together, both in `src/render/play_surface.cpp`, both specific to the
glued-ring mechanics M17.21 built on.

**1. Only one side of a seam ring is tinted.** Reported directly: on `cylinder`, only one
of the two edges that meet at the wrap loop carries the rail colour - a cylinder's seam
reads as a line down one side of the join and nothing on the other.

`surfaceRail` (`play_surface.cpp:135`) only ever checks `i == 0`/`j == 0` (a cell's own low
corner) against the `SeamMap`'s `Side::Min` face. Its comment reasons that the Min end and
the Max end are "the same physical ring... so tinting both would double the rail rather
than move it" - true of the exact corner *locus* (file 0's low corner and file (nx-1)'s high
corner really do sit at one coincident 3-D point, since the parametrisation is periodic) but
false of the *mesh*: each cell's patch is built with its own vertex copies, so the cell at
`file == nx - 1` has its own high-corner row sitting at that same point, and today it is
never visited - `surfaceRail` early-returns before ever checking a non-zero corner index,
so that cell's own near-seam vertices never get a chance to blend in the colour. The result
is exactly the report: colour fades in from one side of the join and snaps to plain board
colour on the other, instead of reading as one ring. (`SeamMap::build`, `src/view/
seams.cpp`, already generates a `Side::Max` face for every cell in that column too, with the
*same* colour as its `Side::Min` partner - confirmed by reading it: both ends of one pair
share one `canonical = min(cell, partner)` for the ramp-position/dimming computation, so
there is no second colour to reconcile, only a second corner to visit.)

Fix: `surfaceRail` should match `Side::Max` at a cell's *high* corner
(`i == kSurfaceSubdiv`/`j == kSurfaceSubdiv`) exactly as it matches `Side::Min` at the low
corner, each on its own cell, each independent of the other (not "either side", both when
both apply - they are on different cells, not the same corner, so there is nothing to
double). No change to `SeamMap`, no change to the colour, no change to the Min-corner path -
purely extending the one function to also look at the other end.

**2. The Klein slide resets before the shape has actually come home, when `kleinShift` is
nonzero.** Reported directly: dragging/following continuously around the Klein bottle's
rank axis does not stay smooth through a whole lap the way it should - it visibly resets,
and the right fix isn't the generic "two laps" `wrapSlide` already assumes but something
tied to the board's own size.

The reset is `wrapSlide`'s own job working correctly against the *wrong period*. In
`PlaySurface::build` (`play_surface.cpp:213-214`), the rank slide wraps at a fixed `2 * nz`
cells - "two laps... where a seam reverses a coordinate" (the comment's own words), which is
exactly right when `kleinShift == 0` (the shipped default) but stops being right the moment
it isn't. Read `kleinSurf` (`src/render/overture_scene.cpp:298-336`) to see why: the
cross-section angle sampled at a given `(u, v)` is `lemniscateAngle(u + phase * v)`, where
`phase = kleinShift / nx` (`shape_sequence.cpp:88`, `pose.collapsePhase`). Going once round
`v` (one lap) does two things at once: it rotates the cross-section by `kleinTwist` half-
turns (the `chi` term, a function of `v` alone - this is the one `wrapSlide`'s "two laps"
already accounts for), *and* it shifts the effective `u` sampled by `phase` - a spiral, by
the comment's own words at `kleinSurf`'s call site ("the square lines spiral round the tube
rather than the whole cylinder turning rigidly"). The embedding only returns to its exact
starting point once **both** have come full circle: the twist after a multiple of `2 /
gcd(kleinTwist, 2)` laps (1 or 2, which is what "two laps" already safely covers for any
integer twist), and the spiral only after a multiple of `nx / gcd(nx, kleinShift)` laps -
which for a shift that shares no common factor with `nx` (an 8-wide board shifted by 1
square, say) is **`nx` laps**, not 2. Wrapping the raw slide at `2 * nz` cells when the true
period is `nx` times longer snaps the accumulator back to a value that *looks* like the
start but isn't - the spiral has not finished - which is the visible reset.

This is exactly what was reported: *"N is the number of squares per dimension,
perpendicular to rotation"* - rotating around the rank axis, the perpendicular dimension is
file, and the period genuinely scales with `nx`, not with a constant.

Fix: in `PlaySurface::build`, when the surface kind is `Klein`, widen the rank slide's wrap
period from `2 * fnz` to `2 * fnz * fnx` - a conservative multiple that is always a multiple
of the true period for *any* integer `kleinTwist`/`kleinShift` (it does not need to be the
exact minimal period; `wrapSlide`'s whole purpose, per its own comment, is only to stop a
long drag grinding the float down to noise, so a larger-but-still-bounded window costs
nothing). Every other kind (`Tube`, `Torus`, non-orientable `Band`) has no such shift term
and keeps its existing period unchanged - this is scoped to `Klein` alone, gated the same
way the existing `kind` switch already is. The file slide's own period (`2 * fnx`, used by
every kind) needs no change: `lemniscateAngle` is already exactly one-lap periodic in its
full argument (`u + phase * v`), and sliding `u` alone, at fixed `v`, moves that argument by
exactly what `u` moves - the shift term rides along unaffected, so the existing "two laps,
always safe" logic for the file axis was never wrong. `searchSlide`'s own candidate grid
(the align/follow camera's search, same file) also needs no change: it only cares about the
cross-section's *orientation* for picking a camera-facing offset, which is the `chi`/twist
term alone, already correctly bounded by the existing `2 * nz` - the shift affects *which*
file lands where, not which way the tube faces, so widening its search window would only
make the align feature slower for no visible gain.

**Tests.**
- Seam rail: extend the existing differential test (seam rails agree with `SeamMap`) to
  check the *second* corner too - on `cylinder`/`torus`/`klein`, the high-corner row of the
  `nx - 1`/`nz - 1` cells carries the same colour as the low-corner row of the `0` cells on
  the same ring, not just one or the other. A `--shot` of `cylinder --geometry` with
  `showSeams` on, inspected directly (not just asserted), should show one continuous ring,
  not a line that fades on one side.
- Klein slide: a property test that samples `PlaySurface::build` at `pose.slideV = k * nz`
  for `k = 0 .. nx` (with `kleinShift` set to a value coprime with `nx`, e.g. 1) and asserts
  the resulting corner positions only repeat at `k` a multiple of the true period derived
  above (today's code would show them repeating at `k = 2`, which should go red first) -
  and a companion that `kleinShift = 0` reproduces the exact same corner positions as before
  this change, at every `k`, bit for bit (the default experience must not move at all).
- `tools/test.sh --build render` green; validation-clean captures of `klein --geometry`
  with `kleinShift` left at its default (0, unaffected) and, if there is a `--shot` flag or
  settings override already in the codebase for setting it (check before adding one), at a
  nonzero value through a fuller slide range, inspected directly for a clean, unbroken
  spiral rather than a jump.

**Acceptance.** A cylinder's (and torus's, and Klein's) seam reads as one coloured ring from
both sides of the join, matching the flat view's legend. Dragging or following continuously
around a Klein bottle's rank axis with a nonzero `kleinShift` no longer snaps partway
through - the reset point moves out to the true period, and the default (`kleinShift = 0`)
experience is pixel-for-pixel unchanged. `tools/test.sh --build render` and `--build app`
green.

### Status: both sides of a seam ring, and the Klein slide's reset, fixed (2026-10-05, opencode)

Both fixes are in `src/render/play_surface.cpp`; `SeamMap` and the surface functions were
not touched.

- **Seam rail, both sides.** `surfaceRail` now matches the seam at the corner that meets the
  join on *that* cell: `Side::Min` at the low corner (`i`/`j == 0`), `Side::Max` at the high
  corner (`== kSurfaceSubdiv`), each independent, so the `nx-1`/`nz-1` cell's own
  high-corner vertices are tinted with the same colour as the `0` cell's low-corner ones.
  No colour, `SeamMap` or Min-path change.
- **Klein rank wrap.** The rank slide's wrap window widens from `2 * fnz` to `2 * fnz * fnx`
  **only for `Klein` and only when `pose.collapsePhase != 0`**. The widened window is a
  multiple of the true spiral period for any integer `kleinTwist`/`kleinShift` (no gcd
  needed); gating on the shift is the one deviation from the spec's sketch, and it is what
  keeps the shipped `kleinShift = 0` path bit-for-bit identical - an unconditional widening
  changes the `k >= 2` sampling the companion test pins. Every other kind and the file slide
  keep their existing windows.
- **Tests.** The seam-rail differential test now checks both corners - the high-corner row
  of the `nx-1`/`nz-1` cells carries the `Side::Max` colour matching the `0` cells'
  `Side::Min` row, on `cylinder`/`torus`/`mobius`/`klein`. New property test
  `a Klein bottle's rank slide period scales with its square shift`: with a coprime shift the
  corners repeat only at `k % nx == 0` (red at `k = 2` before the fix), and a companion
  asserts `collapsePhase = 0` reproduces the pre-change positions bit-for-bit at every `k`.
  `tools/test.sh --build render app` green (186 cases, 4.96M assertions);
  `tools/precommit.sh` green (format, build, arch, unit, property).
- **Verification.** A `--shot` of `cylinder --geometry` at 1600x1200 with seams on, captured
  before and after the `surfaceRail` change and diffed (`build/m17seam_check/`): the changed
  pixels are confined to the seam line's bbox `(607,479)-(956,903)`. Looked at closely, the
  before frame is a white seam with faint colour on one side only; the after frame is one
  continuous hue-ramped cyan-green-blue rail reading from both sides of the join. There is
  no headless flag for `kleinShift` (the capture path hardcodes its own default settings), so
  the nonzero-shift spiral is pinned by the property test rather than a capture; `--slide-v`
  alone uses the default shift and is unchanged.

### Correction: the rank wrap wasn't the bug - the seam itself drifts while sliding (2026-10-05, Claude)

Reported directly, from actual play: dragging a Klein bottle with `kleinShift` set still
visibly pulls the seam's two sides apart mid-drag - "if I rotate the shape so the squares
are aligned, after half a rotation I get a misalignment of 1 square" - even though the
fix just above only ever addressed *when the slide resets*, not whether the seam stays
shut while sliding between resets. It does not. Found and fixed by direct numerical
investigation (a throwaway differential test against `derivedSurfaceAt`, not a guess),
not dispatched to opencode - the user asked for this round to be done directly.

**Root cause.** `kleinSurf` samples `lemniscateAngle(u + collapsePhase * v)` for the
pinch, and `v` is *the same* slid coordinate (`vv + sv`) the ring position uses - nothing
distinguishes "which rank this cell is" from "how far the whole board has been dragged."
A differential sweep (`derivedSurfaceAt(Klein, u, 0 + sv, pose)` against
`derivedSurfaceAt(Klein, 1 - u, 1 + sv, pose)`, every file, several `kleinShift` values,
several `sv`) showed the naive partner `(1 - u, 1)` - what `SeamMap` and the rest of the
renderer assume - is only the *true* geometric partner when
`collapsePhase * (v_lo + v_hi) == 0`, where `v_lo = sv` and `v_hi = 1 + sv` are the two
absolute `v` values the seam's two sides actually land on once the board has slid by `sv`
laps. The true partner is `(1 - u - collapsePhase * (v_lo + v_hi), 1)` - confirmed to
machine precision, not approximately. Two consequences:

1. Even at rest (`sv = 0`), a nonzero `kleinShift` leaves the seam open by exactly
   `collapsePhase` of a file-width - `kleinShift`'s own intent ("line the squares up at
   the seam") was never fully realised; a player could only get it to *look* aligned by
   manually dragging to cancel the residual, which is exactly the "rotate the shape so
   the squares are aligned" step in the report.
2. From there, every additional lap of drag opens it further by `2 * collapsePhase`,
   which is the actual bug: the seam does not stay shut between the (correctly-timed, per
   the fix above) resets - it drifts continuously within each cycle.

**The fix.** `PlaySurface::build` now subtracts `collapsePhase * (0.5 + sv)` from `su` -
the one knob that shifts every sample's `u` together - whenever the surface is Klein with
a nonzero shift. The algebra: the pinch argument becomes
`(u - collapsePhase * (0.5 + sv)) + collapsePhase * (vv + sv)`, and the two `sv` terms
cancel exactly, for every cell, leaving the spiral pattern completely independent of how
far the board has slid. One consequence worth calling out on its own: this also made the
*previous* fix's widened rank-wrap window (`2 * fnz * fnx`, reasoned as "the spiral needs
that many laps to close") unnecessary - if the spiral never depends on the slide amount
in the first place, there is nothing left for a wider window to protect, so the window
was reverted to the plain `2 * fnz` every other warp already uses. Simpler is correct
here, not just simpler.

**Tests.** New `a Klein bottle's seam stays closed under a square shift and a slide`
(`test_play_surface.cpp`) - a differential check through the real `PlaySurface::build`
path (not the throwaway investigation), several shifts, several slide amounts including
multiple whole laps, red before the fix (two genuine failures at `shift=1,
slideVLaps=1` and `shift=3, slideVLaps=0`) and green after. The existing
`a Klein bottle's rank slide period scales with its square shift` test is renamed and its
expectation corrected (`k % nx == 0` → `k % 2 == 0`, matching the now-simpler window) with
the reasoning above recorded in its own comment, rather than left stale and misleading
next to code that no longer matches it.

**Verification.** `tools/test.sh --build render app` green (192 cases, 4.97M assertions,
Vulkan validation on); `tools/precommit.sh` green (format, build, arch, unit, the ~276s
property sweep). No visual capture - there is still no headless way to set `kleinShift` -
but the fix is proven to float precision across every file position, three shift values
and four slide amounts including six full laps, which is a tighter check than a screenshot
would give.

### Correction: the flat<->shape morph spun through every slide-lap, not just one (2026-10-05, Claude)

Reported directly, a third and separate issue in the same area: "if I have rotated klein
6x times in shape mode, the square board would be doing rotation 6-ish times during
morph" - toggling SHAPE (M18.6's `geometryFormed` easing from 0 to 1) visibly spun the
board through as many turns as the board had been dragged beforehand, instead of just
forming in place. Found and fixed directly, test-first, same session as the seam-drift
correction above and for the same underlying reason: `v` was carrying more information
than any one part of `kleinSurf` actually needed.

**Root cause.** `derivedSurfaceAt`'s Klein case scales `ph = kTau * form` - the ring's
angular span grows from 0 (flat) to a full turn as `form` eases from 0 to 1. Inside
`kleinSurf`, the ring position is `p = (v - 0.5) * ph` (`zl` scaled by `ph` via `base`),
and `v` is `vv + sv` - the cell's own lattice fraction plus however far the board has
slid, unreduced, several laps for a long drag. So `p`'s total range as `ph` sweeps its
full span is proportional to `v`'s own magnitude: at `sv` near its wrap ceiling (6 laps,
before the correction above narrowed that ceiling to 2), `p` swept through roughly six
extra ring-turns over the course of the morph, on top of the one turn the shape's own
forming needs - exactly the reported scaling. The correction above already narrows the
*worst case* (the wrap ceiling dropped from up to `nx` laps to a plain 2), which alone
cut the worst measured sweep from 1.69 turns to 0.31 at the new ceiling - but the
dependency on slide position was still there in miniature, and at some slide positions
within the (now smaller) window the sweep was still measurably larger than at rest.

**The fix.** `kleinSurf`'s ring position now reduces `v` to its own 2-lap window before
computing `p` - the same period its cross-section's `chi` rotation already closes on, so
at a fixed `ph` (not ramping - every already-tested steady-state capture) the reduced and
unreduced `v` give an identical point: `cos`/`sin` only see `p mod 2*pi`, and 2 laps is
always a multiple of that for any integer `kleinTwist`. Only while `ph` is ramping does
the reduction matter, which is exactly where it is wanted. The pinch/spiral term just
above keeps the unreduced `v` untouched - it still needs the real lap count, and the
seam-drift correction's own compensation already makes it slide-independent regardless.

**Tests.** New `the Klein morph does not amplify rotation with how far the board has
slid` (`test_overture_scene.cpp`): measures the total unwrapped angular sweep of a fixed
lattice point as `form` sweeps 0 to 1, at slide positions from six laps negative to six
positive. Confirmed red first by disabling the reduction: the same measurement read 5.9
turns at six laps - matching "rotated 6x" almost exactly - and green after restoring it,
every case under two turns.

**Verification.** `tools/test.sh --build render app` green (193 cases, 4.97M assertions,
Vulkan validation on); `tools/precommit.sh` green. No visual capture - same reason as
above, no headless way to drive a live multi-lap drag plus a morph - but the red/green
swing (5.9 turns to under 2, confirmed by disabling and restoring the one-line reduction)
is as direct a demonstration of the fix as a recording would be.

Spec'd in full in
[`M17.12-shapes-above-two-dimensions.md`](M17.12-shapes-above-two-dimensions.md): slide and
invert for `torus3d`/`hyper4` (currently ignored by `buildStacked`), a ghost-based answer to
the hidden-sheet problem (reusing the existing GHOST pipeline rather than inventing one),
pinned adjacency tests, and a new `torus3d_twist`-style variant - a twisted 3-torus,
declarable today as plain data (a `flip` on one `torus3d` axis, exactly as `klein.toml`
already does in 2-D) with the shape view extended to show the twist as a third D>=3
showcase alongside `cube5`/`hyper4`. Third of the three Wave 1 completion pieces, after
M17.21.

### Status: M17.22 built (2026-10-04, opencode)

Parts 1-3 and the new variant's flat-lattice data are in; part 4's twisted **shell** is
deferred, with the reason recorded in
[`M17.12-shapes-above-two-dimensions.md`](M17.12-shapes-above-two-dimensions.md).

- **Slide (part 1).** `playShapePosition` takes an optional `SurfacePose`, and `torus3d`'s
  file/rank fractions are shifted by `slideU`/`slideV` (in cells) before `shellTube`.
  `PlaySurface::buildStacked` now takes the pose, wraps the slide into the same two-lap
  window the 2-D path uses (`wrapSlide`), and forwards it into every sample (the per-cell
  loop and `positionAt`). `hyper4` is a bounded 4-cube with no periodic axis, so its slide
  is a verified no-op, not a fake control. The GUI's middle-drag adds the horizontal slide
  for a stacked shape whose ranks the 2-D `slidesAlongRanks` does not already call a ring
  (`hyper4`), without double-counting `torus3d`.
- **Invert (part 2).** `buildStacked` applies the same revised invert the 2-D board does:
  `evert > 0.5` reverses the outward normal and derives the piece frame from it, leaving
  every tile exactly where it was. **Deviation from the spec's own sketch:** it suggests
  parameterising `shellTube`'s radius as an eversion. The play board's invert is a *side
  swap*, not a geometric eversion (M17.7 revised), and the 2-D path the spec says to match
  does exactly this; a geometric `shellTube` eversion would move every cell and contradict
  the pinned "never moves the board" assertion the 2-D invert test already holds.
- **Ghost picking (part 3).** The renderer already ghosted the stacked mesh - it writes the
  same vertex-colour alpha channel the 2-D path does and uses the same blend pipeline
  (M17.10) - so no renderer change was needed. `PlaySurface::pick` takes the board's alpha
  (`geometryGhost`); on a stacked surface it collects every ray intersection, sorts them
  near to far, and - when ghosted below `kGhostPickVisibility` - accumulates opacity toward
  the eye and returns the first tile where the stacked sheets read as more opaque than
  transparent. At full opacity (the default) it is the nearest hit, bit for bit the old
  behaviour, so every existing caller is unchanged. The GUI passes
  `settings.geometryGhost`.
- **The variant (part 4, data half).** `variants/torus3d_twist.toml` copies `torus3d` and
  gives the rank identification `flip = ["file"]`, exactly as `klein.toml` does in 2-D.
  **Confirmed first, before any shape work**, that it loads and plays on the flat lattice:
  non-orientable, a legal opening (42 moves, no side in check), perft 42/1616 and
  oracle-agreeing, FEN-N round-trip, and the full shipped-variant golden suite green. A new
  golden section proves the twist's consequence: a bishop's diagonals are colour-bound on
  `torus3d` (32 of 64 cells) and reach all 64 on `torus3d_twist` - the 3-D analogue of
  Klein's own test.
- **Adjacency, pinned.** New `[render]` test. `torus3d`: file/rank-adjacent seats are
  ordinary cell steps on one shell; level-adjacent seats share their angular position about
  the core circle and differ in shell radius (a radial nesting move, not Euclidean
  nearness). `hyper4`: file/rank-adjacent are cell steps; the aeon axis is the nesting and
  adjacent aeons are distinct tiles, not a surface step.
- **Deferred: part 4's twisted shell.** Wiring a lemniscate-pinched `twistedShellTube` into
  `buildStacked` needs the normal/frame-continuity walk M17.18 had to add twice for the 2-D
  Klein bottle, adapted to this path - the item the spec itself flags as research-grade.
  Rather than ship a visibly broken shell, `torus3d_twist` ships **flat-lattice-only**:
  `playShapePosition` does not recognise it, so `hasPlaySurface` is false and no geometry
  toggle is offered for it. The spec's own acceptance criterion 2 names this as the
  non-failing outcome; the gluing-language claim the variant exists to make is carried by
  the data (the engine derives the transport and the bishop escapes its colour).
- **Verification.** Four new `[render]` cases (slide, invert, ghost picking, adjacency);
  `tools/test.sh render` green (94 cases, 6.46M assertions); `ctest --preset dev -R gui`
  green (22 tests) with five new validation-clean captures
  (`gui-geometry-torus3d-{slide,invert,ghost}`, `gui-geometry-hyper4-{invert,ghost}`); the
  `torus3d_twist` goldens green; `tools/precommit.sh` green (format, arch, unit, property).
  `nix flake check` was not run (the spec's acceptance asks for it, but AGENTS.md marks it
  the release gate and the task budget allowed skipping it).

---

## M17.23 Bug fix: the Approach/Return camera loses the board entirely

Found 2026-10-04, independently verifying M12.6's runner: a `--play --clip` export of a
single followed move on `torus` has frames that are **completely empty** - not small, not
distant, a single flat background colour across every pixel (confirmed: sampling
`frame_0077.ppm` from a 113-frame clip gives one RGB value for all 2,073,600 pixels). It
happens during the choreography's Return stage (M17.19) - the camera flying from the close
chase shot back to the settled, far-above-the-board view.

### Root cause

The Approach/Return blend (`src/render/shape_sequence.cpp`, one call site:
`seq.camera = view::slerpCamera(session->playerCamera(), want, amount)`) uses
`view::slerpCamera` (`src/view/camera.cpp`). That function interpolates the camera's **eye
position** on a straight line in world space, and *derives* `target` from it afterwards:

```cpp
const Vec3 eye = a.eye() + (b.eye() - a.eye()) * t;
out.target = eye - e * out.distance;  // e is the slerped view direction
```

`OrbitCamera` itself does not work this way anywhere else: `target` is the primary,
authored state (`distance`, `yaw`, `pitch` orbit *around* it) and `eye()` is always a
*derived* method (`target + direction * distance`). `slerpCamera` inverts that relationship
- it treats eye as primary and back-solves a target - for exactly the one blend where the
two endpoints' targets are **not** the same point: `a` is `session->playerCamera()`,
targeting the board's centre; `b` (`want`) is the chase camera, targeting the piece
(`surfaceChaseCamera` sets `cam.target = piece.position`, pinned at the landed square during
Return). A straight eye-space line between a close, piece-centred vantage and a far,
board-centred one, combined with an *independently* slerped orientation, has no reason to
keep either target in view partway through - and empirically does not.

**Why the existing test did not catch it.** `tests/unit/view/test_camera.cpp`,
`"slerpCamera's eye travels a straight line"` is the one test that exercises intermediate
`t` values, and it sets `a.target = b.target = {0,0,0}` - the *same* point for both
endpoints. Under same-target inputs the bug's symptom (the derived target drifting away
from the thing both cameras are actually looking at) is invisible by construction: there is
only one subject position to begin with, and "eye travels a straight line" says nothing
about where `target` ends up at intermediate `t` - the test never asserts it. The
Approach/Return use case this function was built for (M17.19) has **always** had different
targets; nothing exercised that shape until a full clip was captured and inspected frame by
frame.

### The fix

Make `target` the thing that is linearly interpolated, and derive `eye` from it - matching
`OrbitCamera`'s own representation instead of inverting it:

```cpp
out.target = a.target + (b.target - a.target) * t;
out.distance = std::max(0.5f, a.distance + (b.distance - a.distance) * t);  // unchanged
// yaw/pitch/roll: still derived from the slerped quaternion `q`, exactly as today -
// this is the part M17.19 added to fix the 720-degree-spin defect, and it is untouched.
```

The orientation slerp (`q = slerp(qa, qb, t)`, and the roll derivation beneath it) is
**not** the part that is wrong and should not change - `"slerpCamera turns the shortest
way, never the long way"` pins exactly that property and must keep passing unmodified. Only
the eye/target relationship inverts. Remove the `eye = lerp(...); target = eye -
e*distance;` lines entirely; `out.eye()` is then whatever `OrbitCamera::eye()` already
computes from the new `target`/`distance`/orientation, the same as every other camera in
the codebase.

**Why this is correct, not just a different bug.** Both `a.target` and `b.target` are
always on or at the board in every real caller (`session->playerCamera()` targets the board
centre; a chase/follow camera's target is always a cell or a point on the shape) - so a
*linear interpolation between two points that are both at/near the board stays at/near the
board for every `t` in between*, by convexity. That is the literal guarantee the empty
frame is missing. For the degenerate case the existing test already covers - `a.target ==
b.target` - this is additionally an improvement, not merely a different behaviour: `target`
now stays at that exact point for every `t`, rather than only approximately recovering it
(the old formula's `target` was never actually pinned to equal the shared point at
intermediate `t` - no test checked that it did).

### Tests

- **Replace** `"slerpCamera's eye travels a straight line"` with
  `"slerpCamera's target travels a straight line"` - same structure (sample `t` from 0 to
  1, check against the linear-interpolation prediction), asserting `target` instead of
  `eye`. If a test still wants an eye-path property, state the weaker one that is actually
  true now (eye is a smooth, continuous function of `t`, not necessarily straight) rather
  than deleting coverage outright.
- **Keep** `"slerpCamera ends on its endpoints"` and
  `"slerpCamera turns the shortest way, never the long way"` passing unmodified - both are
  about properties this fix does not touch (endpoint exactness, orientation sweep).
- **New regression test, built from the actual failure shape**: two `OrbitCamera`s with
  *different* targets - one far and board-centred, one close and piece-centred, matching
  the real Align/Approach/Return magnitudes (`distance` around 8-10 for the settled camera,
  2-4 for a close chase, per `chaseEyeDistance`'s own `0.7 * span` scale) - and assert that
  for a dense sweep of `t`, **both** `a.target` and `b.target` project to `visible` (or at
  least one point of a small sphere around each does, to allow for some camera slack)
  through `slerpCamera(a, b, t)`'s resulting camera. This is the test that must fail on the
  current code and pass after - confirm it does, the same discipline M17.20 used.
- **End-to-end**: re-run the exact scenario that found this (`torus`, a followed move,
  `--play`/`--clip` through the Return stage) and confirm no frame in the sequence is a
  single flat colour - sample a handful of frames' pixel variance, not just one.

### Acceptance

1. The new regression test (different targets) passes; the existing three `slerpCamera`
   tests pass with only the one named replacement.
2. A re-capture of the clip that found this bug (`torus`, the a1-a5-style followed move,
   `--play`/`--clip` through a full Approach/Return cycle) has no empty frame anywhere in
   the sequence - every frame's pixel variance is above a trivial threshold.
3. `tools/test.sh --build render` green; `--play --clip` determinism (byte-identical on
   re-export) is unaffected, since this changes *what* the camera computes, not whether it
   reads a clock.

### Risks and non-goals

- This is the one call site (`src/render/shape_sequence.cpp`) and the one function
  (`view::slerpCamera`) - no other caller exists today (checked:
  `grep -rn "slerpCamera(" src/` finds exactly one use outside `camera.cpp` itself), so
  there is no other behaviour to protect beyond the three existing tests named above.
- Not a redesign of the choreography's stage timing, easing curves, or the chase/follow
  camera formulas themselves (M17.20's `chaseEyeDirection`/`clearEyeDistance` are untouched)
  - purely how two endpoint cameras are blended into one in-between camera.
- If, after this fix, the board is in frame throughout but the transition still *looks*
  rough (e.g. the board grows/shrinks unevenly, or the pan reads as abrupt), that is a
  framing/pacing judgement call for a human to weigh in on, not a correctness bug - note it
  in the status section rather than attempting further tuning unprompted.

### Status: M17.23 built (2026-10-04, opencode)

The fix is exactly the spec's: `view::slerpCamera` now linear-interpolates `target`
(`out.target = a.target + (b.target - a.target) * t`) and lets `OrbitCamera::eye()` derive
the eye from it, instead of interpolating the eye and back-solving a target. The orientation
slerp, the `distance` interpolation and the roll derivation are untouched; the stale
"the eye travels in a straight line" comment in `src/view/camera.hpp` was corrected to say
`target`.

- **Tests, test-first.** Renamed `"slerpCamera's eye travels a straight line"` to
  `"slerpCamera's target travels a straight line"` with genuinely different target
  endpoints; it asserts the target against the lerp prediction and, since the eye is no
  longer on a line, keeps a weak continuity check (no step jumps a camera's distance).
  `"slerpCamera ends on its endpoints"` and `"slerpCamera turns the shortest way, never the
  long way"` are unchanged and still pass. The new regression,
  `"slerpCamera keeps both endpoints' subjects in frame"`, blends a far board-centred player
  camera (`distance` 9) with a close piece-centred chase camera (`distance` 3.2, within the
  spec's 2-4 range) and requires both subjects projectable for a dense `t` sweep (a small
  per-subject sphere allows camera slack). A throwaway search under `build/` picked a chase
  pose that is a real failure shape rather than a degenerate one.
- **Before/after.** On the current code the regression test fails hard (107 of 202
  assertions); on the fixed code all four `slerpCamera` tests pass (311 assertions) -
  confirmed by stashing only `src/view/camera.cpp` and rebuilding. `tools/test.sh --build
  view render` is green (155 cases, 4.66M assertions); `tools/precommit.sh` green (format,
  arch, unit, property).
- **End-to-end.** Re-captured the exact scenario that found the bug - `torus`, the `a1a5`
  followed move, `--play --clip --follow route --shape-follow chase --cinema --geometry`
  (113 frames). **Before:** five frames had pixel standard deviation below 0.02, three of
  them (`frame_0077`-`0079`) exactly flat. **After:** minimum standard deviation 0.1139
  (`frame_0004`), none below 0.02, so no empty frame remains. A second export is
  byte-identical, so the determinism the clip relies on is unaffected.
- **Deferred (not a correctness bug).** The blend now keeps the board framed throughout;
  whether the Return pan *paces* as well as it could is a human framing judgement, left
  alone as the spec instructs.

---

## M17.24 Bug fix: the flat/shape morph (M18.6) drifts off-centre

Found 2026-10-04, playing with M18.6's new SHAPE morph: the fully-formed shape does not land
centred in the frame. Reproduced and confirmed with captures at `--formed 0`, `0.3`, `0.6`
and `1` on `torus` - the flat board starts centred, and the shape visibly drifts up and to
the right as it rolls up, ending noticeably off-centre at `formed = 1`.

### Root cause

The camera is framed on the shape by `frameBoard`/`frameGeometryCamera`
(`src/gui/main.cpp`/`src/render/play_surface.cpp`), which calls `Session::frameOn` with
`PlaySurface::bounds()` - the current shape's own bounding box. The call site's own comment
states the design precisely: *"The board has just become its own shape, or gone back to
being a diagram... so the camera is put round it - **once, on the change, and never while
the player is turning it**."* `frameBoard` fires exactly once, at the instant the SHAPE
toggle flips (`nowSurface != wasSurface`) - the same instant `Settings::geometryFormed`
*begins* easing from 0. At that instant the shape has barely started to roll up, so the
bounds `frameOn` captures are close to the flat board's. Nothing re-frames the camera as
`geometryFormed` continues easing to 1 over the following frames, so the final, fully-rolled
shape's actual bounds - a different size, in a different place - are never the ones the
camera was centred on. The one-shot design was correct when the toggle was instant (M17);
M18.6 made the shape's bounds a continuous function of `geometryFormed` without revisiting
the framing, which assumed they only ever changed at the toggle instant.

### The fix

Re-frame continuously while the morph is in flight, easing the camera's target (and
distance) toward the shape's *current* bounds each frame, rather than committing to a single
snapshot - this is also exactly what was asked for ("the center should be slowly shifting
during transformation"), not a reason to prefer the simpler one-shot-at-completion
alternative.

- In the interactive loop, wherever `Settings::geometryFormed` is actively easing toward its
  target (not yet settled - compare the live value to the target the way the existing
  `geometryEvert`/`geometryAlignOffset` easing already checks), recompute
  `frameGeometryCamera`'s ideal camera (target + distance from the *current* `formed`
  shape's bounds) every frame, and **ease the live camera toward it** rather than snapping -
  reuse whatever camera-blend helper is already in scope (`blendCamera`/`slerpCamera`-style)
  rather than inventing a new interpolation. Once `geometryFormed` settles, stop - the
  existing one-shot `frameBoard` calls (on a variant load, a save/load, SHAPE's own toggle
  instant) are fine as the *initial* target and do not need to change.
- Preserve the player's own yaw/pitch throughout, exactly as `Session::frameOn` already does
  for the one-shot case - this is a target/distance correction, not a camera angle change.
- `formed = 0` and `formed = 1` must still land exactly where they do today (the M18.6
  byte-identity guarantee for `formed = 1` is untouched by this - only the *path* the camera
  takes while `formed` is between 0 and 1 changes, never the endpoints).

### Tests

- A property test sweeping `formed` from 0 to 1 that the camera's target stays within the
  convex region spanned by the flat board's bounds-centre and the fully-formed shape's
  bounds-centre at every sampled point (the thing the current one-shot design fails) -
  built the same way the M17.23 regression was: from the real easing code path, not a
  synthetic camera pair.
- A test that `formed = 0` and `formed = 1` captures are pixel-identical to before this fix
  (the endpoints do not move, only the path between them).
- A validation-clean `--clip` of a SHAPE toggle on `torus`/`klein` showing the shape staying
  framed throughout, the same visual check the repro used.

### Acceptance

1. A `--formed` sweep from 0 to 1 on `torus` (or a live SHAPE toggle) keeps the shape
   recognisably centred throughout, not just at the two ends.
2. `formed = 0`/`formed = 1` captures are unchanged from before this fix.
3. `tools/test.sh --build render` and `--build app` green.

### Risks and non-goals

- This is a framing-only fix - it does not touch `SurfacePose::formed`, the roll math, or
  the M18.6 mirror-flip deferral already recorded; it only changes when and how often the
  camera asks "where is the shape now."
- Not a general "always track the subject" camera mode - scoped to the morph specifically,
  while `geometryFormed` is actively easing. The settled shape (and ordinary orbiting) keep
  today's behaviour.

### Status: M17.24 built (2026-10-04, opencode + Claude)

- **`Session::easeFrameOn(bounds, headroom, k)`** (new, `src/app/session.{hpp,cpp}`) blends
  `camera_.target`/`camera_.distance` toward `OrbitCamera::frame(bounds, ...)` by `k` in
  [0,1], touching nothing else - the same "angle stays the player's" contract `frameOn`
  already promises, just eased instead of committed outright.
- **`render::easeMorphFraming`/`morphFollowBlend`** (new, `src/render/play_surface.{hpp,
  cpp}`) rebuild the `PlaySurface` at the *current* pose and call `easeFrameOn` with a
  per-frame blend (`dt * kMorphFollowRate`, `kMorphFollowRate = 30`, capped at 1).
- **The interactive loop** (`src/gui/main.cpp`) tracks whether the `geometryFormed` ramp is
  in flight this frame and whether this is the frame it settles on; while in flight it calls
  `easeMorphFraming` every frame, passing `1.0` (an exact snap) on the settling frame so the
  endpoint lands precisely rather than asymptotically. The one-shot `frameBoard` calls
  elsewhere (variant load, save/load, the toggle instant itself) are untouched - they still
  set the *initial* target this now follows.
- **Tests.** `tests/render/test_play_surface.cpp` adds "the morph keeps the camera on the
  shape as its bounds move": sweeps a full flat-to-formed roll, asserts the eased camera
  stays within 0.3 units of the ideal frame at every step and never jumps more than 0.35
  units frame-to-frame (continuity), that the morph's own subject genuinely travels enough
  to matter (`shapeExcursion > 0.5`, measured at 0.80 - a camera that ignored this would be
  visibly off by the end), and that it lands exactly on the formed frame
  (`dist(prev, formedCentre) < 1e-4f`) and is a no-op once settled. 96 assertions, green.
  `tools/test.sh --build render`: 32/32 green.
- **Verification gap, stated plainly.** The implementer's own planned red/green check (break
  the fix, confirm the new test fails, restore it) was interrupted by a sandboxed `/tmp`
  write it never retried inside the repo - the fix and its test were already complete and
  green at that point, so nothing was lost, but that specific confirmation step did not
  finish. Claude independently re-verified by rebuilding and re-running the full `render`
  suite (32/32) and reading the diff directly against the spec (the design matches exactly:
  continuous re-ease, snap on settle, angle preserved). **What could not be verified: the
  live interactive morph itself.** `--shot`/`--clip --formed X` render through `captureFrame`,
  a separate one-shot path this fix does not touch (confirmed empirically - a `--formed`
  sweep via `--shot` shows the same drift before and after, because that path was never the
  one framing the live toggle); there is no synthetic-input tool available in this
  environment to script a live SHAPE click/`G`-press and capture the running window over
  time, and `--script`/a pre-set `geometry_view` setting do not reach the interactive
  per-frame loop's morph trigger either. The fix is believed correct on the strength of the
  code review and the unit test's teeth, not on a watched recording - **please confirm it
  looks right the next time you toggle SHAPE in play.**

## M17.25 Icon-faced cylinder pieces, so a flipped seat still reads

Reported directly: on a Mobius strip or Klein bottle, some pieces come out reversed, and
the sculptural 3-D pieces (the knight's wedge, especially - anything with a clear "facing")
are hard to read once that happens. Repurpose the existing 2D/3D button, while SHAPE is on,
to switch between the ordinary pieces and a new double-sided puck built from the piece's
flat icon, so a reversed seat still shows a legible, recognisable piece from either side.

**Root cause of the reversal, so this is built against the real mechanism, not a guess.**
`PlaySurface::build` (`src/render/play_surface.cpp:358-374`) gives every seat a frame from
a continuity-walked tangent field (`exField`) and `ey = cross(normal, ex)`, then
`seat.quat = quatOf(ex, ey, normal)` - always a proper, right-handed rotation; a piece mesh
is never literally mirrored. But a non-orientable surface has no globally consistent
tangent field, so the walk has to put its one unavoidable flip *somewhere* (M17.18 already
fixed where - at the glued seam, not at the Klein pinch). On either side of that line `ex`
points the opposite way, so a seat's whole frame is rotated roughly 180 degrees from its
neighbour just across it. A sculptural archetype with a strong directional read (`Wedge`'s
knight head, `Monolith`'s cross) suddenly faces the "wrong" way there - not broken, just
confusing, exactly as reported. A flat top-down icon, the kind already used for the 2-D
board, reads as what piece it is from a glance regardless of a 180-degree in-plane spin -
glyphs survive a half-turn far better than a sculpted head does - so building the shape
view's alternate piece out of the *existing* icon art, rather than a new sculptural look,
directly addresses the actual confusion rather than something adjacent to it.

**Build**

1. **Setting and the button.** New `Settings::shapePieceCylinders` (bool, default `false`,
   written to disk alongside the other shape settings). The 2D/3D button
   (`src/render/ui.cpp:1523`) is currently hard-disabled whenever `geometryView` is on ("a
   shape has no 2D rendering to fall back to") - that disabling was only ever about *board*
   rendering. Branch on `shell.settings().geometryView`: off, the button is exactly what it
   is today (flat vs. solid board, disabled in SHAPE); on, the button re-enables and instead
   shows `shapePieceCylinders ? "2D" : "3D"`, toggling that setting - the same label
   language the player already knows, now naming the piece style instead of the board.
   Nothing about the disabled-reason comment for the *board* case changes; this is a second,
   independent meaning gated on the opposite branch of the same `if`.
2. **The mesh.** A puck per archetype, extruded from that archetype's **Primitive** icon
   set specifically (`pieceIcon(IconStyle::Primitive, archetype)` in
   `src/render/piece_icon.{hpp,cpp}`), not Faceted. Primitive is already documented as
   "two or three convex shapes each, never more than eight points" with no cutouts - every
   `PieceIcon` it returns has an empty `cut` field, unlike Faceted (the tower's
   crenellations, the wedge's eye, the horn's ring are real polygon-with-holes cutouts
   there). A convex, hole-free outline triangulates correctly with a plain fan from its
   first vertex; reproducing Faceted's cutouts in 3-D would mean a real polygon-with-holes
   triangulator for a handful of extra notches, which is out of scope here - Primitive
   already reads fine at icon scale and is the deliberate choice, not a shortcut taken
   silently.
   - For each of the archetype's sub-polygons (drawn independently today, e.g. `Dome` is
     base + body + collar + head as four separate convex shapes - see `pieceIcon`'s
     `PieceIcon::polys`), fan-triangulate a top cap at local `z = +halfThick` and a bottom
     cap at `z = -halfThick`, both from the *same* 2-D points (the icon's own 100x100,
     y-down box, recentred so the icon's own centroid sits at local `x = y = 0`) - not a
     mirrored copy, the same outline stamped on both faces, so the piece reads as the same
     icon whichever face is towards the camera. Close the rim between the two caps with a
     thin quad strip around the outline, normal facing outward, so the puck has a genuine
     side wall rather than two coincident caps with nothing between them.
   - One extruded mesh per archetype (the same seven a variant can name, plus the `Slab`
     fallback `pieceIcon` already falls back to for an unanticipated one), appended into
     the *same* shared vertex/index buffers `MeshLibrary::build()` already fills, each with
     its own `MeshRange` - a new `MeshRange cylinderRanges[Archetype::Count]` alongside the
     existing `ranges[Archetype::Count]` on `MeshLibrary`, built in the same function, no
     second GPU buffer.
3. **Instancing and placement.** `BoardRenderer`'s surface-piece path
   (`emitSurfacePiece`, `src/render/board_renderer.cpp:822-851`, and its two call sites at
   the seat loop and the travelling piece) currently places a piece's *foot* on the
   surface: `body.center = centre + normal * kHalf`, the mesh rising from its own local
   `z = 0`. A puck that has to protrude on both sides instead centres *on* the surface:
   when `options_.shapePieceCylinders` is set, `body.center = centre` (no `kHalf` offset),
   against the new `cylinderRanges` mesh instead of `ranges` - everything else about the
   instance (`quat`, `fit`-based scale, colour, alpha, the check tint) is unchanged, so
   GHOST, the capture flourish and the contact shadow all keep working exactly as they do
   today; only which mesh is bound and where its local origin sits differ. The final draw
   loop (`record`, ~`board_renderer.cpp:1734`) picks `cylinderRanges` instead of `ranges`
   for the whole frame's batches when the setting is on - one option, not a per-instance
   branch, since every surface piece switches together.
   - Scoped to the surface/shape path only. The flat board and the ordinary 3-D (non-shape)
     board are untouched - this setting has no effect unless `geometryView` is also on, and
     the button that drives it is only reachable there in the first place.

**Tests**

- `MeshLibrary::build()` produces a non-empty, valid `cylinderRanges` entry for every
  `Archetype` that `pieceIcon` returns a non-empty set for (the existing "every archetype
  produces a usable mesh" test, extended rather than duplicated) - index bounds inside the
  shared buffer, a closed manifold-ish rim (no dangling edges: every rim edge's two
  triangles share it).
- A property test that the puck's silhouette, viewed from directly along `+normal` and
  directly along `-normal`, matches the archetype's own Primitive `IconPoly` outline up to
  the known recentring/scale - i.e. the same icon really is on both faces, not a mirrored
  pair (seeded directly by the reported bug: a mirrored pair would still "work" for this
  test's naive form, so assert the two caps' projected outlines are the *same* polygon, not
  merely congruent).
- `decoForScreen`-style settings plumbing test: `Settings::shapePieceCylinders` round-trips
  through save/load, defaults to `false`, and `coordinateLabelsShown`-style, a small pure
  predicate (if one is warranted once the button's branch is written) is unit-testable
  without a device.
- `--shot` of `klein --geometry` with `shapePieceCylinders` on, cropped in on a seat right
  at the flip locus and its neighbour just across it: both should show the same recognisable
  icon the right way up, where today's sculptural pieces visibly disagree. A matching
  capture with the setting off is pixel-identical to today's `klein --geometry` shot - the
  default must not move anything.
- `tools/test.sh --build render` and `--build app` green.

**Acceptance**

1. While SHAPE is on, the 2D/3D button is live (not greyed out) and toggles
   `shapePieceCylinders`, labelled consistently with the existing 2D/3D language.
2. With it on, every piece on the shape is a puck carrying its own Primitive icon on both
   faces, sized to its seat the same way the sculptural pieces are (`fit`), protruding
   visibly above and below the surface rather than standing only on top.
3. On `klein`/`mobius`, a piece just across the flip locus from another of the same type
   reads as the same recognisable piece on both sides of the line - the originally reported
   confusion is gone by inspection, not just by the silhouette-matching test above.
4. With the setting off (the default), nothing changes anywhere - flat board, ordinary 3-D
   board and the shape view's sculptural pieces are all pixel-identical to before this
   milestone.

**Risks and non-goals**

- Faceted icons' cutouts (tower crenellations, wedge eye, horn ring) are not reproduced in
  3-D by this milestone - the puck always uses Primitive geometry regardless of the
  player's `pieceIcons` setting, which continues to govern the flat board exactly as today.
  A faithful Faceted extrusion is future work if the plainer look is not enough.
- This does not change `seat.quat`, the tangent-field walk, or where the one unavoidable
  flip sits (M17.18's territory) - it only changes what gets drawn with that frame, so it
  does not need to and must not touch `play_surface.cpp`'s geometry beyond reading `quat`/
  `normal`/`fit` exactly as the sculptural path already does.
- Not offered outside the shape view: a flat or ordinary 3-D board has no non-orientable
  seam to be confusing about, so the button's repurposed meaning only exists while SHAPE is
  already on.

### Status: M17.25 built (2026-10-04, opencode + Claude)

Built as specified, with four deviations recorded below.

- **Setting and button.** `Settings::shapePieceCylinders` (bool, default `false`,
  `shape_piece_cylinders` in the settings file) and `BoardOptions::shapePieceCylinders`,
  plumbed through `optionsFrom`. `viewModeButton(settings, flatView, surfaceAvailable)`
  (new free function in `src/render/ui.{hpp,cpp}`, unit-tested with no device) states the
  branch; the HUD button now uses its `label`/`enabled` and, on press, toggles
  `shapePieceCylinders` when `pieceStyle` and the board otherwise.
- **Mesh.** `MeshLibrary::cylinderRanges[Archetype::Count]`, appended into the same
  vertex/index buffers in `MeshLibrary::build()`. `addIconCylinder` extrudes each
  `pieceIcon(IconStyle::Primitive, archetype)` sub-polygon into a thin prism: the *same*
  outline (recentred on the icon's own centroid, `kCylinderIconScale`) on both caps, a rim
  of outward quads between them. `kCylinderIconScale`/`kCylinderHalfThick` are public so
  the property test states the exact map.
- **Placement and draw.** `emitSurfacePiece` puts a puck's centre *on* the surface
  (`lift = 0` vs `kHalf`) when the option is set; everything else about the instance is
  unchanged. `record` picks `cylinderRanges` for the frame's piece and flourish batches
  when `shapePieceCylinders && surface && !flat && surfaceIndices` is non-empty - one
  option, and gated on the surface path so flat/ordinary boards cannot see it.
- **CLI.** Added `--piece-cylinders` (help text + `AGENTS.md`) so the state is reviewable
  with `--shot`/`--clip`; the capture path uses fixed default settings and had no other way
  to reach it.

Deviations from the spec:

1. **Ear clipping, not a fan.** The spec says "fan-triangulate from the first vertex"
   because Primitive is "convex shapes each" - but it is not: `kPCrownBody` and
   `kPCrownTeeth` are concave, and a fan from `p0` genuinely overflows their outlines
   (checked numerically: 1 and 6 of their fan triangles' centroids fall outside the
   polygon). A fan would
   fill the crown's valleys and leave an unsupported cap flange beyond the rim. I used an
   ear clipper (the same algorithm as `widgets::triangulate`, copied onto `IconPoint` so
   `piece_mesh.cpp` keeps no ImGui dependency). This is required to satisfy the spec's own
   "silhouette matches the IconPoly outline" test, which a fan would fail.
2. **`surfaceAvailable` in the branch.** The spec says branch on `geometryView`; I branch
   on `geometryView && a play surface exists && !flatView`. `geometryView` is sticky across
   a variant load, so raw `geometryView` on `standard` (which has no surface) would have
   given the button the new meaning with no shape on screen. With `surfaceAvailable` the
   else-branch is *exactly* the old control and its old `enabled = !geometryView`; the only
   behaviour change is a real shape view. (This is why the default path's board pixels are
   untouched.)
3. **The flourish switches too.** The spec names only the main batch loop; the capture
   flourish draws through the same `emitSurfacePiece`, so leaving it on `ranges` would draw
   a shrinking sculpt at the puck's centre. Both loops use `cylinderRanges`; the shadow
   loop stays on `ranges` (Disc has no puck).
4. **"Closed rim" test allows even edge counts.** A puck is a union of prisms and two
   sub-polygons that share a corner (e.g. the tower body and a merlon) each contribute a
   coincident rim wall there, so a geometric edge can be shared by 4 triangles, not 2. The
   test canonicalises edges by position and forbids odd counts (a crack), rather than
   demanding exactly 2.

Tests and results:

- `every archetype produces a usable mesh` extended: every piece archetype has a
  non-empty, in-bounds `cylinderRanges`, every non-piece an empty one.
- New "an icon-faced cylinder is a closed double-sided puck" (no cracks) and "the puck
  stamps the same icon on both faces" (caps point-for-point identical in buffer order and
  equal to the Primitive outline up to the recentring/scale - a mirrored pair fails it).
- `settings survive a restart` round-trips `shapePieceCylinders`; the defaults test pins
  it `false`. New UI test pins the four button states (off/shape/degenerate/flat-wins).
- `tools/test.sh --build render app`: 189 test cases, 4,962,659 assertions, green
  (RADV RENOIR, validation on). `tools/precommit.sh`: OK, including the ~278 s property
  sweep. `nix flake check` not run (release gate, out of the inner loop).

Visual captures (`build/m1725/`, converted with ImageMagick) - looked at closely, not just
"the shot ran":

- `klein --geometry` with it on: pucks with clean, upright Primitive icons (a pawn, a rook,
  a king) around the figure-eight; the button reads `2D` and is live.
- The crossing crop (`cmp_cross.png`, on left / off right): with the setting off the
  sculptural king at the flip locus lies almost flat on its side and the pawn sits skewed -
  the reported confusion; with it on the same seats are legible icons the right way up.
- `torus --geometry` with it on works the same way.
- Scoping: `standard`, `cube5`, `klein` (ordinary 3-D) and `standard --geometry` each
  render byte-identical with `--piece-cylinders` on and off.
- The default: a `klein --geometry` capture with the setting off is byte-identical to a
  pre-change build (stash/rebuild/capture) **except** an 18x10 px region at `(212,931)` -
  the `3D` glyph itself, now drawn live (Acceptance 1) where it used to be greyed. Every
  board, piece and panel pixel is unchanged.

One tension to name: the spec's Tests paragraph asks for the setting-off capture to be
"pixel-identical to today's", while Acceptance 1 asks the button to be live while SHAPE is
on - the button's own paint is the difference, and this build follows Acceptance 1. The
board and the pieces are pixel-identical; `shapePieceCylinders` off is still a no-op
everywhere outside that one label.

### Revision: a true cylinder - a token circle with the icon on it, not an icon-shaped slab (2026-10-05)

Reported directly, after looking at the built version: the flat 2D view does not draw a
bare icon silhouette - it draws a *circle* (one neutral token colour, the same for both
sides) with the figure in the piece's own colour *on* it (`Ui::drawPieceGlyph` plus the
`AddCircleFilled`/`AddCircle` pair around it at `src/render/ui.cpp:1761-1775` - "one token
colour for both sides... the disc is furniture... the thing being read is the figure").
M17.25 as built instead cut the puck's own silhouette to the icon's outline, which is a
correctly-shaped but differently-styled piece, not the flat view's piece in 3-D. Build the
actual flat recipe as a solid: a true circular puck (hence "cylinder" - a real one now, not
an icon-profile prism), the token colour on its disc, the icon in the piece's own colour
sitting on top of that disc, just as the flat view already composites it.

**Why this needs two meshes glued into one instance pair, not one mesh.** The existing
instancing pipeline tints a *whole* mesh one colour: `MeshVertex::color` is "multiplied
into the instance's colour", and `emitSurfacePiece` sets that one instance colour to the
piece's white or black (`pc`, `src/render/board_renderer.cpp:828-850`). A single mesh
cannot show a neutral token-coloured disc *and* a white/black icon at once through that one
multiply - the disc would always inherit whichever side's colour the icon has. The flat
view sidesteps this because it issues two separate draw calls (circle, then glyph) with two
separate colours; the shape view's answer is the equivalent in meshes: keep today's
icon-extrusion mesh (`addIconCylinder`, `cylinderRanges`) exactly as it is - it already
carries plain white (1,1,1,1) vertices, so the existing per-side instance tint already
colours it correctly - and add a **second**, new mesh per archetype: a true circular disc
(top cap, bottom cap, a round side wall - a real cylinder, `N`-segment fan, the same
segment count the flat view's own circles use, 24, for the same look at the same angle).

**The disc's colour has to be an instance tint, not baked into the mesh.**
`MeshLibrary::build()` (`board_renderer.cpp:113`) runs once, at renderer creation, before
any theme is necessarily current, and is never called again - `BoardRenderer::setTheme`
(`board_renderer.hpp:159`) changes `theme_` live (a palette switch in settings) with no
mesh rebuild to go with it. Baking `theme_.pieceToken` into the disc's vertices at mesh-
build time would freeze the disc to whichever theme happened to be live at startup and
never follow a later switch - exactly the bug the existing pieces avoid by *not* baking
their colour: every archetype mesh ships plain white (1,1,1,1), and the actual colour is
the *instance's*, chosen fresh every time `buildInstances` runs (which is after
`setTheme`, every frame). The disc mesh must follow the same rule: plain white vertices,
identical in kind to the icon mesh's, and the token colour supplied as the disc
*instance's* colour (`theme_.pieceToken`, read live, the same way `pc` already reads
`theme_.whitePiece`/`theme_.blackPiece` live) rather than anything baked at build time.

**Build**

1. `piece_mesh.{hpp,cpp}`: a new `addIconDisc` (or similar) building the plain circular
   puck, plain white (1,1,1,1) vertices exactly like `addIconCylinder`'s - cap radius
   matched to today's puck footprint (reuse whatever `fit`-based scale the existing puck
   already lands at; do not re-derive a new size unless the icon no longer looks right
   against it), half-thickness close to `kCylinderHalfThick` but a hair *less* than the
   icon mesh's, so the icon decal sits proud of the disc rather than fighting it in the
   depth buffer at the exact same plane. A new `MeshRange discRanges[Archetype::Count]`
   alongside `ranges`/`cylinderRanges`, same shared buffers, built the same way
   `cylinderRanges` is - no new parameter to `MeshLibrary::build()`.
2. The icon mesh (`cylinderRanges`, unchanged in its own geometry) becomes the decal riding
   on the disc: shrink its footprint to sit inside the disc the way the flat view's glyph
   sits inside its circle (`radius * 0.82` there - match that ratio, scaling
   `kCylinderIconScale` or wherever the puck's own size is set, by eye against the new
   disc). Its half-thickness should be slightly *more* than the disc's, by the same small
   margin the disc's is less than today's combined value, so the decal's caps sit just
   outside the disc's - a real, if tiny, proud step, not a coplanar fight.
3. `emitSurfacePiece`/the surface seat loop (`board_renderer.cpp`) emits **two** instances
   per surface piece when `shapePieceCylinders` is on, not one: the disc instance (against
   `discRanges`, coloured `theme_.pieceToken` - read live off the current theme exactly
   where `pc` already reads `theme_.whitePiece`/`theme_.blackPiece`, so a palette switch
   repaints the disc the same frame it repaints everything else - and with no check tint,
   matching the flat view, where only the status line announces check, never the token
   circle) and the icon instance (against `cylinderRanges`, exactly as today - piece
   colour, GHOST alpha, check tint all unchanged). Both share the same
   `centre`/`normal`/`quat`/`fit` the single instance uses
   today; GHOST's alpha should still apply to *both* (a ghosted piece fades as one object,
   disc and figure together) - carry the alpha onto the disc instance's colour too, just
   not the white/black tint or the check blend.
4. The draw loop and the capture flourish need the same two-instance treatment wherever
   they currently branch on `cylinderPieces` - review `record` and the flourish path
   (`board_renderer.cpp` around the two spots the current `cylinderPieces` bool already
   gates) for every place a single range lookup now needs to become two.

**Tests**

- Extend/replace the M17.25 silhouette tests: the icon decal's own two-cap-same-outline
  property still holds and still needs to pass. Add a test that the new disc mesh is a
  closed, round solid (same closed-rim check the icon mesh got, adapted) and that every one
  of its vertices is plain white (1,1,1,1) - the same invariant the icon mesh's own
  vertices already have to hold, and for the same reason: colour lives in the instance, not
  the mesh, so a theme switch after startup still repaints it. A render test that the disc
  *instance* (not the mesh) carries `theme_.pieceToken` and that switching `setTheme`
  changes what the next frame's disc instance colour reads as, with no mesh rebuild.
- A `--shot` of `klein --geometry --piece-cylinders`, cropped on one piece, inspected
  closely: a visible neutral-coloured circular puck with the piece's own figure sitting on
  it in white or black, legible as "the flat view's token, but a solid object" rather than
  as a flat-coloured icon-shaped slab. Compare side by side with a flat `--shot` of the same
  variant to confirm the two actually look like the same visual language.
- `tools/test.sh --build render app` green.

**Acceptance.** A piece on the shape, with the setting on, reads as a round token in the
theme's one shared background colour with the piece's own white/black figure on its face -
recognisably the same composition as the flat 2D view's pieces, carried into a solid shape,
not an icon-profile prism in one flat colour. Everything else M17.25 already established
(button wiring, scoping to the surface path, GHOST/check behaviour on the figure, the
original "no regression with the setting off" guarantees) is unchanged and must still hold.

### Status: M17.26 - the true token disc built (2026-10-05, opencode)

Built as specified. The shape view's piece is now two meshes: the unchanged icon
(`cylinderRanges`) shrunk into a decal, on a new plain white circular disc
(`discRanges`).

- **Mesh.** `addIconDisc` (`piece_mesh.cpp`) builds a real cylinder - two round caps, a
  round side wall, 24 segments like the flat view's circles - from the icon's own
  footprint. `MeshLibrary::discRanges[Archetype::Count]` is built in the same shared
  buffers as `ranges`/`cylinderRanges`, one range per archetype. Every vertex is plain
  white (1,1,1,1) and height 1, exactly like the icon mesh's. `addIconCylinder` is now
  the decal: `kCylinderDecalScale = kCylinderIconScale * 0.82` (the flat view's
  glyph-to-circle ratio), with a new `kCylinderDiscHalfThick = 0.10` against the icon's
  unchanged `kCylinderHalfThick = 0.11`, so the decal's caps sit just proud.
- **Instances.** `emitSurfacePiece` emits the disc first and the icon second per surface
  piece when `shapePieceCylinders` is on, sharing the one `centre/normal/quat/fit`
  transform. The disc carries `theme_.pieceToken` read live (no check tint) and the
  flourish/GHOST `alpha`; the icon is untouched. `InstanceSet::discCounts` /
  `flourishDiscCounts` record how many of each batch are discs, so both stay in one
  contiguous span and one buffer.
- **Draw.** `record`'s opaque and flourish loops draw the disc prefix from `discRanges`
  and the remainder from `cylinderRanges` when `cylinderPieces` is on; a batch with no
  discs (the flourish's blood-flash Cell) falls back to `ranges`, so that flash draws
  again instead of vanishing on a puck board.

Deviations:

1. **Per-archetype disc radius, from the icon's own extent.** `addIconDisc` derives its
   radius from the icon's furthest outline point from the centroid (`iconExtent`, the same
   recentring `addIconCylinder` uses) times `kCylinderIconScale`. That is "today's puck
   footprint" for each archetype; it keeps the 0.82 ratio exact for every figure rather
   than picking one shared radius the icons would not all fill.
2. **The disc shares the icon's instance transform including its height-scaled Z.** Giving
   the disc a different Z scale would make the decal's proud step grow with piece height;
   sharing it keeps the decal centred and equally proud on every archetype.
3. **One batch, a disc prefix, not a second `InstanceSet` vector.** The disc and icon are
   two ranges of one contiguous batch and one uploaded buffer, which is why
   `discCounts`/`flourishDiscCounts` exist; a separate vector would have meant a second
   upload path in `record` for no testable gain.
4. **The disc uses only the flourish fade as alpha.** There is no per-piece GHOST alpha in
   the code (GHOST is the board mesh's vertex alpha); the only piece alpha is the capture
   fade, and the disc carries it so a fading piece fades as one object.

Tests and results:

- `every archetype produces a usable mesh` extended to `discRanges` (non-empty and
  in-bounds for pieces, empty for non-pieces).
- New "a token disc is a closed, round, plain-white solid": every vertex is (1,1,1,1),
  every rim vertex is one radius (round, unlike the icon outline), caps at +/-, and the
  position-canonicalised edge check finds no odd edge (a crack).
- "the puck stamps the same icon on both faces" now states the decal scale
  (`kCylinderDecalScale`); it still compares the caps point-for-point in buffer order.
- New "the disc instance carries the token colour live from the theme": 32 discs (one per
  surface piece), each reading `theme_.pieceToken` with alpha 1, the icons after them
  keeping the piece colour, and a `setTheme` switch repainting the next build's discs with
  no mesh rebuild - the test that would fail if the token were baked into the mesh.
- `tools/test.sh --build render app`: 191 test cases, 4,973,101 assertions, green
  (RADV RENOIR, validation on). `tools/precommit.sh`: OK, including the ~283 s property
  sweep.

Visual captures (`build/m1726/`, cropped/upscaled with ImageMagick) - looked at closely:

- `klein --geometry --piece-cylinders`, cropped on three pieces: each is a shaded round
  disc (an ellipse under the camera, as a puck on a surface should be) in the neutral
  token colour, with the piece's own white or black figure centred on it - a white king
  and a black pawn read exactly as the flat view's circle-plus-figure, and the flip-locus
  black piece on the inner sheet stays legible.
- `klein --flat` (with its interface, which is what draws the tokens) side by side: the
  flat token is the same composition - neutral circle, own-coloured figure inside at
  roughly 0.82 - so the two views now speak one visual language.
- Non-regression: `klein --geometry` without `--piece-cylinders` is still the sculptural
  pieces (a black king lying on its side at the flip locus, as before), and
  `standard --geometry --piece-cylinders` is still the ordinary 3-D board with sculptural
  pieces (`standard` has no play surface, so the setting is ignored). The default path's
  draw is unchanged: `discCounts` is zero wherever the surface path is not.

### Correction: the 2D/3D button could get stuck disabled on a plain variant (2026-10-05, Claude)

Reported directly: "your edits have disabled the normal 2D mode... when I try using it
on cube5 or standard." Found and fixed directly, test-first.

**Root cause.** `viewModeButton`'s fallback branch (everywhere the shape-piece meaning
does not apply) kept the button's enabled state exactly as it was before M17.25:
`!settings.geometryView`, unconditionally. `geometryView` is a persisted setting with
nothing that resets it when switching to a variant with no surface at all - it stays
true from whatever glued variant was last browsed with SHAPE on. On `standard`/`cube5`
(`surfaceAvailable == false`), that stale `true` disabled the board's ordinary flat/solid
toggle for a reason that no longer applies there - there is no shape standing in for the
flat board to protect the toggle from contradicting, because there is no shape at all.

**The fix.** The fallback's enabled state is now `!(geometryView && surfaceAvailable)` -
greyed out only where a shape is actually standing in for the flat board, which is the
one state the original design meant to guard against. Unaffected: the repurposed-button
branch (unchanged), and the "flat wins over the shape" degenerate case, which still greys
out exactly as before (`surfaceAvailable` is true there, so the new condition agrees with
the old one).

**Tests.** The existing `the 2D/3D button is re-purposed on the shape view` test's
"`geometryView` set on a variant with no surface" case is corrected from
`CHECK_FALSE(b.enabled)` to `CHECK(b.enabled)`, with the reasoning above recorded in its
own comment - confirmed red against the unfixed code first, green after.

**Verification.** `tools/test.sh --build render app` green (193 cases, 4.97M assertions,
Vulkan validation on); `tools/precommit.sh` green. A fresh `--shot` of `standard` (default
settings, so `geometryView` starts false and cannot itself reproduce the stuck-true
scenario a capture can't simulate) shows the button live and unaffected in the common
case; the stuck-state scenario itself is pure settings logic, covered directly by the
corrected unit test.

## M17.27 Click the piece, not just the square it stands on

Requested directly: "instead of choosing the 'square' to move from, also allow the
player to choose the piece. And if the ghost mode is enabled, the choice can bypass
through the squares" - with a concrete case where this is not a nicety but a real gap:
"right now I cannot choose any piece in the 'shape' mode on 3d torus for instance."

**Root cause, read from both picking paths.** Every click in this engine resolves to a
*tile*, never a piece, and the piece selected is whatever happens to occupy that tile:

- The ordinary 3-D board (`Session::clickPixel`, `src/app/session.cpp:786`) tests
  `view::pickBox` against a thin box at the *square's own plane* -
  `view::Vec3{0.45f, 0.45f, 0.06f}` half-extent, `src/view/camera.cpp:352` - 0.12 units
  tall, centred on the board, regardless of how tall the piece standing there actually is.
- The shape view (`PlaySurface::pick`, `src/render/play_surface.cpp:1129`) is worse: it
  tests the surface *patch* triangles directly - zero thickness, the bare tile plane, no
  height tolerance at all (M17.12 part A's sub-grid patches replaced a single flat quad
  with several, but none of them ever had height).

A piece's own mesh stands *above* this plane, along the seat's normal
(`emitSurfacePiece`, `src/render/board_renderer.cpp:829` onward: foot at
`centre + normal * kHalf`, rising to roughly `centre + normal * (kHalf + 0.8 * fit *
hgt)` where `hgt` already folds in `heightFor`/`pieceHeightScale`). On a flat board,
under a roughly top-down camera, a piece's screen footprint mostly overlaps its own
tile's, so the gap is rarely felt. On a curved or stacked shape it is not: a piece stands
along the *surface's own normal*, which on `torus3d`'s small nested shells can point in
almost any direction relative to the camera, so the piece's visible silhouette can sit
well outside its tile's own screen-space footprint - a click lands on the piece, misses
the zero-height tile plane entirely, and nothing is selected. This is the reported "I
cannot choose any piece," and it is a direct, explicable consequence of "click tests the
tile, not the piece" - not a regression in the picking math itself.

**The fix.** Give both picking paths a piece-shaped hit candidate, not just a tile-shaped
one, derived from the same geometry `emitSurfacePiece` already draws from (read that
function for the exact placement/scale math - the bounding volume below has to track it,
not approximate it independently and drift out of sync as pieces are retuned):

1. **Shape view (`PlaySurface::pick`).** Needs to know which seats are occupied, which it
   currently has no way to - thread the position in (a `view::PositionView`/equivalent,
   however the call site in `src/gui/main.cpp` around line 1172 already has access to
   `session->session()`'s own position - check what is already in scope there before
   adding a new parameter type). For each occupied seat, test a capsule (or a simple
   cylinder - a capsule only matters if a grazing ray near the top/bottom cap is a real
   miss worth avoiding, judge by trying both) along the seat's normal, from the surface up
   to the piece's own top, radius matched to its footprint (`fit`-scaled, same as the
   render path). Merge these into the *same* sorted-by-distance, ghost-alpha-accumulated
   hit list `pick` already builds for tile patches (the spec for M17.22's ghost picking,
   further up this file, is the pattern: nearest hit wins outright when opaque, otherwise
   walk the sorted hits accumulating `1 - alpha` per ghosted tile until the running
   visibility crosses the threshold). A piece is always opaque (`options_.pieceHeightScale`
   does not ghost pieces, and nothing in this codebase does - "the pieces stay opaque" is
   stated everywhere GHOST is mentioned), so a piece hit should behave like an opaque tile
   hit in the same sort - it wins as soon as the ray reaches it, *unless* a ghosted tile in
   front of it has not yet accumulated enough visibility to be seen through. This is
   exactly the behaviour requested: "if ghost mode is enabled, the choice can bypass
   through the squares" - a translucent tile in front of a piece should not block the
   click from reaching it, the same way it already does not block the click from reaching
   the tile behind it.
2. **Ordinary 3-D board (`Session::clickPixel`/`view::pickBox`).** Lower priority - the
   report is specifically about shape mode - but the same gap exists in kind, just less
   often felt. Either extend `pickBox`'s per-placement extent to the piece's own height
   when a cell is occupied (simplest: a taller box only for occupied cells, still axis-
   aligned to the board plane rather than the piece's true orientation, which is fine here
   since the ordinary board's pieces stand straight up), or add a second pass the same
   shape as the shape-view fix. No ghosting exists on the ordinary board (`geometryGhost`
   is a shape-view-only concept), so this half has none of the alpha-accumulation
   complexity - purely "a taller hit box where a piece stands."

**Tests.** A property/unit test building a `PlaySurface` for `torus3d` (the reported
case), occupying a seat whose normal points away from a simple top-down projection (the
whole point of the bug), and confirming a pick ray aimed at the piece's rendered position
(not the tile centre) resolves to that cell - red before the fix (misses, returns
`kInvalidCell`), green after. A ghost-specific case: a piece on a far seat, a nearer
seat's tile ghosted translucent and directly in the ray's path, confirms the pick still
resolves to the far piece once the nearer tile's accumulated opacity crosses the
visibility threshold - and that it does *not* resolve there when the nearer tile is fully
opaque (the existing M17.22 behaviour, unchanged). A flat-board/ordinary-3-D regression
check that an ordinary click still resolves the same cells it always did (no behaviour
change for the common case, only a enlarged hit volume where a piece actually is).

**Acceptance.** On `torus3d --geometry`, clicking directly on a piece's visible body
selects it, from camera angles where the old tile-only pick would miss. With GHOST
enabled and a piece on a hidden/inner shell, clicking through the now-translucent outer
shell selects the piece behind it, the same way it already reaches the tile behind it.
Clicking empty space, or a tile with no piece, behaves exactly as before - this adds a
hit candidate, it does not remove or change the existing tile-click behaviour.

**Risks and non-goals.** Keep the bounding volume a simple capsule/cylinder, not a true
mesh hit-test against each archetype's actual triangles - correctness here is "did the
player's intended click land near the piece's visible silhouette," not pixel-perfect mesh
picking, and a tight analytic shape is cheap and good enough. Do not let this touch move
legality, `VariantId`, or anything beyond which `CellId` a click resolves to - same
contract `ClickCell` already has.

### Status: M17.27 part 1 built, part 2 deferred (2026-10-05, opencode)

**Built (part 1 - the report's own case).** `PlaySurface::pick` takes the drawn position
(`const view::PositionView* pieces`), the height scale and the cylinder flag, and the GUI
click handler (`src/gui/main.cpp`) passes `&session->snapshot()`, the live
`pieceHeightScale` and `shapePieceCylinders` - the same values the renderer drew with, so
the pick cannot test a different board than the one on screen. For every **occupied** seat
it builds a hit volume along the seat's normal, from the rendered foot to the piece's own
top (`1 + (heightFor(def) - 1) * pieceHeightScale` folded in exactly as
`emitSurfacePiece` does), radius `0.8 * fit * kPieceFootRadius` - the same `0.8 * fit`
body scale the render path uses and the same mesh foot radius the body is built from. That
radius is now the single constant `kPieceFootRadius` in `piece_mesh.hpp`, read by both
`addFoot` and the picker, so retuning a piece's mesh cannot silently leave the hit volume
behind. Icon-puck mode (`shapePieceCylinders`) gets a symmetric volume about the surface
at the puck's half-thickness instead, since a puck is centred on the surface and
protrudes both ways (M17.25). The test is a closed-form capsule test (`rayCapsule`), not a
per-triangle mesh hit - the spec's non-goal.

Both branches of `pick` take the candidate. The stacked path merges it into the **same**
distance-sorted list as the tile patches, flagged opaque, so the existing ghost walk is
unchanged and does the work: an opaque piece wins as soon as the ray reaches it, *unless*
a ghosted tile in front has already accumulated `kGhostPickVisibility` opacity - exactly
"ghost mode bypasses through the squares"; with the board opaque the nearest tile still
wins (M17.22 unchanged). The 2-D single-sheet path takes the nearer of the tile and piece
hits. Only `CellId` resolution moved - no legality, `VariantId` or `ClickCell` contract
was touched.

**Tests.** Two new `[render]` cases in `tests/render/test_play_surface.cpp`, both on
`torus3d` (the reported variant). `the shape view picks a piece standing off its own tile`
occupies each seat in turn and aims a ray at a point on the piece's own rendered axis (not
the tile centre); wherever the tile-only pick is a clean miss, the piece-aware pick must
resolve to that seat - only the clean-miss seats are asserted, so a body genuinely hidden
behind a nearer shell cannot make it flap. `a ghosted tile lets the shape view reach the
piece behind it` requires the opaque pick to stop at the near tile and the bare tile-only
ghosted pick to miss the body, while the piece-aware ghosted pick reaches it. Both were
run red first with the piece candidates disabled: the first fell to 1 of 7 assertions, the
second to `reached := 0`; both then green. `tools/test.sh --build render` green (137
cases, the M17.22 ghost and shape-pick tests unchanged), `--build app` green (69),
`tools/precommit.sh` green (arch/unit/property). The geometry/play-shape GUI captures
(`ctest -R 'gui-geometry|gui-play-shape|gui-ghost'`, including the `torus3d` shots) are
green.

**Part 2 (the ordinary `view::pickBox` path) deferred, deliberately.** The report and every
acceptance bullet are shape-mode; the spec marks part 2 lower priority. It is also not the
same simple change here: the ordinary pick lives in `app::Session::clickPixel` (L95),
which sits *below* `render` (L100) where `heightFor` lives, and **no** variant sets
`heightPermille` (checked all `variants/*.toml`), so the derived height is the only source.
Tracking the real piece height in `app` would invert a layer, and a fixed-height
approximation is not safe on the nD lattices: on `cube5` the three screen axes stack levels
along Z at `ViewConfig::depthSpacing = 2.5`, while a tall piece's box reaches ~1.42, so an
occupied level's box would overlap the level above/below and change which cell a click
resolves to - an unrequested nD behaviour change with no report behind it. The spec
explicitly invites this fallback ("if X proves too deep, ship Y and record why"). The
ordinary path's existing regression is still pinned: `test_session.cpp`'s `a click resolved
from a pixel reaches the right cell` (and the `[app]` suite) exercise `Session::clickPixel`
on `standard` unchanged.

**What could and could not be verified.** The picking geometry is verified directly and
numerically, which is the only way it can be: the two tests above build a real
`PlaySurface`, occupy real seats, and check the `CellId` a real camera ray resolves to at
pixels aimed at the piece's rendered body. A *live* mouse click cannot be driven headlessly
in this repo: `--script`'s `click e2` is parsed straight into a `ClickCell` action and never
passes through `clickPixel`/`PlaySurface::pick`, and `--shot`/`--clip` inject no SDL events,
so the actual `SDL_EVENT_MOUSE_BUTTON_DOWN` -> pixel -> `pick` call is covered by the
compiled call site plus the unit tests, not by a scripted click or a screenshot. A
screenshot could not have shown what a click resolved to in any case. The ordinary-board
part (2) is unbuilt, as recorded above.

### Fix: a straight glide's waypoint sat half a cell off the line (2026-10-16)

`PlaySurface::nearestBoundary` sampled the boundary between two seats from `(file, rank)`
while seat centres are sampled at `(file + 0.5, rank + 0.5)`, so for an orthogonal step the
waypoint landed on the start square's far edge corner-wise: a rook went to a corner, then
the middle of the edge. Diagonals were off by the same half cell. Only glides use it, so
knights (which arc) were unaffected; every glued shape was. The half-cell now belongs to
the base coordinate. Test: "a straight step's boundary waypoint lies between the two
squares" pins the convention against `pointAt`. The look-ahead morph test relied on the
detour for its clip and now scans files for one that still clips.
