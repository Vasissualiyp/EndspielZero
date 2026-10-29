# M9 — Steam Workshop & Packaging

**Goal:** users publish and consume variants safely. The sandboxed data-only
design (ADR-0005) is what makes this tractable — there is no code to sandbox.

## M9.1 Variant package format

- A package is a directory/zip: `variant.toml`, optional assets (piece sprites,
  models, board textures), `README.md`, a manifest with name, version, author,
  engine-compatibility range, and the computed `VariantId`.
- Asset budget and format whitelist (dimension caps, size caps, no executables,
  no archives-in-archives). Enforced by the validator, tested with hostile inputs.
- Deterministic packaging: the same inputs produce a byte-identical package, so
  the hash is meaningful.

## M9.2 Validator (the security boundary) **[INVARIANT]**

A single `endspielzero validate <package>` used by the publish flow, the client on
install, and CI. Checks: schema validity, typed rule signatures, resource budgets
(cells, directions, timelines, rule step budget, memory estimate), asset
whitelist, path-traversal safety, and a smoke play-out of N random games to prove
the variant does not deadlock or explode. Hostile-input corpus + fuzzing.

## M9.3 Steam integration

- Steamworks SDK behind a thin interface with a working offline/no-Steam fallback
  path, so the game and its tests never require Steam. **[INVARIANT]**
- Subscribe/unsubscribe/update flows, local override directory for authors,
  in-game browser with the validation status shown honestly.
- Because Steamworks is proprietary, it lives in an optional, isolated module
  with a documented licensing note (GPL + Steamworks interaction addressed in
  `docs/LICENSING.md`) — flagged now because it is a real issue, not a detail.

## M9.4 Distribution

- Reproducible Linux build from the flake; Windows and macOS builds; a
  self-contained CLI tarball for server operators.
- Versioning: engine semver plus a *variant format* version; compatibility matrix
  documented and tested by loading old packages.

## Acceptance facts

1. A package authored with the CLI, published, subscribed and played end to end.
2. The hostile-package corpus is rejected with precise messages; fuzzing finds no
   crash or hang.
3. The game builds, runs, and passes tests with Steam absent.
4. Packaging is byte-reproducible.
5. Old-format packages load or are refused with a clear version message.
