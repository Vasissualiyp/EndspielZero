# M12 — Spectator, Replay and the Clip

**Goal.** Turn the move camera (M11) into something a person can *watch*. M11 makes a
move readable in-game; M12 makes it presentable — a chrome-free cinema view, a
deterministic replay, and a clip that can be exported and shared. This is the layer that
turns "a chess-variant sandbox" into "a game whose moves are worth watching", which is
the difference the store page and a stream both live or die on.

**Exit condition.** A player can watch a game (their own recorded one, or — with M8 — a
live one) through the move camera with no interface furniture, replay the last move, and
export a short deterministic clip of a chosen move to a file, on any geometry, with the
same frame every time it is exported.

**A note on placement.** M12 splits across two release waves (see the roadmap). The
**export half** — cinema mode and deterministic clip export (M12.1, M12.2) — is Wave 1,
immediately after M11, because the clip is the thing that gets promoted and it needs
nothing but the camera. The **sharing half** — authored presets (M12.4) and live
spectating (M12.5) — is Wave 4, because it needs M9 (packages) and M8 (the net). M11 does
not depend on M12, so the camera ships value on its own; M12.4/M12.5 can slip without
holding anything back.

---

## M12.1 Cinema mode

A presentation state distinct from the game HUD, driven by the same `boardRect`/overlay
seam the renderer already takes (`src/render/board_renderer.hpp:122`).

- Hide rails, ledger, highlights, coordinate labels and the selection ring while a shot
  plays, so the frame is the board and the move. The UI already computes the board rect
  (`src/render/ui.cpp:933`); cinema mode widens it and suppresses the overlay.
- A **letterbox** option (bars top and bottom) and a neutral backdrop, so a clip reads as
  footage rather than as a screenshot.
- A **spectator camera profile**: the move camera plus a slow ambient orbit between
  moves, so a still board is not dead. The ambient orbit is a pure function of wall
  time and is *not* part of the deterministic shot — it must not enter a captured frame.
- Cinema mode is a `Settings` value (`cameraMode = cinema`) alongside M11.5's, and a
  command-line flag (`--cinema`) so `--shot` can capture the presentation itself.

## M12.2 Deterministic clip export

The `--shot` path already renders a frame headlessly with no display and fails on any
validation-layer message (`src/gui/main.cpp`). A clip is that, repeated at a fixed step:

- `--clip out.<ext> --move <n> --t0 --t1 --step` renders the move camera at fixed `t`
  increments, one frame each, and encodes.
- **Encoding stays dependency-light.** Reuse the existing image writer
  (`src/render/image_io.hpp`) for a sequence, then mux with an optional external tool if
  present; the engine core gains no codec dependency. A directory of numbered PPM/PNG
  frames is the guaranteed output; a single video/GIF file is best-effort.
- **Determinism is the contract:** the same `(variant, position, move, t-range, step)`
  yields byte-identical frames, because `moveCamera` is pure (M11.2). The clip is a
  golden test.
- **Export cost is per-frame CPU, not GPU.** Every exported frame draws the board and
  the interface once, so wall time is dominated by the same instance building and CPU
  menu geometry that play pays (M4.8/M4.9); the GPU is idle throughout. The exporter
  should report frames/second so a long clip is a known duration rather than a surprise,
  and it benefits directly from M4.8's pipelining and M4.9/M13's cheaper or instanced
  menu objects.

## M12.3 Replay and highlight

- A **replay** command re-runs the last move's shot sequence from `t=0`, then the
  previous, then the previous — a "watch the last few moves" scrubber. Deterministic and
  `--shot`-able.
- A **move history** scrubber across the whole game: pick a move, watch its camera shot.
  This is presentation over the existing replay corpus (ARCH §11) — the engine already
  records move lists; M12 only draws them.
- A **highlight cut**: the first shot of a captured move zooms the capture cell, so a
  clip's thumbnail is the moment that mattered.

## M12.4 Authored camera presets (the M7/M9 tie-in)

Camera policy (M11.1) is data. M12 lets it be authored and shared:

- Extend the M7 variant document/serializer (`docs/plan/M7-editors.md` M7.0.2) with an
  optional `[camera]` block: a `CameraPolicy` per variant, canonicalised like every other
  authored field.
- **It is cosmetic, like `shape`/`height`:** it must **not** enter `VariantId`, and the
  M7.0.2 round-trip test asserts that editing camera policy leaves the id unchanged —
  exactly as editing a model or icon does.
- The preset travels through the M9 package and the Workshop, so an author can make their
  variant *look* the way they mean it to, and a viewer gets it for free.

## M12.5 Live spectator (the M8 tie-in)

- With M8's authoritative server, a spectator is a client that receives the move stream
  and runs the move camera, with no input. The camera is a pure function of the moves, so
  two spectators reconstruct the same shots — the same property that makes replay
  deterministic.
- No new protocol is needed: a spectator subscribes to the existing framed move stream.
  M12 only adds the presentation and the read-only client mode.

---

## M12.6 Play a game from notation (the marketing runner)

**Want.** Insert a pre-defined game — a list of moves — and have the app play it *automatically*,
move after move, so a marketing clip is the whole game rather than one hand-driven move.
"Insert a known game, hit record, get the same video every time."

