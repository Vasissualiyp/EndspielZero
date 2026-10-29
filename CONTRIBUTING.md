# Contributing to EndspielZero

Read [AGENTS.md](AGENTS.md) first — it is the map and the rulebook for humans and
agents alike.

## The loop

1. Find or write the failing test that names the behaviour you want.
2. Implement the smallest thing that makes it pass.
3. `ctest --preset dev -L unit` for the fast loop; `tools/precommit.sh` before
   committing; `nix flake check` before opening a PR.
4. Update docs and `AGENTS.md` in the *same* commit if navigation changed.

A pull request with a red suite is not reviewable. There is no exception for
"work in progress" — open a draft instead.

## Hard rules

They are listed in AGENTS.md ("The rules"). The two that reviewers reject on most
often: **a golden was changed without an explanation**, and **an optimisation
landed without a differential test against the oracle** (ADR-0009).

## Decisions

Anything that constrains future code gets an ADR in `docs/adr/` (use the
`cb-adr` skill). ADRs are immutable once merged — supersede, never edit.

## Licensing

Inbound = outbound: by contributing you license your work under
GPL-3.0-or-later. Every file carries `SPDX-License-Identifier: GPL-3.0-or-later`.
Do not paste code from sources whose license you have not checked; in particular
do not transcribe code from other chess engines. Transcribing *published test
data* (perft node counts, reference positions) is fine and expected — cite the
source in the golden file.
