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

### 2026-11-28 - M22.1 built (the pure copies module)

`src/view/adjacent.hpp/.cpp` (`adjacentCopies`, `copyPlacement`, `sourceCell`), tests in
`tests/unit/view/test_adjacent.cpp` (cylinder, torus rings 1-3, mobius, klein, the no-copy
variants, `copyPlacement`, and the `[property][view]` agreement with `Geometry::step` over
every shipped glued variant, orthogonal and diagonal steps). Nothing is drawn yet, so every
existing capture is unchanged (lab on or off); M22.2 must keep the switch-off instance list
bit-identical and cover the flat, lab and neural-VR paths.

Deviations / decisions:
- `viaSeams` is exactly the spec's "copy-local coordinate -> central coordinate": the shift
  by whole boards composed with the folded face transforms. On a translation gluing it is
  therefore the identity (the copy shows the same cell); it is the reflection on a twisted
  one.
- It is built by folding a representative mid-copy cell the way `Geometry::stepBoundary`
  does (lowest axis first = file-then-rank, as the spec asks); the same order the engine
  uses, so the diagonal copies of a Klein bottle agree with the engine (property test).
- `rings` is clamped to 3. A copy exists only if every face crossed is `Periodic` with a
  transform; otherwise it is dropped (no copy through a mirror or an open face).
- Only variants with exactly two drawn axes get copies; any 3-axis variant is empty.
- `copyPlacement` shifts by the board's extent only (not `gridGap`), as specified; the
  sub-board grid of a variant with extra axes is not accounted for yet.
- Not header-only deviation: none (`view` has .cpp files).

### 2026-11-28 - M22.2 built (drawing the copies: 3-D board, flat tokens, lab and neural VR)

What was built:
- `view/adjacent.*`: `adjacentAlpha` (1 for the centre, `0.85 * 0.58^(r-1)`), the
  constants `kAdjacentAlpha`, `kAdjacentFalloff`, `kAdjacentDim` (0.35, toward
  `Theme::ink`) and `kAdjacentMaxInstances` (6400), `adjacentDrawn(on, surface)` (the one
  predicate, read by `optionsFor` and the interface), `fitAdjacent` (drops whole outer rings
  to fit the budget at `2 * cells` instances a board, so an 8x8 torus keeps three rings and
  a larger board loses the outermost first) and `adjacentTokens` (the flat tokens).
- `BoardOptions::showAdjacent`, `adjacentAmount` (eased with `shapeEase`) and
  `adjacentRings`. `BoardRenderer::buildInstances`, ordinary path only, emits every copy's
  cells (source parity colour + last-move tint, no other cue, dimmed, alpha by order) and,
  on the solid board, its pieces (unmirrored, dimmed, same alpha; the piece in flight is not
  copied; no shadows) into the new `InstanceSet::adjacent` / `adjacentBatches`, farthest ring
  first in each batch, with CPU-side `adjacentCells` (source, order) for tests. `record`
  draws them with the blended pipeline after the opaque board and scaffold, before the
  shadows; they sit at the end of the instance buffer, so with the switch off the buffer
  layout is unchanged. The centre's glued rails on a copied axis move to the end of the
  copies' cell batch at alpha `1 - 0.55 * a` (`kAdjacentRailFade`); mirrors never fade. The
  plinth stays under the centre only.
- Flat view: `Ui::buildGameHud` draws the copies' tokens (dimmed disc, rim and figure,
  `focus * alpha`) before the centre's, from the same copies and cap.
- Lab and neural VR: the copies carry `fxCell` of their source cell, so boot, check
  ripple and game-end power-down light them with it; `eachBoardInstance` (deploy, stow,
  relayout carry) and `carryVr` (the VR glide) include `adjacent`, so the tiling rides the
  slab and the glide. Under `vrLight` the copies are hologram light like the centre.
- Debug access: transient `Settings::showAdjacent`, `adjacentRings` (1-3, default 2),
  `adjacentFormed` (0..1, default 1); console `adjacent [on|off|1|2|3]`; CLI `--adjacent`,
  `--adjacent-rings N`, `--adjacent-amount A`.

