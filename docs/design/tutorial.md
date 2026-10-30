# The tutorial: teaching everything a player needs, by playing

Brainstorm, 2026-10-29. Feeds **M14.3** ([`../plan/M14-onboarding.md`](../plan/M14-onboarding.md));
nothing here is built yet. M14 already fixes the rules of the game: a lesson is *played,
not read*, one goal at a time, validated by the real engine, and the curated path is five
steps, not fifty. This page is about *what* has to be taught and *which formats* teach it
best, so M14.3 can be specced from it.

---

## 1. What a player has to know

Listed so nothing is assumed. The right-hand column is where the knowledge is first
*needed* - a lesson that arrives before that moment is a lecture; one that arrives after it
is a rescue.

| # | Knowledge | First needed |
|---|---|---|
| K1 | Select, move, the legal-move highlight, undo, the ledger | the first click |
| K2 | The camera: orbit, zoom, 2D / 3D / SHAPE views | the first board that is not flat |
| K3 | How the pieces move *(chess itself - many Steam players do not know it)* | standard |
| K4 | A piece is a set of vectors: leap, slide, hop, forward-only | the first unfamiliar piece |
| K5 | **Wrapping**: an edge can be glued to another edge (cylinder, torus) | cylinder |
| K6 | The seam colours: which edge leads where (the legend) | cylinder |
| K7 | **Twists**: an edge glued with a flip arrives mirrored (Möbius, Klein) | Möbius |
| K8 | **A pawn can turn round**: crossing a twist reverses forward (ADR-0020) | Möbius |
| K9 | **Mirrors**: a wall can reflect a ray instead of stopping it | mirrorbox |
| K10 | Check and mate *through* a seam | any glued board |
| K11 | More axes: slices, reading a 3-D / 4-D board, a move along an extra axis | cube5 |
| K12 | Rules beyond movement: explosions, forced captures, fields | atomic, mustcapture, charged |
| K13 | Time: boards per turn, the present, travelling back, timelines | 5d |
| K14 | The computer opponent, and that it can be turned off | the first game |
| K15 | That you can make your own piece / board | after the curated path |

## 2. Lesson formats - which one teaches what

Every format below is data plus the existing engine: a position (FEN), a goal the engine
can check, and text. None needs new rules code.

1. **Do it (the default).** One position, one goal: *play the move that X*. The goal is a
   set of engine-legal moves, never a special case. Teaches K1, K5, K7, K9 well.
2. **Predict, then reveal.** *Before* the highlight appears: "click every square this rook
   can reach". Scored against the engine's own legal set; the misses and extras are then
   drawn in two colours. This is the single best format for topology (K5, K7, K11): it makes
   the player build the mental model, and the engine marks it honestly. On a torus the
   first guess is always "the same as a normal board" - the reveal is the lesson.
3. **Watch, then do.** The move camera (M11) plays a demonstration move across a seam, with
   the shape view (M17) morphing the board into its surface half way; then a mirrored
   position for the player to repeat. Teaches K5/K7 viscerally - *this* is the trailer moment
   used as pedagogy.
4. **Flat ↔ shape toggle at the right moment.** The same position shown flat, then as the
   donut / band (SHAPE), with the piece's path traced on both. One toggle answers "why does
   the rook come back mirrored" better than any sentence.
5. **Puzzles.** Mate in one or two that *needs* the seam: a back-rank mate delivered by
   wrapping round a cylinder; a Möbius promotion only reachable by a flipped pawn; a Klein
   bishop attacking "the wrong colour". Validated by the opponent's own search (a mate
   score), so a broken puzzle fails a test.
