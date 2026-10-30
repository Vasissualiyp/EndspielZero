# Which variants to ship, and how each should start

Brainstorm, 2026-10-29. Two questions: **which boards** go in the demo and the launch
library, and **which starting position** makes each one an engaging game rather than a
puzzle that is over on move one. Unlike most brainstorms this one has numbers: the new
baseline opponent (M10.2-M10.3, `src/ai/`) was run over every shipped start and over
candidate replacements.

---

## 1. How a start position was judged

Four numbers per start, all from the real engine:

| Measure | Meaning | Good |
|---|---|---|
| **caps@1** | captures available to White on move one | 0 |
| **contact** | White first moves after which Black has *any* capture | 0 of N (standard chess: 0/20) |
| **first capture** | the ply of the first capture when the opponent plays itself (2,500 nodes per move) | 5 or later |
| **decided by** | the ply the self-play game ended, if it did within 40 | not before ~20 |

Caveat: the opponent is the demo floor (depth 2 plus captures, material plus mobility), so
"decided by ply 3" means *a shallow search found a forced win from the start*. That is a
broken opening whatever a strong player would do. "Still going at 40" does not prove a
start is good, only that it is not trivially broken.

## 2. What the shipped starts look like

| Variant | caps@1 | contact | Verdict |
|---|---|---|---|
| standard, cylinder, mirrorbox, charged, mustcapture, atomic | 0 | 0/20 | calm; the familiar opening |
| hyper4_8x8 | 0 | 0/99 | calm |
| 5d | 0 | 0/20 | calm (it is standard chess on its first board) |
| **torus** | 6 | 52/54 | **queens see each other across the wrap on move one** (d1→d5); the self-play is decided by ply 7 |
| **atomic_torus** | 6 | 50/54 | **White wins on move one** (Qxd5 explodes next to the king) |
| **mobius** (now with pawns) | 4 | 24/24 | **the rooks take each other across the twist on move one** (a1xh8: on a Möbius band the two back ranks are one 16-cell loop) |
| klein | 2 | 45/48 | contact from move one; the self-play stripped one side bare by ply 40 |
| cube5 | 5 | 56/56 | contact from move one - every first move allows a capture |
| hyper4 | 8 | 37/39 | contact from move one |
| torus3d / torus3d_twist | 1 | 26/34, 34/42 | mild contact; the 4x4x4 armies are tiny |
| **torus3d_8x8** | 15 | 176/183 | **decided by ply 2** in self-play |
| t6 | 0 | 33/41 | contact on move two |

The pattern: **every board with a glued rank has a broken start**, for the same reason. On a
glued axis an army's back is also a front, so the back rank faces the enemy across the wrap,
and the usual array puts sliders on open lines aimed straight at each other.

## 3. Principles for a start on an exotic board

1. **Wall every face that touches the enemy.** On a glued rank axis that is both the front
   and the back. A pawn wall behind the pieces is not decoration; it is the board's own
   requirement.
2. **Leave a gap of two empty ranks on each glued side**, so a knight's jump across the
   wrap lands on nothing and pawns meet only after each side has pushed. On a torus this
   means the board needs ~10 ranks for full armies.
3. **No slider may see an enemy piece down any line, across any seam, at move one.** On a
   Möbius band the two back ranks are *one* loop through the seam - so the corners must hold
   something that blocks (a knight), or the edge files must be empty.
4. **The royal piece is never adjacent to a seam at the start,** and in atomic variants
   never within one capture of a square the enemy can reach in two moves.
5. **Symmetric under the board's own symmetry.** On a twisted board that symmetry *is* the
   twist (White's h1 corresponds to Black's a8), not the usual left-right mirror.
6. **The hook should appear early.** Some useful seam crossing should exist within the first
   three or four moves, or the variant plays like standard chess for its first ten minutes
   and the player never sees why it is different.

These are cheap to check, so they should become a **start-position linter**: a test (or
`endspielzero lint-start <variant>`) that reports caps@1, contact and the first-seam-crossing
ply for every shipped variant and fails on caps@1 > 0 unless the variant says the contact
is deliberate. The probe code that produced these tables is ~60 lines over `ai::chooseMove`.

## 4. Proposed starts, measured

| Board | Proposal | caps@1 | contact | first capture | decided |
|---|---|---|---|---|---|
| mobius (current) | `rnbqkbnr/pppppppp/…/PPPPPPPP/RNBQKBNR` | 4 | 24/24 | ply 1 | - |
| **mobius, narrow** | `1nrqkrn1/1pppppp1/8/8/8/8/1PPPPPP1/1NRQKRN1` | **0** | **0/18** | ply 5 | ply 25 |
| mobius, knights in corners | `nrbqkbrn/pppppppp/…/NRBQKBRN` | 2 | 22/22 | ply 7 | not by 40 |
| mobius, narrow with bishops | `1rbqkbr1/1pppppp1/…/1RBQKBR1` | 2 | 18/18 | ply 1 | ply 19 |
| torus (current, 8x8) | `8/8/8/rnbqkbnr/pppppppp/8/PPPPPPPP/RNBQKBNR` | 6 | 52/54 | ply 5 | ply 7 |
| torus, walled, 8x8 | `8/pppppppp/rnbqkbnr/pppppppp/8/PPPPPPPP/RNBQKBNR/PPPPPPPP` | 0 | 16/16 | ply 2 | ply 13 |
| **torus, walled, 8x10** | `8/8/pppppppp/rnbqkbnr/pppppppp/8/8/PPPPPPPP/RNBQKBNR/PPPPPPPP` | **0** | **0/16** | ply 7 | **not by 40** |
| atomic_torus, walled, 8x10 | same as above | 0 | 0/16 | ply 3 | **ply 3** (a knight jumps the back wall and its blast reaches the king) |
| klein (current) | `8/8/8/4rnkr/8/8/8/RNKR4` | 2 | 45/48 | ply 5 | not by 40 |
| klein, armies apart | `8/8/8/8/rnkr4/8/8/4RNKR` | 2 | 46/48 | ply 3 | not by 40 |

