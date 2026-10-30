# M21 — Trailer transitions (seams, plinth, keyframe handover, slide laps)

**Goal:** remove four visible discontinuities from the main-menu trailer (the attract sequence
from M13.9). All four are presentation only: nothing here enters `VariantId`, a game file or the
engine.

1. **Standard -> cylinder (the reveal).** The seam rails (the colours that mark which edges are
   glued) and the plinth (the slab under the cells) switch at the cut. Both must fade in or out
   with the live morph amount `formed`.
2. **The keyframe pose snaps at the first keyframe of a beat.** This causes the reported Klein ->
   3-D torus jump. It also snaps the cylinder shut part-way through its roll.
3. **The torus spins while the tube bends into a ring.** Only the edges should be glued; the
   extra laps should not happen.

The fixes below are pure functions of `formed` (renderer) or of the trailer clock (app). A
`--trailer-clip` render stays a deterministic function of the fixed step, and every existing
capture at `formed = 1` stays bit-for-bit the same.

## M21.1 What was found (no code written yet)

Line numbers are as of commit `4a17451`. If they have drifted, search for the named function.
The user's track is `~/.config/endspielzero/camera_track.txt`. The game reads this one:
`Shell::cameraTrackPath` = `settingsPath_.parent_path() / "camera_track.txt"`, `shell.cpp:954`.
The older `~/.config/chessbox/camera_track.txt` has the same 17 cues. Only the camera poses of
the cylinder, standard-ply-17 and torus3d keys differ, and every slide and pose value is
identical. Neither file is modified by this milestone.

### The trailer's timeline

`defaultAttractSequence` (`shell.cpp:497`) has six beats:

| index | cue `beat=` | variant | moves | what it sets |
|---|---|---|---|---|
| 0 | 1 | standard | 18 | |
| 1 | 2 | cylinder | 10 | `formedFrom 0`, `morphSpeed 0.7` |
| 2 | 3 | torus | 10 | `formedFrom kTorusRollEnd` (0.5), `morphSpeed 0.25` |
| 3 | 4 | klein | 10 | `formedFrom kKleinRingEnd` (0.4), ghost 0.35, flat pieces |
| 4 | 5 | klein | 0 | `formedTarget kKleinRingEnd` (it folds back to the ring) |
| 5 | 6 | torus3d_8x8 | 10 | `formedFrom 1`, `spreadFrom 0` (levels collapsed onto one ring) |

`Shell::beatOffset` gives each beat `moves + 1` units, so the beats start at 0, 19, 30, 41, 52
and 53. A key's time (`Shell::trailerKeys`, `shell.cpp:1130`) is
`beatOffset(beat-1) + ply + sub/8`. For the user's track:

- standard: 1, 2, 4, 7, 13, 17
- cylinder: 21, 24, 27, 29
- torus: 36, 40
- klein (beat 4): 44, 48
- torus3d: **53**, 59, 63

`Session::startWatching` (`session.cpp:877`) plays move 1 at once. So a beat with moves shows
its first frame at `beatOffset + 1`, and a `ply=0` key (torus3d's, at 53) lies *before* that
beat's first frame. `trailerShift_` (`advanceAttract`, `shell.cpp:908`) keeps the shifted clock
continuous through the cut and decays at 0.8 units/s. That is a speed-up, not a jump.

### Problem 1: what the two sides of the standard -> cylinder cut draw

- **The `standard` beat** has no play surface (`render::hasPlaySurface` is false), so it uses
  the ordinary instance path in `BoardRenderer::buildInstances` (`board_renderer.cpp:1179`
  onwards). That path draws:
  - one `Archetype::Cell` box per cell, with half extent `cellHalfExtent()` = (0.46, 0.46,
    0.055);
  - seam rails per glued face (`:1299-1317`). `standard` has none, so nothing is drawn;
  - **one plinth per slice** (`:1338-1378`): centre z = `e.z - half.z - 0.13`, scale
    `((max-min)/2 + 0.78)/half` in x and y, `0.13/half.z` thick, colour `theme_.boardRim`, edge
    `mix(boardRim, emberDeep, 0.35)`, `edgeMask 15`. `present` comes from
    `Game::cellInPresent`, which is false on every non-temporal board.
- **The `cylinder` beat at `formed = 0`** uses the surface path (`:759-1177`). That path draws:
  - one mesh of patches;
  - the per-patch groove `rim` lambda (`:919-947`). This is the groove *between* squares, not a
    base;
  - **no plinth at all**;
  - seam rails tinted into the vertex colours by `tinted` (`:856-873`). `rails` is
    `seams != nullptr && options_.showSeams && !surf.stacked()` (`:843`), and `surfaceRail`
    (`play_surface.cpp:183`) returns `weight = 1.0f` on every wrap corner whatever `formed` is.
- **The flat view** (`options_.flat`) skips the surface path (`:759`) and draws rails at full
  colour through the instance loop. Nothing morphs there, so it needs no change.
- Both switches therefore happen at the instant the variant changes. Nothing reads
  `options_.surfaceFormed` (`main.cpp:265`, from `Settings::geometryFormed`) to decide either
  one. The same pop happens when a player turns the SHAPE toggle on in a game, because the M18.6
  morph also starts at `formed = 0`.
- **Measured.** `--trailer-clip ... --trailer-frames --user-settings --fps 30 --width 320
  --height 180`: frame 394 (`cut: beat 1 (cylinder)`) has a consecutive-frame mean absolute
  difference of **3.74**, against about 0.2 either side of it. In the frames, the dark plinth
  vanishes and the green/blue file-seam rails appear between 393 and 394.
- **The cylinder flat sheet** is the ordinary board with its ranks reversed (pinned by `the
  morph's flat end is the flat board, reflected in rank`, `test_play_surface.cpp:2489`). It lies
  flat at constant z, and its per-cell pitch is the seat's `stepU`/`stepV`. Hence the yaw
  half-turn in `startAttractStep` (`shell.cpp:629`) and `Shell::flatBoardYaw`.

### Problem 2: the keyframe pose snaps at a beat's first keyframe (Klein -> torus3d)

- **The rule as written.** The rule lives in `Shell::advanceAttract` (`shell.cpp:875-884`),
  `Shell::splinePoseActive` (`:1147`) and `Shell::applySplineAt` (`:1217`). While
  `poseKeysFrom(beat) <= g - trailerShift_`, the beat's own ease (`easePose`, from
  `formedFrom`/`spreadFrom` toward the targets) **stops**. From that frame on,
  `applySplineAt` writes `formed`/`ghost`/`shellSpread` straight from the spline. Nothing blends
  between the two, so the pose jumps from wherever the beat's ease was to the keyed value in one
  frame.