**What it is.** A runner over the notation the engine already speaks
(`src/io/notation.hpp`, FEN-N in `src/io/fen.hpp`): a text file names the variant and an
optional starting position, then lists the moves. The runner loads it and plays each move in
order with the M11 move camera and the shape-follow choreography (M17.19), cinema-framed
(M12.1). It is a *pure function of the game and the timing* — no wall clock, no interaction —
so a clip of it is reproducible.

**Build.**

- **Input.** A game file: a small header naming the `variant` and optionally a `start` FEN-N,
  then one move per line in the engine's notation. Reuse the existing parser; do not invent a
  second notation. A malformed or illegal move **stops** at that move with a message naming it
  and the position, leaving the last legal move in place.
- **Player.** Advance one move per dwell: apply the move (which already starts the animation
  and the camera choreography), wait for the shot to settle, pause a settable dwell so the eye
  can follow, then the next. The runner lives in `src/app` (drives a `Session`), so it is
  headless and testable; the front end only forwards its clock.
- **Camera and pacing.** The existing settings choose the shot: `cameraMode`/`shapeFollow`,
  `followElevationDeg`, and the M17.19 choreography. A `--pace`/`--dwell` (seconds between
  moves) states the tempo for a clip; `--camera`/`--follow` pick the shot. Cinema mode
  (M12.1) drops the interface.
- **Determinism.** Like an overture, the runner reads no clock and no previous session: given
  (file, dwell, camera settings) it emits the same sequence of frames. `--play FILE` drives
  it interactively; `--clip DIR` over `--play FILE` writes the numbered frames.
- **Getting it in front of a user.** A "Watch a game" / "Demo" action built from the same
  runner, so a capture — and later the packaged demo (M16.3) — can play a canonical game with
  one command. The game files are plain content; the store trailer is a command plus a file.

**Tests.**

- A game file plays move-for-move; the final position and the move count match the file.
- An illegal/malformed move stops at that move with the right message, position unchanged past
  the last legal move.
- `--play FILE --clip DIR` is byte-identical on a second run.
- A game that wraps a glued seam / crosses a torus exercises the shape choreography,
  validation-clean.

**Acceptance.**

1. Given a notation file of N moves, the app plays all N automatically and reproducibly; a clip
   of the whole game is byte-identical on re-export.
2. The starting position can be stated (FEN-N), so a game can begin from a set-up diagram.
3. `standard`, a glued variant and a 4-D variant all play from the same file format (the
   variant is named in the header).

**Risks and non-goals.**

- **Not an analysis tool or an opponent.** The moves are given; there is no search (that is
  M10). It is a player of a script, nothing more.