6. **Graduation game.** Each world ends with a short game against the computer from a
   *reduced* position (a few pieces, the world's topology), so the player uses the idea in
   play once before moving on. The baseline opponent (M10.2-M10.3) is exactly strong
   enough for this, and the lesson can cap its depth.
7. **Just-in-time cards.** Outside the tutorial, the first time something happens in a real
   game - the first seam crossing, the first flipped pawn, the first check through a seam -
   a one-line card names it and links the glossary (M14.4). Shown once each, remembered in
   settings. This rescues players who skipped the tutorial, which is most of them.
8. **Make one.** A last, optional lesson in the piece designer: give a piece the camel's
   [1,3] leap and watch its reach on the preview board, then on a torus. Teaches K4 and K15
   in one go.

**Hints, three steps:** first the piece to move glows, then the target region, then the
move itself is shown. The third hint never fails the lesson - it only skips the "well
done".

## 3. A curriculum

One world per idea. Every world: two to four lessons, then a graduation game. The worlds
after World 2 are opt-in from the library ("Learn this board"), not a gate.

| World | Board | Lessons (format) | Knowledge |
|---|---|---|---|
| 0 Hands | standard | move a piece (1), undo (1), orbit the camera (1), 2D/3D toggle (1) | K1, K2, K14 |
| 0.5 Chess in five minutes *(skippable)* | standard | one "predict the moves" per piece (2), check and mate (5) | K3, K10 |
| 1 Pieces are vectors | standard + designer preview | rook as [1] slide, knight as [1,2] leap, pawn as forward-only (2) | K4 |
| 2 Edges that join | cylinder → torus | rook round the cylinder (3), predict the torus queen (2), seam legend (7: card), back-rank mate through the seam (5), graduation | K5, K6, K10 |
| 3 Edges that twist | Möbius | rook needs two laps (3/4), predict the mirrored arrival (2), **a pawn that turns round** (1, then 5: promote it), graduation | K7, K8 |
| 4 Walls that reflect | mirrorbox | bishop bank shot (1), predict (2), puzzle (5) | K9 |
| 5 Rules | atomic, mustcapture | blow up a king (1), the forced-capture trap (5) | K12 |
| 6 Up and across | cube5 | read the slices (1 + axis card), a rook move along the third axis (1), the unicorn's triagonal (2), graduation | K11 |
| 7 Time | 5d | move on the present board (1), travel back (1), see the new timeline (4), graduation | K13 |
| 8 Make your own | designer | the camel (8) | K4, K15 |

The Möbius world deserves the most care: it is the board with the new flipping pawn, the
strongest trailer shot, and the one where intuition is most wrong. Proposed sequence:

1. *Watch*: a white pawn captures across the seam and comes back upside down (the flat
   view draws a flipped piece inverted).
2. *Predict*: "where can this pawn go now?" - the player clicks; the reveal shows it moving
   towards White's own home.
3. *Do*: promote a flipped pawn on White's **first** rank.
4. *Puzzle*: the only winning move is a capture across the seam that flips a pawn onto its
   new last rank and promotes in the same move.

## 4. How it is built (for the M14.3 spec)

- **Lesson files**, one per world, loaded like variants and validated the same way:

  ```toml
  [[lesson]]
  id = "mobius.flip"
  variant = "mobius"
  fen = "4k3/8/n7/8/8/8/7P/4K3 w - - 0 1"
  kind = "play"                 # play | predict | watch | puzzle | game
  goal = ["h2a6"]               # play: any of these; predict: from = "h2"
  text = "Take the knight - across the seam."
  hints = ["the h-pawn", "the edge on the right is the edge on the left, mirrored"]
  view = { shape = true, follow = "route" }
  ```

- **One checker, the engine's.** `play` accepts any move in `goal` that is also in
  `Game::legalMoves()`; `predict` compares the clicked set with the legal destinations;
  `puzzle` asks `ai::chooseMove` for a mate score; `game` runs the opponent with a capped
  depth. No lesson can be "correct" by special case.
- **Tests:** every lesson's goal moves are legal; every `predict` lesson's answer set is
  non-empty; every puzzle is a forced mate the search finds; every lesson loads. A rule or
  geometry change that breaks a lesson fails `ctest`, not a player.
- **State:** finished lessons in `Settings` (like `seenWelcome`); just-in-time cards each
  have a seen-flag there too.
- **Capture flag:** `--lesson <id> --shot` renders a lesson's opening frame, for review and
  for the store page.

## 5. Open questions

- Should World 0.5 (chess itself) exist, or should the store page simply say "for chess
  players"? A Steam audience that has never played chess is a large market and a hard
  teach; the cheap option is the predict-the-moves format, which needs no new text.
- How far should the shape view be shown before the player has played a flat game? It is
  the hook, but the flat board is where moves are readable.
- Does the tutorial need voice-over-style captions timed to the move camera, or is a fixed
  text line under the board enough? The latter is cheaper and localizes trivially.