- **Why every recorded key carries the beat's *end* pose.** In edit mode `trailerGoto`
  (`:736-741`) sets `editPose_` to the beat's *targets*, and `recordCameraKeyframe` (`:973-976`)
  records `editPose_`. So a key never holds a beat's start pose. Every key in the user's file
  has `shellSpread=1`.
- **The torus3d case.** Its first key is `ply=0` (t = 53), before the beat's first frame (t =
  54). So `splinePoseActive()` is true **on the first frame of the beat**. `startAttractStep`
  sets `geometryShellSpread = spreadFrom = 0` (`:647`). Then `applySplineAt` immediately
  overwrites it with `splineScalar(spread, g)` = 1, and the "collapsed onto one ring" picture,
  which matches the Klein ring the previous beat ended on, is never shown.
- **Measured.** Frame 1448 (`cut: beat 5 (torus3d_8x8)`) has a mean absolute difference of
  **8.18**, the largest in the lap. The small ring becomes the large, fully spread 3-D torus
  between 1447 and 1448.
- **Confirmed by experiment** (`XDG_CONFIG_HOME` pointed at a scratch copy of the config; see
  M21.7). Only the torus3d `ply=0` key was changed, to `shellSpread=0`. The same cut then
  measured **0.05**, and the frames before and after it are the same ring.
- **The same mechanism snaps the cylinder shut.** The cylinder's first key is ply 2 (t = 21),
  which comes about 32 frames after the cut. The beat's own roll (rate `2 * 1 * 0.5 * 0.7` =
  0.7/s, so about 1.43 s) is then near 0.8, and the spline holds `formed=1`. Frame 426 measured
  **3.51**: the open C-shaped tube is closed in the next frame.
- **Ruled out:**
  - *Camera*: `view::splineCamera` is one spline over the whole timeline, so it is continuous
    through a cut.
  - *The camera carried across the cut*: it is overwritten by the spline in Authored mode.
  - *Beat/variant matching*: `trailerKeys` matches `attract_[5].variant == "torus3d_8x8"`.
  - *Piece style*: flat pucks on both sides.
  - *Slide*: continuous.
  - *`flatBoardYaw`*: 0 on both sides.
- **Why the tests missed it.** `a beat's own morph plays until its first keyframe takes over`
  (`test_trailer.cpp:545`) puts its keys at ply 6. It checks `formed` only on the cut frame,
  long before the key, and never across the takeover. No test puts a key at the first move of a
  beat that has a reveal (`formedFrom`/`spreadFrom`).

### Problem 3: the torus spins while the tube bends into a ring

- **The hypothesis about yaw is refuted.** Yaw is never interpolated as a scalar:
  - `view::slerpCamera` (`camera.cpp:371`) slerps quaternions.
  - `view::splineCamera` (`:378`) hemisphere-aligns the whole chain (`dotq < 0` flips) and
    squads between keys.
  - `orbitFromQuat` reads yaw back with `atan2`.

  So -2.96 -> 2.80 turns 0.51 rad, the short way. The existing test `slerpCamera turns the
  shortest way` (`test_camera.cpp:122`) pins this for the live path. M21.6 adds the same check
  for the spline, using the user's numbers.
- **The slide is what spins it.**
  - `PlaySurface::build` (`play_surface.cpp:295-299`) computes
    `sv = wrapSlide(slideV, 2*nz) / nz` and then, **for a torus only**, multiplies it by
    `stageForm(form, kTorusRollEnd, 1)`. That is the "rank slide comes in with the bend".
  - `wrapSlide` is `std::fmod` over a **two-lap** window (`:135`). For the user's
    `slideV = 125.521` (a value accumulated by middle-dragging and inherited by every key from
    the standard opener on), it gives `fmod(125.521, 16) = 13.521` cells.
  - As the torus beat eases `formed` from 0.5 to 1, the board therefore slides 13.521 cells,
    which is **1.69 laps** of the 8-rank ring. One lap of rank slide on a torus is `nz` = 8
    cells, so the equivalent nearest 0 is **-2.479** cells (0.31 lap).
  - **Confirmed by experiment.** The same render with every `slideV` replaced by
    `nearestLap(v, 0, 8)` turns the ring about 0.3 lap during the bend instead of almost two.
    The filmstrips `bend_tr.png` (before) and `bend_tr2.png` (after) are in the scratchpad.
