# M10 — Per-Variant Trainable AI

**Goal:** a competent opponent for *any* variant, including ones the developers
never saw. This is a research milestone; the plan commits to the infrastructure
and to an honest evaluation methodology, not to a strength number.

**Scope and waves.** M10 is split across two release waves. **M10.2-M10.3 (search + the
derived baseline evaluator) ship in Wave 2**, before the Steam demo: they are the minimum
viable *generalizable* opponent that every curated variant needs, and they are deliberately
small. **M10.4-M10.5 (learning + evaluation methodology) stay in Wave 3**, after the demo, as
the research half that raises the ceiling. The demo does not wait on a trained model - only
on the floor described in M10.3.

## M10.1 Why this is hard here

Every standard chess engine assumption is unavailable: no handcrafted piece
values (pieces are user-defined), no known board size, no colour-binding
heuristics (Klein bottles destroy them), no opening book, no endgame tables, and
a branching factor that can be enormous (5-D chess move sets are huge). So the
architecture must *derive* its knowledge from the variant definition and from
self-play, and must degrade gracefully.

## M10.2 Search

- Generic alpha-beta with iterative deepening, transposition table (Zobrist from
  ARCH §11), move ordering, quiescence over "forcing" moves — where "forcing" is
  derived from the variant (captures, checks, and VM-declared forcing effects)
  rather than hardcoded.
- MCTS/PUCT as an alternative backend, selected per variant, because enormous
  branching factors favour it. Both backends share the same move generator and
  are compared on the same benchmark set.
- Search operates on move *sets* where the variant demands it (M6), so the
  temporal case is not a special case bolted on.
- **Tests:** search returns a legal move for every shipped variant; fixed-depth
  search results are deterministic and reproducible; no allocation in the inner
  loop; mate-in-N puzzle suites per variant.

## M10.3 Automatic baseline evaluation

Before any learning: a *derived* evaluator computed from the variant spec —
mobility-based piece values estimated by Monte-Carlo sampling of each piece type's average
move count on random positions of that variant's geometry, plus royal safety, plus
goal-distance terms for `OnEnterRegion` win conditions. This alone gives a playable opponent
for a brand-new variant with zero training, and it is the baseline every learned model must
beat.

**Minimum viable opponent (the demo gate).** The floor this milestone must clear for the
demo is intentionally low: a move that is always legal, takes what it can, and does not hang
a piece to an immediate reply, at search depth one or two, with no training. On top of the
existing movegen and legality that is roughly a weekend of work, not a research result, and
the mobility sampling is what extends it from standard chess to every variant for free.
Everything else in M10.3 is refinement above that floor. What the demo requires is the floor
— an opponent that never plays an illegal move and never makes a board look broken — not the
full evaluator.

## M10.4 Learning

- NN input encoding derived from the variant (cell planes per piece type per
  colour, field planes, geometry-aware adjacency), with a fixed maximum shape so
  one architecture serves many variants.
- Self-play pipeline: generation, training, gating (a new model must beat the
  current one over a statistically significant match before promotion).
- Inference in-engine: a small dependency-free int8/int16 quantized evaluator so
  the shipped binary needs no ML runtime, floats confined to the (isolated,
  non-core) trainer. **[INVARIANT]**
- Reproducibility: seeds, data provenance, model hashes recorded; a model is
  identified by `(VariantId, modelHash)`.

## M10.5 Evaluation methodology

- Per-variant Elo ladders versus the derived baseline and versus fixed-strength
  reference opponents; for standard chess, versus a published engine at limited
  depth as an external sanity check.
- Published results include hardware, time control, and confidence intervals.
  No strength claims without them. **[INVARIANT]**

## Acceptance facts

1. Every shipped variant gets a legal-playing opponent with zero training.
2. Standard-chess search passes a mate-in-N suite and reaches a documented,
   measured strength at a fixed time control.
3. The learning pipeline promotes a model only through a gated match.
4. Inference adds no float arithmetic to the engine core and no new runtime deps.
5. All results are reported with hardware, time control, and CIs.

## Status

**M10.2-M10.3 floor built (2026-10-29), `src/ai/` (layer 85, integer-only, in the arch
test's no-float list).**

- `ai::Evaluator` derives every piece's value from its average move count over 48 random
  positions of the variant's own board with blockers at half the opening density (standard
  chess comes out pawn 82 / knight 242 / bishop 290 / rook 425 / queen 712), plus a mobility
  term (3 per move of difference) so the opponent develops rather than shuffles.
- `ai::chooseMove` is negamax alpha-beta, depth 2 plus a capture/promotion quiescence of 4,
  over `Game::legalMoves`/`play`/`undo` - so rules, seams, flips and timelines are respected
  with no code here. MVV-LVA ordering, a node budget (60k default, nodes not time, so a
  move is reproducible), a stop flag, and "same side still to move" handled for multi-jumps
  and temporal turns. Deterministic; ties go to the first move in generation order.