Tests: `tests/unit/view/test_adjacent.cpp` (+4: alpha by order, the predicate, the ring
budget, the flat tokens); `tests/render/test_offscreen_render.cpp` ("the tiled view draws a
board's neighbours, and nothing with the switch off": bit-identical instance lists off and at
amount 0, flat and solid; nine boards of cells at one ring; copy colour = dimmed source
colour, never the selection; alpha strictly falling with order and 1 only on the centre at
rings 1-3; cylinder/mobius/klein whole boards; nothing on `standard`);
`tests/unit/app/test_debug_console.cpp` (+1, the console command); GUI ctest `gui-adjacent`
(`tests/check_adjacent.cmake`: reproducible at a fixed amount, differs from off, amount 0
equals off, flat differs, shape ignores the switch, lab and VR draw it). `[render],[app],[view]`:
794 test cases pass; `ctest -L gui` passes (60 tests). Switch-off byte identity was checked against a
HEAD build on 11 captures (torus solid/flat/shape, klein manifold, a move, lab, lab flat,
lab deploy, lab skin, VR space, VR picture): all identical.

Sheets: `out/m22.2/sheet-a.png` (cylinder, torus, mobius, klein, solid and flat, both
themes), `sheet-b.png` (lab solid/flat/deploy, VR space/picture, rings 1 and 3, solid and
flat), `sheet-c.png` (lab deploy in flight, mobius reveal at amount 0.1/0.3/0.6/1).

Deviations / decisions:
- Spec written before M23-M25: the lab and neural-VR paths did not exist. Copies ride the
  slab via the existing nearest-slab fallback of `slabUnder` (they lie outside every bezel)
  and the VR glide via their source cell's offset; nothing else in the lab knows about them.
  They can overlap lab furniture beside the slab; not addressed.
- `kAdjacentDim` lives in `view/adjacent.hpp`, not at the top of `board_renderer.cpp`, so
  the flat tokens share it; the renderer's own new constants (`kAdjacentRailFade`,
  `kLastMoveTint`) are at its top.
- "Seam rails fade with `a`" read as: glued rails on a copied axis keep 45% at full reveal
  (`kAdjacentRailFade` 0.55), so the seam colours still teach the gluing.
- `BoardOptions::adjacentRings` added (the spec named only the switch and amount), and the
  settings fields exist transiently now because the flat tokens read them; M22.3 persists
  `showAdjacent`/`adjacentRings`, M22.4 eases `adjacentFormed`.
- The instance budget is per board `2 * cells` (an upper bound, independent of captures) so
  the ring count never changes mid-game; no shipped 2-D variant hits the cap.
- Copies get no contact shadows; on the manifold/lab theme their hologram pieces read
  fairly bright even dimmed - a tuning item for M22.6. Camera framing unchanged: the copies
  spill past the frame (the spec allows it).

### 2026-11-28 - M22.3 built (switch, setting and button)

What was built:
- `Settings::showAdjacent` / `adjacentRings` are saved as `adjacent_boards` / `adjacent_rings`
  (parsed, written, rings clamped 1-3 by `sanitize`); `adjacentFormed` stays transient. Both
  are in the `moved` comparison of `ui_menus.cpp`. Settings -> BOARD gets a checkbox and a
  rings slider (disabled while the switch is off).
- The game dock's **ADJ** button (`render::adjacentButton`, a free function like
  `viewModeButton`): offered only where `view::adjacentCopies` is non-empty, lit while the
  switch is on, live in 2D and 3D, greyed (and not lit) only while the shape stands in for
  the board. The rule is the one predicate `view::adjacentDrawn` that `optionsFor` already
  reads. It sits between 2D/3D and SHAPE. `--adjacent`, `--adjacent-rings`,
  `--adjacent-amount` and the console `adjacent` already existed (M22.2).
- Neural interface (M25.1): the button goes through the same `widgets::button` /
  `drawLiveControl` as the rest of the dock, so it is frameless `implant`-ink in the lab and
  neural VR. That path draws every cold label in `rift` whether lit or not, so a lit ADJ
  would not read; it alone drops `cold` while lit under `implantFrameless`, which makes it the
  bright `implant` ink. (SHAPE and the other cold dock buttons keep that quirk; not touched,
  to leave existing lab captures as they were.)

Tests: `tests/render/test_ui_labels.cpp` (ADJ offered / enabled / lit / greyed on the
shape, flat wins), `tests/unit/app/test_shell.cpp` (round-trip of the two keys, `adjacentFormed`
not saved, clamping); `tests/check_adjacent.cmake` compares amount 0 to off with `--cinema` (the lit ADJ
button otherwise differs) and checks the button lights. `[render],[app],[view]`: 796 test cases pass;
`ctest -L gui` passes (60 tests).

