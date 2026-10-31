# AGENTS.md — EndspielZero

Read this first. It is the map. Design rationale is in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md); the plan is in
[`docs/plan/00-roadmap.md`](docs/plan/00-roadmap.md); decisions are in
[`docs/adr/`](docs/adr/README.md).

## Do not read the plot **[INVARIANT]**

The game's story is a **paid asset** and is kept out of this repository entirely: it lives in
the private sibling repo `ChessBox_Steam/` (`../ChessBox_Steam/narrative.md`). Do not open,
read, quote or reason from it — or from any other story, cutscene or campaign-narrative
material — unless the user explicitly asks you to work on the narrative. No code, spec, test
or ADR may depend on it, and it must never be cited to justify a technical decision. **Never
commit it to this repo**, and never add a tracked file that links to it. Engineering work is
driven by the specs and the engine, never by the fiction.

## Who does what — read this before writing any code

This repository is worked by more than one assistant, and they have different jobs. These
are defaults; **the user can always override them**, and an explicit instruction in the
current conversation beats everything here.

- **If you are Claude:** do **not** write implementations. Your job is to produce
  *detailed specifications* that a less capable model can reproduce exactly: the files and
  functions to touch, the data structures and their invariants, the algorithms, the edge
  cases, the tests to add, and the acceptance criteria. Commit specs, plans and design
  notes; leave the code to opencode. Where you can, leave the failing test or the exact
  expected behaviour rather than the implementation.
- **If you are opencode:** you are the implementer. Your job is to turn Claude's
  specifications (the milestone plans in [`docs/plan/`](docs/plan/), the ADRs, and any
  spec written for you) into working, tested code, and to keep the suite green. Read the
  spec, implement it, test it, and update the status sections the spec names. **If no spec
  exists for what you've been asked to do, see "When there is no spec yet" below before
  writing any production code.**

If neither role matches what you are being asked to do right now, follow the user.

## When there is no spec yet **[opencode, read this before a bare "fix X" / "add Y"]**

Everything above assumes a spec already exists. Claude's `docs/plan/` entries are written
the way they are *because* the investigation happened before a line of implementation was
written — the root cause found by reading the real code and reproducing the real failure,
the fix stated precisely enough that writing it is close to mechanical. A bare request
aimed straight at opencode, with nothing in `docs/plan/` naming it, has not had that
investigation. Asked to diagnose and implement in the same pass, under the pull to just
produce a diff, a model reliably treats the symptom rather than the cause — this has
happened here, repeatedly, which is why this section exists.

**First, check whether a spec already covers the task** — a `docs/plan/` entry naming it,
or one pasted into the prompt. If one does, implement from it directly; the rest of this
section is already done and repeating it is wasted motion, not rigor.

**If none exists, do the investigation as its own pass, written down before touching
production code:**

- **Reproduce the problem first.** A screenshot, a failing assertion, a measured number -
  something that shows the broken behaviour exists, not a description of it. Do not
  propose a fix before this exists.
- **Read the actual code the bug or feature touches**, not just the surrounding area,
  until the root cause (a bug) or the right integration point (a feature) is found in the
  real implementation - not inferred from its name, its tests, or what it's plausibly
  supposed to do.
- **For a bug: say why the existing tests missed it.** This is the single most reliable
  way to land on the real cause instead of the symptom, and it is usually one sentence once
  the cause is actually found - a test built on a degenerate case, a property nothing
  checked, an assumption nobody stated.
- **For a feature: name the existing pattern to extend**, by file and function, rather than
  inventing a parallel mechanism. Grep for the nearest already-solved version of the same
  problem before writing a new one.
- **Write the above down** - a short spec in the relevant `docs/plan/` file (continuing its
  numbering if one is already open for the area, a new file if none fits), in the same
  voice and the same level of detail the Claude-written specs in this directory already
  use: what was found, the fix or feature stated precisely, the tests that will prove it,
  and - for anything genuinely uncertain - an explicit, honest fallback. ("If X proves too
  deep, ship Y and record why" is permission to have, not a failure to avoid; M17.22's
  twisted-shape work used exactly this and shipped clean.)
- **Only then implement**, test-first as every other rule here already requires.
- **Verify against the real artifact, not the test run.** "Tests pass" is necessary and not
  sufficient - take the screenshot, read the capture, check the number a human would check,
  before calling it done.

This is slower than answering in one pass. It is the actual, measured difference between a
clean fix and a bug-infested one-shot on this codebase - not a style preference.

