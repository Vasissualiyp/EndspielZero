# Polish backlog

Findings from a read of the repo on 2026-09-29, at `ac49c66`. Ordered by
(impact ÷ effort), not by area. Everything here is *polish* — it makes the game
feel finished — as distinct from the M7–M10 milestones, which add capability.
Each item names the file it lands in.

Nothing here contradicts an ADR. Where an item reverses a decision that was made
deliberately and recorded in a comment, that comment is quoted, because the
reason it was made is also the reason it is cheap to finish.

---

## Tier 1 — settings that lie

Three options in the settings screen are read from disk, written back to disk,
and consumed by nothing. A player toggles them, the file changes, the game does
not. That is worse than not offering them: it teaches the player that the
settings screen is decorative.

### 1. `showCoordinates` draws nothing

`src/app/settings.hpp:36` declares it, `src/render/ui_menus.cpp:828` offers the
checkbox, `src/app/settings.cpp:129` persists it — and no renderer or interface
code ever reads it. There are no coordinate labels anywhere in the build:

```
$ grep -rni "coord\|label" src/render/board_renderer.cpp src/render/ui.cpp
# nothing that draws a rank or file name
```

**Fix, small:** an ImGui overlay is enough and stays below the renderer's
concerns. `view::layout` already projects a cell to screen space and
`cellName(d, c)` (`src/io/notation.hpp`) already produces `e4` on a 2-D board
and the tuple form elsewhere. Label the edge cells of the on-screen slice in
`t.boneFaint`. Honour the setting.

**Fix, better:** label the *axes* too, not just the files and ranks. On `hyper4`
a player cannot tell which of the four axes runs across the screen without
opening the axes control and reasoning about it. A one-line legend —
`file → · rank ↑ · level ⊞ · w ⊟` — answers the question a 4-D board raises
first.

### 2. `vsync` is hardcoded

`src/render/window.cpp:102` pins `VK_PRESENT_MODE_FIFO_KHR` with a comment that
is correct about *why* FIFO is the right default, but the setting still exists at
`src/render/ui_menus.cpp:796`.

**Fix:** either query `vkGetPhysicalDeviceSurfacePresentModesKHR` and select
`IMMEDIATE`/`MAILBOX` when the setting is off (swapchain rebuild already exists
in `Window::buildSwapchain`, so this is a parameter, not a new path) — or delete
the option and say in the settings screen that the game is always v-synced
because it draws on input. Both are honest; the current state is not.

### 3. `confirmMoves` never confirms

`src/app/settings.hpp:57` — "Ask before playing a move rather than playing it on
the second click." `app::Session` has no such state. The second click plays.

**Fix:** the machinery is already there in a shape that generalises — the
`PendingPromotion` pattern (`src/app/session.hpp:129`) is exactly "a move the
player has committed to except for one thing", and it already freezes the board
and is already driven by the scripted-action tests. Add a `PendingMove` beside
it and a `confirm` / `cancel` action, so the confirmation path is testable
headlessly like everything else in that layer.

### 4. `Theme::console()` is unreachable

`src/view/theme.hpp:110` ships a second complete palette. Nothing constructs it:
all three holders (`ui.hpp:135`, `board_renderer.hpp:144`, `session.hpp:165`)
hardcode `manifold()`. The settings screen has no theme row.

