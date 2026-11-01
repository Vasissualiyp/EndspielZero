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
| `get [<setting>]` | Print all settings, or one |
| `set <setting> <value>` | Change one setting. The value goes through the settings-file parser, so it accepts the same syntax as the file |
| `post [on\|off]` | Turn post-processing (HDR scene, emissive bloom, tone map, grain/dither; ADR-0025) on or off; no argument toggles it. Default on; works without a loaded game. Also `get`/`set postProcess` or `post_process`, and `bloomScale` or `bloom_scale` (finite, clamped to 0..4, multiplies the theme's bloom; 0 skips the bloom passes) |
| `dev [on\|off]` | Show or hide developer presentation (fps and unfinished main-menu entries); no argument toggles it. Default off. Also reachable through `get`/`set devOverlay` or `dev_overlay` |
| `boot` | Replay the board's power-on sequence (M23.10 section 3) from now; needs a loaded game. The front end stamps it on its next game-screen frame. The first click or key completes it |
| `info` | Variant summary: axes, cells, pieces and their atoms, mark tags, compounds, castles |
| `fen` / `setfen <fen>` | Show or replace the position |
| `place <cell> <symbol>` | Put a piece on a cell (`Q` white, `q` black) |
| `remove <cell>` | Clear a cell |
| `side w\|b` | Set the side to move |
| `moves` | List the legal moves |
| `play <move>` | Play one move (`e2e4`) |
| `undo` / `reset` | Take back one move / restart the game |
| `script <text>` | Run click-script lines, the same language as `--script` |
| `perft <n>` / `divide <n>` | Count nodes / split the count by first move |
| `hash` | Position hash and its recomputation, to catch incremental-hash drift |
| `marks` | Marks on the board, with time to live and owner |
| `unmoved` | Which pieces still count as unmoved |
| `attacked <cell>` | Whether the side not to move attacks the cell |
| `moveset <text>` | Evaluate moveset text against this board |
| `expr <s-expression>` | Evaluate a rule expression against the position |
| `clear` | Empty the scrollback |

Every command except `help`, `load`, `goto`, `get`, `set`, `dev` and `clear` needs a running
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
  - `--screen <name>` picks the screen.
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
  - `--dev` forces developer presentation on for this run without saving it.
  - `--post 0|1` forces post-processing off or on and `--bloom <0..4>` scales its bloom, for
    this run only. Use `--post 0` for A/B captures and as the recovery path if the device
    cannot run post (it is reported once on stderr and the legacy path is used).
  - `--boot-t <s>` renders the board as if its boot sequence were `s` seconds old (0 to 1.8;
    later is settled), for captures. Run-only.
  - `--theme <name>` picks the palette.
  - `--matte` turns materials off.
  - `--ui-scale N` sets the interface scale.
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
