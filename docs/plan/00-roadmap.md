# EndspielZero Roadmap

High-level plan. Every milestone has its own detailed plan file in this
directory. Read `docs/ARCHITECTURE.md` first.

## Rules of engagement (apply to every milestone) **[INVARIANT]**

1. **Test-first, always.** No production line is written before a failing test
   names the behaviour it must produce.
2. **100% of tests pass before the next step begins.** Not the next milestone —
   the next *step*. A red suite blocks all forward motion.
3. **No step is done without: unit tests, a property/differential test where an
   invariant exists, a doc update, and a bench if it is on a hot path.**
4. **Every architectural decision gets an ADR** in `docs/adr/`, numbered,
   immutable once merged (supersede, never edit).
5. **The naive oracle is sacred.** Optimisations are proven equal to it.
6. **Determinism is a test, not an aspiration.** Every milestone adds to the
   replay corpus.
7. **AGENTS.md is updated in the same commit** as anything that changes how an
   agent should navigate or build the repo.

## Milestone sequence

| # | Milestone | Ships | Status | Detailed plan |
|---|---|---|---|---|
| M0 | Foundations & infrastructure | nix flake, CMake, CI, test harness, L0 base | **done** | [M0](M0-foundations.md) |
| M1 | 2D generalized core engine, headless | standard chess, perft-exact to depth 6, ASCII CLI | **done** | [M1](M1-core-2d.md) |
| M2 | N-dimensional generalization | 3D/4D boards, dimension-lift invariance | **done**, dim dispatch deliberately not built | [M2](M2-nd-generalization.md) |
| M3 | Boundary geometry | cylinder, torus, Möbius, Klein, mirrors, N-D analogues | **done**, two gaps recorded | [M3](M3-geometry.md) |
| M4 | Vulkan renderer + interaction | playable 2D/3D/4D+ board on screen | **done**, three deviations recorded; M4.8/M4.9 follow-ups (frame pacing, animation budget) | [M4](M4-renderer.md) |
| M5 | Variant VM + custom fields | explosive, checkers, regional variants; quantum design | **done**, variant catalogue partial | [M5](M5-variant-vm.md) |
| M6 | Temporal / multiverse (5D chess) | faithful 5D chess, generalized extra axes | **done**; two axis generalizations planned, not required | [M6](M6-temporal.md) |
| M7 | GUI authoring: piece editor, then board/gamemode editors | author pieces and whole variants in the GUI: vector moves, fields, rules, 3-D and 2-D models | in progress; **only the piece editor moved into Wave 1** (2026-10-16 — the shipped piece models are not good enough to ship and must be re-authored by hand); the board and gamemode editors stay Wave 2. 2026-10-29: authored models persist and render (M7.3.4), element placement, save gated on the loader, fields/rules form (M7.2) | [M7](M7-editors.md) |
| M8 | Client–server multiplayer | authoritative server, lockstep-verified replay | not started | [M8](M8-net.md) |
| M9 | Steam Workshop + packaging | variant packages, validation, signing, distribution | not started | [M9](M9-workshop.md) |
| M10 | Trainable per-variant AI | search + learned eval, self-play harness | **split**: M10.2-M10.3 (baseline opponent) in Wave 2 - **its floor is built** (2026-10-29: derived evaluator, depth-2 alpha-beta, play vs computer); M10.4-M10.5 (learning) in Wave 3 | [M10](M10-ai.md) |
| M11 | The move camera | follow a move through any geometry: seams, mirrors, grid axes, 4-D+ | in progress: pose/policy, portalled and grid-axis behaviour, session integration and docs done; image goldens deferred (a pose property test stands in) | [M11](M11-move-camera.md) |
| M12 | Spectator, replay and the clip | cinema view, deterministic clip export, play a game from notation (marketing runner), shared camera presets, live spectating | in progress: cinema mode and the frame-sequence clip exporter done; the notation runner (M12.6) is planned for the marketing clips; presets/spectator are Wave 4 | [M12](M12-broadcast.md) |
| M13 | Generic variant overtures | a data-only variant animates on the library screen: derived surface, derived move, no scene code | in progress: signature, surface catalogue, selection and the demo move done (2-D 8x8); higher-D extruded grid outstanding | [M13](M13-overtures.md) |
| M14 | Onboarding, tutorial and first-run polish | a stranger is playing something interesting within ten minutes and understands why it is different | in progress: the first-run welcome and the curated "start here" path are built (M14.1/M14.2); the played tutorial and the legibility/accessibility pass are not | [M14](M14-onboarding.md) |
| M15 | Campaign: the geometry ladder | a designed progression with a concept boss per leg, ending on a `t6` puzzle finale | not started | [M15](M15-campaign.md) |
| M16 | Steam publishing: store, demo and release | Coming Soon page, build pipeline, demo/Next Fest, ratings, launch | in progress: M16.2 packaging + SteamPipe scaffolding done; M16.1 store page is process | [M16](M16-publishing.md) |
| M17 | Play on the shape: the geometry view | a button turns the play board into its own topology - the torus is a donut, the Klein bottle a bottle - and a move is played on it | not started, first in the release sequence's Wave 1 tail | [M17](M17-geometry-view.md) |
| M18 | Production polish: the game stops reading as a tech demo | capture/move juice, contact shadows, a cinema vignette/grade, an ambient background drawn from the game's own geometry instead of abstract confetti, and v1 audio | spec'd, not started; sits beside the release sequence rather than inside it - see its own Dependencies section | [M18](M18-production-polish.md) |
| M22 | Adjacent boards: the tiled view | a switch (2D and 3D) draws the board's neighbours through its glued edges - a cylinder repeats, a torus tiles, a Möbius band and Klein bottle tile with their flips - rings of neighbours go out from the centre, more transparent the higher the order, and they arrive from "infinity" and merge with the centre | planned (2026-10-30), not started; medium effort (about a week); **must be ready before the demo is published** - target mid-December 2026, see Wave 2 item 8b | [M22](M22-adjacent-boards.md) |