**Status: M0-M6 complete.** A playable, perft-exact engine with N-dimensional boards,
non-trivial boundary topology, a Vulkan interface, a rule VM, and 5D-chess-style time
travel. Next, in **release order** (visuals and clippable things first — see the roadmap's
release sequence): Wave 1 M11 (move camera), M12 (clip export/cinema), M13 (generic
overtures), M17 (play on the shape) and the piece editor of M7 (re-author the shipped piece
models - only the piece editor, not the board or gamemode editors), then M16.1 (Coming Soon
page); Wave 2 M14 (onboarding), M10.2-3 (the baseline opponent the demo needs), M16.2-3
(build pipeline + demo) and the board and gamemode editors of M7;
Wave 3 M10.4-5 (learned AI), M15 (campaign) and M8 (multiplayer); Wave 4 M9 (Workshop);
Wave 5 M16.4-5 (release). The fixed schedule anchor is **Steam Next Fest,
February 2027**. Milestone numbers are stable IDs, not a schedule; M11 only needs M4/M6 and
M13 lets data-only variants animate.
See [`docs/plan/00-roadmap.md`](docs/plan/00-roadmap.md); each milestone plan ends with a
status section recording what was built, what was deferred, and why.

## What this project is

A FOSS sandbox for finite-board game variants of arbitrary shape: any number of
dimensions, arbitrary boundary topology (torus, Klein bottle, …), piece movement
expressed in one vector-move algebra, custom data fields, and 5D-chess-style time
travel. Engine core is dependency-free C++23 driven by declarative variant data.
Performance and extensibility are the two hard constraints.

## Commands

```bash
nix develop                      # core dev shell (no graphics deps)
nix develop .#gfx                # adds SDL3 + Vulkan (M4+)
cmake --preset dev && cmake --build build/dev
ctest --preset dev               # fast suite; the slow label is excluded by the preset
ctest --preset dev -L unit       # the tightest loop; labels: unit property golden perft arch render
ctest --preset release-slow      # deep perft + wide property sweep - RELEASE, it is ~10 min
                                 # (the same in Debug is over an hour: depth-5 perft at -O0)
tools/test.sh --build view app   # ONLY the tests a change can touch; runs the tags directly
                                 # (see "Testing what you touched" below) — prefer this while iterating
tools/precommit.sh               # format + tidy + fast tests — run before every commit
nix flake check                  # THE gate: both compilers, sanitizers, coverage, goldens, bench
tools/package.sh                 # a self-contained release prefix + a headless smoke test (M16.2)
tools/steam_upload.sh            # SteamPipe upload of that prefix (needs AppID + credentials)

./build/dev/src/cli/endspielzero                          # interactive
./build/dev/src/cli/endspielzero "load torus" board moves  # batch; nonzero exit on error
./build/release/bench/endspielzero_bench                   # release only

# The game. Needs the gfx shell; run from the repo root so variants/ resolves.
./build/dev/src/gui/endspielzero_gui                       # standard chess
./build/dev/src/gui/endspielzero_gui klein                 # any variant in variants/
./build/dev/src/gui/endspielzero_gui cube5 --shot out.ppm --script "click c1"
./build/dev/src/gui/endspielzero_gui --shot out.ppm --screen settings   # any screen
./build/dev/src/gui/endspielzero_gui --shot o.ppm --screen menu --t 3.0   # the main-menu attract
                                 # sequence (M13.9), 3s into its loop
./build/dev/src/gui/endspielzero_gui --trailer-clip out/ --user-settings   # the whole trailer to
                                 # out/trailer.mp4, one lap, fixed step, no interface; add
                                 # --trailer-frames for PPMs (diff them to find discontinuities)
./build/dev/src/gui/endspielzero_gui --shot o.ppm --screen menu --t 14 --trailer-edit
                                 # the trailer in its camera-edit mode (PLY/BEAT, REC KEY, shape buttons)
./build/dev/src/gui/endspielzero_gui torus --shot o.ppm --screen newgame --t 0.6  # an overture
./build/dev/src/gui/endspielzero_gui torus --clip frames/ --screen newgame       # a frame sequence
                                 # (--frames N --t0 a --t1 b; mux with ffmpeg externally)
./build/dev/src/gui/endspielzero_gui standard --script $'click e2\nclick e4' \
                                 --follow route --move-t 0.5 --cinema --shot m.ppm
                                 # a gameplay shot: the move camera, no interface
./build/dev/src/gui/endspielzero_gui torus --geometry --cinema --follow route \
                                 --shape-follow chase --script $'click a1\nclick a5' \
                                 --clip frames/            # shape follow: chase or turntable
./build/dev/src/gui/endspielzero_gui --play game.cbgame --geometry --cinema --follow route \
                                 --shape-follow chase --clip frames/   # M12.6: play a saved game
                                 # --dwell S, --fps N, --play-at S, --width W --height H
./build/dev/src/gui/endspielzero_gui standard --shot o.ppm --screen body   # the designer tabs
./build/dev/src/gui/endspielzero_gui standard --shot o.ppm --theme manifold # dark glass alternative
./build/dev/src/gui/endspielzero_gui standard --shot o.ppm --matte          # material-free A/B capture
./build/dev/src/gui/endspielzero_gui standard --screen welcome --ui-scale 3 --shot o.ppm # scaling review
./build/dev/src/gui/endspielzero_gui standard --shot o.ppm --screen designer --dims 4
./build/dev/src/gui/endspielzero_gui torus --shot o.ppm --geometry        # the board as its shape
./build/dev/src/gui/endspielzero_gui torus --shot o.ppm --geometry --evert 1  # ...turned inside out
./build/dev/src/gui/endspielzero_gui torus --shot o.ppm --geometry --slide 2.5  # ...slid round it
./build/dev/src/gui/endspielzero_gui torus --shot o.ppm --geometry --ghost 0.4  # ...translucent, you see through it
./build/dev/src/gui/endspielzero_gui torus --shot o.ppm --geometry --formed 0.5  # ...half-way formed
./build/dev/src/gui/endspielzero_gui klein --shot o.ppm --geometry --piece-cylinders  # icon-faced pucks
./build/dev/src/gui/endspielzero_gui torus --shot o.ppm --flat             # the 2D token view
```

