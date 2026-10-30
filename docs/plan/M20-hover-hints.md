# M20 — Hover hints (Ctrl-peek, attack hints)

**Goal:** three board-reading aids, each switchable in Settings → BOARD, all presentation-only
(none enters `VariantId`, the position, a game file or the move history):

1. **Ctrl-peek.** Holding Ctrl and hovering a piece (either colour) shows that piece's moves,
   in a colour distinct from the selected-piece legal-move colours (moss/blood) and from the
   selection colour (ember).
2. **Attack hint on empty squares.** Hovering an empty square shows every piece that attacks it
   and the path each takes to reach it. Very faint.
3. **Attack hint on a move target.** Hovering a currently highlighted legal destination that is
   attacked by the opponent shows the same overlay as (2), with no modifier key. Very faint.

(2) and (3) are one mechanism with two triggers; build it once.

4. **Defenders.** During (2) and (3) the player's own pieces that defend the square (could
   recapture there) are shown too, with their paths, in a second faint tint (`Theme::peek`,
   fainter) distinct from the attackers' faint red.

**Decision (precompute vs per hover):** per hover. One hover is one probe copy plus one
forward generation per side, the same work a precomputed all-squares map would cost, and
case 3 needs the position *after* the move so a map of the current position cannot serve it.
`setHover`'s unchanged-argument early-out is the cache. Nothing is added to the hot path. A
lazily built, per-snapshot attack map is the right next step only if a "show all attacked
squares" view is wanted.

## M20.1 What was found (no code written yet)

Line numbers are as of commit `a180468`; search for the named function if they have drifted.

- **There is no hover state anywhere in the board path.** `SDL_EVENT_MOUSE_MOTION`
  (`src/gui/main.cpp:1560`) only drives orbit/pan/slide, and it does not look at `consumed`.
  Picking happens on click only, in `SDL_EVENT_MOUSE_BUTTON_DOWN` (`main.cpp:1502-1537`):
  - shape view (`optionsFor(*shell).surface`, inside `#ifdef CB_HAVE_IMGUI`): builds a
    `render::PlaySurface` with `PlaySurface::build(variant, poseFrom(settings, variant))` and
    calls `surf.pick(session->cameraOver(...), boardRect.width, boardRect.height, x, y,
    settings.geometryGhost, &session->snapshot(), settings.pieceHeightScale,
    settings.shapePieceCylinders)` (`main.cpp:1517-1525`), then `apply`s a `ClickCell`.
  - flat / 3-D board: `Session::clickPixel(px, py, w, h)` (`src/app/session.cpp:872`), which
    casts `camera().pickRay(...)` against `placements_` with `view::pickBox`.
  **`clickPixel` is not a picker: it mutates.** It applies a `ClickCell` on a hit and *clears
  the selection and calls `refreshSnapshot()` on a miss*. Hover therefore cannot call it; the
  pure ray-cast has to be factored out first (M20.3). `PlaySurface::pick` is pure and reusable
  as-is, but it lives in `render/` (L100), so the shape-view hover pick must stay in `main.cpp`
  - `app::Session` (L95) cannot call it.
- **Highlighting is one source of truth:** `PositionView::highlighted()` (`src/view/snapshot.hpp`),
  set by `Session::refreshSnapshot` (`session.cpp:469-482`) from `game_->legalMoves()` filtered
  by `selected_`. `refreshSnapshot` rebuilds the whole snapshot with `PositionView::capture`,
  so anything added to the snapshot is reset on every action unless refreshSnapshot re-adds it.
- **`BoardRenderer` consumes it in exactly two places**, each with its own copy of the
  `isHighlighted` lambda:
  - the shape-surface `fillFor` lambda (`src/render/board_renderer.cpp:758-779`, the legal-move
    branch at `:771`, the last-move tint `0.22f` at `:776`);
  - the 3-D/flat instance loop (`board_renderer.cpp:1165-1168` lambda, fill branch
    `:1241-1251`).
  The flat view (`options_.flat`) gets its cell colours from that same instance loop (only its
  pieces are drawn by the interface), so there is no third site. The other moss/blood fills in
  `src/render/ui.cpp:965-970` and `src/render/ui_menus.cpp:120-133` are the piece-designer and
  move-preview diagrams, not the play board, and are untouched.
  `BoardOptions::showLegalMoves` (`board_renderer.hpp:73`) gates the legal-move branch;
  `optionsFrom` (`main.cpp:242`) maps `Settings` -> `BoardOptions`. The renderer keeps no
  per-snapshot cache, so a changed snapshot is drawn on the next frame.