Each completed milestone's plan file ends with a **Status** section recording what was
built, what was deferred, and why - including the places where the plan turned out to
be wrong. Read those before trusting the plan text above them.

## Release sequence — visuals first

**Milestone numbers are stable identifiers, not a schedule.** M0-M6 shipped in numbered
order; from M7 on, the order the milestones are *built* is the priority order below, and a
milestone keeps its number even when it is built out of sequence. M7 is already in
progress, so renumbering it would invalidate every commit message and cross-reference that
names it; the release sequence is the single ordering truth instead. The table above is
sorted by number because a number is an ID; this section is sorted by *when*.

The order is chosen so the game is **showable before it is complete**: the things that
make a clip, then the things that make the clip convert, then the things that make it a
game.

**Wave 1 — clippable (the marketing hook).**

*Before the camera and the clip, land M4.8/M4.9 (frame pacing and the animation
budget).* A camera and a clip are only as good as the frame they are drawn in, both are
renderer/app work with no new feature surface, and the shipped loop is still fully
serialised with CPU-built menu geometry (M4). They are small, they unblock M11's frame
budget and M12's export cost, and nothing else in Wave 1 depends on them - so they go
first and can ship on their own.

1. **M11 — the move camera.** Depends only on M4/M6, both done. This is the single most
   clippable feature and the pitch itself: a piece wrapping a Möbius seam, a Klein turn, a
   6-D move. Nothing else adds as much store-page value, and nothing blocks it.
2. **M12 — the clip (export half).** The deterministic frame-sequence exporter and cinema
   mode, which turn M11 into a shareable file. The live-spectator and shared-preset halves
   stay in Wave 4, because they need M8 and M9.
3. **M13 — generic overtures.** The library screen already animates for the shipped
   variants; this makes it animate for *any* variant - polish, and the prerequisite for
   Wave 4's user content.
4. **M17 — play on the shape.** The button that turns the play board into its own topology
   and keeps the game playable on it. It comes **before** the store page on purpose: it is
   the single strongest trailer the game has - chess played *on a donut* is the store
   page's argument in one shot - and it needs no opponent, campaign or editor. It reuses
   the M13 warps, so it is a small step on top of work already done, and it is the one
   feature in this list a viewer will not have seen in any other chess game.