**Fix:** a `theme` string setting beside `pieceIcons` — the same shape, chosen
for the same reason ("a name rather than an enum because this layer is below the
renderer"). It costs one setting and one switch, and it turns dead code into a
feature. `tests/unit/view/test_theme.cpp` already pins the contrast invariant
for both, so a third palette later is data.

---

## Tier 2 — the moment a move is played

This is the most-looked-at half-second in the game and three details in it are
unfinished.

### 5. A captured piece vanishes before it is captured

`Session::playChecked` (`src/app/session.cpp`) calls `game_->play()` and *then*
starts the animation. `refreshSnapshot()` therefore removes the victim at frame
zero, so the player watches a piece slide gracefully across a portal and off a
wall towards a square that is **already empty**. The capture — the thing that
actually happened — is the one part with no animation at all.

**Fix:** the renderer already knows how to suppress a piece for the duration
(`board_renderer.cpp:665` skips the mover at `anim->travellingTo()`). Add the
mirror of it: carry the captured piece on the animation, draw it at
`Move::captureCell` until the mover's path reaches it, then sink or dissolve it
over ~80 ms. Note that this must read `captureCell`, not `to` — the gotcha
already recorded in AGENTS.md about en passant applies here verbatim, and this
is the second piece of code to trip on it.

### 6. Nothing marks a rule effect firing

`atomic` detonates a 3×3 neighbourhood; `charged` displaces; `mustcapture`
constrains. On screen, the pieces are simply gone next frame. The rule VM knows
exactly which cells it touched — `rules::Outcome` and the effect execution in
`src/rules/vm.cpp` — and the view has no channel to hear about it.

**Fix:** return the touched cell set from the engine alongside the outcome and
let `MoveAnimation` flash it. This is the single highest-impact visual item in
the list: the variants that the rule VM exists *for* are currently the ones that
explain themselves least. Warm, not cyan — an explosion is a game event, not
geometry, and the AGENTS.md colour rule is load-bearing.

### 7. `MovePath::unexplained` glides silently

`src/view/move_anim.hpp:48` — "True when nothing in the variant explains the
move, which is what a castle or a rule-effect displacement looks like from
here." A castle therefore animates as a king gliding two squares while the rook
teleports. Trace the second piece too: the move carries enough to find it, and a
castle is the one move in standard chess that a new player most often gets
wrong.

---

## Tier 3 — feedback the mouse should be giving

### 8. No hover

`src/gui/main.cpp:389` handles `SDL_EVENT_MOUSE_MOTION` only for panning and
orbiting. Nothing highlights the cell under the cursor. On a flat 8×8 that is
survivable; on `cube5` or a Klein bottle, where the cell under the pointer is
genuinely ambiguous, it is the difference between playing and guessing.

**Fix:** `Session::clickPixel` already resolves a pixel to a cell exactly and
headlessly (ADR-0011 is explicit that CPU picking was chosen partly for this).
Add `hoverPixel` with the same body and a `hovered_` cell, and give it a faint
rim in the renderer. The same call should drive the cursor shape: a pointer over
a movable piece, an arrow over a legal target, the default elsewhere.

### 9. No drag-and-drop

Click-then-click is the only way to move. Drag is what most players reach for
first, and it costs one more state on the session plus the hover resolution from
item 8.

### 10. Window is anonymous and forgets itself

`src/gui/main.cpp:295` opens `"EndspielZero"` at a fixed 1440×900 forever. The title
never names the variant or whose move it is; the size and position are not
persisted; there is no window icon.

**Fix:** `SDL_SetWindowTitle` on every variant change (`EndspielZero — klein`) is
two lines. Persisting `windowWidth` / `windowHeight` / `maximized` in `Settings`
is the same shape as `fullscreen`, which is already there and already honoured
at `main.cpp` startup.

---

## Tier 4 — the game around the game

### 11. A game cannot be saved

`AGENTS.md` lists `src/io` as holding "variant TOML loader, notation, FEN-N,
ASCII board, **replay**" and the roadmap's exit gates require "replay corpus
reproduces byte-identical hashes". There is no replay module in `src/io`, and
the only persistence a player has is `lastVariant`.

The format, though, already exists and is already tested: `applyScript` /
`parseAction` (`src/app/session.hpp`, `session.cpp`) is a complete, replayable
action log, and its docstring says so — "Written for tests, and equally the
basis of a replay format."

**Fix:** `src/io/replay.{hpp,cpp}` — variant name, FEN-N start position, and the
action lines. Save and load from the pause menu. This is small because the hard
part was done for the tests, and it unlocks the determinism gate the roadmap
already claims, plus shareable positions, plus the M8 lockstep verification.

### 12. No clocks

`AGENTS.md` lists clocks in L80 `src/game`; `EndReason::DrawClock` refers to the
variant's halfmove limit, not a wall clock. Grep finds nothing else. Either
build them (a `game::Clock` with increment, per-side, paused with the shell —
note the no-floats rule in core, so use integer milliseconds) or strike the word
from the layer table. The doc and the code should not disagree about whether a
subsystem exists.

### 13. No resign, no draw offer, no claim

A finished game arrives only through adjudication. There is no way to concede
and no way to agree. Cheap in `Game` (`GameResult` and `EndReason` both have
room — add `Resignation`, `Agreement`), and needed before M8 puts two humans in
one game.

### 14. The move list is six lines deep

`buildGameHud` shows "the last handful of moves, not a ledger", and the comment
is right that the full history "belongs on a screen someone opened on purpose".
That screen does not exist yet. `Screen::GameInfo` and `Screen::PieceMoves` show
what the *variant* is; nothing shows what this *game* has been. Add
`Screen::History` beside them — it reuses the pause frame and `moveText`, and it
is where a save/load button naturally lives.

### 15. Notation is long-algebraic only

`moveText` produces `e2e4` (`src/io/notation.hpp:22`). Correct, unambiguous, and
not what any chess player reads. Short algebraic with disambiguation, `+`/`#`,
`O-O`, and `x` is a self-contained function over `legalMoves()` — and it is a
genuinely nice generalisation problem, since SAN has to degrade gracefully on a
6-D torus where "the b-file" means nothing. Keep `moveText` as the machine form;
add `sanText` for the player.

---

## Tier 5 — reach

### 16. Keyboard navigation is off by one styling detail

`src/render/ui.cpp:177` disables `ImGuiConfigFlags_NavEnableKeyboard`, and the
comment gives the exact reason: "Keyboard navigation draws a focus ring that is
larger than the item it surrounds, which makes a menu look like its rows are
different heights… it is off until the ring can be styled to match the rest."

That is a complete and actionable bug report. `FrameBorderSize` /
`ImGuiCol_NavHighlight` in `applyStyle()`, or a custom ring drawn from
`GetItemRectMin()` (per the AGENTS.md gotcha — never from the saved cursor
position), closes it. The payoff is the whole menu tree becoming reachable
without a mouse, which is the cheapest accessibility win available here.

### 17. In-game keys are three, undocumented on screen

`Esc`, `u`, `r` in game; `q` on the main menu. No `Ctrl+Z`, no arrow keys to
step history, no `f` for flat view, no `Space`. `--help` lists them; the game
does not. A keys row on the pause screen, and a `?` overlay, cost nothing and
are the first thing a player looks for.

### 18. No sound at all

`Settings` carries `volumeMaster`, `volumeMusic`, `volumeEffects` with a comment
that they are "stored and shown disabled rather than hidden, so the settings
screen does not silently change shape when sound arrives". The scaffolding is
honest and already in place, so the remaining work is a small `src/audio` layer
(L95-ish, beside `app`; SDL3 already provides the mixer) and perhaps six cues:
place, capture, portal crossing, check, game end, menu step. A board game with
no click when a piece lands reads as a prototype no matter how good the
rendering is.

### 19. Difficulty is advertised but never taught

Every variant declares a `difficulty` and a one-line `description`, and the
library sorts and colours by it. Nothing teaches the mechanic. `torus` and
`klein` are unplayable-by-feel for a newcomer even with seams drawn — the M4
status file says as much: "Playing a Klein bottle without it is genuinely hard."
A per-variant "what is different here" panel, sourced from
`docs/variants/*.md` (which already exist, one per variant, and are already
written), would close the loop between the docs and the game. `Screen::GameInfo`
is the natural home.

---

## Tier 6 — the tests that guard the look

### 20. The GUI screenshot tests assert nothing about the picture

`tests/CMakeLists.txt:54` runs nine screens through `--shot` and checks two
things: the process exits 0, and the Vulkan validation layers were silent. The
PPM it writes is never examined. Every screen in the game could render solid
black and `ctest -L gui` would stay green.

This is the weakest link in an otherwise unusually well-guarded codebase, and it
guards precisely the thing this document is about.

**Fix, in increasing order of strictness:**

1. **Cheap and robust:** assert the frame is not degenerate — a minimum count of
   distinct colours, non-trivial luminance variance, and that the board
   rectangle the screen asked for actually contains board-coloured pixels. This
   alone catches a blank screen, a panel drawn off-viewport, and a board the
   rails have covered.
2. **Palette conformance, and it enforces a real rule:** count saturated-cyan
   pixels and assert there are none on `standard` (a flat board has no seams)
   and some on `klein`. That is the AGENTS.md colour law — "saturated colour is
   reserved for geometry that is not flat" — made mechanical instead of
   remembered.
3. **Perceptual goldens:** a checked-in 16×16 average-luminance signature per
   screen with a tolerance, so a layout regression fails but a driver's
   rasterisation difference does not. Full-image goldens would be too brittle
   across GPUs; a downsampled signature is not.

`writePpm` is currently the only writer (`src/render/image_io.hpp:34`). A PNG
writer would make these captures reviewable in a browser and attachable to a
PR — worth it once the signatures above exist to point at.

---

## Suggested order

If the goal is "the game feels finished" with the least work:

1. Items **1–4** — stop the settings screen from lying. A day, and it removes
   the worst impression the game currently makes.
2. Item **5**, then **8** — the capture animation and hover. These are the two
   details a player notices within ten seconds of the first game.
3. Item **20.1** and **20.2** — before changing any more pixels, make the
   screenshot tests capable of failing. Everything after this is safer.
4. Item **6** — rule-effect feedback, which is what makes `atomic` and `charged`
   legible as games rather than as engine features.
5. Item **11** — replay, which is small, closes a gap the roadmap already
   asserts is closed, and pays for itself again in M8.
6. Items **16**, **10**, **17** — reach and window manners.
7. Item **18** — sound, the largest single piece of remaining polish, and the
   one that most changes whether the game reads as shipped.