- **Per-piece move generation is private** (`MoveGen::generateForPiece`, `movegen.cpp:204`,
  declared `movegen.hpp:78`). Ctrl-peek on an opponent piece needs a public wrapper.
- **`isAttacked` returns a bool, not a path.** Box boards without hoppers use a reverse walk
  (`movegen.cpp:351`); glued boards and variants with hoppers use `isAttackedByForwardScan`
  (`movegen.cpp:334-349`), which copies the position, puts a hypothetical piece of
  `opponent(by)` on the target if the target is empty or holds a piece of `by` - the piece type
  used is **`static_cast<PieceTypeId>(v_->pieces.size() - 1)`** (the last declared type; type
  `0` is the empty sentinel) - runs `generatePseudoLegalFor(probePos, by, /*includeCastles=*/
  false, ...)` and looks for `captureCell == target`. Neither records the cells a ray crossed.
  "Path" therefore needs new code; do **not** touch the hot `generateAtom` loop (perft
  performance is a hard constraint, ADR-0009).
- **Moves do not say which direction produced them.** `Move` carries `from`, `to`,
  `captureCell` and `atomIndex` (index into `v_->pieces[type].atoms`), but not the direction.
  On a non-box board `generatePseudoLegalFor` ends with `out.sortAndUnique()`, and `Move`
  equality ignores `atomIndex`, so two directions (or two atoms) that reach the same target
  collapse into one move. The path must be recovered by re-walking every direction of the atom.
- **Offset captures do not land on the target.** For an atom with `captureAt` (checkers jump,
  marks-based en passant: `movegen.cpp:167-184`) and for classic en passant
  (`movegen.cpp:185-195`), `captureCell != to`. A forward probe finds these attackers through
  `captureCell == target`, but the ray never reaches the target.
- **Settings reach the session through `Shell::applySettings`** (`src/app/shell.cpp:1337`):
  `session_->setFlatView(...)`, `setConfirmMoves(...)`, etc. That is the pattern to extend; do
  not pass settings in on every hover call.
- **Scripts go through the action layer:** `--script` -> `Session::applyScript`
  (`session.cpp:894`) -> `parseAction` (`session.cpp:29`, declared `session.hpp:399`) ->
  `Session::apply`. Nothing records `Action`s: a game file is built from `Game::moveHistory()`,
  so an action that never plays a move cannot leak into a save.
- **Layering** (enforced at configure time by `cb_layer` in `cmake/EndspielZeroLayer.cmake`,
  plus `tests/arch/test_source_hygiene.cpp` "the declared layer graph is acyclic and
  bottom-up"; `docs/ARCHITECTURE.md` cites a `tests/arch/test_layering.cpp` that does not
  exist): movegen L50 (deps: position) < view L60 (deps: position only) < game < app L95
  (deps: io, ai, game, view, audio) < render L100 (deps: view, app, ...). Everything this spec
  adds respects that: `MoveGen` gains functions over `Position`; `PositionView` gains plain
  `std::vector<CellId>` members and must **not** include anything from movegen; `Session`
  reaches `MoveGen` via `game_->moveGen()` (`game.hpp:51`); the renderer reads the snapshot.
  The movegen directory is under the arch test's no-floating-point rule - nothing here needs
  floats there.

## M20.2 Engine: `MoveGen::attackers` and `MoveGen::movesFrom`

Add to `src/movegen/movegen.hpp`, public section (needs `#include <vector>`):

```cpp
struct Attacker {
  CellId from{kInvalidCell};   // the attacking piece's cell
  std::vector<CellId> path;    // cells strictly between `from` and the target, in walk order
};
/// Every piece of `by` that attacks `target` - the same meaning as isAttacked, including the
/// hypothetical-enemy reading for an empty or own-occupied target - with the cells each
/// ray crosses. One entry per (piece, route): a rook on a torus that reaches the target both
/// ways round appears twice with the same `from`. Empty path for a single step, a leap, an
/// offset capture (captureAt / en passant) or when the route cannot be re-traced. Not on any
/// hot path: it copies the position and allocates. Ordered by `from`, then route.
[[nodiscard]] std::vector<Attacker> attackers(const Position& p, CellId target,
                                              Color by) const;
/// Pseudo-legal moves of the single piece on `from`, whichever side it belongs to, appended
/// to `out` (castling and compound moves excluded - they are side-level). Public wrapper of
/// generateForPiece, for Ctrl-peek. No-op on an empty cell. May contain duplicates on a
/// non-box board; callers that need distinct targets dedupe.
void movesFrom(const Position& p, CellId from, MoveList& out) const;
```