**A build directory remembers which shell configured it.** One configured under
`.#gfx` will not build under the plain shell - the Vulkan and SDL headers are gone.
Use `.#gfx` for anything touching the renderer, the interface or the GUI.

`--shot` renders one frame - board, rails, ledger, everything - to an image with **no
display required**, and exits nonzero if the Vulkan validation layers said anything. It
is how the interface gets reviewed, and `ctest -R gui-frame` runs it.

The CLI is the fastest way to inspect anything: `info` prints a variant's axes,
geometry and canonicalised atoms; `board` renders N-D boards as labelled slices;
`divide <n>` splits perft by first move.

## Testing what you touched

`ctest --preset dev` is the whole fast suite, but it rebuilds and runs the ~4-minute
property sweep and the perft counts. Those are for engine changes; a colour or a camera
should not wait on them. Run only the tags a change can reach:

| Changed | Run |
|---|---|
| `src/view` (camera, layout, seams, move anim/camera) | `tools/test.sh --build view` |
| `src/render` (deco, overtures, UI, board renderer) | `tools/test.sh --build render` |
| `src/app` (shell, session, settings, overture player) | `tools/test.sh --build app` |
| `src/io`, `src/game`, `src/rules`, `src/variant` | `tools/test.sh --build io game rules variant` |
| `src/pieces`, `src/movegen`, `src/geometry`, `src/position` | `ctest --preset dev` (the property/perft sweep lives here) |
| anything in `src/base`, `src/space`, `src/diag` | `ctest --preset dev`, then `nix flake check` |
| a golden change | `ctest --preset dev -L golden` |

`tools/test.sh --build <tag...>` builds first and then runs the Catch2 tags directly, so
the binary tested is the one just compiled - a stale binary is how a wrong change looks
green. `tools/test.sh quick` is every fast label (`unit render golden arch`);
`tools/test.sh all` is exactly `ctest --preset dev`. The tags are `[base]`, `[space]`,
`[geometry]`, `[pieces]`, `[variant]`, `[position]`, `[rules]`, `[movegen]`, `[view]`,
`[temporal]`, `[game]`, `[io]`, `[app]`, `[net]`, `[render]`, plus `[dims]`, `[variants]`
and `[golden]` for the cross-cutting N-D files. Before a commit, the format and arch
checks still run (`tools/precommit.sh`); `nix flake check` is the release gate, never the
inner loop.

## The rules (non-negotiable)

1. **Test first.** No production line before a failing test names the behaviour.
2. **Green before next step.** A red suite blocks all forward motion — not just
   the next milestone, the next step.
3. **Never "update" a golden to make a build pass.** Investigate.
4. **The naive oracle in `tests/oracle/` is sacred.** Every optimisation is proven
   equal to it by differential test. See ADR-0009.
5. **No floats in `endspielzero_base` or `endspielzero_core`.** Determinism. Enforced by a test.
6. **No allocation in movegen.** Enforced by the allocation tripwire.
7. **Architectural decisions get an ADR.** Immutable once merged; supersede, never edit.
8. **Update this file in the same commit** as anything that changes navigation or commands.

