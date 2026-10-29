# M16 — Steam Publishing: Store, Demo and Release

**Goal.** The game is on Steam, discoverably, from a public Coming Soon page through a
demo that carries a Next Fest, to a launch that satisfies Valve's checklist and terminates
cleanly. This is the milestone that turns everything else into a product people can buy.

**Exit condition.** A public Coming Soon page is collecting wishlists; a build uploads to
Steam through an automated pipeline; a demo app is published, has passed build review, and
has carried a Next Fest; the content survey (including AI disclosure) and age ratings are
complete; the game ships and the release process terminates.

**Relationship to M9.** M9 is *Steam Workshop* — the UGC content pipeline (server APIs,
validation, signing, distribution of variant packages). M16 is the *storefront and
release*: page, depot/build pipeline, demo, ratings, launch. They do not overlap: M7 builds
the package shape, M9 moves authored content, M16 ships the game itself.

---

## M16 is phased, because its parts belong to different release waves

Like M12, M16 is one numbered milestone whose work spans the whole schedule. The store page
belongs to the *visuals* wave (it needs the clips, and wishlists accrue over time); the
release belongs after the game wave.

### M16.1 Store page and Coming Soon (early — with the visuals)

- **Steamworks account and Steam Direct fee** ($100 per app, recoupable) with tax and bank
  details; "Create New App…" to get an AppID.
- The **"Your Store Presence" checklist**: capsule/branding images, screenshots, written
  description, tags, and a **trailer** (at least one public trailer is required for Next
  Fest trailer consideration). Most of the assets are captured directly from the game
  (`--shot` and the M12 clip exporter), so this is packaging, not new rendering.
- **Content survey**: age-rating questions and the **AI-disclosure** question (pre-generated
  vs live-generated). The project is AI-assisted, so this is answered honestly and early;
  it is a compliance item, not an afterthought.
- **"Mark As Ready For Review"** → Valve review, submitted at least **7 business days**
  before the intended live date → **"Post as Coming Soon"**.
- Rule to respect: a new product must have its Coming Soon page public for **at least two
  weeks** before release.

### M16.2 Build and depot pipeline

- SteamPipe/steamcmd: app build scripts, depots, branches (a `beta` branch for Next Fest
  and press), and a **repeatable, scripted upload** that CI can run. No hand-copied builds.
- A clean-machine smoke test of a downloaded build: the game launches, loads a variant,
  plays a move, and exits — the CI equivalent of the `nix flake check` gate, for the
  shipped artifact.
- The build must be **self-contained**: the fonts (`src/render/CMakeLists.txt`) and
  `variants/` ship beside the binary; a packaged build is tested from a clean directory.

### M16.3 Demo and Next Fest

- A separate **demo app** attached to the base game (per the Demos docs), built from the
  M14 demo scope: curated variants, the tutorial, the read-only library, the baseline
  opponent (M10.2-M10.3), no editor, no online. A demo where the player can only move
  pieces against nobody reads as a tech demo, so the opponent is part of the demo scope,
  not the full game - and it depends on the M10.2-M10.3 slice landing in Wave 2, not on the
  learned AI (M10.4-M10.5) that stays in Wave 3.
- **Next Fest participation** — three editions a year (February, June, October); a title
  may join **only one ever**. Registration is from the base game. Workback: demo and store
  page submitted for review **~3–5 weeks** before the Fest; the demo must be live before
  the Fest starts; the game must not release before the Fest concludes.
- Target: **Steam Next Fest, February 2027** (October 2026's registration and demo-review
  deadlines already passed). Confirm exact dates on the event page; the general shape is
  registration ~10–12 weeks prior, demo build review ~3 weeks prior.
- Optionally opt in to the **official trailer** selection (requires a public trailer on the
  store page before registration closes) and to **Press Preview**.

### M16.4 Release checklist

- **Pricing** in all required currencies; regional rules.
- **Age ratings** (including the mandatory Germany and Indonesia surveys).
- **Release-date rules**: a concrete release date must be set with adequate notice (Valve
  requires the store page to have been public ≥2 weeks; a specific release date is
  announced ahead of launch).