Private additions:

```cpp
/// The position isAttackedByForwardScan probes: `p` with a piece of opponent(by) of type
/// pieces.size()-1 on `target` when the target is empty or holds a piece of `by`.
[[nodiscard]] Position probePosition(const Position& p, CellId target, Color by) const;
/// Every route along `atom` (of the piece standing on `from` in `probe`) whose walk lands on
/// `target`, as the cells strictly between. Empty result = no route found.
[[nodiscard]] std::vector<std::vector<CellId>> tracePaths(const Position& probe, CellId from,
                                                          const MoveAtom& atom,
                                                          CellId target) const;
```

`isAttackedByForwardScan` is changed only to call `probePosition` (same copy it makes today,
returned by value; no behaviour or cost change).

`movesFrom`: `if (p.at(from).empty()) return; generateForPiece(p, from, out);`.

`attackers` (always the forward method, so one code path is right on boxes, seams and hoppers
alike; correctness over speed, it runs once per hover change):

1. `const Position probe = probePosition(p, target, by);`
   `MoveList list(v_->moveUpperBound());`
   `generatePseudoLegalFor(probe, by, /*includeCastles=*/false, list);`
2. Collect the distinct `from` of every move with `captureCell == target` (sorted).
3. For each such `from`, with `piece = probe.at(from)`:
   - If **any** matching move from it has `to != target` (offset capture / en passant), and no
     matching move from it has `to == target`, emit one `Attacker{from, {}}`.
   - Otherwise, for every atom of `v_->pieces[piece.type].atoms` whose index appears as the
     `atomIndex` of a matching move with `to == target` (do not trust `atomIndex` alone after
     `sortAndUnique` - also try every other atom with `capture != Cannot` and
     `captureAt.empty()`; cheap and order-independent), call `tracePaths` and emit one
     `Attacker` per distinct path (dedupe identical vectors across atoms, e.g. a queen modelled
     as rook+bishop atoms never duplicates, but a custom piece might).
   - If no route was found (should not happen), emit `Attacker{from, {}}` rather than dropping
     it. Never throw.

`tracePaths` - a walker with **occupancy**, no emission. Replaying the bare geometry without
occupancy is wrong: on a torus the "up" ray may be blocked and the "down" ray clear, and an
occupancy-free replay of "up" would pass straight through the blocker to the target. For the
atom and the piece on `from` (`side = piece.colorOf()`, `ci = size_t(piece.orientation())`):

```
if atom.fromRegion[ci].active() && !contains(fromCoord) -> return {}      (as generateAtom)
if atom.unmoved && piece.hasMoved()                     -> return {}
for di in [atom.dirBegin[ci], atom.dirEnd[ci]):
  startDir = v_->dirTable[di]; Walker w = geom.start(from, startDir)
  cells = {}; hurdle = false
  for k = 1 .. (atom.maxK == kUnlimited ? rayCap_ : atom.maxK):
    if !geom.step(w) || rayClosed(w, from, startDir): break
    blocked = v_->hasBlockingMarks && probe.blockedAt(w.cell)
    if w.cell == target && !blocked:
      Hop  : accept iff hurdle
      else : accept iff k >= atom.minK
      if accepted: push cells to result; break     (a slider stops at the probe anyway)
      else if mode != Leap: break
    if blocked: if mode == Leap { cells.push(w.cell); continue } else break
    occupied = !probe.at(w.cell).empty()
    Hop  : if !hurdle { cells.push(w.cell); if occupied hurdle = true; continue } else break
           (the landing cell must be the very next one: generateAtom breaks after it)
    Slide: if occupied break; cells.push(w.cell)
    Leap : cells.push(w.cell)                     (a leaping rider passes over everything)
```