Deviations / decisions:
- The ADJ button is the cost of one more dock slot. At 3x scale on a 1280x720 window with the
  shape buttons showing, a full-width ADJ wrapped the dock to a third row and the ledger
  (`HUD panel bands...` test) lost its room, so a *wrapped* (compact) dock gives ADJ
  `px(34)` instead of `px(52)`. Unwrapped it is 52.
- Dock captures of glued variants (torus, klein, mobius, cylinder, ...) and the Settings
  screen now show the new button / rows even with the switch off, as the spec requires; the
  3-D board itself is unchanged with the switch off. Variants with no copies (standard,
  cube5, mirrorbox, ...) have a byte-identical dock.
- Settings -> BOARD was not captured (the screen has no scroll flag); the rows are plain
  ImGui checkbox/slider calls beside the existing ones.
- Sheets: `out/m22.3/sheet1.png` (torus off / on / flat / shape, console), `sheet2.png`
  (standard, klein lab, settings, torus neural VR), `sheet3.png` (ADJ lit vs off in lab
  and VR).

### 2026-11-28 - M22.5 built (picking and hover on the copies)

What was built:
- `Session::pick` (new; `pickPixel` is now its `.cell`) tests the board and every copy the
  front end is drawing: the nearest box wins and a hit on a copy returns the central cell it
  shows (`sourceCell`, via `copyPlacement`) plus the copy's `(stepU, stepV)`. `clickPixel` uses
  it, so a click on a copy selects and moves the source piece (reach across the seam). The
  copy list is the renderer's (`adjacentCopies` + `fitAdjacent` at `2 * cells`), so picture
  and pick cannot disagree on a large board's dropped rings. With the switch off, mid-reveal,
  with copies ignored, or on a variant with no copies, the code path is the old single-board
  test untouched.
- `Session::AdjacentView` / `setAdjacent`: the front end feeds it every frame from
  `view::adjacentDrawn(showAdjacent, surface)`, `adjacentRings`, `adjacentFormed` and
  `adjacentPick` (top of the frame loop in `main.cpp`), so lab, neural VR and lab-off all
  pick the same way. The hover loop passes the copy to `Session::setHover(cell, ctrl,
  copyU, copyV)`; hover hints (attack/peek) come from the source cell as before.
- Hover mark: the source cell on the centre is marked as always; the one copy cell under the
  pointer gets the centre's hover lift (`BoardOptions::hoverCopyU/V`, set in `optionsFor`).
  The hover script verb takes `hover <cell> [ctrl] [copy <u> <v>]` for captures and tests.
- `Settings::adjacentPick` (default true, `adjacent_pick`, saved) - the spec's "option, off
  by default: ignore copies" read as: ignoring is off by default, so copies are pickable
  unless the player turns this off. The Settings -> BOARD checkbox is shown only while the
  tiled view is on, so Settings with the switch off is byte-identical to before.

Tests: `tests/unit/app/test_adjacent_pick.cpp` (+7: cylinder copy pixels equal the source's
pixel, mobius reflected sources, off / ignored / mid-reveal give `kInvalidCell`, `standard`
unchanged, hover names the copy, click and move through copies, the hover verb);
`test_shell.cpp` (`adjacent_pick` round trip); `test_offscreen_render.cpp` (one copy cell lit,
centre unchanged); `check_adjacent.cmake` (hover on a copy differs from hover on the board
in solid, flat, lab and neural VR, and reproduces). `[render],[app],[view]` pass; see the
commit message for counts.

Deviations / decisions:
- Copies are pickable only once fully arrived (`adjacentFormed` 1): M22.4 will move them
  during the reveal, and a click on a moving ghost is not an action. Revisit with M22.4.
- Not header-only deviation: none. Lab and VR need nothing extra: `carryBoard` and the VR
  glide are render-only, picking reads the session's own placements and camera as before.
- Copy hover lift equals the centre's (`kHoverBrighten`); the copy is dimmed and translucent
  so it reads faint. A higher-order ring's hover is correspondingly fainter.
- Sheet: `out/m22.5/sheet.png` (torus solid hover on the board vs a copy, flat, mobius, lab,
  neural VR).
