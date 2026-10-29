# Project Skills Plan

Repetitive procedures in EndspielZero live in the repo as project-local skills
(`.claude/skills/<name>/SKILL.md`), not in anyone's head. Rationale: this
codebase is deliberately uniform — adding the 20th piece, the 5th geometry, or
the 12th effect primitive is the *same* sequence of edits every time, and that
sequence spans several directories plus tests plus docs. Encoding it makes the
work fast for agents and consistent for humans.

## Conventions **[INVARIANT]**

- Prefix every skill `cb-` so they are unmistakable in a mixed skill list.
- Frontmatter `description` states the trigger in the user's words, not ours.
- Every skill ends with the same two steps: **run the gate** and **update the
  docs row**. A skill that can leave the repo red is a bug.
- Skills are tested (M0.7): frontmatter validity, referenced paths exist,
  referenced commands exist in the dev shell.
- A skill is written the *third* time a procedure is performed, not the first —
  by then its real shape is known. The table below records the intent; each row
  is created in the milestone named.

## Skill catalogue

### `cb-tdd-step` (M0)
The core loop. Given a behaviour to add: pick the layer, write the failing test
first (which file, which label), implement minimally, run `ctest -L <label>`,
then the fast gate, then commit with the message convention. Encodes which
preset to use for what (`dev` for iteration, `asan` before commit, `release` for
bench) and the rule that a red suite blocks progress.

### `cb-gate` (M0)
Run the full milestone exit gate: both compilers × Debug/Release, asan, tsan,
tidy, coverage thresholds, perft goldens, replay corpus, bench comparison.
Explains how to read each failure class and what *not* to "fix" (e.g. never
regenerate a golden to make a test pass — investigate first).

### `cb-new-module` (M0)
Scaffold a module in a layer: directory, `CMakeLists.txt` with only the allowed
downward link edges, header/source pair with SPDX header, a test file registered
under the right CTest label, and the AGENTS.md index row.

### `cb-adr` (M0)
Create the next-numbered ADR from the template, with the required sections
(context, decision, consequences, alternatives, how to reverse). Refuses to edit
an existing ADR — supersede instead.

### `cb-new-piece` (M1)
Add a piece type: declare its atoms in the variant TOML, expected direction
counts per dimension count, a unit test for expansion, an oracle differential
test, a movegen test on a hand-built position, and — if the piece appears in a
perft-covered variant — a golden update with justification.

### `cb-new-variant` (M1)
Add a variant: the TOML file under `variants/`, loader validation test, a
starting-position golden, a perft golden at the depths that run in under a
second, a short `docs/variants/<name>.md` describing its rules, and the registry
row. Includes the checklist for the things authors forget (promotion regions,
royal piece declaration, orientation axis per colour, draw/stalemate policy).

### `cb-perft-golden` (M1)
Generate, verify, and update perft goldens. Encodes the depth budget per variant,
where published reference counts come from for standard chess, the rule that a
changed golden needs an explanation in the commit message, and the divide-and-
conquer procedure (perft-divide bisection against the oracle) for localising a
movegen bug to a single move.

### `cb-bench-baseline` (M1)
Run the bench suite in the right preset with the machine quiesced, record to
`bench/baselines/<arch>/`, and interpret the delta report. Encodes the noise
threshold and the requirement to record CPU model and governor alongside numbers.

### `cb-new-geometry` (M3)
Add a boundary topology: the identification list, the induced direction
transform, a table-vs-analytic differential test, involution/closure property
tests for the generated transition group, a ray-walk golden (a rook's orbit
length on the surface is a strong fingerprint), and the renderer seam metadata.

### `cb-new-effect` (M5)
Add a rule-VM primitive: the opcode, its typed signature, the validator rule
that rejects malformed use, the interpreter case, a step-budget cost, unit tests
for the effect in isolation, one variant that uses it as an acceptance test, and
the docs table row. Includes the determinism checklist.

### `cb-shader` (M4)
Add a shader/pipeline: GLSL source, CMake SPIR-V compile rule, descriptor layout
and push-constant declaration kept in sync with the C++ struct (plus the static
assert that keeps them honest), validation-layer clean run, and a RenderDoc
capture check.

### `cb-variant-from-description` (M5)
The high-leverage one: turn a natural-language description of a chess variant
("checkers, but on a torus") into a variant TOML plus its test set, by walking
the authoring checklist and the effect catalogue. This is the skill that makes
the sandbox usable by non-programmers via an agent.

## Non-goals

Skills do not replace `AGENTS.md`. AGENTS.md answers "where is everything and
what are the rules"; skills answer "walk me through this specific recurring
procedure". Keep the overlap to a cross-reference.