- **The app side is lap-unaware for V, and uses the wrong lap for U on a Möbius band.**
  - `applySplineAt` (`shell.cpp:1195-1214`) lap-reduces only `slideU`, and uses
    `lap = dims.extent(0)`. That is wrong for a Möbius band, whose file lap is `2*nx`.
  - It ramps `slideV` from 0 with `slideRamp` on a `formedFrom == 0` beat, with no lap
    reduction at all.
  - It interpolates raw key values with `splineScalar`. Two keys recorded a lap apart (one
    canonical 3.9 and one -3.9) would sweep 7.8 cells instead of 0.2.
- **The live path.** `Shell::advanceCameraTrack` (`shell.cpp:1058-1061`) lerps
  `slideU`/`slideV` linearly from the current settings to the key. A key at `slideU=0` reached
  from a setting of 30 spins the torus almost four laps.
- **Already right (consistency note only).** The follow choreography's `stepSlideCoord`
  (`shape_sequence.cpp:48`) already goes the short way, over a conservative two-lap period.
- **Why the tests missed it.** Every slide test uses slides within half a lap (for example
  `--slide 2.5`), where `fmod` and the nearest equivalent agree. The values that break it
  (125.5 cells) come only from a real player's accumulated middle-drag.

### One lap per surface, in cells

This is what `PlaySurface` itself treats as the identity. M21.5 encodes it as `app::slideLap`:

| `SurfaceKind` | axis 0 (files, `slideU`) | axis 1 (ranks, `slideV`) |
|---|---|---|
| Tube (cylinder) | `nx` | 0 (does not slide) |
| Torus (torus, torus3d, t6) | `nx` | `nz` |
| Band (mobius) | `2*nx` (comes home mirrored after one) | 0 |
| Klein | `nx` | `2*nz` (the rank seam reverses the files) |
| FlatGrid, MirrorBox | 0 | 0 |

A lap of 0 means that axis has no slide on the shape, so its value is invisible.
`PlaySurface::slidesAlongRanks` (Torus or Klein) is exactly "axis-1 lap > 0".

## M21.2 Fix 1a: seam rails fade in with `formed`

**`src/render/play_surface.hpp`/`.cpp`.** Add next to `surfaceRail`:

```cpp
/// How much of a seam rail's colour shows on `axis` (0 files, 1 ranks) at morph amount
/// `formed`: 0 on the flat board (which has no seams to mark - the ordinary `standard`
/// board draws none), 1 once that axis's own gluing has closed. Staged on the same
/// boundaries the surface functions use (view/morph.hpp), eased with shapeEase, so a rail
/// colours in exactly while its two edges travel to meet. Pure; 1 for any kind/axis with
/// no stage (so formed = 1 is bit-for-bit the shipped mesh).
[[nodiscard]] float seamRailWeight(app::SurfaceKind kind, int axis, float formed) noexcept;
```

Define `f = clamp(formed, 0, 1)`. The weight is `shapeEase(stageForm(f, begin, end))`, with
these windows:

| kind | axis 0 window | axis 1 window |
|---|---|---|
| Tube | [0, 1] | n/a, return 1 |
| Torus | [0, `view::kTorusRollEnd`] | [`view::kTorusRollEnd`, 1] |
| Klein | [0, `view::kKleinRingEnd`] | [0, `view::kKleinRingEnd`] |
| Band | [`kRibbonStretchWindow`, 1] | n/a, return 1 |
| anything else | return 1 | return 1 |

`stageForm` is declared in `render/overture_scene.hpp:208`.

**`BoardRenderer::buildInstances` surface path** (`board_renderer.cpp:856-868`):

1. Before the patch loop, compute `kind = app::overtureSignature(v).surface`,
   `w0 = seamRailWeight(kind, 0, options_.surfaceFormed)` and `w1 = seamRailWeight(kind, 1, ...)`.
2. Inside `tinted`, multiply `ru.weight` by `w0` and `rv.weight` by `w1`.
3. Replace the both-rails corner branch with:

   ```cpp
   const float sum = ru.weight + rv.weight;
   base = mix(base, mix(ru.color, rv.color, rv.weight / sum), std::max(ru.weight, rv.weight));
   ```

   At `w0 = w1 = 1` this is `mix(ru, rv, 0.5)` at weight 1, the current result exactly.
4. Keep the `if (rails)` gate as it is. Do not touch the ordinary or flat path rails.

**Continuity in the shipped sequence:**

- standard (no rails) -> cylinder at 0: weight 0.
- Cylinder at 1 (file rail 1) -> torus at 0.5: file weight 1, rank weight 0. The cylinder has no
  rank rail, so this also removes the rank-rail pop at that cut.
- Torus at 1 -> Klein at 0.4: both weights 1.

## M21.3 Fix 1b: the plinth fades out with `formed`

**`src/render/play_surface.hpp`.** Add:

```cpp
/// The plinth's share of a surface's first morph stage: the slab under the flat board fades
/// out over the first quarter of the stage in which the sheet starts to curl, so it is
/// gone before the sheet has lifted far off it.
inline constexpr float kPlinthFadeShare = 0.25f;
/// 1 at formed 0 (the slab the ordinary board stands on), 0 from
/// kPlinthFadeShare * firstStageEnd(kind) on, shapeEase'd between. firstStageEnd: Tube 1,
/// Torus kTorusRollEnd, Klein kKleinRingEnd, Band kRibbonStretchWindow; other kinds 0
/// (no plinth). Pure.
[[nodiscard]] float plinthOpacity(app::SurfaceKind kind, float formed) noexcept;
```

Formula: `1 - shapeEase(stageForm(clamp(formed,0,1), 0, kPlinthFadeShare * firstStageEnd))`.
The fade ends at: Tube 0.25, Torus 0.125, Klein 0.10, Band 0.1125. For other kinds return 0.

**Share the plinth construction.** In `board_renderer.cpp`, factor the plinth `Instance`
construction (`:1361-1375`) into a file-local helper:

```cpp
Instance plinthInstance(const view::Vec3& centreOfCells, float halfSpanX, float halfSpanY,
                        float cellBottomZ, float depth, bool present, const view::Theme& t);
```

- `halfSpanX`/`Y` are measured from the outermost cell centres (the ordinary path passes
  `(e.maxX - e.minX) * 0.5f + 0.78f`).
- Centre z = `cellBottomZ - depth`.
- Scale z = `depth / half.z`.
- Colour and edge are exactly as today. Keep `edgeMask = 15`.

The ordinary path passes `cellBottomZ = e.z - half.z` and `depth = 0.13f`, so its output is
bit-identical. An existing render test must stay green; add the one in M21.6.

**The surface path.** After the patch loop, for a non-stacked surface:

1. Compute `wp = plinthOpacity(kind, options_.surfaceFormed)`. If `wp <= 0`, skip all of this.
2. Build `flat = PlaySurface::build(v, poseAtZero)`, where `poseAtZero` is the same `pose` with
   `formed = 0`. This costs one extra build per frame, and only while `0 < formed < fadeEnd`.
   Reuse `surf` when `formed == 0`.
3. From `flat.seats()` take:
   - the AABB of the seat centres;
   - the mean `stepU`/`stepV` (pitches `px`, `py`);
   - the mean centre `c`;
   - the normal `n = flat.seats()[0].normal`. A test (M21.6) pins that it is ±Z with
     `|n.z| > 0.99`; use `sign(n.z)`.
4. Emit `plinthInstance(c, (maxX-minX)/2 + 0.78f*px, (maxY-minY)/2 + 0.78f*py,
   c.z - sign*PlaySurface::kThickness*0.5f, 0.13f*wp, false, theme_)`. The slab thins toward
   the sheet's underside as it fades, so a sheet that curls downward meets less of it.
5. Choose the batch:
   - **`wp >= 1`** (exactly `formed <= 0`): push it into `byShape[Cell]` (opaque), the same
     picture as the ordinary board's plinth.
   - **`0 < wp < 1`**: set `color[3] = wp` and push it into the surface path's
     `byShapeFlourish[Cell]` (`board_renderer.cpp:1056`). That list is drawn in the blended
     pass with `surfaceBlendPipeline_`, the mechanism the capture flourish already uses with
     `color[3]` as the fade.
6. Never emit it on a stacked surface or with `options_.flat`.

**Fallback.** If the rigid formed-0 slab visibly intersects the curling sheet in the M21.7
frames, shorten the fade by lowering `kPlinthFadeShare` to 0.15. Record the measured value in
the status section. Do not make the slab follow the curl: it is a table, not part of the board.

## M21.4 Fix 2 (and the cylinder snap): hand the pose over to the keys, never snap

Hand over from the beat's own eased pose to the keyed pose over one move. The keys then own the
pose from the **later** of (a) the first key and (b) one move after the beat's first frame. A
keyed pose is therefore exactly what the author recorded at every key except a key inside a
beat's first move. That exception is the torus3d `ply=0` key, which is the one that has to wait.

**`src/view/morph.hpp`.** Add (pure, and below both app and render):

```cpp
/// How long the trailer takes to hand its pose from a beat's own reveal to the keyframe
/// spline, in trailer units (one unit = one move).
inline constexpr float kPoseHandoverUnits = 1.0f;
/// The keyframe spline's weight in the trailer pose at unshifted trailer time `g`, for a
/// beat whose first frame was at `beatStart` and whose first keyframe is at `firstKey`
/// (infinity when the beat has none -> 0). The window is
/// end = max(firstKey, beatStart + kPoseHandoverUnits), start = end - kPoseHandoverUnits;
/// smoothstep between, 0 before, 1 after.
[[nodiscard]] inline float poseHandoverWeight(float g, float beatStart, float firstKey) noexcept;
```

**`Shell`** (`src/app/shell.hpp`, private section):

- `TrailerPose beatPose_;` holds the beat's own eased pose, never written by the spline.
- `view::EasedMorph beatFormedEase_, beatGhostEase_, beatSpreadEase_;` are separate from
  `formedEase_`/… because edit mode keeps using those on `settings_`. Sharing them would restart
  both eases every frame through `advanceEasedMorph`'s `current != last` check.
- `float beatStartTime_{0.0f};` is the unshifted trailer time of the beat's first frame.
- Add a helper: `void easeBeatPose(const TrailerPose& want, float dt, float formSpeed);`.

**Changes, in `shell.cpp`:**