Recommendations:

- **Möbius → the narrow start.** The a- and h-files are empty, and the two edge pieces are
  knights, which block the back-rank loop. The bishops go, which costs little: on a
  Möbius band a bishop changes colour across the seam anyway. It is the only candidate that
  is quiet at move one, and its self-play reaches a seam capture and a decision in a normal
  game length. **Do this one now**: it is the board the new flipping pawn lives on, and it
  is the trailer board.
- **Torus → 8 files x 10 ranks, walled front and back.** It is the only torus start that
  measured calm, and it keeps the full army. The extra two ranks matter: on 8 ranks a walled
  start still has the pawn walls one rank apart (contact 16/16).
- **atomic_torus → drop it from the curated list, or give it 12 ranks.** Atomic punishes
  contact much harder than chess does, and on 10 ranks a knight still reaches a blast square
  by ply 3. Measure a 12-rank version before shipping it anywhere a newcomer will find it.
- **Klein → needs its own design pass.** Neither start is quiet: with both axes glued (one
  twisted), the armies are in contact wherever they stand on an 8x8. Candidates to measure
  next: 8x10 with the walled array, and a version with pawns whose orientation axis is the
  *file* - on the Klein bottle it is the rank seam that flips the file, so file-wise pawns
  would actually turn round (ADR-0020); rank-wise pawns never do.
- **cube5, hyper4, torus3d_8x8 → the same treatment as the torus.** Their contact comes from
  the extra axes putting armies one step apart along an axis nobody looks at. A linter run
  is the first step; torus3d_8x8's ply-2 decision is the most urgent.

## 5. The lineup

### Demo / Next Fest (Feb 2027): six boards, each with one idea

| Board | The one idea | Why it earns a slot |
|---|---|---|
| **standard** | nothing new | the anchor; proves it is real chess; the AI's home ground |
| **cylinder** | the edge joins | the gentlest exotic board; calm start already |
| **Möbius** (narrow start) | the edge joins *mirrored* - and a pawn turns round | the strongest single hook: nothing else has a pawn that walks home after crossing a twist; the band in SHAPE view is the trailer shot |
| **torus** (8x10 walled) | no edges at all | the donut; needs the new start to be playable |
| **mirrorbox** | walls reflect | bank shots are instantly readable; calm start already |
| **cube5** | a third axis | the "wow, 3-D" board; needs a start pass first |

Time travel (**5d**) is the most impressive and the hardest to teach; put it in the demo
only behind the tutorial's World 7, or show it in the trailer and keep it for launch.
Rule variants (atomic, mustcapture) are familiar from chess sites and cost nothing; keep
them in the library but not the demo's front door - they do not show what makes *this* game
different.

### Launch library, beyond the shipped set: ideas worth authoring

Each is data only (a `variants/*.toml`), unless marked.

1. **King of the Torus** - torus, walled start, plus a rule: a king that reaches the four
   centre squares wins (`on_move_end` + `end_game`). Short, decisive games; gives the
   no-edges board a goal other than mate.
2. **Möbius Race** - Möbius with pawns only plus kings: the first pawn to promote wins. The
   flipping rule *is* the game: crossing the twist turns a pawn round towards the near edge,
   so the seam becomes a shortcut that changes whose race it is.
3. **Bank Shot** - a long, narrow mirrorbox (6x12) with mirror walls on the files: every
   bishop is a billiard ball. Calm start by construction (walls glue nothing).
4. **Atomic Möbius** - explosions across the twist. Measure first; probably 8x10.
5. **Twisted Cylinder ladder** - the same army on a cylinder, then Möbius, then Klein: a
   three-step campaign leg (M15) where the only change is the gluing.
6. **Torus mini** - 6x8 torus, half armies (rook, knight, king, knight, rook + pawns), walled:
   five-minute games for the AI to be good at.
7. **Mirror Torus** - files glued, ranks mirrored (a cylinder with reflecting ends): pawns
   bounce back off the far wall *flipped* (a mirror across the orientation axis also turns a
   pawn round, ADR-0020), which gives promotion a second life.
8. **4-D standard** (hyper4_8x8 already) - promote it once its start is linted: contact 0/99
   is the calmest exotic board there is.
9. **Klein pawns** - pawns along the file axis on a Klein bottle, so they flip at the twisted
   rank seam. Needs a promotion *region* that is not a last rank (there is none on a closed
   surface); that is a small loader feature (`[promotion] cells = [...]`), not engine work.
10. **Charged → "Veterans"** - rename; the mechanic (a piece that has moved three times
    becomes a knight) is good, the name says nothing.

### Not worth shipping as-is

- **atomic_torus** at 8x8 (won on move one), **torus3d_8x8** (decided by ply 2) - until
  re-started.
- **t6** in the main library: the derived values show why (a knight is worth ~3x a rook on a
  six-dimensional torus); it is a spectacle for the trailer and the campaign finale, not a
  game a person can read.

## 6. Next steps

1. Apply the Möbius narrow start (update the perft golden; the trailer does not use Möbius's
   start).
2. Author `torus` at 8x10 walled and re-pin its perft; check the overture and shape view on a
   non-square torus.
3. Build the start-position linter as a test over `shippedVariantNames()`.
4. Measure Klein, cube5, hyper4 and torus3d_8x8 candidates with it and pick starts.
5. Author King of the Torus and Möbius Race - the two strongest new ideas, both pure data.
