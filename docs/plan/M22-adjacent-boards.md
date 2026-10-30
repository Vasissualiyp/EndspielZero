# M22 — Adjacent boards (the tiled view)

**Goal:** a switch, like SHAPE but available in both the 2D and the 3D board view, that draws the
board's neighbours through its glued edges: on a cylinder the board repeats left and right, on a
torus in all four directions, on a Möbius band and a Klein bottle with the flips the gluing
implies. Both drawn directions are covered - left/right (the files) **and** front/back (the ranks), so a
torus or a Klein bottle tiles in the plane, not just along a strip. Rings of boards go out from the
centre: order 1 is the +-1 neighbours, order 2 the
next, and so on (`Settings::adjacentRings`, 1 to 3, default 2). **The higher the order, the more
transparent**: the centre is opaque, order 1 nearly so, and each further ring fainter, so the
tiling reads as "the same board continuing" fading into the distance rather than a wall of
identical boards. Switching it on plays a short animation: the neighbouring boards arrive from "infinity" and merge into the line of boards around
the central one.

Presentation only: none of it enters `VariantId`, a game file or the engine. The copies are a
*view* of the same position, never a second game.

## Schedule (2026-10-30)

**Must be ready before the demo is published.** The Next Fest demo build is cut in early January
2027 (roadmap Wave 2, item 9), so this lands **by mid-December 2026**. The must-have for the
demo is M22.1 + M22.2 + M22.3 and the arrival animation without the per-ring stagger
(M22.4, simplified); M22.5 (picking), M22.8 (up/down) and ring orders beyond 2 may follow the
demo. If any must-have is at risk by 1 December, keep both the 3-D board and the flat-token path in the cut:
the switch is meant to work in both views, so shipping only one is not a fallback.

## M22.0 How hard is it?

**Medium: roughly a working week for the first version, most of it in four places that are not
the drawing itself.** The drawing is the easy part: the renderer already builds one
`Instance` list from a `Placement` list, so a copy is the same loop with an offset and a source
cell. The work is:

1. **Getting non-translation gluings right** (the hard 20%). A cylinder or torus copy is a
   translation, so the copy's cell at (nx + i, j) shows the piece on cell (i, j). A Möbius or
   Klein copy is reflected: crossing the file seam reverses the rank, so the copy shows the board
   rank-reversed. The engine already has the exact answer in `Geometry::faceTransform` (a signed
   permutation + offset, `geometry/transform.hpp`); the view must use *that*, not re-derive it, or
   the copies would disagree with how pieces actually move across the edge.
2. **The flat view's pieces are drawn by ImGui, not by the renderer** (`ui.cpp`, "A flat board's
   pieces are drawn here"). Every copy needs its tokens drawn there too, with the same transform.
3. **Picking and hover.** A click or hover on a copy should act on its source cell. The flat/3-D
   pick (`Session::pickPixel`) tests only the central placements.
4. **Camera framing and the near/far planes**: the framed bounds grow to nine boards (or the
   copies spill past the frame, which is also fine and arguably nicer).

The animation is cheap once copies exist: it is a pure function of one eased number that moves and
fades each copy.

Not hard, but easy to get wrong: draw order and blending (copies are drawn dimmer/translucent
over the same plinth and background), and keeping `--shot` deterministic (the reveal amount must
be a pure function of one number, like `geometryFormed`).

**Out of scope for the first version** (say so in the status section): boards of three or more
axes (the 3-D torus, `t6`: copies per sub-board are a different problem), and the shape view (a
shape is already cyclic; the switch is greyed there). Mirror walls get no copies (a mirror leads
nowhere).

## M22.1 The copies, as a pure function

New module `src/view/adjacent.hpp/.cpp` (layer `view`, below `app` and `render`, depends only on
`geometry`/`variant`; no floats beyond layout offsets, no Vulkan):