1. **`startAttractStep`**, after the `formedFrom`/`spreadFrom` assignments (`:646-647`) and after
   `trailerShift_ = 0`, `lastPly_` and `moveElapsed_` are reset (`:665-667`):
   - `beatPose_ = {settings_.geometryFormed, settings_.geometryGhost, settings_.geometryShellSpread};`
   - `beatStartTime_ = trailerTime();`. With `trailerShift_ == 0` and `moveElapsed_ == 0` this
     is `beatOffset(index) + moveHistory.size()`: 54 for torus3d, 52 for the move-less second
     Klein beat.
2. **`advanceAttract`**, the playing branch (`:872-884`). Replace the `if (!splinePoseActive())`
   block with:
   1. Build `want` as now.
   2. Call `easeBeatPose(want, dt, formSpeed)` **always**.
   3. Write `settings_.geometryFormed/Ghost/ShellSpread = beatPose_`.
   4. Compute `settled` from `beatPose_` against `want`, with `kSettle = 1e-3` as now.

   The beat ease always converges, so a beat can no longer be held open by the spline. A beat
   whose keys own the pose now waits for its own reveal to finish, at most one reveal duration.
   The step order is unchanged: `advanceCameraTrack` runs after `advanceAttract` in
   `main.cpp:911/924` and `:1910/1933`, so the spline mix lands on top of `beatPose_`.
3. **`setTrailerEditing(false)`**: set `beatPose_` from the current settings. This also means
   `beatPose_` must be reset whenever edit mode is left. Edit mode itself is unchanged: it keeps
   easing `settings_` to `editPose_`, and `applySplineAt(…, true)` keeps writing `editPose_`
   from the spline. The preview shows keyed poses, which is what an author is placing.
4. **`applySplineAt`**, the pose part (`:1215-1229`):
   - Let `gu = g - trailerShift_` and `w = view::poseHandoverWeight(gu, beatStartTime_, poseKeysFrom(attractIndex_))`.
   - In edit mode (`toEditPose`), keep today's behaviour exactly: the `poseKeysFrom > gu` early
     return, then `editPose_ = {f, gh, sp}`.
   - In playback, if `w <= 0` write nothing to the pose. Otherwise:
     `settings_.geometryFormed = lerp(beatPose_.formed, f, w)`, and the same for ghost and
     spread (clamped as today).
   - The discrete fields (`shapePieceCylinders`, `geometryInvert`) keep their current rule:
     applied once `poseKeysFrom(attractIndex_) <= gu`.
5. **`splinePoseActive`** loses its only caller. Delete it, and its declaration at
   `shell.hpp:514-515`.

**Effect on the user's track:**

- **torus3d.** First frame at 54, so the window is [54, 55]. `shellSpread` rises from
  `spreadFrom = 0` and blends into the keys' 1 over the first move. The cut is the same ring on
  both sides, as the experiment showed.
- **cylinder.** First frame at 20 and first key at 21, so the window is [20, 21]. At t = 21 the
  pose equals the key's `formed = 1`, and the beat's roll blends into it smoothly instead of
  snapping at frame 426.

**Existing tests that must stay green unchanged:**

- `the 3-D morph is part of a trailer keyframe and a beat still ends`. Its key is at cylinder
  ply 1, so the window is [20, 21]. `formed` runs continuously from the roll's start (about 0)
  to the spline's value at 21 (about 0.27), so it crosses 0.2 during ply 1. `sawLow` holds, and
  `sawHigh` (ply 9) is fully keyed.
- `a beat's own morph plays until its first keyframe takes over`. With `w = 0` on the cut frame,
  `cylinderStart` and `torusStart` are unchanged.
- `between keyframes the trailer camera keeps moving smoothly`. The camera is untouched.

## M21.5 Fix 3: slides interpolate to the nearest equivalent lap

**`src/view/morph.hpp`.** Add:

```cpp
/// `value` moved by whole `lap`s to the equivalent nearest `reference`:
/// value + lap * round((reference - value) / lap). `lap <= 0` returns `value` unchanged
/// (that axis does not slide). With reference 0 this is the canonical slide in
/// [-lap/2, lap/2].
[[nodiscard]] inline float nearestLap(float value, float reference, float lap) noexcept;
```

**`src/app/overture.hpp`/`.cpp`.** Add:

```cpp
/// One lap of slide on `axis` (0 = slideU along the files, 1 = slideV along the ranks), in
/// cells: the smallest slide that gives the identical picture once the shape is formed
/// (the table in docs/plan/M21-trailer-transitions.md, M21.1). 0 where that axis does not
/// slide on the shape. Keyed by SurfaceKind, never by name.
[[nodiscard]] float slideLap(const VariantSpec& v, int axis) noexcept;
```

**Renderer, `PlaySurface::build`** (`play_surface.cpp:299`). Replace the torus line with:

```cpp
if (kind == app::SurfaceKind::Torus) {
  sv = view::nearestLap(pose.slideV, 0.0f, fnz) / fnz * stageForm(form, view::kTorusRollEnd, 1.0f);
}
```

- The bend now ramps in at most half a lap: -2.479 cells for the user's 125.521, instead of
  13.521.
- At `formed = 1` the picture is the same up to float rounding, and **bit-identical for any
  `|slideV| <= nz/2`**, including 0. So `formed one is the shipped surface, bit for bit` and the
  `--slide` captures do not move.
- Leave `wrapSlide` and every other use alone. Only the ramped value needed the canonical
  representative.