The `upAxis/upSign` frame tracking of `generateAtom` only decides whether the piece lands
flipped; it never changes which cells a ray visits, so `tracePaths` does not need it. For a
leap (`maxK == 1`, e.g. a knight's `(1,2)` vector) the target is reached at `k == 1` and the
path is empty; for a leaping rider the path is the intermediate *landing* cells, not the cells
jumped over - that is what the ray visits.

**Probe piece type caveat:** the probe is `pieces.size() - 1` of `opponent(by)`. It only ever
stands on the target and is never `by`'s, so it never generates; it matters only as an
occupied enemy cell. Rule 3 below never needs a probe at all (the target holds the mover's
piece after the move), except when a rule-VM effect has emptied it (atomic), where the probe
gives the right reading.

Tests - new file `tests/unit/movegen/test_attackers.cpp`, tags `"[unit][movegen]"` (the
glob in `tests/CMakeLists.txt` picks it up; no CMake change). Use `test::loadVariant` from
`tests/support/variants.hpp` and `fromFen`; cell helpers as in `tests/unit/position/
test_marks.cpp`.
- standard chess, `R6k/8/8/8/8/8/8/K7`-style with a white rook on a1, a2-a7 empty, target a8
  (empty) by white -> one attacker `a1`, path `a2..a7` in that order; add a blocker on a4 ->
  no attacker from a1.
- knight attacker -> path empty. Pawn on e4 attacks empty d5 (hypothetical-enemy rule, capture
  atom `must`) -> reported, path empty; the pawn's forward push atom (`cannot`) never reports
  e5.
- torus (`variants/torus.toml`): a rook whose only clear route to the target wraps over the
  seam reports the wrapped cells in order; with the other way round blocked it reports exactly
  one route; with both clear it reports two `Attacker`s with the same `from`.
- hopper: an inline TOML variant (as `test_marks.cpp:174` does with `loadVariantToml`) with a
  piece declared by `[[piece.move]]` `vector = [1, 0]`, `mode = "hop"`, `max = "inf"`; the
  path includes the cells before the hurdle and the hurdle itself, and a target two beyond the
  hurdle is not attacked. (No shipped variant has a hopper, and `test::randomVariant` never
  generates one.)
- offset capture: the jumper variant of `test_marks.cpp:174` - the jumper is reported as an
  attacker of the man it would jump, with an empty path.
- `movesFrom` on the opponent's piece equals, as a set of `(from, to, captureCell, promoteTo)`,
  the moves with that `from` in `generatePseudoLegalFor(p, thatColor, /*includeCastles=*/false,
  ...)` (sets, because the latter is `sortAndUnique`d on non-box boards and `movesFrom` is
  not).

Property - add to `tests/property/test_movegen_props.cpp`, tag `"[property][movegen]"`, in
the same shape as "the reverse-walk attack query agrees with a forward scan" (seeds 1..24,
`test::randomVariant`, `randomPlayout` depth 4): for every cell `c` and both colours,
`!gen.attackers(pos, c, by).empty() == gen.isAttacked(pos, c, by)`, and every path cell is
empty in `pos` unless the attacker's atom is a leap/hop. This guards the two answers against
drifting apart - the whole point of reusing the forward probe. Add one loop over the shipped
2-D variants (`standard`, `torus`, `klein`, `mobius`, `cylinder`, `mirrorbox`, `atomic`,
`charged`) from their start positions as well; skip `5d` (temporal, M20.8).

## M20.3 Session: hover state

`Session` (`src/app/session.hpp/.cpp`) owns the logic; `main.cpp` only forwards the pointer.

New public API on `Session`:

```cpp
struct HintOptions {            // in namespace cb::app, session.hpp
  bool ctrlPeek{true};
  bool attackHints{true};
};
void setHintOptions(const HintOptions& o);      // recomputes hints if changed
void setHover(CellId cell, bool ctrl);          // kInvalidCell = pointer off the board
[[nodiscard]] CellId hoverCell() const noexcept { return hoverCell_; }
/// The cell under a pixel of the board rectangle, or kInvalidCell. Pure: no selection
/// change, no snapshot rebuild. Refuses (kInvalidCell) when boardLocked().
[[nodiscard]] CellId pickPixel(float px, float py, float width, float height) const;
```

- `pickPixel` is the ray-cast half of `clickPixel` (`camera().pickRay(...)`,
  `view::pickBox(ray, placements_, view::Vec3{0.45f, 0.45f, 0.06f})`), moved out verbatim.
  `clickPixel` becomes `pickPixel` + its existing miss/hit handling, unchanged in behaviour.
- `Shell::applySettings` (`shell.cpp:1337`) calls
  `session_->setHintOptions({settings_.peekMovesOnCtrl, settings_.showAttackHints})` next to
  `setConfirmMoves`, so `Session` never sees the `Settings` struct and switching a checkbox off
  clears the overlay immediately (the settings screen already calls `applySettings` when
  `request.settingsChanged`).
- Private state: `CellId hoverCell_{kInvalidCell}; bool hoverCtrl_{false}; HintOptions
  hints_{};` and a private `void refreshHints();`.
- `setHover` stores `(cell, ctrl)` and calls `refreshHints()` - but returns immediately if
  `(cell, ctrl)` equals the stored pair, so a still mouse or a key-repeat Ctrl costs nothing.
- `refreshSnapshot()` calls `refreshHints()` as its **last** line. `PositionView::capture`
  has just wiped the hint vectors, and recomputing from the stored hover cell (rather than
  leaving them empty until the mouse moves) is what makes "select e2, keep the pointer on e4"
  and "move, then the pointer still sits on the destination" right, and means a move/undo/
  reset never leaves a stale hint.
- Script verb: add `ActionKind::Hover` and `bool ctrl{false}` to `Action`; `parseAction`
  accepts `hover <cell> [ctrl]` and `hover off` (cell `kInvalidCell`), placed next to the
  `click` branch and using `parseCell(v.dims, where)` the same way. `apply` handles
  `ActionKind::Hover` by calling `setHover(a.cell, a.ctrl)` and returning `{}`; add it to
  neither refusal list (watching / engine-to-move), because `refreshHints` applies its own
  gate. Nothing records actions, so it cannot reach a game file; it exists so `--shot` is
  reproducible.

Resolved into `PositionView` (`src/view/snapshot.hpp`), same pattern as `highlighted`:

```cpp
[[nodiscard]] std::span<const CellId> peekMoves() const noexcept { return peek_; }
[[nodiscard]] std::span<const CellId> hintAttackers() const noexcept { return hintFrom_; }
[[nodiscard]] std::span<const CellId> hintPath() const noexcept { return hintPath_; }
void setPeekMoves(std::vector<CellId> c) { peek_ = std::move(c); }
void setHint(std::vector<CellId> attackers, std::vector<CellId> path,
             std::vector<CellId> defenders, std::vector<CellId> defendPath) { ... }
[[nodiscard]] std::span<const CellId> hintDefenders() const noexcept;
[[nodiscard]] std::span<const CellId> hintDefendPath() const noexcept;
```

All three sorted and deduplicated. (`view` depends only on `position`; these are plain
`CellId` vectors, no movegen type crosses into `view`.)

`refreshHints()`: clear all three, then return with them empty if
`!hoverValid` (cell `>= cellCount()`), `shotInFlight() || watching_`, `game_->isTemporal()`,
`game_->result() != GameResult::InProgress`, `pending_.active || pendingMove_.active`, or
`!boardVisible(cell)`. Otherwise evaluate in order, first match wins (`pos =
game_->position()`, `gen = game_->moveGen()`):

1. **Peek** - `hoverCtrl_ && hints_.ctrlPeek && !pos.at(cell).empty()`:
   - piece colour == `pos.sideToMove()`: the distinct `to` of `game_->legalMoves()` with
     `m.from == cell` (includes castling, as clicking would);
   - otherwise: `MoveList l(variant_->moveUpperBound()); gen.movesFrom(pos, cell, l);` distinct
     `to` - pseudo-legal ("their moves ignore check"; say so in the settings caption).
   Peek ignores the selection; the selection's own highlights are still drawn (M20.5).
2. **Empty square** - `hints_.attackHints && pos.at(cell).empty()` and `cell` is **not** in
   `snapshot_.highlighted()`: `gen.attackers(pos, cell, opponent(pos.sideToMove()))`.
3. **Move target** - `hints_.attackHints && selected_ != kInvalidCell` and `cell` is in
   `snapshot_.highlighted()` (empty or capture): take `const Move* m = findMove(selected_,
   cell)` (first legal match, so with several promotion choices the first is used; that does
   not change who attacks the square), build the position after it with the **existing Game
   copy pattern** - `Game after = *game_; (void)after.play(*m);` (Game is copyable: the engine
   job already does `[game = *game_]`, `session.cpp:952`, and `ai/search.cpp:287` copies it),
   which runs the rule VM so atomic explosions and the like are reflected - and query
   `after.moveGen().attackers(after.position(), cell, opponent(mover))` with `mover =
   pos.sideToMove()`. This makes a target defended only through the moving piece's old square,
   or newly exposed by its departure, read right. `Position::make` alone is not enough: it does
   not run `rules::RuleEngine`.
   The attacker set is computed with `by` = the side that does **not** move the selected
   piece; the target holds the mover's piece afterwards, so no probe is placed.

Defenders (rules 2 and 3): the same query with the other colour. Rule 2: `gen.attackers(pos,
cell, pos.sideToMove())` (the probe puts a hypothetical enemy on the empty cell, so these are
the pieces that could recapture there). Rule 3: `after.moveGen().attackers(after.position(),
cell, mover)`; the cell holds the mover's own piece, which `probePosition` replaces by the
hypothetical enemy, so the mover itself is not counted and its old square is already empty.
Results feed `hintDefenders` / `hintDefendPath`.

From the result: `hintAttackers` = every `Attacker::from`; `hintPath` = the union of every
path (both sorted/unique). Empty attacker list => all spans empty.

Note: `boardLocked()` also includes `engineToMove()`. Hints are refused only for
`shotInFlight() || watching_` (both move the camera or own the board); while the computer
thinks the player may still read the board, and rule 2 then reports the attacks of the side
not to move (the player) - acceptable, and simpler than special-casing.

Tests - extend `tests/unit/app/test_session.cpp`, tag `"[unit][app]"` (it already builds a
standard `Session` and clicks cells):
- standard start, `setHover(e2, true)` -> `peekMoves == {e3, e4}`; `setHover(a8, true)` (black
  rook, opponent) -> empty; `setHover(b8, true)` -> `{a6, c6}`; `setHover(e2, false)` -> all
  empty (own piece, not highlighted, no Ctrl).
- after `1. e4`, black to move: `setHover(d5, false)` -> `hintAttackers == {e4}`,
  `hintPath` empty. After `1. e4 e5 2. Qh5` (black to move), `setHover(e2, false)` ->
  `hintAttackers == {e1, f1, g1, h5}`, `hintPath == {f3, g4}`.
- white to move, select `e2`, hover `e4` -> hint computed on the position after `e2e4`
  (assert against a position where moving the piece changes the answer, e.g. a pinned or
  screening piece: a bishop on b5 behind which a black rook attacks the target only once it
  leaves).
- `setHover` twice with the same arguments leaves `snapshot()` identical (compare the spans).
- a move clears or recomputes hints: hover a cell, play a move with `apply(ClickCell...)`,
  the spans reflect the new position.
- `setHintOptions({false, false})` -> every hover yields empty spans.
- `parseAction(v, "hover e4 ctrl")` -> `kind == Hover`, `ctrl == true`; `"hover off"` ->
  `cell == kInvalidCell`.
- `pickPixel` returns the same cell `clickPixel` would and leaves `selected()` and the
  snapshot unchanged (mirror "a click resolved from a pixel reaches the right cell").

## M20.4 Settings

`src/app/settings.hpp`, in the `// ---- board` block after `showLegalMoves` (`:35`):

```cpp
/// Holding Ctrl over a piece shows its moves (either side; the opponent's ignore check).
bool peekMovesOnCtrl{true};   // key peek_moves_on_ctrl
/// Hovering an empty square, or a highlighted destination, shows what attacks it.
bool showAttackHints{true};   // key show_attack_hints
```

One setting covers (2) and (3), as the requirement says each *feature* can be off and 2/3 are
the same overlay; **if the user wants 2 and 3 separately toggleable, split into
`showAttackHintsEmpty` / `showAttackHintsMoves`** (trivial: `refreshHints` already branches on
the rule, and `HintOptions` grows a third bool).

- `settings.cpp` parse chain (`:81`, after `show_legal_moves`): two `else if (key == ...)`
  branches with `asBool(value)`; write (`:205`): two `out << "... = " << boolText(...)` lines.
- `src/render/ui_menus.cpp`, BOARD group after `Checkbox("Highlight legal moves", ...)`
  (`:1199`): `Checkbox("Ctrl over a piece shows its moves", &s.peekMovesOnCtrl)` and
  `Checkbox("Hover shows what attacks a square", &s.showAttackHints)`, followed by one faint
  caption in the file's existing style (`PushStyleColor(ImGuiCol_Text, col(t.boneFaint))` +
  `TextUnformatted("the opponent's moves ignore check")`) - there is no tooltip helper in
  this file.
