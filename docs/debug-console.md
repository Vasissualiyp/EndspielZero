<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Debug interface

There are two ways to reach a feature that the ordinary interface has no button for: the
in-game **debug console** and the GUI's **command-line flags**. Both are developer
tools. They are functional rather than pretty, and players never need them.

**Rule:** every important feature gets a way in through one of these. Usually that is a
console command, or a setting reachable through `get`/`set`. When a feature needs to be
captured headlessly, it also gets a CLI flag. See AGENTS.md, "Debug access".

## The console

Open and close it with **F12**, or with **`` ` ``** when no text field has focus. It is
drawn by `Ui::drawDebugConsole` (`src/render/ui.cpp`). Commands are parsed and run by
`app::DebugConsole` (`src/app/debug_console.{hpp,cpp}`). The console owns no window, so
tests drive it directly (`tests/unit/app/test_debug_console.cpp`). The up and down arrows
walk the command history. `help` (or `?`) prints the command list from
`DebugConsole::help()`, which is the authoritative list. Keep it and this page in step.

| Command | What it does |
|---|---|
| `load <variant>` | Start a variant from `variants/` |
| `goto <screen>` | Switch to `menu`, `game`, `paused`, `settings`, `editor`, `newgame` or `welcome` |
| `transition <from> <to>` | Preview a screen move (M23.11 section 6) as if arriving from `<from>`: `menu`, `newgame`, `watch`, `welcome`, `settings`, `quit`, `info`, `pieces`, `game` or `paused`. `menu`/`newgame`/`watch` to `game` traces the menu off, `game` to `paused` traces the pause panel on, `paused` to `game` traces it off, `game`/`paused` to `menu` powers the board down for 0.8 s then traces the menu on. With the lab on, menu-to-menu changes fly between their camera stations over 0.6–1.1 s, collapse/grow the projector subject, and transfer object salience. Any key or click skips it; at calm intensity it is one 0.3 s cross-fade |
| `get [<setting>]` | Print all settings, or one |
| `set <setting> <value>` | Change one setting. The value goes through the settings-file parser, so it accepts the same syntax as the file |
| `post [on\|off]` | Turn post-processing (HDR scene, emissive bloom, tone map, grain/dither; ADR-0025) on or off; no argument toggles it. Default on; works without a loaded game. Also `get`/`set postProcess` or `post_process`, and `bloomScale` or `bloom_scale` (finite, clamped to 0..4, multiplies the theme's bloom; 0 skips the bloom passes) |
| `get`/`set presentationIntensity` | The board's presentation intensity (`presentation_intensity`, persisted, clamped to 0..1, default 1): the glitches, the boot sweep, the check ripple and the end power-down. Below one half every effect takes its calm form |
| `station <name>\|off` | Pin the lab camera to a loaded station; `off` restores the screen station and camera flight. `--station` takes precedence for captures |
| `salience <focus>\|off` | Pin one lab object focus at full glow: `projector`, `magazine`, `bench`, `door`, or an authored focus name; `off` restores screen-driven easing |
| `lights` | List the loaded lab lights with kind, position in metres, radius, authored intensity and group; prints `lab is off` when disabled; needs no game |
| `get`/`set roomLight` | Practical room-light gain (`room_light`, persisted, finite 0..2, default 1); machine lights keep their authored intensity |
| `projector [on\|off]` | Toggle the bench hologram; works without a game. Also `get`/`set projector` (persisted, default on) |
| `implant [on\|off]` | Toggle organic menu formation, dissolve, afterimage and saccadic dimming on the lab path. Also `get`/`set implantFx` (`implant_fx`, persisted, default on). Violet-white text stays when effects are off |
| `heartbeat [on\|off]` | Toggle the implant's faint double pulse (`heartbeat`, persisted, default on interactively). Calm intensity always suppresses it |
| `living [on\|off]` | Living vision on the lab menu (M24.4c): lens warp, gaze, focus shift, depth of field (`livingVision`, persisted, default on) |
| `tremor [on\|off]`, `wander [on\|off]` | Run-only: the 0.8 px glow tremor and the idle daydream: after 10 s without input the viewport defocuses and a sharper spot drifts between fixations (M24.4d; captures default both off) |
| `wake` | Replay the launch waking-up sequence (M24.7): eyelids, two blinks, projector power-on, the implant handshake. Needs the lab, post and implant on; any key or click skips it. `get`/`set wakeOnLaunch` (`wake_on_launch`, persisted, default on) turns the launch sequence off |
| `handshake` | Replay only the implant handshake (phosphene mapping, one front, the menu locking in); run-only |
| `gaze` | Print the gaze point, `uiFocus`, focus distance, `daydream` amount and target. `gaze <x> <y>` pins it (fractions of the viewport), `gaze ui\|room` pins menu or room focus, `gaze target <key>` fixates a `[[gaze]]` target of `lab.toml`, `gaze off` releases |
| `warp <k>\|default` | Override the lens warp strength (default 0.030) |
| `lab [on\|off]` | Toggle the static lab behind menu screens (default off); works without a game. Also `get`/`set labScene` or `lab_scene` |
| `dev [on\|off]` | Show or hide developer presentation (fps and unfinished main-menu entries); no argument toggles it. Default off. Also reachable through `get`/`set devOverlay` or `dev_overlay` |
| `boot` | Replay the board's power-on sequence (M23.10 section 3) from now; needs a loaded game. The front end stamps it on its next game-screen frame. The first click or key completes it |
| `ripple [<cell>]` | Play the check ripple (M23.10 section 4) from `<cell>` (default: the royal of the side to move) without touching the game. It wraps through glued seams. Calm intensity and non-LED themes have none |
| `seamhover <axis>\|off` | Pin the seam-legend highlight on a glued drawn axis by name (e.g. `file` or `rank`); `off` restores mouse hover. That axis brightens, other glued seams dim, and mirrors stay unchanged. Requires a game |
| `hudfx [turn\|move\|think\|all]` | Preview the live HUD feedback (M23.11 section 4) without touching the game: `turn` replays the status panel's turn flip, `move` the new ledger row, `all` (default) both; `think` toggles the engine-thinking indicator on and off. The check text is timed by `ripple`. At calm intensity (below one half) these change instantly |
| `endfx win\|draw\|off [white\|black]` | Preview the game-end power-down (section 5): `win` is a decisive end with `<side>` (default white) as the loser, `draw` a draw, `off` clears it. Ages count from the decision: nothing moves for the first 3 s, then a slow power-down. The LEDs reach the hold by about 9 s; then the pieces shut down one by one, the winner's king last, and by about 12–14 s (more for big armies) only the standby LED is lit. The game itself is untouched; a real end plays by itself, and an undo out of one powers the board back up |
| `info` | Variant summary: axes, cells, pieces and their atoms, mark tags, compounds, castles |
| `fen` / `setfen <fen>` | Show or replace the position |
| `place <cell> <symbol>` | Put a piece on a cell (`Q` white, `q` black) |
| `remove <cell>` | Clear a cell |
| `side w\|b` | Set the side to move |
| `moves` | List the legal moves |
| `play <move>` | Play one move (`e2e4`) |
| `undo` / `reset` | Take back one move / restart the game |
| `script <text>` | Run click-script lines, the same language as `--script`. `script orbit 1.2 -0.35` orbits by yaw/pitch in radians; use with `post on` / `post off` to inspect the floor glow at different angles |
| `perft <n>` / `divide <n>` | Count nodes / split the count by first move |
| `hash` | Position hash and its recomputation, to catch incremental-hash drift |
| `marks` | Marks on the board, with time to live and owner |
| `unmoved` | Which pieces still count as unmoved |
| `attacked <cell>` | Whether the side not to move attacks the cell |
| `moveset <text>` | Evaluate moveset text against this board |
| `expr <s-expression>` | Evaluate a rule expression against the position |
| `clear` | Empty the scrollback |

Every command except `help`, `load`, `goto`, `get`, `set`, `dev`, `post`, `lab`, `lights` and `clear` needs a running
game, and says so if there is none.

### Adding a command

1. Add a branch in `execute` in `src/app/debug_console.cpp`. Commands that need a game go
   after the `needGame()` guard.
2. Add one line to `DebugConsole::help()`, padded to the existing column.
3. Add a test in `tests/unit/app/test_debug_console.cpp`.
4. Add a row to the table above.

When a feature is a persisted preference, prefer a `Settings` field over a new command,
because `get`/`set` already reach every field. Add a command only when the action is not a
setting (replaying an animation, dumping state) or when a short alias is worth it.

## Command-line flags (GUI)

`./build/dev/src/gui/endspielzero_gui [variant] [flags]`. Run it from the repo root under
`nix develop .#gfx`. `--help` prints the full list (parsed in `src/gui/main.cpp`). The
groups:

- **Headless capture:**
  - `--shot <file.ppm>` renders one frame and exits. It exits nonzero on validation
    errors.
  - `--clip <dir>` writes a frame sequence, controlled by `--frames N`, `--t0`, `--t1`
    and `--fps`.
  - `--width` and `--height` set the frame size.
  - `--trailer-clip <dir>` and `--trailer-frames` render the trailer.
- **What to show:**
  - `--screen <name>` picks the screen (`pausequit` is the pause menu's quit prompt).
  - `--from <screen> --transition-t <s>` renders the screen move from `menu`, `newgame`, `watch`,
    `game`, `pause`, `welcome`, `settings`, `quit`, `info` or `pieces` to `--screen`, `s` seconds into it (M23.11 section 6; past the
    move's end it is exactly the plain frame). `--presentation <0..1>` sets the presentation
    intensity for the run, so `--presentation 0` shows the calm cross-fade. Example:
    `--screen menu --from pause --transition-t 0.4` (the board powering down).
  - `--script <lines>` replays clicks (`click e2`).
  - `--ui-click` presses an interface control.
  - `--play <file.cbgame>`, `--play-at S`, `--dwell S` and `--watch` replay a saved game.
- **Time:**
  - `--t S` sets the screen or overture time.
  - `--visual-time S` sets the explicit presentation seconds for holograms and LEDs.
  - `--move-t P` pins the move animation's progress.
- **Board view:**
  - `--geometry` shows the board as its shape.
  - `--flat` shows the 2-D token view.
  - Shape controls: `--formed`, `--evert`, `--slide`, `--slide-v`, `--ghost`,
    `--piece-cylinders` and `--dims`.
- **Camera:** `--cinema`, `--follow <mode>` and `--shape-follow chase|turntable`.
- **Look:**
  - **Lab:**
    - `--lab 0|1` overrides the static menu room for this run. Captures default off,
      including with saved settings; use `--lab 1` to opt in.
    - `--station <name>` requires `--lab 1` and shows the room from a named station
      regardless of screen: menu, tutorial, library, watch, settings, quit or sky (looks up through the skylight).
    - Console `station <name>|off` pins a station; `salience <focus>|off` pins its object glow.
      Menu screen changes fly between stations. `--from <screen> --transition-t <seconds>`
      previews the flight; with `--clip`, `--t0`/`--t1` sweep transition age per frame.
    - `--projector 0|1` overrides the bench hologram for this run. The menu and
      tutorial project the manifold; Settings projects a tesseract; Library projects
      the selected overture. Drag on the left to turn it about vertical; lab drag
      elevation is ignored. `projector on|off` updates the projector setting.
    - `--implant 0|1` overrides the implant effects for this run. `--panel-t <seconds>`
      pins panel record ages during a station preview, including after the camera settles.
      `--living 0|1`, `--tremor 0|1`, `--gaze-wander 0|1`, `--gaze X,Y`, `--gaze-ui 0|1`, `--gaze-target KEY` and `--warp K` drive living vision (M24.4c); `--daydream-t <s>` pins the eye `s` idle seconds into its daydream and `--daydream-wake <s>` then moves the mouse and steps `s` more seconds (M24.4d); `--heartbeat 0|1` overrides the double pulse; captures default it off. `--wake 0|1` overrides the launch sequence; `--wake-t <seconds>` pins its age for a capture (captures default to no wake); `--handshake-t <seconds>` pins only the handshake's age (M24.7). The renderer
      keeps the hardware palette; only the lab menu UI uses the synaptic hue.
      With `--lab 1`, `--screen settings` enters Settings from the menu.
    - `--overture-t <0..1>` pins Library progress for captures (alias for `--t`).
    - `--room-light <0..2>` overrides the room practicals for this run only; machine
      lights remain unchanged. Console `get`/`set roomLight` saves the gain; `lights`
      lists the authored lights.
    - Console `lab [on|off]` toggles it; `get`/`set labScene` (alias `lab_scene`)
      reaches the saved preference. Room parts, anchors and stations live in
      `assets/lab/lab.toml`. OBJ parts use metres, Z up and an authored origin;
      their size is a multiplier, while procedural parts use full extents.
  - `--seam-hover <axis>` pins the seam-legend highlight by axis name for this run (e.g.
    `klein --seam-hover file`). An unknown or unglued axis exits with an error.
  - `--dev` forces developer presentation on for this run without saving it.
  - `--post 0|1` forces post-processing off or on and `--bloom <0..4>` scales its bloom, for
    this run only. Use `--post 0` for A/B captures and as the recovery path if the device
    cannot run post (it is reported once on stderr and the legacy path is used).
    The board's floor glow is dithered in both paths: post on applies TPDF once after
    the float scene, post off applies a fixed-seed ±1 LSB TPDF to the backdrop itself.
    Post's intentional grain is separate from this quantisation dither.
  - `--boot-t <s>` renders the board as if its boot sequence were `s` seconds old (0 to 1.8;
    later is settled), for captures. Run-only.
  - `--check-t <s>` renders the check ripple at age `s` (0 to 0.9) from the last check the
    `--script` gave (or from the side to move's royal when it never gave one); `--end-t <s>`
    renders the game-end power-down at age `s` counted from the decision (the board is untouched
    for the first 3 s); the LEDs reach the hold by about 9 s; then the pieces shut down one by one, the
    winner's king last, and by about 12–14 s (more for big armies) only the standby LED is lit. Example: `--theme manifold --script "..." --end-t 0.7`.
  - `--event-t <s>` renders the HUD turn flip (0 to 0.25) and the newest ledger row's reveal (0 to
    0.2) at age `s`; `--think-t <s>` shows the engine-thinking indicator at age `s` (the capture has
    no search to wait for); `--check-t` also times the status panel's CHECK spike (0 to 0.3).
    Example: `--script $'click e2\nclick e4' --event-t 0.1`.
  - `--theme <name>` picks the palette.
  - `--matte` turns materials off.
  - `--ui-auto` enables framebuffer-height scaling for captures (off by default for
    `--shot`, `--clip`, and `--trailer-clip`). `--ui-scale N` is exact and takes precedence
    over `--ui-auto`, in either order. Interactive sizing defaults to automatic: use
    `set guiScaleAuto false` (alias `gui_scale_auto`) to disable it. `guiScale` (alias
    `gui_scale`) is the multiplier. The automatic factor is height / 720 rounded to the
    nearest quarter step and clamped to 1..3; 1080p uses 1.5, 1440p 2, 2160p 3. Resizing
    rebuilds fonts only when that effective factor changes.
  - `--hud-framing 0|1` toggles the HUD's camera safe area for this run. The viewport
    always stays full-screen. Console `get`/`set hudFraming` (alias `hud_framing`) controls
    the persisted preference. Off preserves the original camera for material A/B tests.
  - `--capture-t S` pins the newest captured tray icon's bottom-to-top scan, over
    0..0.35 seconds. Ordinary captures stay settled; interactive moves use their capture
    event's visual timestamp. Console `play <move>` reaches the same capture event.
  - `--user-settings` uses the player's saved settings instead of defaults.
- **Other:**
  - `--variants-dir <dir>` and `--piece` set where variants and pieces come from.
  - `--trailer-edit` opens the trailer's edit mode.
  - `--bench-frame` measures frame time.

AGENTS.md "Commands" has worked examples.

### Capturing on this machine

Running the binary directly can fail with a `GLIBC_2.43` error that comes from the alsa
entry in `LD_LIBRARY_PATH`. Run it under `nix develop .#gfx` with that entry removed. On a
machine with no GPU, also point `VK_DRIVER_FILES` at the Nix Mesa lavapipe ICD.

M23.11 result panel: `result` opens the current decided game's result immediately.
Esc or the close button collapses it to RESULT in the status panel; RESULT reopens it.
REMATCH uses Reset and boots the board; MENU returns to the main menu. Undo removes
an ended result. For a deterministic capture use `--result --end-t 9` (or 12) after a
mating script. Without `--result`, existing captures retain their panel-free conditions.

M23.11 review: `review [<ply>|off]` browses the game's history without touching it (no ply
starts at the end; `off` returns to the final position). Left/Right step, Home/End jump,
Esc leaves; while reviewing the board takes no move and the dock offers the step buttons
and EXIT REVIEW. A clicked ledger row jumps to it. REVIEW is also on the result panel, and
is hidden for a game that cannot be replayed (FEN-started or temporal). Capture a review
frame with `--script "$MATE" --review 2 --end-t 9`; in a script, `review 2`, `review next`,
`review prev`, `review off`.

### M24.4c-3d: the library wall projector and rack

With `lab on`, `goto menu` shows the larger bench manifold; `goto newgame`
selects the wall projector and slides out the selected variant's rack slabs (one
for standard, five for cube5, eight for hyper4). `projector off` disables either
projector. `transition menu newgame` and `transition newgame menu` preview the
handoff, with each subject collapsing into its own lens. Capture these with the
existing `--lab 1 --screen newgame --from menu --transition-t <seconds>`;
`--overture-t` still pins the selected variant's formation. Slab motion uses the
same explicit transition age, and calm presentation snaps the slabs to their targets.