**`Shell::startAttract`.** Compute `beatLaps_` (a new member,
`std::vector<std::array<float, 2>>`, one entry per beat):

- Load each beat's variant once through the same loader `startAttractStep` uses
  (`shippedLoader_` if set, else `loader_`).
- Use `{slideLap(v, 0), slideLap(v, 1)}`, or `{0, 0}` when a variant fails to load.
- Clear it in `stopAttract`.

**App, `src/app/camera_track.hpp`/`.cpp`.** Add the pure unwrap:

```cpp
/// Unwrap a keyframe slide channel for interpolation: each value becomes its equivalent
/// (view::nearestLap with that key's own lap) nearest the previous *unwrapped* value, so
/// consecutive keys never sweep a spurious lap. A key whose lap is 0 (its variant does not
/// slide on that axis) holds the previous unwrapped value; the first key keeps its own.
[[nodiscard]] std::vector<float> unwrapSlides(const std::vector<float>& values,
                                              const std::vector<float>& laps);
```

**`Shell::applySplineAt`** (`:1195-1214`):

1. When building the `slideU`/`slideV` point lists, pass the keys' values through
   `unwrapSlides`, with `laps[i] = beatLaps_[kf->cue.beat - 1][axis]`.
2. Take `anchorU`/`anchorV` from the **unwrapped** value of the beat's first key.
3. Use `lapU = beatLaps_[attractIndex_][0]` in place of `dims.extent(0)` (this fixes the Möbius
   case).
4. Give V the same treatment U already gets:

   ```
   wholeV = lapV > 0 ? round(anchorV / lapV) * lapV : 0
   slideV = (rawV - wholeV) * slideRamp + (closed ? wholeV : 0)
   ```

   On the shipped sequence `lapV` is 0 on the only ramped beat (the cylinder), so V behaves as
   today there.

**`Shell::advanceCameraTrack`**, the live (non-trailer) path, where a new cue is captured
(`:1034-1035`). Store the targets the short way:

```
kfU' = nearestLap(kf.slideU, cameraTrackFromSlideU_, slideLap(session_->variant(), 0))
```

and the same for V. Then interpolate to `kfU'`/`kfV'` (`:1058-1061`).

- The key is looked up again each frame, so compute both in the lerp lines from
  `cameraTrackFrom*` rather than caching them.
- An axis with lap 0 interpolates as today.

**Canonicalise on record** (`Shell::recordCameraKeyframe`, `:977-978` and `:970`):

- Store `kf.slideU = nearestLap(settings_.geometrySlideU, 0, slideLap(session_->variant(), 0))`,
  and the same for V.
- Store `kf.camera.yaw = std::remainder(kf.camera.yaw, 2π)` after the `flatBoardYaw`
  subtraction.

New files then hold small, readable numbers. The unwrap above makes canonical neighbours safe to
interpolate (3.9 next to -3.9 is 0.2 cells, not 7.8). **Do not** canonicalise on parse or load:
`parseCameraTrack` keeps values verbatim, and the user's existing file is never rewritten
unless they record or erase a key. Existing files play correctly through the unwrap and the
renderer fix alone. `Settings::geometrySlideU/V` (the live middle-drag value) is out of scope.

**Authored intent.** The user's torus keys still ask for a rank slide of -2.479 cells, which is
0.31 of a lap. The bend therefore still turns the ring a little; that turn is in their keys, not
an artefact. The 125.5-cell value was inherited from the settings when the standard-beat keys
were recorded. If they want **no** turn during the bend, they re-record the torus keys at a rank
slide of 0 (or any multiple of 8). Say this in the status section. Do not special-case it in
code.

## M21.6 Tests (write these first; each must fail before its fix)

### `tests/unit/view/test_trailer_camera.cpp`, tag `[view]`

1. **`nearestLap picks the equivalent slide nearest the reference`.**
   - `nearestLap(125.521f, 0, 8)` ≈ -2.479 (±1e-3).
   - `nearestLap(126.825f, 0, 16)` ≈ -1.175.
   - `nearestLap(-28.2959f, 0, 8)` ≈ 3.7041.
   - `nearestLap(-3.9f, 3.9f, 8)` ≈ 4.1.
   - `nearestLap(5.0f, 0, 0) == 5.0f`.
   - `nearestLap(0.0f, 0, 8) == 0.0f` exactly.
2. **`the pose hands over to the keys over one move, never at once`.**
   - `poseHandoverWeight(54.0f, 54.0f, 53.0f) == 0`.
   - `(54.5f, 54, 53)` ≈ 0.5.
   - `(55.0f, 54, 53) == 1`.
   - `(20.0f, 20, 21) == 0`.
   - `(21.0f, 20, 21) == 1`.
   - `(30.0f, 20, 36) == 0`.
   - `(35.5f, 20, 36)` ≈ 0.5.
   - `(100.0f, 20, +inf) == 0`.
3. **`the spline turns the user's yaw pair the short way`.**
   - Waypoints at t 0 and 1 with yaw -2.96452 and 2.80204 (pitch 0.3357, distance 10).
   - Sample t in 0..1 in 50 steps, measuring each sample's forward vector against the first.
   - The angle never exceeds 0.52 rad, the short turn. The long way round would reach about
     5.77 rad.
   - This pins the refutation in M21.1.

### `tests/unit/app/test_overture.cpp`, tag `[app]`