- the `moved` comparison (`ui_menus.cpp:1402-1430`): add
  `before.peekMovesOnCtrl != s.peekMovesOnCtrl || before.showAttackHints != s.showAttackHints`.
  Forgetting it means the checkbox works until restart but is never saved. This is ImGui code
  with no unit-test seam; check it by hand (toggle, restart the GUI, the box is still off).
- Test: extend "settings survive a restart" (`tests/unit/app/test_shell.cpp:318`) to set both
  to `false` and read them back; that covers parse and write.

## M20.5 Rendering

`BoardOptions` gains nothing: the session has already emptied the spans when an option is off,
so **the renderer draws whatever the snapshot contains** (the file's stance: a highlight it
draws came from the engine).

Colour: add `Rgba peek` to `view::Theme` (`src/view/theme.hpp`, next to `moss`/`blood` at
`:74-75`) and set it in `manifold()` too (next to `t.moss` at `:166`). It must be distinct from
that theme's `moss`, `blood` **and `ember`** (the selection fill). `ember` differs per theme -
console's is orange `0xF0A23C`, manifold's is electric blue `0x2E4BFF` - so "a cool blue" is
wrong for manifold. Starting values: console `peek = 0x9B7BEA` (violet), manifold
`peek = 0xE0A030` (amber). Add to `tests/unit/view/test_theme.cpp` a case over
`{Theme::manifold(), Theme::console()}` that `peek` is not within a small RGB distance of
`moss`, `blood` or `ember`.

