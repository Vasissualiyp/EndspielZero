# M14 — Onboarding, Tutorial and First-Run Polish

**Goal.** A stranger who has never seen a chess variant is playing something interesting
within ten minutes, and understands *why this game is different*. The sandbox's power is
invisible in a screenshot: a newcomer lands on a library of fourteen boards, most of which
they cannot read, and the one thing that makes the project special — that the board can
have any shape and any number of dimensions — is the one thing nothing explains. This
milestone makes the power legible and the first ten minutes work.

**Exit condition.** A first-run player, with no documentation and no prior exposure, can
start from a curated "start here" list, be taught the chosen variant *by playing it*, and
never see an empty board, an unexplained term, or a dead end. Every screen's text is
legible at every UI scale, and every loader/validator error reaches the player in the
loader's own words.

**Why this is a milestone and not a task.** A powerful store page that converts badly is
the failure mode. The engine, the renderer and the clip camera can all be excellent and
the game still die on reviews that say "beautiful but I had no idea what to do." Onboarding
is therefore a first-class deliverable, scheduled before the marketing peak (see the
roadmap's release sequence), not a coat of paint applied after launch.

---

## M14.1 First-run experience

The first launch is a designed sequence, not the main menu:

- **A one-screen what-is-this.** One sentence — "chess on any board you can describe:
  cylinders, Möbius bands, six-dimensional tori, and time travel" — and one button,
  "Start with the classic", plus "show me the strange ones".
- **A good default.** No first-run ever lands on an exotic board. The default is standard
  chess; the exotic is opt-in.
- **No empty state anywhere.** Every list (variants, pieces, moves, settings) has content
  on first run; `charged` borrowing the standard board (`src/app/overture.hpp:35`) is the
  model — the nearest good answer, never a blank pane.
- The sequence itself is a value type driven by `app::Shell` (`src/app/shell.hpp:17`), so
  it is unit-testable through the action log with no window and no GPU, like every other
  screen.

## M14.2 Curated modes — "start here"

The library already orders by an existing difficulty rule; M14 surfaces it as guidance
rather than a sort:

- A small **"Start here"** set — standard → cylinder → torus → cube5 → 5d — each with a
  one-line "what changes here". The progression walks a player from the board they know to
  the board they cannot picture, one legible step at a time.
- A **"Surprise me"** entry that picks a variant and *explains the rule it chose* in a
  caption, so a random pick still teaches.
- The full library stays available; the curated set is a front door, not a gate.
- Each curated entry names its own win/draw/loss differences in plain words, drawn from
  `VariantDoc`'s description and difficulty (M7), never hand-copied.

## M14.3 The tutorial is played, not read

No wall of text. A tutorial is a short, scripted position with one goal at a time:

- **Standard first**, teaching the interface (select, move, the legal-move highlight, undo,
  the ledger) — the things the engine already exposes through `PositionView`'s highlight
  and selection (`src/view/snapshot.hpp:36`).
- **Then one geometry lesson per curated variant**, each a single forced move that only
  makes sense once the rule is understood: a rook wrapping a cylinder; a bishop returning
  mirrored off a Möbius seam; a move onto a past board. The player learns the topology by
  completing a move, not by reading about it.
- Scripted positions are data, loaded like any position, and the tutor validates each
  step with the real engine — a "correct" move is a real legal move, never a special case.
- The move preview the editor already builds (`M7.1.4`, a real `DimSpec` + `expandAtom`)
  is the same picture the tutorial shows, so "what does this piece do here" has one answer
  in the editor, the camera and the tutorial.

## M14.4 Legibility of the exotic

The things that make the game special are the things most likely to be opaque. Make them
say themselves:

- **A seam legend.** The seam colours carry real information (a portal's hue, brightness
  for a folded face — `src/view/seams.hpp`); a legend maps colour to "where this edge
  leads", once, in the corner, dismissible.
- **An axis explainer.** The `cube5`/`5d` boards are drawn as slices along extra axes; a
  caption says which axis is which and what moving along it means. Derived from
  `DimSpec`/`ViewConfig`, not authored per variant.
- **"What does this piece do on *this* board?"** A per-variant, per-piece reachability
  diagram, reusing the editor's preview and the M6 slice viewer — the answer to the single
  question a newcomer cannot otherwise answer.
- **No undefined terms.** "Torus", "Klein", "multiverse", "pitch" are either explained on
  first encounter or linked to a one-paragraph card. A term the player meets twice gets a
  card; a term they meet once gets an inline gloss.

## M14.5 UX polish and accessibility

- **Errors in the engine's own words.** Every `Result<T, ErrorCode>` surfaced at the
  control or screen that caused it, using the loader/validator message verbatim — the M7.0.3
  "one validator, no second dialect" rule, extended to the first-run and tutorial paths.
- **Every pixel through `Ui::px()`** (the existing invariant): a missed hardcoded size
  scales the text and leaves the panel behind; the onboarding screens are drawn at small
  UI scales too, where it shows up first.
- **Colour is never the only signal.** Seams, legal moves, check and last-move are each
  distinguishable without hue, for the two shipped themes and for a colour-blind player.
  The existing `test_theme.cpp` contrast test extends to the new marks.
- **Rebindable, discoverable input.** The hot-seat and orbit controls are listed in-game;
  nothing is a hidden keyboard shortcut.
- **A demo / Next Fest scope**, stated explicitly: the curated variants, the tutorial, the
  full library read-only, and *no* editor and *no* online. Enough to fall in love with;
  not enough to replace buying.

## Tests and acceptance

- **Headless first-run:** through the action log, a fresh `Shell` reaches a playable game
  from the initial screen without any missing-content dead end; every screen reachable in
  first-run has non-empty content.
- **Tutorial correctness:** every tutorial step's "correct" move is generated by the real
  engine and is legal; a property test asserts no tutorial move is a special case.
- **No undefined terms:** a test walks the curated set's visible strings and fails on a
  term not present in the glossary (a cheap guard against rot).
- **Scaling:** `--shot` of every onboarding screen at the minimum and maximum `Ui::px()`
  scale is validation-clean and lays out (no clipping, no overlap).
- **Contrast:** the extended `test_theme.cpp` asserts every new mark clears the contrast
  floor on both themes and both square colours.

### Acceptance facts

1. A first-run player reaches a legal standard-chess move with no documentation.
2. Every curated variant has a single forced move that teaches its distinguishing rule,
   and the move is engine-legal.
3. Every visible curated-set term is defined in the glossary; the test fails otherwise.
4. Every onboarding screen is legible at both UI-scale extremes, with colour contrast
   checked on both themes.
5. Loader and validator errors reach the player verbatim from the one validator.
6. The demo build contains the curated set and tutorial, and exposes no editor or online.

## Risks and non-goals

- **The tutorial is content, and content rots.** Every lesson is data validated against the
  live engine, so a rule change that breaks a lesson fails a test rather than shipping a
  broken tutorial.
- **Scope creep into "campaign".** M14 is a first-run and legibility milestone, not a
  puzzle campaign; a lesson is one goal, and the curated path is five steps, not fifty.
- **Non-goals:** no achievements, no progression systems, no localization (the strings are
  centralized for a later translation pass, but none ship here), no engine, geometry or
  rules changes. The milestone is `app`/`render`/`io` presentation over the existing seams.

## Status

**M14.1 revised: the tutorial is summoned, not automatic (2026-10-01).** The curated
sequence no longer opens by itself on first launch; the shell always opens on the main
menu, and a new **Tutorial** row (index 04, between Multiplayer and Editor) opens
`Screen::Welcome` on demand. `back()` returns it to the main menu. `Settings::seenWelcome`
and `Shell::dismissWelcome` remain (leaving the sequence is still recorded) but no longer
decide the startup screen. `tests/unit/app/test_shell.cpp` pins the new behaviour: a fresh
shell opens on the menu, the tutorial is reachable, and backing out returns to the menu.

**M14.1 (first-run) and M14.2 (curated path) built (2026-09-30).** `Screen::Welcome` is the
first-run screen (`Settings::seenWelcome`, persisted, plays once); `Shell::dismissWelcome`
leaves it and opens the main menu. The screen states what the game is in one sentence and
offers the curated path - `app::curatedVariants()`: standard → cylinder → torus → cube5 → 5d,
each with a one-line "what changes here" - plus "Surprise me" (`Shell::surpriseVariant`,
which skips the classic and captions its pick with the variant's own description) and "show
me all the boards" into the full library. Headless tests: a first run opens the welcome and
only once; the path starts at standard and each step is captioned; surprise picks a
non-standard library variant. `endspielzero_gui _ --shot o.ppm --screen welcome` captures it.

**M14.4 (legibility) started (2026-10-01).** The seam legend is built:
`view::seamLegend(seams, cfg, dims)` reduces the per-cell portal ramp to one entry per glued
drawn axis (`SeamMap` already colours both ends of an identification the same), mirrors are
excluded because they lead nowhere, and the game HUD draws the swatch and "the file edges
are the same edge" under the axis names when `Settings::showSeams && showSeamLegend` on a
glued board. Pinned by `tests/unit/view/test_seams.cpp` (standard empty, cylinder one "file"
entry, torus file+rank, Moebius's twisted file, mirrorbox empty). The axis explainer already
named the screen axes; it now sits beside the seam legend. The glossary is also built:
`app::glossary()` ships the definitions (torus, cylinder, Mobius band, Klein bottle, glued,
seam, dimension, slice, timeline, time travel) and the game-info screen's TERMS section
shows the ones a board's own name or description uses. Acceptance fact 3 is pinned by
`tests/unit/app/test_shell.cpp`: every term is defined and unique, unknown terms stay
unknown, the exotic vocabulary is covered, and every word of every curated caption is either
plain-English scaffolding or a glossary term, so a caption that reaches for a new exotic word
fails until it is defined. Still to build: per-piece reachability.

Planned but not built: M14.3 (the played tutorial), the rest of M14.4 (per-piece
reachability), M14.5 (the accessibility pass). It reads variant
descriptions and difficulty (already in the variant data), the move preview
(`expandAtom`/`tracePath`, M4/M5) and the visuals M11/M13 produce; it does **not** depend on
M7's editors, so Release Wave 2 can run M14 and M7's board and gamemode editors in either order.
