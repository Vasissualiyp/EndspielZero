# The move camera

The board stops being a static diagram a piece moves *across* and becomes a place a piece
moves *through*. The view travels with a move - through a glued seam, off a reflecting
wall, between the sub-boards of a grid axis - on any board, in any dimension, with no
per-geometry code. See [M11](plan/M11-move-camera.md) for the plan and ADR-0016/0017 for
the decisions; this page is the reference for the code that ships.

## The one idea

The camera reads only data the engine has already generalised:

- `view::MovePath` (`src/view/move_anim.hpp`) - the route, as `PathStep`s tagged
  `Interior`/`Portal`/`Bounce`, each carrying the **post-transport** `Direction`, the
  `faceAxis` and the `faceSide`.
- `view::Placement` (`src/view/layout.hpp`) - every cell already placed in world space,
  including whole sub-boards on a grid axis.

It never decodes a `Coord`, never consults an identification, and contains **no branch on
dimension count or topology**. A Klein seam is a `Portal`; a mirror is a `Bounce`; a `t6`
depth move is a run of interior steps between two world points that happen to be far
apart. That is the whole reason one function covers 2-D, 6-D, glued and mirrored boards.

## The types (`src/view/move_camera.hpp`)

```cpp
struct CameraPose { Vec3 target; float distance, yaw, pitch, roll; };
struct CameraShot { float t0, t1; CameraPose a, b; Ease ease; ShotKind kind; };
enum class FollowMode { Off, Piece, Route, Both };
struct CameraPolicy {
  FollowMode follow;  float lead, pull, minDistance, deadline;
  PortalPolicy portal; BouncePolicy bounce; LeapPolicy leap; GridAxisPolicy grid;
};
```

`CameraPose` is the same fields `OrbitCamera` has, so a pose applies by assignment
(`toOrbit`). `roll` has no `OrbitCamera` field yet and is dropped.

## The functions

- `routeRuns(path, placements) -> std::vector<RouteRun>` decomposes a route into straight
  runs, split at every non-interior step. A run ends at the cell it was entered from; the
  next begins on the far side, carrying the post-transport direction. A run whose `dir`
  has support on a `ViewConfig` **grid** axis is a grid-axis run.
- `moveCamera(path, placements, cfg, scene, policy, t) -> CameraPose` is pure in `t`: no
  clock, no previous frame. With `follow == Off` it returns the settled board framing (a
  strict no-op, so every pre-M11 golden holds). Otherwise the target leads the moving
  piece by `policy.lead`, looks along the run's transported direction, and pulls back with
  travel, bounded by `policy.minDistance`. A grid-axis run with `GridAxisPolicy::Cut`
  frames the source slice before the crossing and the destination slice after, instead of
  dollying across the empty gap.

**The oracle.** With `lead = 0`, `pull = 0` and linear time, at a run boundary the target
is the piece's own world position. Every smoothing term is a deviation from that; the
tests in `tests/unit/view/test_move_camera.cpp` pin it, plus purity, the transported turn,
and the grid-axis cut.

## How it reaches the screen (`src/app/session.cpp`)

`Session::camera()` folds the shot in as an **offset in the shot's own frame**: the pose
`moveCamera` would use with following off is subtracted first, then the difference is
scaled by `followStrength` and eased in and out at the ends of the move. So the player's
own orbit, pan and zoom survive, and the move camera only leads them.

- Off is the default (`Settings::cameraMode == "off"`), so the shipped look is unchanged.
- `Session::shotInFlight()` is true while a shot owns the view; `clickPixel` returns
  nothing then, and otherwise uses the **effective** camera, so the pause pull-back cannot
  desync picking.
- `MoveAnimation::progress()` is the shared clock: the animation and the camera read the
  same number, so they cannot disagree about where the piece is.

Settings: `cameraMode` (`off`/`piece`/`route`) and `followStrength`.

## Capturing a shot

`--shot FILE [--screen NAME] [--t]` renders one frame and `--clip DIR [--frames N --t0 a
--t1 b]` renders a deterministic sequence; the capture path feeds a fixed timestep and
states each frame's `t` outright. A clip frame at time `t` is byte-identical to the
`--shot` at the same `t`.

## Following a move on a shape (M17.19)

When the geometry view is on and the move camera is not `off`, a played move runs a
choreography instead of M11's lead/pull blend. It is what makes a torus read as a place the
piece travels *through*. The stages are **strictly sequential**, and morphing and camera
motion do not overlap outside Travel (where following and morphing go together by
definition):

- **Align.** The camera is still; the board **morphs** - its `PlaySurface` slide - only if the
  player's own line to the start square is actually blocked (`alignSlideToFace`).
- **Approach.** The camera flies from the player's view to the chase pose behind the piece at
  the start of its route, **with the board held still**. The piece is pinned at the start
  through Align and Approach, so it never moves before the camera has reached it.