## Layer map — dependencies point strictly down (ARCH §1)

Each layer is a CMake target linking only to lower layers, so a violation is a
**link error**, not a review comment.

| Layer | Dir | Holds |
|---|---|---|
| L110 | `src/cli` | the scriptable command-line front end |
| L110 | `src/gui` | the playable window's main loop (exe only) |
| L100 | `src/render` | Vulkan, offscreen target, instanced renderer, piece meshes, ImGui panels, variant overtures |
| L105 | `src/net` | the multiplayer front end: framed protocol, in-process and TCP transports, fault injector, authoritative server, thin client |
| L95 | `src/app` | screens, settings, interaction: actions in, snapshot out |
| L93 | `src/audio` | SDL3 sound: procedurally synthesised SFX and a device stream, silent when no device is present (M18.5) |
| L90 | `src/io` | variant TOML loader, notation, FEN-N, ASCII board, replay |
| L80 | `src/game` | history, undo, adjudication, repetition, clocks |
| L70 | `src/temporal` | turn/timeline axes, present, branching (M6) |
| L60 | `src/view` | N-D projection, camera, picking, snapshots, the theme |
| L50 | `src/movegen` | expansion, ray walk, staged gen, legality, perft |
| L40 | `src/position` | cells, occupancy, field columns, hash, make/unmake |
| L30/35 | `src/pieces`, `src/variant` | vector-move algebra, resolved `VariantSpec` |
| L25 | `src/assets` | authored content: a piece's body and flat icon, as integer vector data |
| L20 | `src/geometry` | identifications, transition group, transport |
| L10 | `src/space` | `DimSpec`, `Coord`, `Direction`, strides, `CellId` |
| L0/5 | `src/base`, `src/diag` | containers, arenas, `Result`, bitsets, RNG, Zobrist, tracing |

Levels must be unique; an arch test enforces it. `src/view` and above may use floating
point, the layers below may not - that boundary is the whole point of where `view` sits.

## Three representations of "a square" — get this right (ARCH §2)

- `Coord` (18 B POD, `(x,y,z,…)`) — **only** L9 IO and L10 UI and tests.
- `CellId` (`uint32`, flat lattice index) — **everything hot**. Movegen never
  decodes a coordinate.