- Optional but planned-later: achievements, trading cards, Steam Cloud. Explicitly
  optional here so they cannot block launch.
- **Steam Deck verification** pass (the game is a C++/Vulkan desktop app; run the
  compatibility review) — a meaningful discoverability and review positive if it passes.
- The **review build** submitted and approved; the store page flip switched.

### M16.5 Launch and post-launch

- Launch build live, launch **visibility round** used (Steam grants a launch impressions
  round), announcement posted to the Community Hub.
- **Review monitoring** for the first week: triage crashes and blocking bugs; a hotfix path
  through the M16.2 pipeline.
- **Post-launch updates** ride Steam's update-visibility rounds; each is announced, not
  silent.

## Tests and acceptance

Most of M16 is process, so its "tests" are verifications of the shipped artifacts rather
than unit tests:

- **Build reproducibility:** a scripted upload from a clean checkout produces a build that
  passes the clean-machine smoke test, run in CI.
- **Self-containment:** a downloaded build runs from a directory containing only the
  shipped files — fonts and variants included.
- **Store-page preparation is real:** every required asset exists at the specified
  resolution and the content survey (incl. AI disclosure) is complete before "Mark As
  Ready For Review".
- **Demo scope:** the demo build exposes the curated set and tutorial and no editor or
  online — asserted by the same feature-gating test M14 defines.
- **The demo has an opponent:** every curated demo variant has a legal-playing baseline
  opponent (M10.2-M10.3), asserted headlessly by extending M10's "search returns a legal
  move for every shipped variant" test to the demo set. A geometry or rule change that
  breaks the opponent on a demo board fails a test rather than shipping a broken Next Fest
  build.
- **Next Fest readiness:** demo build submitted by the edition's deadline and live before
  the Fest starts.

### Acceptance facts

1. A public Coming Soon page is live, with trailer, screenshots, description, tags, and a
   completed content survey (AI disclosure included).
2. A scripted, repeatable upload ships a self-contained build that passes a clean-machine
   smoke test.
3. A demo app is published and passes build review, plays a legal baseline opponent on
   every curated variant, and the game participates in exactly one Next Fest.
4. The release build is approved and the game launches, with a working hotfix path.
5. M16 and M9 do not duplicate each other: M16 ships the game, M9 moves authored content.

## Risks and non-goals

- **Deadline risk is the whole risk.** Next Fest build-review and trailer deadlines are
  hard, and a failed build review costs a resubmission. The mitigation is the M16.2
  pipeline existing before the demo, not after.
- **Pipeline-per-build is easy to postpone and expensive to do under deadline.** Build the
  automated upload as soon as there is anything to upload (Wave 1), not when the demo is
  due.
- **A failed Next Fest is unrepeatable** — a title gets one. Do not enter a demo that is
  not ready to be judged.
- **Non-goals:** no consoles, no storefronts other than Steam in this milestone, no
  business/company formation (a tax setup is a prerequisite, not this milestone's work),
  no marketing *content* — M16 is the storefront and the pipeline; the campaigns and clips
  are M11/M12/M13/M14.

## Status

Planned, not started. Its phases are placed across the release waves: M16.1 (store page)
belongs with Wave 1's visuals; M16.2/M16.3 (pipeline, demo) with Wave 2 for the February
2027 Next Fest; M16.4/M16.5 (release) after the game wave. It depends on M11/M13 for the
assets and M14 for the demo scope; it is independent of M9 and must not absorb it.

### As built (2026-09-30)

M16.2's pipeline is in: `tools/package.sh` installs a self-contained prefix
(`bin/` + `share/endspielzero/{variants,fonts}`) with `cmake --install` and smoke-tests it by
running the packaged game headlessly from the package root - the CI shape of "a downloaded
build launches and renders a frame". `tools/steam_upload.sh` fills the SteamPipe scripts in
`packaging/steam/` from `STEAM_APPID`/`STEAM_DEPOTID`/`STEAM_USER` and runs `steamcmd` (or
prints the command), with credentials never in the repository and `STEAM_PREVIEW=1` to
validate without publishing.

**Outstanding:** the store page, content survey, age ratings and the release itself are
process (M16.1/M16.4/M16.5); the demo (M16.3) waits on M14.