- `Session` runs it on a worker thread (`std::async`, the game copied) via `advanceEngine`
  from the frame loop; the board is locked on the computer's turn; undo goes back to the
  player's turn; a stale answer is dropped. Settings `vs_computer` / `computer_plays_white`,
  on the new-game screen and in Settings. Script verb `engine` plays one engine move
  (captures: `--script $'click e2\nclick e4\nengine'`).
- Tests: `tests/unit/ai/test_search.cpp` (a legal, reproducible move on all 18 shipped
  variants; takes a free piece; does not hang its queen to a defended pawn; mate in one;
  standard values in order), and three session tests.

**Endgame conversion and difficulty levels (2026-10-30).** The floor above could not convert
a won endgame: depth 2 cannot see a mate three moves away, nothing in the evaluation rewarded
driving the lone king out, and the search did not know a repeated position is a draw, so K+R+B
against K shuffled for ever. Fixed in `src/ai/`:

- `chooseMove` is **iterative deepening** (1 up to `SearchOptions::depth`, default a cap of 64;
  `maxNodes`, 60k default, is what bounds it). The answer is the best move of the last
  *completed* iteration; an iteration the budget or stop flag cuts short is discarded (the
  search unwinds with an aborted flag, no half-searched static scores travel up); if depth 1
  does not finish the first move in order stands. The previous best move is searched first.
- **Transposition table**, fixed size (about 2 slots per budgeted node, a power of two, up to
  2^20), allocated per `chooseMove`, keyed by `Game::currentHash()` (side to move included),
  full key compared, mate scores stored relative to the node. On a temporal variant the hash
  does not cover the boards, so the table orders moves there and never cuts off.
- **Repetition is a draw in the search**: at ply > 0 a position already seen (in the game or on
  the search line, `Game::repetitionCount() >= 2`) scores 0; the halfmove clock is `Game::result`'s.
- **Mop-up term** in `Evaluator`, derived from the variant, no chess tables: for variants with
  exactly one royal piece type, the royal's pseudo-legal move count on every cell of an empty
  board and (up to 1024 cells) a uint8 BFS distance table over its move graph. When one side
  has only royals left and the other has at least a median piece's worth, the evaluation is
  material + 18 * (best royal mobility - the lone king's) + 9 * (largest distance - distance
  between the kings), signed for the side to move, replacing the mobility term. No royal or
  several royal types: skipped.
- **Levels** `ai::optionsForLevel(1..5)` (default 3, `Session::setEngineLevel`, setting
  `computer_level`, slider on the new-game screen and in Settings): depth cap / node budget /
  root noise = 1: 1 / 2k / 200, 2: 2 / 8k / 100, 3: 4 / 30k / 30, 4: none / 60k / 0,
  5: none / 100k / 0. Root noise is a deterministic offset from the position hash, the level and
  the move (`base/rng.hpp`), never added to a mate score, so a mate in one is never missed. The
  mop-up term is on at every level.
- Tests: mates with K+Q, K+R, K+R+B against a lone king within the bounds (fast, small
  budgets) and level 4 against a level 2 defence (slow); no repetition or draw-clock draw;
  tight budgets give a legal reproducible move; every level deterministic and legal on all 18
  variants; level 1 never misses a mate in one; level 4 beats level 1 over two games (slow);
  session level and setting round trip.
- Measured (Release, standard board): about 14k nodes/s, so level 3 is ~2 s, level 4 ~4 s and
  level 5 ~7 s a move in a middlegame. Mates, in plies (both sides engine): level 4 against
  level 4: KQK 15, KRK 31, KRBK 83; level 4 against level 2: 13, 23, 15; level 3 against level 3:
  31, 55, 21; level 1 against level 1 draws, as intended. Level 4 beat level 1 from the start
  position as either colour. The node rate is the cost of the legal-move generator's attack
  test (`MoveGen::isAttacked`), not of the search; a faster one would raise every level.

Not built: MCTS, the goal-distance term for `OnEnterRegion` wins, a pawn-progress bonus and
checks in the first quiescence ply (optional extras), a mop-up term for variants with several
royal types, time-based strength. Cost to know: on glued 3-D boards each node pays the
forward-scan attack test (ADR-0010); in a Debug build that is ~30 ms a node on `torus3d_8x8`,
which is why the search runs off the frame. The first use of the opponent as a design tool is
`docs/design/variant-lineup.md`.

Originally: planned, not started. **Split across waves:** M10.2-M10.3 (the baseline opponent) is in
Release Wave 2, ahead of the M16.3 demo, because a Next Fest build with no opponent reads as
a tech demo; M10.4-M10.5 (learning and evaluation) remain in Wave 3. Depends on the existing
movegen and legality (done); the search is shared with M15's puzzle solver, which is why the
campaign stays in Wave 3 and the demo does not.