At the top of `board_renderer.cpp` (beside `kShadowScale`, `:46`):

```cpp
constexpr float kPeekTint = 0.55f;        // peek destination
constexpr float kHintAttackerTint = 0.14f;
constexpr float kHintPathTint = 0.08f;    // all "very faint": under the 0.22 last-move tint
```

Replace the two `isHighlighted` lambdas with one file-local helper
`bool cellIn(std::span<const CellId> s, CellId c)` (linear `std::find`; spans are tiny) and use
it for highlighted, peek, attackers and path. In **both** fill sites (shape `fillFor`, instance
loop), the order is:

```
fill = parity colour
if cell == selected                    -> ember                 (unchanged, return/skip rest)
else if showLegalMoves && highlighted  -> moss / blood as today (skip hint tints)
else if peek                           -> mix(fill, theme_.peek, kPeekTint) when empty,
                                          mix(fill, theme_.peek, kPeekTint + 0.15f) when occupied
else if showLastMove && last move      -> mix(fill, ember, 0.22) (unchanged)
then, for cells not selected and not legal-highlighted:
  if hintAttacker -> fill = mix(fill, theme_.blood, kHintAttackerTint)
  else if hintPath -> fill = mix(fill, theme_.blood, kHintPathTint)
  else if hintDefender -> fill = mix(fill, theme_.peek, kHintAttackerTint)
  else if hintDefendPath -> fill = mix(fill, theme_.peek, kHintPathTint)
```