5. **M7 (the piece editor only) — re-author the pieces.** Pulled forward from Wave 2 at
   the user's direction (2026-10-16). The shipped piece models are primitive archetypes
   and are not good enough to ship: a person has to draw better ones, and can only do
   that once the editor exists. The pieces are in *every* clip, overture, shape-view shot
   and store-page frame, so the **piece editor only** — the shared authoring document and
   serializer (M7.0), a piece's movement and what it references (M7.1-M7.2), and the 3-D
   profile / 2-D icon editors that draw it (M7.3-M7.4) — lands **before** the store page,
   not in Wave 2. **The board editor and the gamemode editor (M7.5: axes, topology, start
   position, piece list, rules, policy, validate/save) are explicitly out of Wave 1** and
   stay in Wave 2 with the rest of authoring; a piece is authored on its own, ahead of the
   variant that will hold it. It depends only on M5/M6 (done) and shares the variant data
   model with M13, so it can run beside the visuals.
6. **M16.1 — the Coming Soon page.** It needs nothing but the clips M11/M12/M13/M17
   produce, and **wishlists accrue from the day it is live**, so it starts here, in
   parallel with the visuals, not at release. The Steam review is 7 business days; the
   app, fee, assets, trailer and content survey are the gate.

**Wave 2 — polish and demo (convert, and make Next Fest).**
7. **M14 — onboarding and first-run polish.** A stranger's first ten minutes: a tutorial,
   curated start-here modes, legible defaults, and an in-game answer to "what does a rook
   do on *this* board". A powerful store page that converts badly is the failure mode this
   wave exists to prevent.
8. **M10.2-M10.3 — the baseline opponent (the demo needs someone to play).** A sandbox
   with no opponent is what makes a build read as an engine tech demo. The *minimum viable
   opponent* - a legal move that takes what it can and never hangs a piece to an obvious
   reply, search depth one or two, no training - is deliberately small (a weekend on top of
   the existing movegen and legality, not a research result), and the derived evaluator
   extends it to *every* variant by sampling mobility on that variant's own geometry. It
   lands **before** the demo, so Next Fest has a legal opponent on every curated board;
   M10.4-M10.5 stay in Wave 3.
8b. **M22 — adjacent boards (the tiled view).** Added 2026-10-30 at the user's direction, **to be
    ready before the demo is published**: it is the second "the board is a surface" visual, and
    works in both the 2D and 3D board views. Scheduled to land
    **by mid-December 2026**, a clear two weeks ahead of the demo build (early January 2027,
    item 9), so the demo is cut from a tree that already has it. Cut line if it slips: M22.1
    (the pure copies + property test), M22.2 drawing in the 3-D board and the flat tokens, M22.3
    switch/button/setting and the minimal arrival animation (M22.4 without the ring stagger) are
    the **must-have**; picking (M22.5), rings beyond 2, and the third axis (M22.8) follow after
    the demo. See [M22](M22-adjacent-boards.md).
