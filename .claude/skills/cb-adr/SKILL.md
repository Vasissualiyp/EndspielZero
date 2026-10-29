---
name: cb-adr
description: Create the next numbered EndspielZero ADR from the template. Use when a decision constrains future code - a data structure choice, a dependency, a rules policy, a performance trade-off.
---

# Write an ADR

## When

Anything that a later code review would settle with "we decided that already".
Data structure choices, dependencies, rule policies on exotic boards,
performance trade-offs, protocol shapes.

Not for: implementation details with one obvious answer, or anything reversible
in an afternoon.

## How

1. Find the highest number in `docs/adr/` and take the next one.
2. Copy `docs/adr/template.md` to `docs/adr/NNNN-<kebab-title>.md`.
3. Fill in every section. The two that matter most and get skipped most:
   - **Alternatives considered** - each with the reason it lost. An ADR with no
     rejected alternative is a description, not a decision.
   - **How to reverse this** - what would have to change, and roughly what it
     would cost. This is what tells a future reader whether they are allowed to
     revisit it cheaply.
4. Add the row to the table in `docs/adr/README.md`.
5. If the decision creates an invariant, add it to `docs/ARCHITECTURE.md` marked
   `**[INVARIANT]**`, and to `AGENTS.md` if an agent could violate it by
   accident.

## Immutability

**Never edit a merged ADR** beyond changing its Status line to
`Superseded by ADR-NNNN`. Write a new ADR that supersedes it and explains what
changed. The record of what we believed and when is the whole point.