- `DirId` (`uint16`, index into the variant's global direction table).

## Where to add what

| Task | Go to | Skill |
|---|---|---|
| Story, cutscene, campaign scenario, art, voice | the private sibling repo `ChessBox_Steam/` - **never this repo** | |
| New piece | `variants/*.toml` (data only) | `cb-new-piece` |
| New variant | `variants/`, goldens, `docs/variants/` | `cb-new-variant` |
| New topology | `variants/*.toml` geometry block | `cb-new-geometry` |
| New rule mechanic | `src/rules/` opcode + tests + docs table | `cb-new-effect` |
| A piece's look | `variants/*.toml` `shape` / `height` (data only) | `cb-new-piece` |
| UI colours | `src/view/theme.hpp` - two themes, one struct, no renderer changes | |
| UI panel treatment and action labels | `Theme::panelStyle`, `monoLabels` and `interfaceRule`; `widgets::panelCorners` in `src/render/ui_widgets.cpp` and `Ui::actionFont` in `src/render/ui.cpp`. Settings → Looks → Theme selects the whole look, including the backdrop | |
| Ambient background (drifting shapes or a perspective grid floor) | `Theme::backdrop` in `src/view/theme.hpp`, dispatched by `DepthField::draw` in `src/render/deco.cpp`; the same field draws behind menus and behind the board | |
| A surface's material (wood, metal, glass, crystal, glow) | `Theme::materials` + `shaderMaterial` in `src/render/board_renderer.cpp` + the dispatch in `src/render/shaders/board.frag`; see ADR-0021 | |
| A sound effect | `src/audio/audio.hpp` - add a `Sound` and its synthesis, then one `audio::play` at the event site | |
| A flat piece icon | `src/render/piece_icon.cpp` - polygon tables, two styles | |
| An authored piece body or icon | `src/assets/piece_model.cpp`; the designers are `src/render/ui_designer.cpp` | |
| A menu's decorative object | `src/render/deco.cpp` + `decoForScreen` | |
| How a menu object answers a drag | `Ui::updateObjectDrag`; the turn is an offset on the object's own animation | |
| The trailer's camera (calm sway, follow, authored keyframes, edit mode) | `Shell::trailerCamera`/`setTrailerEditing` in `src/app/shell.cpp`, `view::steadySway`, the panel in `Ui::buildTrailerEditRows`. Never write the trailer's camera into `Settings`. See `docs/plan/M13-overtures.md` (2026-10-16) | |
| A variant's library animation | `src/render/overture_scene.cpp`; which one, and its cycle, in `src/app/overture.cpp` | |
| A variant with no bespoke overture | nothing - it gets `derivedOvertureScene` from its geometry (`app::overtureSignature`), keyed by `SurfaceKind`, never its name. See `docs/overtures.md` |
| Playing on the shape (a glued 2-D board warped to its surface, or a `torus3d`/`hyper4` authored shape) | `render::PlaySurface` in `src/render/play_surface.cpp` - seats, frames, sizes, tiles and picking, all off one sampling; the D>=3 shapes come from `playShapePosition` in `src/render/overture_scene.cpp`. `BoardRenderer::buildInstances` draws them. The mode is `Settings::geometryView`, offered only where `render::hasPlaySurface`; `Settings::geometryEvert` is the pose. See ADR-0019 and `docs/plan/M17.12-shapes-above-two-dimensions.md` | |
| The tutorial / onboarding screen | `Screen::Welcome` and the curated path in `src/app/shell.cpp`; the screen is `Ui::buildWelcome` in `src/render/ui_menus.cpp`, opened from the main menu's Tutorial row (not on first launch) | |
| New module in a layer | that layer's dir + CMake edge + test + this table | `cb-new-module` |
| A decision | `docs/adr/` | `cb-adr` |
| Perft mismatch | bisect with `divide` against the oracle | `cb-perft-golden` |
| Performance work | `bench/baselines/` first, then the hot path | `cb-bench-baseline` |

Variants ship, all as data: `standard`, `cylinder`, `torus`, `mobius`, `klein`,
`mirrorbox`, `cube5` (3-D), `hyper4` (4-D), `torus3d`, `t6` (6-D torus), `atomic`,
`atomic_torus`, `mustcapture`, `charged`, and `5d` (turn and timeline axes). See
[`docs/variants/README.md`](docs/variants/README.md).

Skills live in `.claude/skills/` and are catalogued in
[`docs/plan/skills.md`](docs/plan/skills.md). Prefer them: the procedures are
long, multi-directory, and identical every time.

## Conventions

- C++23. `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`.
- Types `PascalCase`, functions `camelCase`, members `trailing_`, constants `kName`.
- SPDX header on every file: `// SPDX-License-Identifier: GPL-3.0-or-later`.
- Errors: `Result<T, ErrorCode>` in the engine; exceptions only at the IO boundary.
- Tests mirror the source tree: `src/movegen/ray.cpp` → `tests/unit/movegen/test_ray.cpp`.
- Commits: `<area>: <imperative summary>`; a golden change must explain itself.

## Gotchas

These are the ones that have actually cost time here, not hypotheticals.

- **A VariantSpec must outlive, and must not move under, every `Position` and
  `MoveGen` that refers to it.** They hold a pointer to it. Own it somewhere stable;
  the CLI's `Session` is non-movable and heap-held for exactly this reason.
- **Direction transport, not just position.** A piece crossing a seam has its
  *direction* mapped too, or non-orientable boards are silently wrong. The transform's
  action on directions is derived, never authored, so it cannot be declared wrongly.
- **Ray semantics on glued boards are subtle — read ADR-0010 before touching them.**
  Rays end on orbit closure (`cell` *and* `direction` back to the start), duplicates
  are removed from the move list rather than by cutting the ray short, and
  `isAttacked` uses a forward scan on glued boards because the fast backward walk is
  unsound there. Each of those was a real bug first.
- **An oriented atom's direction span is not closed under transport.** A Klein seam
  turns "forward" into "backward", so backward attack search uses the *unoriented*
  expansion, per colour, and checks the arrival direction.
- Direction counts explode with dimension (`[1,2,3]` at D=8 → 2688). There is a
  load-time budget; respect it.
- An atom of order `r > D` expands to *nothing* — deliberate, tested, not an error.
- "Forward", "the last rank", castling and bishop colour-binding are meaningless on
  some topologies. Per-variant policy; never hardcode. A glued board also needs a
  different *opening array* — the usual one starts in check on a torus.
- **Do not specialize on dimension count.** The ray walk iterates a direction's
  support (1–3 axes), not the dimension count; `docs/plan/M2-nd-generalization.md`
  records the measurement reasoning.
- `kMaxDims = 8` lives in `src/space/dims.hpp` and nowhere else.
- **A piece model is integer permille and has no floats in it.** `src/assets` is content:
  it is saved, shared and eventually signed, so two machines must agree on it byte for
  byte. Trigonometry belongs to the assembly step above it. Geometry is cosmetic and does
  **not** enter `VariantId` - changing how a bishop looks must not make a saved game
  unreplayable.
- A flat piece is a figure in its *own* colour on a token that is the **same** for both
  sides. Drawing white as dark-on-light and black as light-on-dark makes the disc the
  thing the eye sorts by, and the disc is the furniture. `Theme::pieceToken` is the one
  colour that has to clear both piece colours, which pins it into a narrow band.
- An editor page that draws its own frame must be dispatched *before* `buildEditor` opens
  the shared one - a page that begins a second pane inside the first leaves ImGui holding
  both, and says so in a red box.
- Placing a widget with `SetCursorScreenPos` moves the layout cursor. Put it back, or
  everything drawn after lands inside whatever you were drawing on.
- **`widgets::fillPolygon`, never `PathFillConcave`, for a piece outline.** ImGui's own
  concave fill closes a valley over - a notch an author cuts comes back filled - so the
  fill here ear-clips instead. It *copies* its points first, and that is not tidiness:
  callers pass `dl->_Path`, and every triangle drawn rewrites `_Path` underneath the loop
  reading it. The symptom is vertices in the 1e33 range, not anything that looks like a
  drawing bug. `triangulate` fills; it does not validate - `assets::validate` is what
  decides whether an outline is a shape at all.
- The piece designer is one pane with three tabs, and the two model tabs draw *inside*
  it. An editor page that opens its own pane must be dispatched before `buildEditor`
  opens the shared one.
- The designer's move preview builds a real `DimSpec` and runs the real `expandAtom`, so
  "what does this piece do on a 4-D board" is answered by the engine rather than by a
  drawing. Extent shrinks as dimension grows - 9, 7, 5 - because 9^4 is unreadable.
- **An atom's magnitudes are units of *movement*, not cells** - see ADR-0013. An axis
  declares a `pitch` for how many cells one unit covers; the turn axis uses 2, because
  boards along it alternate whose move it is and one turn of travel is two boards. The
  scaling is applied once, where the direction table is built, so nothing below the
  variant layer ever sees anything but lattice cells. The oracle applies the same rule
  from its own code, deliberately.
- **Pieces have no model files.** A shape is assembled from primitives against an
  archetype (`src/render/piece_mesh.cpp`); a variant may name one, and anything it
  leaves out is derived from how the piece moves. An unknown name falls back rather
  than failing - a variant is never unplayable for want of a model.
- The palette is `view::Theme`, one struct, and there are two of them: `console()` is
  the shipped candlelit wooden set and `manifold()` the dark glass/crystal alternative.
  Colours live in the theme; shader material branches live in the renderer.
  `tests/unit/view/test_theme.cpp` pins the load-bearing part: both piece colours must
  read on both squares. That test found the old board pair sitting at 1.2:1.
- **A material is sampled in object space** (`fragObject`) and keyed on what the object
  is, not where it is. A piece's grain is keyed by type and colour so it does not swim
  when moved or shrunk; the surface mesh is one identity instance and its wood grain
  currently stays in world space while the square slides (ADR-0021).
- Saturated colour is reserved for geometry that is not flat: seams, wrapped moves,
  extra axes. The shell itself is cool and neutral, so spending a seam hue on a button
  breaks the one rule a player learns by playing.
- **A seam is coloured by the portal it belongs to, not by being a seam.** Both ends of
  one identification share a colour off a hue ramp, so a cylinder reads as one gradient
  repeated and a Moebius band as one folded - and brightness, not hue, distinguishes two
  portals that a twist puts on the same face. `src/view/seams.cpp`.
- **`Move::captureCell` is not `Move::to`.** They differ for exactly one move - en
  passant, where the square landed on is empty - and reading `to` to decide whether a
  capture atom applies made the move look illegal. That left the animation with a route
  of no steps, a one-point polyline, and a crash. `src/view/move_anim.cpp`.
- The three interface typefaces come from the flake as `CB_FONT_DISPLAY`,
  `CB_FONT_BODY` and `CB_FONT_MONO`, and are copied beside the binary by
  `src/render/CMakeLists.txt`. Chakra Petch is not in nixpkgs and is fetched by hash at
  a pinned commit. A missing font is never fatal - the interface falls back and looks
  plainer.
- **Audio is interactive only, and a missing device is never fatal.** `audio::Mixer::create`
  is called solely by the interactive `main` (M18.5); `--shot`/`--clip` never reach it, so a
  capture cannot require an audio device - and `audio::play` with no sink installed is a
  no-op, which is the state every unit test runs in. The mixer logs and returns null when
  SDL has no device, and the game plays on in silence. Off really is off: a gain of zero
  skips the buffer rather than scaling it, and the synthesis normalises each effect to a
  fixed peak so nothing clips.
- **Every hardcoded pixel size in the interface goes through `Ui::px()`.** A missed one
  scales the text and leaves the panel behind. `widgets::menuEntry` takes its scale as a
  required argument for exactly this reason - a default let four menu rows draw at the
  wrong size and the menu looked ragged.
- Draw overlays from `ImGui::GetItemRectMin()`, never from the cursor position captured
  before the item: they are not always the same.
- The seam rim is a constant *world* width, derived in the shader from the instance's
  scale. A fraction-of-the-box band turns the board's plinth into a solid orange slab.
- Debug is pinned to `-O0` because nix injects `-O2`; a Catch2 `[.]`-hidden test still
  runs if a filter matches any of its other tags.
- **An overture is a pure function of `t`.** `overtureScene(which, t, intro, theme)` reads
  no clock and no previous frame, which is what makes reverse playback free - the player
  counts `t` down - and a screenshot reproducible. Anything that needs to remember
  something between frames belongs in `app::OverturePlayer`, not in a scene.
- **`--shot` hands the interface a fixed timestep on purpose**, and the interface hands
  that same timestep to the overture as its dt. `setProgress` therefore *pins* the player
  so the capture stays where it was put; pinning without holding put `--screen newgame`
  almost half a cycle past the `--t` it was given.
- A Klein bottle cannot be closed with a circular cross-section. `klein` glues its ranks
  with the file reversed, and a half-turn brings a circle back *shifted*, not reflected -
  so `kleinSurf` pinches the cross-section into a figure-eight, for which a half-turn
  *is* the reflection. `tests/render/test_overture_scene.cpp` states each variant's own
  gluing as arithmetic, which is the test that caught it.
- `ImDrawList::AddCircle` with `num_segments = 0` divides by a tessellation table that is
  zero-filled until `NewFrame` has run - so it is an integer divide by zero in a headless
  draw list. Always state the segment count.
- **The move camera reads the trace, never the topology.** `view::moveCamera`
  (`src/view/move_camera.cpp`) is a pure function of `MovePath`/`Placement`/`ViewConfig`/
  `CameraPolicy`/`t` - no `Coord`, no dimension count. `Session::camera()` folds it in as
  an offset so the board, the labels and the pick ray agree; picking is locked while a
  shot is in flight, and `clickPixel` uses the *effective* camera, not the raw member, or
  the pause pull-back desyncs it. A camera choice is `CameraPolicy` data, never a branch
  on `dims`. See `docs/camera.md`.
- A capture's `--clip`/`--shot` path is settled by **two thrown-away frames** before the
  real ones: the first lets a screen change reset the pane transition (after the tick),
  the second advances it to done. With one, the library draws at alpha zero; with the
  pane left unsettled the frames come out blank.
- **The geometry view is the renderer drawing the board on its surface (M17).** A glued
  2-D variant with `Settings::geometryView` on builds its cells from `render::PlaySurface`
  and draws them as ordinary instances in `BoardRenderer::buildInstances` - so the
  session's own camera, depth buffer and MSAA apply, and orbit and zoom work with no
  special path. `render::hasPlaySurface` is the gate: only a glued 2-D variant has a
  surface to become, so nothing else offers the toggle. Picking ray-tests the *same*
  sampling the instances came from, so a click can never select a different cell than the
  one under the cursor. `standard --geometry` is a strict no-op. Read ADR-0019 before
  touching any of it; each bullet below was a bug first.
- **A square on a surface is a patch *of* it, never a rectangle placed on it.** A flat
  tile is tangent at one point, so on anything that curves fast the squares cut into each
  other at one end and stand off it at the other - fish scales, not a board. Each cell is
  built as a grid of corners lying on the surface, with thickness, and the whole board
  goes down as **one mesh** with its colours in its vertices (`MeshVertex::color`, white
  on every authored shape). Pieces stay instanced: a piece *is* an object standing on the
  surface, and it keeps the seat's frame - +X along the files, +Y across the ranks, +Z
  out - as a quaternion.
- **An instance's scale is applied in the mesh's own frame, before its orientation.**
  Scaling after a rotation scales the *world* axes; the two agree only where the
  orientation is identity, which is the whole of the flat board and none of the pieces
  standing on this one.
- **The pose is `SurfacePose`, and it is presentation only** - it never enters
  `VariantId`. `slideU`/`slideV` move the board *along* its surface (middle-drag on the
  shape: left/right along the ranks, up/down along the files; `--slide`/`--slide-v`):
  sliding one cell along the files is sampling at `u + 1/nx`, so a1 lands exactly where b1
  was and a lap of a Moebius band comes home mirrored. The wrap is `fmod(s, 2 * nx)` in
  *cells*, in `PlaySurface` alone - `Settings` does not know the board's extent (M17.9).
  `evert` **swaps the outward side**: past the halfway point the normal is reversed, so the
  pieces stand on the other face while the squares do not move. It is *not* the geometric
  eversion the surface functions carry - that mirrors the whole board about the origin and
  lands every cell somewhere else, which is not a board (M17.7, revised). The `INVERT`
  button and `]`/`[` set a **target** (`Settings::geometryInvert`) the front end eases
  `geometryEvert` to, so the pose stays a pure function of its number.
- **"Outward" is the side away from the shape's axis, not the formula's sign.**
  `cross(dv, du)` alone is not enough once the parametrisation is posed, so `PlaySurface`
  precomputes the cross-section centroid per rank and flips the normal to point away from
  it; the invert then negates that (M17.7).
- **The geometry view is framed with `headroom = 0`.** `OrbitCamera::frame` lifts the
  look-at for piece crowns, but `PlaySurface::bounds()` already pads for the pieces, so
  the default headroom double-counts and drops the shape ~30 px down the window.
  `Session::frameOn` takes a `headroom` for exactly this (M17.11).
- **A ghosted board is a second pipeline, not an alpha on the existing one.** `GHOST`
  (`Settings::geometryGhost`, `--ghost`) puts the board mesh's alpha in
  `MeshVertex::color.a` and draws it, after the opaque pieces, with `blendEnable` and
  `depthWriteEnable = false` (`BoardRenderer::surfaceBlendPipeline_`). The pieces stay
  opaque. Alpha 1 is bit-identical to the opaque path (M17.10).
- **Shapes above two dimensions are authored and keyed by name, and cannot be faithful.**
  `playShapePosition` gives `torus3d`'s nested shells and `hyper4`'s tesseract; they are
  *not* read off the gluing (`hyper4` has none), so they are keyed by the variant's name,
  unlike the derived 2-D surfaces. A `d > 3` manifold cannot embed in 3-D, so these are
  self-intersecting immersions: overlapping cells, hidden sheets, and axes whose adjacency
  is not visual nearness. **A faithful playable `t6` quintic is impossible** - the 6-torus
  folds ~10 lattice cells onto each drawn tile - so `t6` stays on the lattice and any
  playable quintic must be sliced. Read `docs/plan/M17.12-shapes-above-two-dimensions.md`
  before extending this.
- **A stacked (D>=3) tile's frame wraps a periodic axis and one-sides a bounded boundary,
  per axis.** Clamping a periodic neighbour returns the cell's own position and scatters
  the frames as fish scales; halving a bounded boundary's one-sided step draws it half
  size. `PlaySurface::buildStacked` chooses wrap vs one-side from the geometry's
  `boundaryKind` (M17.13).
- **A move on the shape is sampled, not teleported.** `render::surfaceMoveSample` walks the
  seats `view::MoveAnimation::path()` names (a leap arcs, a glide hugs; a seam step is not a
  cut). Following it is **one continuous chase** - `render::surfaceChaseCamera` centres the
  piece and sits the camera behind it along the travel; `Settings::followUpright` rolls the
  frame so the piece's own up (its surface normal) is the view's up, or leaves it unrolled so
  the piece rides the shape. `OrbitCamera::roll` is applied in `viewProj` *and* `pickRay`
  (the one `cameraBasis`), or the draw and the pick disagree; it is 0 for every ordinary view.
  **Anti-clip is on by default** (`Settings::geometryAlign`): `render::alignSlideU` searches
  slide offsets that keep the followed cell facing the camera with nothing of the shape
  between (`PlaySurface::blocked`), so a piece on a torus's inner ring is turned to the
  outer side rather than the camera clipping through the tube. `torus`/`klein` only
  (M17.15-M17.17).
- **A normal field is built by a continuity walk, not a per-point test.** On a non-orientable
  shape one flip is unavoidable; `PlaySurface::build` seeds file 0 against the cross-section
  centroid and then makes each next sample agree with its neighbour, never closing the loop,
  so the flip lands at the seam instead of at the Klein cross-section's pinch, where the
  centroid test divides by ~zero (M17.18).
- **An 8-cell figure-eight is aliased, and that is why `lemniscateAngle` exists.** Equally
  spaced values of the Klein cross-section's angle land in pairs, which made every other
  file twice the width of its neighbour. The curve is walked at constant speed instead;
  the reparametrisation is odd in the angle, which is what keeps the rank seam's file
  reversal closing.