```cpp
struct AdjacentCopy {
  std::int8_t stepU{0}, stepV{0};   // which neighbour: -R..+R along each drawn axis
  std::uint8_t order{0};            // ring distance max(|stepU|, |stepV|); 0 is the centre
  Transform viaSeams;               // maps a copy-local coordinate to the central board's
                                    // coordinate (identity for the central board itself)
};
/// The copies to draw for a 2-D glued variant: the central board plus every board within
/// `rings` steps through periodic (Glued) faces, ordered by decreasing order (farthest first,
/// so translucent boards blend correctly) and then in a fixed order. Empty for a variant with
/// no periodic drawn face (and for any variant with more than two spatial axes in this
/// milestone). The count is (2R+1)^2 on a torus, 2R+1 on a cylinder.
[[nodiscard]] std::vector<AdjacentCopy> adjacentCopies(const VariantSpec& v,
                                                       const ViewConfig& cfg, int rings);
/// Where a copy's cell is drawn: the central placement of `source` shifted by the board's
/// own extent along each stepped axis, in the copy's reflected frame when `viaSeams` flips it.
[[nodiscard]] view::Placement copyPlacement(const AdjacentCopy&, const view::Placement& central,
                                            const VariantSpec&, const ViewConfig&);
/// The central-board cell that copy-local cell `c` shows.
[[nodiscard]] CellId sourceCell(const AdjacentCopy&, CellId copyLocalCell, const VariantSpec&);
```

Rules:

- A step of +1 along drawn axis `a` exists iff `geom.boundaryKind(axis, Side::Max)` is
  `Periodic`; -1 iff `Side::Min` is; a step of +k is k such crossings. The copy at (+1, +1)
  exists iff both do. `viaSeams` is the
  composition of the face transforms crossed, in the order crossed (so a Klein corner copy is
  right whichever way round it is reached - ADR-0004 says the group is closed under composition;
  where the two orders disagree on a gluing whose transforms do not commute, use the order
  file-then-rank and record it in the status section).