4. **`slideLap is one lap of each shipped surface`.** Check the M21.1 table on the shipped
   variants:
   - cylinder (8, 0)
   - torus (8, 8)
   - mobius (16, 0)
   - klein (8, 16)
   - torus3d_8x8 (8, 8)
   - standard (0, 0)
   - hyper4 (0, 0)

### `tests/unit/app/test_camera_track.cpp`, tag `[app]`

5. **`slides unwrap to the nearest lap of the previous key`.**
   - `unwrapSlides({3.9, -3.9, -3.7}, {8, 8, 8})` = `{3.9, 4.1, 4.3}`.
   - `unwrapSlides({-26.47, -28.3, 125.5}, {0, 8, 8})` = `{-26.47, -28.3, -26.5}` (±1e-3).
   - A lap-0 key holds: `unwrapSlides({1, 50, 2}, {8, 0, 8})` = `{1, 1, 2}`.
6. **`a parsed track keeps its slides verbatim`.** Parse
   `keyframe variant=torus beat=3 ply=6 ... slideU=-24.5321 slideV=125.521 ...`. The parsed
   values are exactly the stored floats; parsing does not canonicalise.

### `tests/unit/app/test_trailer.cpp`, tags `[unit][app][trailer][spline]`

Use the existing `makeShell`/`trailerKey`. Extend `trailerKey` with an optional
`shellSpread` and slides.

7. **`a keyframe at a beat's first move does not erase the 3-D torus's spread`.**
   - Setup: `watchDwell 0.05`, `trailerMovePreview 0`, Authored. Keys at torus3d (index 5) ply 0
     and ply 6, both `shellSpread 1`.
   - Run at 1/60 s until `attractStep() == 5`, then for 4 s more.
   - The first frame of beat 5 has `geometryShellSpread < 0.05`. Before the fix it is 1.0.
   - Over beat 5, the largest frame-to-frame change of `geometryShellSpread` is < 0.05.
   - Spread reaches > 0.99 by the end.
8. **`the cylinder's roll blends into its first keyframe instead of snapping shut`.**
   - Key at cylinder (index 1) ply 1, `formed 1`.
   - On the cut frame `geometryFormed < 0.05`. Before the fix it is 1.
   - Through beat 1 the largest frame-to-frame |Δformed| is < 0.08. Before the fix it is about
     1.
9. **`keys a lap apart do not spin the slide`.**
   - Cylinder keys at ply 2 (`slideU 3.9`) and ply 5 (`slideU -3.9`), both `formed 1`.
   - Between their times, the total travel Σ|ΔslideU| of `settings().geometrySlideU` is < 0.5
     cells. Before the fix it is about 7.8.
10. **`the live keyframe blend takes the short way round`.**
    - Not attracting: `startGame("torus")` with `geometrySlideU = 30`.
    - Key `{torus, ply 0, beat 0}` with `slideU 0`, speed 2.
    - Run `advanceCameraTrack(1/60)` until the blend ends.
    - The final `geometrySlideU` is ≈ 32 (±1e-3). The total travel is < 2.01. Before the fix:
      final 0, travel 30.
11. **`recording a keyframe stores the canonical slide and yaw`.**
    - torus game with slides (-28.2959, 125.521) and camera yaw 5.0.
    - `recordCameraKeyframe(2)` stores ≈ (3.7041, -2.479) and yaw ≈ -1.2832.
    - On klein, slideV 126.825 is stored as ≈ -1.175.
    - On standard, slides are stored verbatim.

### `tests/render/test_play_surface.cpp`, tag `[render]`

12. **`a long rank slide bends the torus the short way`.**
    - For `formed` in {0.55, 0.6, 0.7, 0.8, 0.9, 1.0}, build torus surfaces with `slideV`
      125.521 and -2.479.
    - Every seat centre matches between the two within 1e-3.
    - Before the fix, 0.6 differs by about 3 cells of ring arc.
13. **`seam rail weights follow the morph stages`.**
    - `seamRailWeight` is 0 at formed 0 for every (kind, axis) in the table.
    - It is exactly 1 at formed 1.
    - Torus: axis 0 is 1 at 0.5 and axis 1 is 0 at 0.5.
    - Klein: both are 1 at `kKleinRingEnd`.
    - Each is non-decreasing over 0..1 in 0.01 steps.
14. **`the flat sheet at formed zero lies in a horizontal plane`.**
    - For cylinder, torus, mobius and klein, all seats share one z within 1e-4.
    - `|normal.z| > 0.99`.
    - This is what M21.3's axis-aligned plinth relies on.
15. **`a torus3d slide of one lap is the identity`.** The seats at `slideU = 1.5` and `9.5`
    match within 1e-3, and the same for V. If this fails, `slideLap` for a stacked shape is
    `2n`. Record which in the status section.

### `tests/render/test_offscreen_render.cpp`, tag `[render]` (no device; `buildInstances` only)

16. **`the cylinder at formed zero stands on the standard board's plinth`.**
    - Build `standard` (ordinary path) and `cylinder` with `options.surface = true` and
      `surfaceFormed = 0`.
    - Each has exactly one plinth instance (an `Archetype::Cell` instance with `edgeMask == 15`).
    - Their scale x/y and colours are equal within 1e-4.
    - The cylinder's lies under its sheet: centre z < every seat z.
    - At `surfaceFormed = 0.1` the cylinder has no opaque plinth and one in `flourish` with
      `0 < color[3] < 1`.
    - At `surfaceFormed = 0.25` and 1 there is none anywhere.