The hint tints are layered **on top of** the last-move tint rather than in an `else`: the
piece that just moved is very often the attacker, and an `else` would hide the hint exactly
there. In `fillFor` this means restructuring the early `return`s into a single `fill` variable.

## M20.6 Input wiring (`src/gui/main.cpp`)

- Factor `CellId pickCellAt(app::Shell&, const render::BoardRect&, float x, float y)` out of
  the click handler (`main.cpp:1502-1537`): the shape branch (`#ifdef CB_HAVE_IMGUI`,
  `optionsFor(*shell).surface`, refused while `shotInFlight()`) builds the `PlaySurface` and
  calls `surf.pick(...)` exactly as now; otherwise `session->pickPixel(x - boardRect.x,
  y - boardRect.y, boardRect.width, boardRect.height)`. The click handler then does
  `ClickCell` with that cell for the shape branch and keeps calling `clickPixel` for the flat
  branch (its miss-deselects behaviour must not change).
- **Resolve hover once per frame, not per event.** `PlaySurface::build` is a whole-surface
  build; doing it for every `SDL_EVENT_MOUSE_MOTION` (hundreds per second) is wasteful. In the
  event loop only record `hoverDirty = true; hoverX = e.motion.x; hoverY = e.motion.y;` (and
  `hoverDirty = true` on `SDL_EVENT_KEY_DOWN`/`SDL_EVENT_KEY_UP` whose `e.key.key` is
  `SDLK_LCTRL`/`SDLK_RCTRL`, so peek appears and disappears without moving the mouse). Keep
  these as locals beside `orbiting/panning/sliding` (`main.cpp:1451-1454`). After the event
  loop, before drawing, if `hoverDirty`:
  `ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0`; cell = `kInvalidCell` when the last
  motion was `consumed` (ImGui `WantCaptureMouse`), when not `inGame`, while
  `orbiting || panning || sliding`, when `shell->attracting()`, or when the point is outside
  `boardRect`; otherwise `pickCellAt(...)`. Then `session->setHover(cell, ctrl)` and clear
  `hoverDirty`.
- Only real input sets `hoverDirty`, so a `--script "hover e5"` hover is never overwritten by
  a pointer that is not there (the `--shot`/`--clip` paths run no pointer input).
- The loop already wakes and redraws on any event (`SDL_WaitEventTimeout`, `main.cpp:1478`),
  so motion triggers the frame that shows the hint.

## M20.7 Verification (required — not just a green suite)

1. `tools/test.sh --build movegen app view` green; then `ctest --preset dev` (the property
   label runs the new attackers/isAttacked sweep) because `src/movegen` changed.