- The copy's cell coordinates are the central ones transformed by `viaSeams` (`Transform::
  applyToCoord`), then `toCell`. This is exactly the question `Geometry::step` answers when a
  piece crosses the edge, which is the property test below.

Tests (`tests/unit/view/test_adjacent.cpp`, `[unit][view]`):

- cylinder (files periodic), `rings` 1: copies {(0,0), (-1,0), (+1,0)} (`rings` 2: five, with
  orders 2,1,0,1,2); the copy at +1 shows cell (i, j) at
  its (nx + i, j); `sourceCell` is the identity on coordinates; ranks are not stepped.
- torus: nine copies at `rings` 1, twenty-five at 2; mobius: the +-1 file copies are rank-reversed (`viaSeams` reverses the
  rank), and the copy at +2 files is the central orientation again; klein likewise on its axis.
- `standard`, a mirror box and any variant with 3+ spatial axes: empty.
- **Property** (`[property][view]`): for every shipped glued 2-D variant and every cell on a
  periodic face, stepping one cell outward with `Geometry::step` lands on
  `sourceCell(copy, outward-neighbour-cell-in-copy-frame)` - so the picture and the engine agree
  about what is across the edge. This is the test that matters.

## M22.2 Drawing the copies

`render::BoardOptions` gains:

```cpp
bool showAdjacent{false};          // the switch (Settings::showAdjacent)
float adjacentAmount{1.0f};        // 0..1, the live reveal (see M22.4); a pure number
```

`BoardRenderer::buildInstances`, the ordinary (non-surface) path only - the 3-D board and, via
the same instances, the flat board's cells:

- The copies are drawn farthest first (`order` descending), which is the back-to-front order
  the blended pass needs, since order >= 1 is translucent.
- For each `AdjacentCopy` other than the centre, with amount `a = shapeEase(adjacentAmount)`
  (`render/play_surface.hpp`), emit the cell instances and piece instances of the central loop
  with `placement = copyPlacement(...)`, fill from the **source** cell (its parity-colour, its
  last-move tint), pieces from `snapshot.at(sourceCell)`. Pieces keep their own orientation; the
  mesh is not mirrored (a mirrored knight would read as a different piece).
- Copies are drawn dimmer and **more transparent the higher their order**: colour mixed toward
  the background by `kAdjacentDim` (start 0.35), and alpha
  `a * adjacentAlpha(order)` with `adjacentAlpha(r) = kAdjacentAlpha * kAdjacentFalloff^(r-1)`,
  `kAdjacentAlpha = 0.85`, `kAdjacentFalloff = 0.58` (order 1: 0.85, 2: 0.49, 3: 0.29). A pure
  function in `view/adjacent.hpp`, so the tests and the flat tokens share it. Anything with
  alpha < 1 goes to the blended pass (the one the capture flourish and the plinth fade already
  use, `surfaceBlendPipeline_`); the centre stays in the opaque pass untouched. Pieces on a
  copy fade with their board, so a far ring's pieces are ghosts too. They carry **no** selection, legal-move, hint, hover or check marks - only the last
  move tint, so the eye reads the centre as the real board. Constants named at the top of
  `board_renderer.cpp` with the other tints.
- The plinth (the slab, `plinthInstance`) is drawn once under the centre; copies stand on the
  page, not on a slab, so they read as "more of the same world", not as extra boards.
- Seam rails stay on the centre only, and fade with `a` on the edges that now lead somewhere.
- Instance budget: nine boards of 64 cells + pieces is under 1,000 instances; rings 2 and 3 on a
  torus are 25 and 49 boards (about 5,000 instances at 64 cells, still one draw per shape). Cap
  the total by `kAdjacentMaxInstances`: when a board is large, drop the outermost rings first
  (never a partial ring), and record the cut in the status section.

The flat view: `ui.cpp`'s token loop (`session.flatView() && !surfaceView`) iterates the same
copies and draws each token at `camera.project(copyPlacement(...))`, dimmed by the same
constants and with alpha `a`. One shared helper, `view::adjacentTokens(snapshot, copies)`, so the
two views cannot disagree.

Tests (`tests/render/test_offscreen_render.cpp`, `buildInstances` only, no device): instance
count is nine times the cell count on a torus with the switch on and `adjacentAmount` 1, and
exactly the central count with the switch off or amount 0 (a **bit-identical** instance list to
today, so every existing capture is unchanged); a copy's cell colour equals its source cell's;
no copy instance carries the selection/legal-move colours; the alpha of copy instances strictly
decreases with order and is 1 only for the centre (rings 1, 2 and 3); `--shot` of `torus --adjacent` at a
fixed amount is reproducible.

## M22.3 Switch, setting and button

- `Settings::showAdjacent` (default off; `adjacent_boards` in the file) and
  `Settings::adjacentRings` (1-3, default 2; `adjacent_rings`; a slider in Settings -> BOARD),
  parsed, written and in
  the `moved` comparison of `ui_menus.cpp` (the last time this was forgotten the setting did not
  persist).
- A game-screen button **ADJ** beside 2D/3D and SHAPE, shown only where `adjacentCopies` is
  non-empty, lit while on, live in both 2D and 3D; greyed while SHAPE is on (the shape is
  already cyclic). A checkbox in Settings -> BOARD. `--adjacent` CLI flag for shots.
- The flat/shape conflict that bit `flat_view`+`geometry_view` must not recur: define the rule once
  in `optionsFor`: `showAdjacent && !surface`, and have the UI read the same predicate.

## M22.4 The arrival animation

A live number `Settings::adjacentFormed` (transient, not saved), eased toward 0 or 1 by the
front end exactly as `geometryFormed` is (`render::advanceFormMorph`, same rate setting
`geometryFormSpeed`), so a pose is a pure function of its number and `--shot --adjacent-amount
0.4` reproduces. It is `BoardOptions::adjacentAmount`.

For a copy at step `(su, sv)`, ring distance `r = order = max(|su|, |sv|)`, and board extents
`(W, H)` in world units:

- **Position.** The copy sits at its home offset plus an outward push along its step direction:
  `home + dir * far * (1 - e)^2 * (W or H)` with `e = shapeEase(clamp((amount - delay) /
  (1 - delay)))`, `delay = 0.15 * (r - 1)`. At `amount = 0` that is `far` boards out, `far = 12`:
  "infinity" in perspective, off the frame in the flat view; at 1 it is exactly home.
  Corner copies come in along the diagonal and arrive together with the edge copies.
- **Fade.** alpha `e * adjacentAlpha(r)`, so a copy materialises while far away and is fully
  drawn before it touches its neighbour; no hard pop at either end. The staggered `delay` means
  the inner ring arrives first and each outer ring follows into the gap it leaves, so the
  tiling builds outward and the high orders arrive as the faintest, last ghosts.
- **Merge.** Over the last 10% of `e` the copy's dimming eases to `kAdjacentDim`, so it settles
  into the dim steady state instead of arriving bright and then dimming.
- **Camera.** Does not move during the reveal; the arrival is the motion. Optionally (flagged
  `kAdjacentFrame`, default off) the framed bounds ease to include the copies.
- Reversed on switch-off: the copies leave to infinity.

Tests (`[unit][view]`, pure functions in `view/adjacent.hpp`): `adjacentOffset(amount)` is exactly 0
at 1, `far` boards at 0, monotonic; the reveal alpha is 0 at 0 and `adjacentAlpha(r)` at 1;
`adjacentAlpha` is strictly decreasing in the order (1 > 0.85 > 0.49 > 0.29) and equals 1 only
for the centre; and an outer ring never starts before the ring inside it.

## M22.5 Picking and hover

`Session::pickPixel` (and `clickPixel`, which uses it) tests the copies too when the switch is
on: the ray's nearest hit over centre and copy placements wins, and a hit on a copy returns
`sourceCell`. Hovering a copy marks the source cell on the central board (and, faintly, the copy
under the pointer) - the cell you would act on. A click on a copy acts on the source cell,
including selecting and moving, so the player can reach across the seam. (Option, off by
default: ignore copies for picking, `Settings::adjacentPick`.)

Tests (`[unit][app]`): on a cylinder with the switch on, a pixel at a copy cell returns the same
`CellId` as the pixel at its source; with it off, the copy's pixel returns `kInvalidCell`.

## M22.6 Verification (the real artifact, not just the tests)

1. `tools/test.sh --build view app render`, then `ctest --preset dev`: the property test in M22.1
   is the one that must be green on every shipped glued variant.
2. Screenshots: `cylinder`, `torus`, `mobius`, `klein` with `--adjacent`, in the 3-D board and with
   `--flat`; look at the Möbius and Klein copies for the rank reversal at the seam (the first
   pieces across the edge must be the ones that actually are there).
3. Rings: `torus --adjacent` at `adjacent_rings` 1, 2 and 3 - check that order 3 is readable as
   a faint ghost, not noise, and tune `kAdjacentFalloff`. The reveal: `--adjacent-amount 0.1 0.3 0.6 1` filmstrip; and a `--clip` of the toggle, diffing
   consecutive frames for a pop (as the trailer work did).
4. The near clip (`Settings::nearClip`) and far plane with copies at `far = 12` boards out.

## M22.8 Up/down (a third axis) - a sketch, not in the first version

A 3-D board (the 8x8x4 3-D torus) has a third drawn axis: its levels stand one above another,
`ViewConfig::depthSpacing` apart. "Up/down" adjacency is the same idea one dimension higher, and
the plan above is written so it extends without a redesign:

- **Same mechanism.** `AdjacentCopy` gains `stepW`; the order is `max(|u|, |v|, |w|)`; `viaSeams`
  is the composition of the face transforms of all three axes. A copy above is the *whole stack*
  shifted up by the stack's height (`levels * depthSpacing`), showing the position through the
  third axis's seam (a plain translation on a torus3d; flipped on a twisted one).
- **The cost is the count, and the view.** Rings go from (2R+1)^2 to (2R+1)^3: 27 stacks at R=1,
  125 at R=2, each a stack of levels, so the instance budget (`kAdjacentMaxInstances`) bites
  immediately; the third axis defaults to **one ring only** (`Settings::adjacentRingsDepth`, 0-1,
  default 0 = off), even when the plane uses two. Copies above and below also stand in front of
  and behind the camera's own line of sight, which is where the near clip and the transparency
  falloff earn their keep; they should be the faintest of all (an extra falloff factor per
  third-axis step).
- **What it needs that the plane does not:** the picking ray crossing levels of copies (the
  nearest-hit rule already handles it), the animation's "from infinity" direction along the
  vertical (a copy above arrives from above), and framing.
- **Boards with more than three axes** (`t6`, the 5-D chess): the extra axes are laid out as a
  grid of sub-boards, not drawn as space, so "adjacent" there means neighbouring *sub-boards*
  through the grid seams - a different picture again (the copies of a whole grid). Out of scope;
  decide after the 3-D case has been looked at.

Decision needed before M22.8 starts: whether the vertical copies repeat the whole stack (above)
or only the nearest level through the seam (one level above the top, one below the bottom,
which is much cheaper and often reads better). Build the whole-stack version first and look at
it.

## M22.7 Order of work

1. M22.1 (the pure module and its property test) - the correctness core, no pixels.
2. M22.2 drawing in the 3-D board, then the flat tokens.
3. M22.3 switch and button.
4. M22.5 picking.
5. M22.4 the animation last, once the steady state is right.

## Status

Planned (2026-10-30); nothing built. Drawn axes: both in-plane directions are in the first
version; the third axis (up/down) is sketched in M22.8, with an open decision on it.