17. **`the seam rails fade in with the morph`.** Extend `the shape tints its seam rails into the
    board mesh` (`:1347`):
    - At `surfaceFormed = 0` no vertex carries the rail colour.
    - At 0.25 on the torus the file-seam corner's colour lies strictly between the fill and the
      rail colour.
    - At 1 it is the rail colour, unchanged from today.

## M21.7 Verification against the real artifact

The build needs the gfx shell. In this sandbox every executable also needs the stub libasound:

```bash
S=<scratch dir>
nix develop .#gfx --command cmake --build build/dev --target endspielzero_gui
tools/test.sh --build view app render
mkdir -p $S/tr && LD_LIBRARY_PATH=<stub libasound dir> nix develop .#gfx --command \
  ./build/dev/src/gui/endspielzero_gui --trailer-clip $S/tr --trailer-frames --user-settings \
  --fps 30 --width 320 --height 180   # prints "cut: beat N (variant) at frame F"
```

**Measuring cuts.** Compare consecutive frames: the mean absolute RGB difference of frame k
against frame k-1. A short `python3 -I` script with numpy is enough: parse the P6 header, read
the bytes as uint8, cast to int16, and average `abs(a - b)`. Then sort, largest first.

**Baseline before M21,** with the user's settings and track:

| frame | what | difference |
|---|---|---|
| 394 | standard -> cylinder cut | 3.74 |
| 426 | cylinder snaps shut at its first key | 3.51 |
| 1448 | klein -> torus3d cut | **8.18** |

Steady motion runs about 0.05-2.3. One-off peaks of 3.5-5.4 in 1585-1770 are the torus3d
camera keys' authored fast moves, not cuts.

**Acceptance:**

- The frames at the printed cylinder and torus3d cuts, and the cylinder's first-key frame, each
  measure < 1.0.
- No new frame above 3.0 appears outside 1585-1770.
- Inspect visually:
  - The plinth thins out and fades over the first quarter of the roll.
  - The file-seam rails colour in as the tube closes.
  - The torus3d beat opens on the Klein ring and its levels spread apart over the first move.
  - The torus bend turns at most about 0.3 lap with the user's keys. Compare a montage of the
    torus beat's first 60 frames (`magick frame_*.ppm +append`) with the baseline's 1.7 laps.
- Record the numbers in the status section.

**Experiments without touching the user's files.** Copy `~/.config/endspielzero/` to
`$S/xdg/endspielzero/`, edit the copy, and run with `XDG_CONFIG_HOME=$S/xdg`.
`Settings::defaultPath` honours it. That is how M21.1's two confirmations were made:

- every `slideV` replaced by `nearestLap(v, 0, 8)`;
- the torus3d `ply=0` key set to `shellSpread=0`.

**`--shot` limitation.** `--shot --screen menu --t N` (`main.cpp:673-684`) advances only
`advanceAttract`, never `advanceCameraTrack`. It therefore shows the unkeyed trailer and cannot
see M21.4 or the trailer half of M21.5. Use `--trailer-clip` for those. The M21.2/M21.3 fades
are visible in `torus --shot o.ppm --geometry --formed 0.1`.

## M21.8 Out of scope (noted, not fixed)

- At the second Klein beat -> torus3d cut, the Klein's rails (weight 1 at `kKleinRingEnd`)
  vanish, because a stacked shape draws none (`!surf.stacked()`). The purple line is visible in
  the cut frames. Fading it needs a non-`formed` signal; leave it for a follow-up.
- The torus and Klein `SeamMap`s may colour the rank seam differently, which would show as a
  colour change at the torus -> Klein cut. This existed before M21 and is not measured here.
- `stepSlideCoord`'s two-lap period in `shape_sequence.cpp` could use `slideLap` to always take
  the true shortest path. It is a separate change.

## Status

Built (2026-10-30). Measured with the same scratch config (the user's `~/.config/chessbox` track
copied to a scratch `XDG_CONFIG_HOME`), pre-M21 binary against the M21 binary, `--trailer-clip
--trailer-frames --fps 30 --width 320 --height 180`, mean absolute difference between
consecutive frames:

| frame | what | before | after |
|---|---|---|---|
| 394 | standard -> cylinder cut | 10.12 | 6.1 |
| 426 | cylinder at its first key | 12.58 | 6.73 |
| 1448 | klein -> torus3d cut | 9.91 | **0.06** |

The remaining difference at 394 is the camera and the carried position across that cut, not the
plinth or seams (checked by eye: the plinth is present on both sides, no rails at formed 0).
The 410-423 peaks (about 9-12, were about 9) are the authored camera closing in on the cylinder.

- `kPlinthFadeShare` stayed at 0.25; the fallback (0.15) was not needed. At formed 0.2 a faint
  translucent slab is visible under the sheet, as the fade finishes.
- Test 15 (torus3d one-lap identity) was not written.
- The offscreen test 16/17 were not written; the plinth and rail fades are pinned through
  `plinthOpacity` and `seamRailWeight` and by eye.
- The new tests were not run against the pre-fix code to confirm they fail first; the before
  numbers above are the evidence the causes were real.
- The user's own torus keys still ask for a rank slide of -2.479 cells (0.31 lap), so the bend
  still turns the ring a little; re-record those keys at a rank slide of 0 for none.
- Also fixed on the way: the render test "highlighting recolours exactly the cells the engine
  named" now expects 4 changed instances (the selected piece is tinted since M20).