2. Perft goldens unchanged and `cb-bench-baseline` shows no movegen regression (only
   `isAttackedByForwardScan` was touched, by extracting `probePosition`; confirm on a glued
   variant, where it is on the legality path).
3. Screenshots through the `hover` script verb:
   ```
   endspielzero_gui standard --script $'click e2\nclick e4\nhover d5' --shot hint-empty.ppm
   endspielzero_gui standard --script $'hover b1 ctrl' --shot peek.ppm
   endspielzero_gui standard --script $'hover g8 ctrl' --shot peek-opponent.ppm
   endspielzero_gui standard --script $'click e2\nhover e4' --shot hint-move.ppm
   endspielzero_gui torus --geometry --script $'hover d4' --shot hint-shape.ppm
   endspielzero_gui standard --flat --script $'hover b1 ctrl' --shot peek-flat.ppm
   ```
   Look at each, in both themes:
   the peek colour is distinct from moss, blood and ember; the hint is faint but readable;
   the torus path wraps; the shape and flat views show the same cells as the 3-D board.
4. The existing `gui-settings` capture test (`tests/CMakeLists.txt`, `--screen settings`) must
   still pass with the two new checkboxes; look at its image once.

## M20.8 Out of scope / fallbacks

- Temporal (5D) variants: attackers across timelines are not defined by `isAttacked` today.
  `refreshHints` returns no hint when `game_->isTemporal()`; record it here when built.
- If `tracePaths` proves fragile on some seam (the property test will say so: an attacker
  with no traced route), ship attacker cells with empty paths (still correct) and record which
  gluing broke.
- Offset-capture attackers (checkers jump, en passant) are shown without a path by design.
- Hint strength is a judgement call; tune the three constants after seeing the screenshots, not
  before.

## Status

Specified (2026-10-30), reviewed against the code at `a180468` (2026-10-30); built (first pass: per-hover recompute); M20.9 is the planned incremental map.

## M20.9 Planned final step: incremental reach map

First pass (M20.1-M20.8) recomputes per hover with the forward probe. The intended end state,
for large boards (100+ cells, 20+ pieces) and for any whole-board overlay, is an incremental
reach map owned by `Session` (not by `MoveGen`'s hot path):

- per piece: its forward reach (cells covered and, per cell, the route); a reverse index
  cell -> pieces whose reach includes it. Both are built from **forward** walks, so the index
  is valid on glued boards; only a backward *walk* is not (`movegen.hpp`, `isAttackedByForwardScan`).
- after a move recompute: the moved piece, any captured/promoted piece, and every piece whose
  reach contains the move's start or end cell (via the index). Everything else is unchanged.
- fall back to a full rebuild when the move carries more than start/end/capture: rule-VM
  effects (atomic explosions), marks or blocking marks, castling/compound partners, temporal.
- rule 3 (position after a selected move) still computes on the copied `Game`.
- acceptance: a property test replays random playouts on every shipped variant and asserts the
  incremental map equals a full rebuild after every move; a benchmark records both costs on a
  large board before this replaces the first pass.

## M20.10 Revision (2026-10-30, user feedback)

- The attack / defend hints, like Ctrl-peek, appear **only while Ctrl is held**. Order under
  Ctrl: a highlighted target with a piece selected -> attackers and defenders after the move
  (so a capturable target is not shadowed by a peek); an occupied square -> peek; an empty
  square -> attackers and defenders.
- The hovered square is **always** marked, a little brighter (`kHoverBrighten`), whatever the
  options say: `PositionView::hover()`.
- Hints are clearer: attacker/defender tint 0.32, path 0.18 (were 0.14/0.08).
- Piece tint (`view/piece_tint.hpp`, `tintedPiece`), used by the 3-D board, the shape view and
  the flat tokens: hovered piece 0.22 accent; selected piece 0.55; a piece in flight keeps the
  selected tint until the animation ends; hint attackers (blood) and defenders (peek) 0.50.
- Right-click without dragging (more than 4 px) cancels the selection, a pending promotion or
  a pending confirmation: `Session::cancelSelection()`; a drag still orbits.
- No dead strip between squares: the pick box half-extent is 0.505 (was 0.45), so neighbouring
  boxes overlap and the nearer cell wins - the hover switches at the middle of the divider
  (`kPickHalfExtent`, shared by `pickPixel` and `clickPixel`, so click and hover agree).
- A piece that lands on the hovered square (own move, engine, undo) suppresses the hover mark
  and tint on it (`suppressedHover_`, set in `refreshSnapshot` when the piece on the hovered
  cell changes) until the pointer leaves that square and re-enters.