- **Travel.** The piece travels; the camera follows, and the board morphs **only when the
  camera's own next path would cross a square** (`render::followClips`), toward the clear pose
  (`alignSlideU`).
- **Return.** The camera flies back to the player's view, board still.
- **Done.** The board morphs home to the player's own slide, camera still; the sequence does
  not end until it is actually home, so nothing snaps.

`render::shapeBeat(elapsed, align, approach, travel, return)` is the pure timeline
(`docs/plan/M17-geometry-view.md`); the front end owns the clock
(`ShapeMoveSequence` in `src/gui/main.cpp`) and drives the move animation's own progress from
it, so the piece is pinned through the lead-in and the return. A **CAMERA** toggle in the game
rail (shown while the shape is on) turns the follow on and off without leaving the board - the
same switch as the settings screen's "Move camera".

### The camera move is a quaternion slerp

Stages 1→2 and 3→4 move the camera with `view::slerpCamera(a, b, t)`: the **eye travels a
straight line** (`a.eye() + (b.eye() - a.eye()) t`, with the look-at derived from it) and the
**orientation is a quaternion slerp** on the shortest arc, converted back to yaw/pitch/roll
through the renderer's own `cameraBasis`. Interpolating yaw/pitch/roll separately, or the eye
*direction* by a plain lerp (which walks through the origin when the two views nearly oppose),
is what produced the 720-degree spins this replaces. During Travel the chase frame is adopted
**fully** (`t = 1`), so the piece is exactly upright.

### The chase frame

The camera's frame is intrinsic to the piece, not to world up:

- `a` - the direction of motion (the route's travel), `b` - the piece's upright (its surface
  normal), `n = a x b` - across it,
- `c = -a cos(theta) + b sin(theta)` - `-a` rotated up by the elevation (`lift = tan`), the
  piece-to-eye direction,
- the camera's **right is `n`**, its **up is `c x n`**.

`c x n` is the piece's upright projected perpendicular to the view - the projection of `b`.
`(a x b) x c = n x c` is its negative and points *down* the piece; using that order hangs the
piece upside down. `render::surfaceChaseCamera` builds this frame explicitly and
`tests/render/test_play_surface.cpp` ("the chase frame's axes are the piece's own") pins
`right = n`, `up = c x n` out of the matrix the renderer actually uses.

### Cost

The align search scores ~100 candidate slides per followed cell. It used to build a whole
`PlaySurface` (patches, seats, picker) for each, which cost ~0.24 s/frame on a Debug build
(~5 fps during a followed move). On an orientable ring it now ranks candidates with a
single-cell normal probe and builds the full surface only until one comes back unoccluded -
the same argmax (an unoccluded candidate always outscores an occluded one), a couple of
builds instead of 144. A non-orientable shape keeps the exact per-candidate build, because a
lone raw normal is not the field its continuity walk draws with.

## Stations, rig moves and the room clamp (M24)

With the lab on, the camera is not only the board's orbit camera: the menus look from
**stations** and the games from the board, and the two are joined by moves.

- **Stations.** Every `[[station]]` in `assets/lab/lab.toml` is a standing eye (1.45 to
  1.75 m, at least 0.25 m clear of the furniture); `LabScene::station` returns its
  `OrbitCamera`. `stationFor` in `src/gui/main.cpp` maps each screen to one; Paused maps to
  `menu`. Console `station <name>|off` pins one.
- **Station flights.** Menu to menu is `view::blendCamera` on the pure `rigEase` curve
  over `rigMoveSeconds` (0.6 to 1.1 s, capped angular speed); it is a pure function of the
  move's age, so a capture can pin any instant (`--from`, `--transition-t`).
- **Deploy and stow.** A game opens as `ScreenMove::Deploy`: the eye rides the slab fetched
  from the rack (`rigFlightCamera` in `main.cpp`), the aim re-derived from the eye, and
  lands exactly on the game camera at the last lock; `Stow` mirrors it. The board stays in
  board space throughout (`view::RigXform` moves the lab, not the board).
- **The room clamp.** In a lab game the free orbit stays inside the room:
  `view::clampOrbitDistance` caps the eye's distance by the room box (inset), applied by
  `render::constrainGameCamera` to the session's stored orbit, and `view::wallFade`
  screen-door fades the room parts between the eye and the board (`wallfade on|off`).
- **Calm.** At calm intensity every station flight, deploy and stow is a 0.3 s cross-fade
  with the camera cut.

## Extending it

A new camera *choice* is a field on `CameraPolicy` (data); a new *geometry* is a new
`SeamKind`/`StepKind`, which the camera already speaks; a new *view style* is a
`ViewConfig`. None of them is a branch on `dims`. If a change needs to know whether the
board is 3-D or a Klein bottle, the data is being ignored - fix that instead.