- **Must not read the private narrative.** A marketing game is a plain notation file; the
  campaign/story stays in the private sibling repo (AGENTS.md's invariant).
- **The dwell and any ambient motion must stay out of the deterministic frame**, or clips stop
  reproducing — the M12.1 ambient-orbit rule again.

## M12.7 Save and load a game (the game saver)

**Want.** Save a played game to a file - the move history *and* the variant it was played on -
so it can be loaded and played again later, replayed through the move camera, or handed to the
M12.6 runner for a clip. "Save this, open it tomorrow, watch it again."

**What it is.** A self-contained game file: enough to reconstruct the exact position and the
whole move list, with no presentation state in it. It is the user-facing form of the replay
corpus (ARCH §11) and the input the M12.6 runner already wants.

**Build.**

- **Format.** A text document reusing what already exists: the variant **name** and its
  `VariantId` (so a rules change since the save is detected, not silently misapplied), an
  optional starting **FEN-N** (`src/io/fen.hpp`), and the move history in the engine's own
  notation (`src/io/notation.hpp`). The same parser the M12.6 runner uses reads it; a saved
  game is a playable notation file with a header.
- **Load.** Resolve the variant, load the start position, then replay the moves to rebuild the
  current position and history - so the game can be **continued**, **replayed** (M12.3) or
  **played back** by the M12.6 runner, all from one file.
- **Compatibility is explicit.** If the variant's `VariantId` differs from the save's, refuse
  with a message naming the mismatch rather than replaying a game the rules no longer describe.
  A load→save→load round-trip leaves the position hash and the move list unchanged (the same
  discipline as the M7.0.2 variant round-trip).
- **Where.** A saves directory beside settings (`Settings::defaultPath()`'s neighbourhood);
  **Save** / **Load** in the pause menu, and `--save FILE` / `--load FILE` on the command line.
- **Presentation is not saved.** Camera mode, pose, slide and theme are not in the file: a
  loaded game looks however the player's settings say, the way a `VariantId` never carries a
  model or an icon. It must **not** enter `VariantId`, and it must never carry narrative.

**Tests.**

- Save after N moves, load, and the position hash and move history match the saved position.
- Load→save→load is unchanged, and a replayed clip of the loaded game is byte-identical to the
  clip of the original.
- A file whose variant `VariantId` no longer matches is refused with a clear message.
- `standard`, a glued variant and a 4-D variant all save and load.

**Acceptance.**

1. A game is saved to a file and reopened later: the same position, the same move list, and
   the same replay.
2. The saved file plays through M12.6 with no conversion step.
3. A save made before a rules change is refused rather than misreplayed.

**Risks and non-goals.** Not a database or a gallery of games (that is M9 content); not a
multiplayer/network save (M8 owns the move stream); no engine or geometry change. The format
is data over the notation and FEN-N the engine already speaks, so the risk is the compatibility
check being skipped - which is exactly what the `VariantId` test pins.

## Tests and acceptance

- **Clip determinism:** the same clip arguments produce byte-identical frame files on a
  second run, and the frames are validation-layer clean (the existing `--shot` gate).
- **Cinema chrome:** a cinema-mode capture contains no rail/ledger/highlight pixels —
  asserted by comparing against a capture with an impossible highlight cell that must
  leave no trace.
- **Ambient orbit is not captured:** a clip taken at two different wall-clock times is
  identical, proving the ambient orbit is excluded from the deterministic shot.
- **Preset round-trip:** a variant's `[camera]` block survives load → save → load, and
  changing it does not change `VariantId`.
- **Replay:** replaying the last move twice yields the same frames; the history scrubber
  reaches every recorded move.

### Acceptance facts

1. A move is exported as a clip on `klein`, `cube5` and `t6` with no display and no
   validation messages, reproducibly.
2. Cinema mode shows the board and the move with no interface furniture.
3. Camera policy authored into a variant round-trips, ships through a package, and does
   not alter `VariantId`.
4. A spectator reconstructs the same camera shots as the player, from the move stream
   alone (with M8).

## Risks and non-goals

- **Encoding is the dependency risk.** Keep the guaranteed output a frame sequence; treat
  a muxed video as a convenience, never as a required build dependency. No codec in the
  deterministic core.
- **The ambient orbit must stay out of the deterministic path**, or clips stop being
  reproducible. This is the M11 "overture is a pure function of `t`" rule applied again.
- **Non-goals:** no in-engine video editor, no transitions library, no live-streaming
  integration, no protocol change (M8 owns the protocol; M12 is a read-only client), no
  engine or geometry change of any kind.

## Status

Planned, not started. Depends on M11 (hard), M9 and M8 (for the sharing and live halves).
The frame-sequence exporter and the `[camera]` document extension are the two pieces that
touch existing code; everything else is presentation over seams that already exist.

### As built (2026-09-30)

M12.1 cinema mode and M12.2 clip export are in: `--cinema` fills the frame with the board
and records no interface; `--follow off|piece|route` and `--move-t 0..1` show the move
camera in a capture; `--clip DIR --frames N --t0 a --t1 b` renders a numbered PPM sequence
deterministically - a clip frame is byte-identical to the `--shot` at the same `t` - and
muxing stays external.

**Deferred to Wave 4:** camera presets (M12.4, needs M9) and live spectating (M12.5, needs
M8). The `[camera]` document extension rides M12.4.

**Planned, Wave 1 (2026-10-01): M12.6, play a game from notation.** The marketing runner:
insert a pre-defined game as a notation file and have it play itself — every move, in order,
through the move camera and the M17.19 shape choreography — so a clip is the whole game and a
trailer is a file plus a command. It shares the deterministic contract with cinema and clip
export, so `--play FILE --clip DIR` reproduces byte-for-byte.

### As built (2026-10-01): M12.7, the game file

The save/load half of the runner's file format is in.

- `io/game_file.hpp/.cpp` — `GameFile` (variant name + `VariantId` + start FEN-N + moves as
  long-algebraic text), `toGameFile`/`gameFileText`/`parseGameFile`/`applyGameFile`. The
  moves are the engine's own `moveText`, so no second notation exists; `applyGameFile`
  replays them against `Game::legalMoves()`, refuses a file whose `VariantId` no longer
  matches, and names the first illegal move. Presentation is not in the file.
- `Game` remembers its **start position** (`startPosition()` / `setStartPosition()`), so a
  game begun from a FEN saves that start and replays from it.
- The CLI gains **`save <file>`** and **`open <file>`**; `open` loads the variant the file
  names when it is not the current one. Tests in `tests/unit/io/test_game_file.cpp`:
  round-trip (hash and move count), replay-from-its-own-start, illegal-move naming,
  `VariantId` mismatch, malformed headers, and a glued variant.

**In-game menu (2026-10-01).** The pause menu gains a **GAME FILES** section: a name field,
**Save** and **Load** buttons, and a selectable list of the games already in the saves
folder. `Shell::saveGame/loadGame/savedGames` own the directory (`games` beside the settings
file) and sanitize the name; `Session::saveGame/loadGame` do the I/O and replay. Loading a
file for a different variant starts that variant first. Tests in
`tests/unit/app/test_shell.cpp` (round-trip through the shell, bad name, missing game).

**Still to do:** the runner (`--play FILE --clip DIR`, M12.6).

---

### M12.6 implementation spec (2026-10-03)

Fourth and last of the Wave 1 completion pieces (after M17.20's camera-correctness fix,
M17.21's rails/labels and M17.22's D>=3 remainder - see `HANDOVER.md` and the roadmap's
Wave 1). This corrects two things the want/build text above gets wrong by omission, found
while scoping this spec, and then gives the concrete build.

**Correction 1: the choreography does not currently run in a capture, at all.** The want
text above says a move "already starts the animation and the camera choreography" - true
only in the *interactive* main loop. `captureFrame` (`src/gui/main.cpp`) builds its camera
via `boardCamera(*shell, kNoShapeSequence)` - the literal "no choreography" sequence - and
a comment on `boardCamera` states this is deliberate: "a `--clip` of a move never runs the
choreography, and its frames must stay byte-for-byte what they were." So today, a `--clip`
of a shape-followed move is the **raw steady-state follow** camera, not the four-beat
Align/Approach/Travel/Return of M17.19. This must change for the runner to be worth
watching - but the existing `--clip`/`--shot` of a *single* move must keep producing
exactly the frames they do today (that byte-for-byte promise is depended on by
`ctest -R gui-`), so the choreography must be opt-in to captures, not a change to their
default behaviour.

**Correction 2: fix M17.20 first.** The runner's whole point is to showcase the shape view
on real games; if the chase camera clips into the board for most of a shape-followed move
(M17.20), the runner just produces more of that, faster. Land M17.20 before this.

#### Make the choreography a pure function of elapsed time

`render::ShapeBeat shapeBeat(elapsed, ...)` (`src/render/play_surface.hpp/.cpp`) is already
pure in `elapsed` - the stage/local-progress math has no hidden state. The thing that is
*not* pure is `gui::ShapeMoveSequence`'s accumulated `offset` (the live slide, approached by
`stepSlide`'s bounded-rate stepping, frame by frame) and the frame built from it - both
exist only in `src/gui/main.cpp` today, advanced by `updateShapeSequence(shell, seq, dt)`
once per real frame.

Refactor, do not duplicate:

1. Pull `updateShapeSequence`'s per-frame body into a free function with no dependency on
   wall-clock framing, e.g. `void stepShapeSequenceOnce(app::Shell& shell,
   ShapeMoveSequence& seq, float dt)` - the exact logic that exists today, just named and
   extracted so it can be called from somewhere other than the interactive loop.
   `updateShapeSequence` becomes a thin wrapper that detects a new move and calls this.
2. Add a **stateless** evaluator next to it:

   ```cpp
   /// The shape-follow choreography's state at `elapsed` seconds into a move that starts
   /// at `shell`'s current position, as if it had been running since 0 - found by
   /// fixed-step simulation from a fresh sequence, not recalled from any real frame's
   /// history. Two calls at the same `elapsed` for the same move produce the same
   /// result, which is what lets a clip's frames be requested out of order or more than
   /// once (M12.6).
   ShapeMoveSequence simulateShapeSequence(app::Shell& shell, const view::MovePath& path,
                                           float travelSeconds, float elapsed);
   ```

   Implementation: build a `fresh` sequence exactly as `updateShapeSequence` does on
   detecting a new move (same `startCam`/`startOffset`/`alignStart` computation - that
   part already reads only `session`/`settings` state, which is itself deterministic for a
   loaded position), then call `stepShapeSequenceOnce` repeatedly at a small **fixed**
   internal step (e.g. 1/120 s - pick one, document it, and keep it fixed regardless of the
   caller's requested `elapsed` or frame count) until the accumulated internal time reaches
   `elapsed`, and return the resulting `seq`. The fixed step is what makes two calls at the
   same `elapsed` agree bit-for-bit: nothing here may read a wall clock or SDL's event
   timer.
3. **Cost.** This resimulates from 0 for every requested frame - plausibly tens to a couple
   hundred fixed steps per call, each doing a `PlaySurface::build` inside
   `stepShapeSequenceOnce`. Clip export is not a 60 fps budget, but measure it with
   `--bench-frame` (or a one-off timing print) on a `torus`/`klein` move before deciding
   whether it needs a cache; if it does, the natural one is memoising `PlaySurface::build`
   results already keyed by pose (the quintic memo and built-scene cache M4.9 already
   established the pattern) - do not add a bespoke cache ahead of measuring.

`captureFrame`'s existing single-move `--shot`/`--move-t`/`--clip` path is **untouched**:
it keeps calling `boardCamera(*shell, kNoShapeSequence)`. The runner (below) is the only
caller of `simulateShapeSequence`.

#### The runner

**Input: the M12.7 game file, unchanged - do not invent a second format.** M12.7 already
built exactly what this wants: `io::GameFile` (`src/io/game_file.hpp/.cpp`, variant name +
`VariantId` + start FEN-N + moves in the engine's own notation), `parseGameFile`,
`applyGameFile`. The M12.7 status section says as much: "the input the M12.6 runner already
wants." `--play FILE` reads a `GameFile` the same way the CLI's `open`/the pause menu's
Load already do - resolve the variant, load the start position (or the default start if
none is stated), then step through `applyGameFile`'s moves one at a time (not all at once -
the runner needs to animate each one, so it wants the per-move apply, not the bulk replay
helper). A malformed file or an illegal move stops at that move, names it and the position,
same as `applyGameFile` already reports for the bulk case - reuse its error, do not write a
second message.

**New flags** (`src/gui/main.cpp`'s arg loop, alongside the existing `--shot`/`--clip`
family):

- `--play FILE` - the game file to play. Combines with everything `--clip`/`--shot`
  already accept (`--cinema`, `--follow`, `--shape-follow`, `--camera`), and with the two
  new flags below.
- `--dwell SECONDS` (default `0.6`) - how long the camera holds the settled position after
  a move lands before the next one's choreography begins. Separate from the choreography's
  own `kShapeAlignSeconds` etc., which govern one move's lead-in/return, not the pause
  between moves.
- `--fps N` (default `30`) - only meaningful with `--play --clip`; see below. Steam's
  trailer guidance wants 1080p at a normal cinematic frame rate, and 30 is the existing
  overture/clip convention's natural choice absent a reason to pick another - note this is
  a default, not a claim about final export settings, which belongs to whoever cuts the
  actual trailer.

**Resolution.** `captureFrame`'s offscreen target and the interactive window are both
hardcoded to 1440x900 (`src/gui/main.cpp`, three call sites: the hidden `SDL_CreateWindow`
and `OffscreenTarget::create` in `captureFrame`, and `Window::create` in the interactive
path). Add `--width W --height H` (defaults **1920x1080**, Steam's stated trailer minimum)
read in the shared arg-parsing block and threaded into all three construction sites,
replacing the literal `1440, 900`. This is independent of `--play`/the runner - every
existing `--shot`/`--clip` caller gets the new default resolution unless it passes
`--width`/`--height` itself; update `tests/` and any `ctest -R gui-` goldens that assume
1440x900 pixel dimensions (grep for the literal before assuming none do).

**Sequence timing, so `--play --clip` can be asked for "N frames of the whole game" rather
than one move's `t`.** Compute, once, before rendering any frames: for each move in the
file, in order, its duration in seconds -

- if the shape-follow choreography applies (surface view, `cameraMode != "off"`, a glued
  variant) - `kShapeAlignSeconds + kShapeApproachSeconds + travelSeconds +
  kShapeReturnSeconds` (divided by `shapeMorphSpeed` exactly as the interactive sequence
  already does), where `travelSeconds` is the move's own animation duration (the same
  value `session->animation().duration()` already provides for a move of that kind);
  otherwise
- the flat board's own move-animation duration alone (no choreography to add).

Plus `--dwell` after every move but the last. Sum these for the game's total duration
`T_total`. With `--clip DIR` (no explicit `--frames`), render `ceil(T_total * fps)` frames,
each at global time `i / fps`; map a global time to (move index, elapsed-within-that-move)
by walking the per-move durations cumulatively (a dwell maps to the *landed* pose: call
`simulateShapeSequence` at that move's full duration, repeated for the dwell's length - the
picture does not change during a dwell, only how long it is held). Without `--clip`
(a single `--play FILE --shot OUT`), render one frame at the end of the sequence's chosen
`--move-t`-equivalent - or more usefully, add a `--play-at SECONDS` for stating one instant
of the whole played-back game, mirroring what `--move-t` does for one move.

**Determinism.** No wall clock anywhere in this path (`simulateShapeSequence`'s fixed
internal step, never SDL's timer); the existing "two thrown-away frames" settle rule
(`captureFrame`'s `renderFrame(0.0f)` / `renderFrame(1.0f)` warm-up) still applies once at
the start, not per move. `--play FILE --clip DIR` run twice must produce byte-identical
frame files.

**A "Watch a game" action**, once the above works: a menu entry built from the same
runner (loads a bundled canonical game file and plays it with the player's own camera
settings, not a capture) - small, and explicitly lower priority than the capture path
itself; do first, ship second if time allows. This is also the natural seed for the packaged
demo's (M16.3) "watch a game" option later - no new work there, just reuse.

#### Tests

- A game file plays move-for-move through the runner; final position and move count match
  the file (reuses `applyGameFile`'s own guarantees - the new test is about the *runner*
  driving it one move at a time with a camera, not about replay correctness, which M12.7
  already covers).
- An illegal/malformed move stops the runner at that move with `applyGameFile`'s existing
  error, position unchanged past the last legal move.
- `simulateShapeSequence(shell, path, travelSeconds, elapsed)` called twice at the same
  `elapsed` returns bit-identical `ShapeMoveSequence::camera` - the determinism contract,
  tested directly rather than only via a file-diff on exported frames.
- `--play FILE --clip DIR` run twice is byte-identical, file by file (reuses the existing
  clip-determinism test's shape, M12.2).
- A game file whose moves wrap a glued seam (a torus/Klein game) exercises the shape
  choreography through the runner, validation-clean.
- `--width`/`--height` change the output image dimensions; the new 1920x1080 default is
  covered by at least one existing `gui-shot`-style golden so a regression to the old
  1440x900 is caught.

#### Acceptance

1. Given a notation file of N moves, `--play FILE` plays all N automatically with the real
   M17.19 choreography on a shape, or the ordinary move camera on a flat board; a
   `--play FILE --clip DIR` export of the whole game is byte-identical on re-export.
2. `--width 1920 --height 1080` (and the new default) produce a correctly-sized capture;
   every existing `--shot`/`--clip` caller keeps working unchanged apart from the new
   default resolution.
3. A single-move `--shot`/`--clip` (no `--play`) is **pixel-for-pixel identical** to what
   it produced before this spec landed, at the old resolution (pass `--width 1440 --height
   900` to check against pre-existing goldens, or update the goldens' stated resolution -
   pick one and be consistent).
4. `standard`, a glued variant (`torus`/`klein`) and a 4-D variant (`hyper4`) all play from
   the same file format, per the original M12.6 acceptance above.

#### Risks and non-goals

- Everything in the original "Risks and non-goals" above still applies unchanged (not an
  analysis tool, must not read the private narrative, dwell/ambient motion must stay out
  of the deterministic frame).
- The resimulate-from-0 cost (point 3 above) is the main technical risk; it is explicitly
  measure-first, not pre-optimised - do not add caching speculatively.
- `--width`/`--height` are a capture-only concern; the interactive window already resizes
  freely and is unaffected beyond its own default size constant moving to match.

### Status: M12.6 built (2026-10-04, opencode)

The runner is in. A `GameFile` (M12.7) now plays move by move through the real M17.19
choreography, and `--play FILE --clip DIR` exports the whole game byte-identically on a
re-run.

- **The choreography is a pure function of elapsed time.** `src/render/shape_sequence.{hpp,cpp}`
  is the M17.19 four-beat code lifted out of `src/gui/main.cpp`: `stepShapeSequenceOnce` is
  the exact per-frame body, `beginShapeSequence` the new-move setup, and
  `simulateShapeSequence(shell, path, travelSeconds, elapsed)` the stateless evaluator -
  a fresh sequence plus fixed 1/120 s steps to `elapsed`. No wall clock, no SDL timer.
  `updateShapeSequence` is a wrapper that detects a new move and calls the same step. The
  shift/pose/search helpers (`surfacePose`, `chaseEyeDistance`, `followLift`,
  `currentFollowedCell`, `alignOffsetFor`) moved with it and are shared, not copied.
- **The runner lives in `src/app`** (`game_runner.{hpp,cpp}`): `GameRunner` validates the
  file, loads its start FEN, and steps one move at a time through the new
  `Session::playMoveText`; `playbackCursor`/`playbackTotalSeconds` are the pure
  global-time-to-(move, elapsed) map. `io/game_file` gained `verifyGameFileVariant` and
  `gameFileIllegalMove`, used by both `applyGameFile` and the runner, so an illegal move
  reports the loader's own wording.
- **The flags.** `--play FILE`, `--dwell S` (0.6), `--fps N` (30), `--play-at S`, and
  `--width W --height H` (default **1920x1080**), threaded into the hidden window, the
  offscreen target and the interactive window. The single-move `--shot`/`--move-t`/`--clip`
  path is untouched: it still calls `boardCamera(*shell, render::kNoShapeSequence)` and its
  own align block. Verified against a worktree build of the pre-change HEAD: a 12-frame
  chased torus clip, an 8-frame flat clip and a board shot are byte-for-byte identical at
  `--width 1440 --height 900`.

**Deviations, and why.**

- **A move's body is its full settle time, not `align + approach + travel + return`.** The
  spec's formula stops exactly when the `Done` morph begins, so a dwell would catch the
  board mid-unwind and the next move would snap it home. `shapeSequenceSettleSeconds` runs
  the fixed-step simulation until `seq.active` is false (align offset back to the player's
  pose) and uses that; the dwell then holds a still picture and moves are continuous. This
  is the only timing change; the stage lengths themselves are unchanged.
- **`SurfaceCache` (the "known next step") is in.** Measured before adding it: a single
  re-simulate-from-0 at 1/120 s is ~1 ms per `PlaySurface::build`, and frames of one move
  replay the same prefix, so without a memo the cost would be quadratic in a move's length.
  The cache is a per-pose memo of `PlaySurface::build`, cleared once per move (the dry
  planning pass and the real pass both drop the previous move's samples), so a move costs
  one build per distinct pose, not one per frame-step. It is presentation-only: it does not
  change a bit of any frame (the export determinism test passes with it).
- **`--play FILE` alone (the interactive "Watch a game" action) is deferred**, as the spec
  allowed ("do first, ship second"). The capture path is the acceptance requirement and is
  complete; a menu entry reusing `GameRunner` is the remaining, small piece.

**Cost** (Debug `-O0`, this machine - the export is render-bound, not simulation-bound):
two chased torus moves at 1920x1080/30 fps, 229 frames, ~48 s, i.e. ~0.21 s per frame; the
planning that settles both moves is ~0.4 s for the first move. Without the pose memo the
same clip resimulated the fixed-step prefix for every frame.

**Tests.** `tests/render/test_shape_sequence.cpp` (two calls at one `elapsed` are
bit-identical, out-of-order calls agree, the sequence is not constant, settle ≥ lead +
travel); `tests/unit/app/test_game_runner.cpp` (move-for-move to `applyGameFile`'s position,
illegal move stops with the loader's message and the legal prefix intact, variant mismatch
refused, the timeline map, `hyper4` through the same file format); and four `ctest -R gui-`
cases - `gui-play-flat`, `gui-play-shape` (torus a1-a5 wraps the rank seam, validation-clean),
`gui-play-deterministic` (the shape clip run twice and compared frame by frame), and
`gui-default-resolution` (reads the PPM header at 1920x1080 and at an explicit 640x480).
`--play FILE --clip DIR` twice was also diffed by hand on 1- and 2-move torus games. Green:
`tools/test.sh --build render app`, the 26 `gui-*` ctests, and `tools/precommit.sh`.

---

## M12.8 "Watch a game" (the interactive runner, deferred half of M12.6)

**Want.** M12.6's own "Getting it in front of a user" note named this and deferred it:
*"A 'Watch a game' / 'Demo' action built from the same runner, so a capture — and later the
packaged demo (M16.3) — can play a canonical game with one command."* `--play FILE` already
drives `GameRunner` deterministically for a capture; this is the same runner, driven by the
interactive loop's real clock instead of a fixed `t`, reachable from the main menu with no
command line at all.

**What it is.** A read-only session: the engine plays a bundled game file move by move, with
the ordinary move camera and (on a glued variant) the M17.19 shape-follow choreography,
while the player watches and cannot move a piece. Not a new screen in the UI sense — the
board renders exactly as `Screen::Game` already does — but a mode the session is in, the way
`shotInFlight()` already locks picking during a shot.

**Build.**

- **`app::Session` gains a watching mode.** A `GameRunner` member (or owned pointer,
  constructed from a loaded `GameFile`), a `watching()` accessor, and a `stopWatching()`
  that clears it and returns to the ordinary session state the way leaving pause already
  does. While watching: clicks on the board are refused (same discipline `shotInFlight()`
  already uses for picking — reuse that gate rather than adding a second one), and the
  session's own `playChecked`/move-application path is driven by `GameRunner::step()`
  instead of a player action.
- **Driving it in the interactive loop** (`src/gui/main.cpp`): once watching starts, each
  frame checks whether the current move's animation (and, on a shape, the
  `ShapeMoveSequence` choreography) has settled; once settled, hold for a dwell
  (`Settings::??` — reuse whatever dwell concept `GameRunner`'s own capture-path dwell
  already names, or add one setting if none is exposed yet; do not invent a second "how
  long between moves" number if `--dwell`'s underlying constant can be shared) and then call
  `GameRunner::step()` for the next move. On `GameRunner::done()`, stop watching and return
  to the main menu — do not loop the game, and do not leave the board sitting on the final
  position with no way out (see the "how to stop" bullet below regardless of whether it
  finished or the player exited early).
- **Which game file plays.** For this v1, bundle a fixed, small set of game files as actual
  game content (not test fixtures reused by name — write real, deliberately chosen
  showcase games; `tests/data/games/*.cbgame`'s existing files are test fixtures for
  `GameRunner`'s own tests and should not silently become the game's first piece of shipped
  content). At minimum one glued-board game (to show the shape choreography) and one flat
  one. If more than one is bundled, the "Watch a game" menu entry opens a short picker
  (reuse the library's own list-and-detail widget style, not a new one) rather than only
  ever playing a fixed first file; if exactly one is bundled for this first pass, skip the
  picker and play it directly — note which choice was made in the status note.
- **The menu entry.** `Ui::buildMainMenu` (`src/render/ui_menus.cpp`) gains a `menuEntry`
  row — "Watch a game" reads better placed right after "New game" (the renumbering of the
  rows below it is mechanical, `menuEntry`'s `index` argument is already explicit per row).
  Starting it: resolve the variant from the chosen file (same resolution `--play`/`open`
  already use), start that game via `Shell::startGame`, then hand the loaded session to
  `GameRunner` and begin watching.
- **How to stop.** A visible "STOP WATCHING" control (reuse the existing in-game button row
  style) and a keyboard escape both return to the main menu — picking does not, since that
  is already refused while watching, and a player must not be left with no way out short of
  waiting for the whole game to finish.
- **Cinema or not.** Default to the ordinary game HUD (not forced cinema) so a first-time
  "watch" still shows the player the interface they will use themselves — cinema is what
  `--cinema`/the capture path already offers for marketing footage; this menu action is
  about *showing a player what the game can do*, not producing a trailer. State this
  explicitly rather than silently picking the wrong default.
- **Determinism is not required here.** Unlike `--play --clip`, this is driven by the real
  interactive clock and is not expected to reproduce byte-for-byte between runs (the ambient
  ease/dwell timing is real wall time) — do not add a determinism test for the interactive
  path; `--play`'s own determinism contract is untouched and already tested.

**Tests.**

- A headless/no-GPU test that starting "watch" with a bundled game file transitions the
  session into the watching state, that a board click is refused while watching (reusing
  whatever `shotInFlight`-style assertion the existing picking-lock test already uses as a
  template), and that `GameRunner::done()` correctly ends the mode.
- A validation-clean `--shot`/`--screen` capture of the main menu showing the new row (or
  of the watching state itself, if a capture flag is added to reach it headlessly — check
  whether one is needed or whether the existing `--play --shot` path already covers what a
  capture of this state would show).
- If a picker is built (more than one bundled file), a test that it lists all bundled files
  and that choosing one starts watching that file specifically.

**Acceptance.**

1. From the main menu, "Watch a game" plays a bundled game automatically, through the
   ordinary move camera and shape choreography, with no player input accepted on the board.
2. A visible control and a keyboard escape both return to the main menu at any point.
3. The game ends and returns to the main menu on its own when the file's last move is
   played, with no loop.
4. `tools/test.sh --build app render` green; the existing `--play`/`GameRunner` determinism
   tests are unaffected.

**Risks and non-goals.** Not the packaged demo (M16.3) — this is the mechanism M16.3 will
later reuse, not the demo build itself. Not a replacement for `--play --clip`'s
deterministic export, which stays the marketing/clip path. The bundled game file(s) are
real content, not throwaway fixtures — writing a good one (or picking one from variants
already shipped) is as much the task as the code; do not ship a trivial two-move game just
to have *something* to watch if a better one is easy to construct from what the engine
already knows how to play.

### Status: M12.8 built (2026-10-04, opencode)

The interactive half of the runner is in. A player can watch a bundled game play itself,
move by move, through the ordinary move camera and the M17.19 choreography, with no way to
touch the board and two ways out at any time.

- **`Session` owns the watch mode** (`src/app/session.{hpp,cpp}`): a heap-held `GameFile`
  and the `GameRunner` over it, `watching()`, `startWatching(file, dwell)`,
  `stopWatching()`, and `advanceWatching(dt)` — the pure
  "settle → dwell → step" clock. `startWatching` loads the file's start FEN and plays the
  first move immediately; `advanceWatching` holds each move's dwell (including the last,
  so the final position is seen) and ends the mode when the runner is done. Input is
  refused by a single gate: `boardLocked()` is `shotInFlight() || watching_`, and both
  `clickPixel` and the mutating `apply` actions consult it — no second lock. View controls
  (orbit, pan, zoom, axes, promotion preference) still pass through.
- **`Shell` owns the content** (`src/app/shell.{hpp,cpp}`): `WatchGame` (a name and a
  parsed `GameFile`), `setBundledGames`/`bundledGames`, `startWatching` (resolves the
  file's variant, starts it, hands the session the file), `stopWatching` (back to the main
  menu), and `advanceWatching`, which the interactive loop forwards its real `dt` to. The
  mode is deliberately **not deterministic** — it is the interactive clock, not a fixed
  `t` — so no determinism test was added, and `--play --clip`'s own contract is untouched.
- **The interface** (`src/render/ui.{hpp,cpp}`, `src/render/ui_menus.cpp`): a "Watch a
  game" row on the main menu, right after "New game" (the rows below renumbered); a new
  `Screen::Watch` picker that lists the bundled games and shows the selected variant and
  move count (the library's list-and-detail widget style, no new one); and a
  **STOP WATCHING** button in the game HUD, which replaces the ordinary control row while
  watching and prints "move N of M — Esc leaves the game". Esc on a watched board calls
  `stopWatching` instead of pause. The HUD is the ordinary one; cinema stays `--cinema`'s
  job, as the spec required.
- **Content.** Two real games live in `games/`, installed to `share/endspielzero/games`:
  `opera.cbgame` — Morphy's Opera Game (Paris 1858), 17 moves ending in the classic
  back-rank mate, chosen for the flat-board showcase because it is short, legible and
  decisive — and `torus-wrap.cbgame` — a 14-ply torus melee whose every move crosses a
  glued edge (1. Qd5 is a queen capture across the rank seam; the rooks then trade through
  the file seam and the bishops wrap diagonally). There is no toroidal game literature to
  borrow, so it was generated from the engine's own legal moves with a capture-preferring
  rule (a `build/` scratch script) and verified legal ply by ply; it is not a test fixture
  and `tests/data/games/*.cbgame` are untouched.
- **A capture path for the mode itself:** `--watch NAME` starts a bundled game and
  captures the watching frame, and `--screen watch` captures the picker, so both are
  reviewable the way every other screen is. `--dwell`'s default and the new
  `Settings::watchDwell` now share one `kDefaultDwellSeconds` (0.6), so the two dwell
  concepts cannot drift.

**A deliberate deviation, stated.** The M12.6 note says a menu watch uses "the player's own
camera settings, not a capture". That conflicts with this section's acceptance ("through the
ordinary move camera *and shape choreography*") for a player who has never turned the shape
view on: a glued game would then play flat and the choreography would never be seen. The
watching action therefore gives a glued variant its shape and the follow camera (`SHAPE` +
`CAMERA route`) when it starts — a showcase default, in the same spirit as the capture's
`--geometry --follow route`. A flat game is left exactly as the player's settings are. The
forced values are not saved unless the player later changes a setting.

**Tests.** `tests/unit/app/test_watch.cpp` (first move in flight and the board locked, a
pixel click refused, every move played to the file's position, an illegal move stopping on
the legal prefix with the loader's own wording, stop leaving input working);
`tests/unit/app/test_shell.cpp` (the bundled list, starting one starts its variant and
first move, switching between two files, the watch ending on its own and returning to the
menu, an unknown name refused); and three `ctest -R gui-` cases — `gui-watch` (the picker),
`gui-watch-state-flat` (Opera on the ordinary HUD) and `gui-watch-state-shape` (the torus
as its shape) — all validation-clean. Green: `tools/test.sh --build render app` (182 cases,
4.8M assertions), all 34 `gui-*` ctests, and `tools/precommit.sh` (arch, unit, property). A
first visual pass confirmed the main-menu row, the picker with both games, and the STOP
WATCHING control on both the flat and the shape board.