9. **M16.2 + M16.3 — the build pipeline and the demo.** The **February 2027** Next Fest is
   the target (October 2026's deadlines have passed): registration closes roughly 10-12
   weeks prior and the demo build must pass review ~3 weeks prior, so the pipeline and the
   demo must exist in **early January 2027**. Build the automated upload before it is under
   deadline, not during.
10. **M7 (the board and gamemode editors) — assemble and package variants.** The document,
    serializer and piece editor landed in Wave 1; this builds the board editor (axes,
    geometry/topology, start position) and the gamemode editor (piece list, rules, policy),
    then validate → test-drive → save as a package (M7.5). It produces demo content,
    delivers the "make your own" promise, and feeds M9.

**Wave 3 — the game (the full-strength opponent and a reason to finish).**
11. **M10.4-M10.5 — the AI, learned.** The baseline opponent already shipped in Wave 2; this
    is the research half - a learned, gated model that must beat the derived baseline, with
    strength reported honestly. It raises the ceiling; it does not gate the demo or the
    campaign.
12. **M15 — the campaign.** Needs the search and the baseline opponent (M10.2-M10.3, from
    Wave 2) for its bosses and M14 for its on-ramp: the ladder from `standard` to the `t6`
    puzzle finale that gives the sandbox a shape and an ending.
13. **M8 — multiplayer.** Authoritative server and verified replay. Independent of M10;
    either can lead the wave, but if only one can precede the marketing peak, a stronger AI
    matters more to a single-player store audience.

**Wave 4 — content at scale.**
14. **M9 — Workshop.** Needs M7 (packages) and M13 (overtures), so it is last by
    construction - the content pipeline that gives the game its tail.
15. **M12 remainder — shared camera presets and live spectating.** Presets ride M9;
    live spectating rides M8.

**Wave 5 — release.**
16. **M16.4 + M16.5 — the release checklist and launch.** After the game wave, so the
    marketing peak lands with an opponent (M10.2-M10.5), a campaign (M15) and a content
    pipeline (M9) already in place - the reviews punish the gap otherwise.

Waves 1-2 are what the marketing timeline cares about: days-to-weeks of work on top of an
already-done M0-M6, every step independently shippable, and the February 2027 Next Fest is
the fixed point they point at. A milestone may be moved between waves only if its
dependencies allow it; the dependencies are recorded at the end of each plan file and must
not be quietly broken.

### Why the foundation is ordered this way (M0-M6)

- **Headless before pixels (M1 before M4).** The engine's design will move a
  lot during M1–M3; renderer code written against an unsettled core is thrown
  away. Graphics is also the least testable part of the system, so it should be
  built against a core that is already proven.
- **Dimensions before topology (M2 before M3).** Boundary identifications act on
  coordinate *and* direction vectors; getting that right is far easier when
  arbitrary-dimension vectors already work and are tested.
- **Topology before the renderer (M3 before M4).** The renderer must visualise
  seams and wrapping. Designing the view layer before knowing what a Klein seam
  looks like in the data guarantees a rewrite.
- **Variant VM after geometry (M5).** The VM's effect primitives need to speak
  in terms of regions and neighbourhoods, which are geometry concepts.
- **Time travel late (M6).** It is the most rule-dense subsystem and it sits on
  top of everything else; it is also where an unproven core hurts most.
- **Authoring before distribution (M7).** The whole point of the sandbox is that
  someone can make a game without touching C++; the Workshop (M9) exists to move
  that authored content, so the editors have to exist first. Multiplayer (M8) is
  independent and could swap with M7 without loss.
- **M7 onward is scheduled by the release sequence, not by number.** M11-M14 are numbered
  after M10 only because the sequence is append-only and M7 was already in progress; the
  build order is the Release sequence section above. That is where "visuals and clippable
  things before multiplayer, learned AI and Workshop" is stated and defended - with two
  exceptions, both about what the marketing peak actually needs. The *baseline* opponent
  (M10.2-M10.3) is small and lands in Wave 2, because a Next Fest demo needs someone to
  play, not a research result. And *only the piece editor* of M7 lands in Wave 1 (2026-10-16,
  the user's direction) - not the board or gamemode editors - because the shipped piece
  models are placeholder primitives that only a hand can improve, and they are visible in
  every clip the store page points at.
- **Escape hatch:** if visible progress is needed sooner than M4, M2 ends with a
  terminal (TUI) multi-slice viewer — cheap, testable, and enough to play 3D/4D
  boards by hand. The Vulkan work stays where it is.

## Milestone exit gates

A milestone is complete only when **all** of the following hold. These are
checked by `nix flake check`, which is the single source of truth. **[INVARIANT]**

- `ctest` green on gcc and clang, in Debug and Release.
- Green under ASan+UBSan; green under TSan once threads exist (M4+).
- `clang-tidy` clean at the configured level; no new warnings at `-Wall -Wextra
  -Wpedantic -Werror`.
- Line coverage of `src/core/` ≥ 90%, branch coverage ≥ 80%.
- The milestone's named acceptance facts (in its plan file) all hold, each backed
  by a named test.
- Benchmarks recorded to `bench/baselines/`; no unexplained regression >10%.
- No allocation-tripwire violations in movegen tests.
- Replay corpus reproduces byte-identical hashes.
- `docs/ARCHITECTURE.md`, `AGENTS.md`, and the milestone plan reflect reality.

## Cross-cutting risk register

| Risk | Milestone | Mitigation |
|---|---|---|
| Combinatorial blowup of direction sets in high dims | M2 | budget check at load; per-variant limits; atom order caps; tests asserting table sizes |
| Transport-table memory explosion | M3 | boundary-only tables + analytic fallback + budget config (ARCH §4.2) |
| Non-orientable geometry has genuinely ambiguous rules (what is "forward" for a pawn on a Klein bottle?) | M3 | make it an explicit, documented, per-variant policy choice; test each policy |
| Rule VM expressiveness insufficient for a target variant | M5 | primitive-extension process documented; each target variant is an acceptance test, so gaps surface early |
| 5D chess rules subtly wrong | M6 | golden test corpus transcribed from the reference game's known positions/puzzles before implementing |
| Vulkan complexity delaying everything | M4 | strict scope: one pipeline, instanced quads/cubes, no PBR, no shadows in M4 |
| The GPU is idle while the CPU rebuilds every menu/overture each frame, and the frame is fully serialised | M4/M11/M12/M13 | pipeline the frame and render to the swapchain (M4.8); memoise/adaptively subdivide the overtures and move them to the instanced path (M4.9/M13); record a frame-time bench; watchable wins over a larger scene |
| Determinism drift (float, iteration order, hash) | all | no floats in engine core **[INVARIANT]**; canonical iteration orders; replay corpus in CI |
| Quantum chess may not fit the model | M5 | scoped as a design study with an interface hook, not promised as delivered |
| Canonical variant serialization drifts from `VariantId` | M7 | pin a load→save→load round-trip with an unchanged `VariantId` before any writer ships |
| User-defined piece geometry is open-ended | M7 | ship the radial revolve + mirror subset first; the primitive fallback keeps any piece playable |
| Camera-induced motion sickness / unreadable high-D view | M11 | safe-zone oracle, lead/pull bounds, shot deadline, cut on grid axes; watchable wins over faithful |
| Picking desynchronises from an animating camera | M11 | interaction is locked while a shot is in flight; picking resumes from the settled pose; explicit test |
| Clip export drags a codec into the deterministic core | M12 | guaranteed output is a frame sequence; muxing is external and best-effort |
| A derived overture surface is subtly wrong (invisible in a still, wrong in motion) | M13 | differential test: the derived surface must equal the hand-authored one where both exist; closure arithmetic shared |
| A powerful store page converts badly (first-run friction, unexplained terms, empty board) | M14 | onboarding is a milestone, not a task; the tutorial is played, not read; curated start-here modes; the marketing peak is gated on it |
| The marketing clock starts before the visuals exist, so there is nothing to promote | M11/M12 | Wave 1 is the hook and is first in the release sequence; until it lands the hand-authored overtures are the teaser |
| A campaign boss is an unfair strength gate (players lose to search, not the board) | M15 | bosses are scoped objectives, not Elo walls; handicaps are declared in the UI; the finale is a puzzle with a unique engine-proved line |
| Campaign content rots when geometry or rules change | M15 | every scenario is validated against the live engine in CI; a rule change that breaks a scenario fails a test |
| A Steam Next Fest deadline is missed (registration or demo build review) | M16 | build pipeline exists before the demo is due; target February 2027; submit demo and page ~3-5 weeks early; a title gets only one Next Fest |
| AI-content disclosure omitted or misanswered on the store page | M16 | content survey completed honestly in M16.1 (pre-generated vs live-generated) before review |

## Definition of "generalizable" used throughout

A feature is generalizable if adding the next member of its family is *data or
configuration*, not new engine code. Concretely, the acceptance question for each
milestone is: "what does it cost to add the (n+1)-th case?" Answers are recorded
in each plan file's **Generalization test** section.
