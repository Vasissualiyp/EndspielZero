# M7 — GUI Authoring: the Piece Editor, then the Game Editor

**Goal.** A person who does not write C++ builds a piece and then a whole variant
inside the running game, and gets back exactly the data files the engine already
loads — a `variant.toml`, a piece model, a piece icon. Authoring is the point of a
sandbox; this milestone is where the sandbox becomes usable by its audience rather
than by its authors. Nothing in this milestone adds movement, geometry or rules to
the engine: it adds *the surface that writes the data the engine already reads*.

**Exit condition.** From an empty editor, a user authors a variant — axes, geometry,
pieces with vector moves, custom fields, rules, a 3-D model and a 2-D icon per piece,
and a starting position — saves it as a package, and plays it, without a text editor.
Opening any shipped variant, changing one thing, and saving reproduces a file that
loads to the same `VariantId`.

**Order of work.** M7.0 (shared machinery) is a hard prerequisite for everything
else: the document model and the serializer come first, test-first. Then the piece
editor in the order a piece is built (movement → fields/rules → geometry → metadata),
then the game editor, which assembles pieces into a variant. Do not start M7.5 before
M7.4 is green.

**Two editors, one document.** The piece editor edits a *piece document*; the game
editor edits a *variant document* that owns a list of piece documents. Both are the
same editable, source-faithful model and both save through the same serializer.

**Scheduled (2026-10-16).** **Only the piece editor** (M7.0 shared machinery + M7.1-M7.4) is
moved up into the roadmap's **Wave 1**: the shipped piece models are placeholder primitive
archetypes and are not good enough to ship, only a hand can improve them, and they are
visible in every clip, overture, shape-view shot and store-page frame. It therefore ships
**before** M16.1's store page. The **board editor and the gamemode editor** (M7.5 — the plan
calls this "the game editor": the axes/topology/start-position half is the board editor, the
piece-list/rules/policy half is the gamemode editor) are **not needed for Wave 1** and stay in
Wave 2. This is a scheduling change only - the build order below (M7.0 first, then the piece
editor, then the board and gamemode editors) is unchanged.

---

## M7.0 Shared machinery (build first, in this order)

### M7.0.1 The authoring document **[INVARIANT: it is a faithful edit model]**

`VariantSpec` is finalized, immutable, and lower than the UI; it cannot be the thing
being edited, and it has thrown away everything cosmetic. So introduce an editable,
source-level model that mirrors what a `variant.toml` can say, losslessly enough to
round-trip:

- New module `src/assets/` (or a set of types in `src/io/`), **integer-only** — piece
  geometry is authored in permille on a 0..1000 grid, exactly like `heightPermille`,
  so the no-floats rule in the deterministic core is never bent. Decide the exact
  home in the ADR below; the two candidates are a new L4.5 module beside `variant/`
  (usable by `io`, `app`, `view`, `render`) or the `io` layer itself.
- Types (names provisional): `assets::PieceModel` (profile + revolve/symmetry +
  elements), `assets::IconModel` (outlines + mirror), `io::PieceDoc` (name, symbol,
  atoms, fields it references, promotes-to, royal, draw-clock, shape/height, model,
  icon), `io::VariantDoc` (axes, geometry identifications, start, policy, fields,
  rule list, piece list, description, difficulty).
- The document holds the atoms as authored (`MoveAtom` fields, un-canonicalized) so
  the UI can show `[2,1]` and the save can either keep or canonicalize it — the
  loader canonicalizes anyway, but the editor should not silently reorder the author's
  input except when it says so.
- **Undo/redo is a document-level command log**, one entry per user gesture, with the
  usual coalescing for drags. It is *not* the game's undo and must not touch it.

### M7.0.2 The canonical serializer **[the linchpin]**

There is no TOML writer today; the editor cannot exist without one.

- `io::writeVariant(const VariantDoc&) -> std::string`, a canonical TOML text.
- **Canonical means stable, not minimal**: fixed key order, fixed number formatting,
  one blank line between blocks, trailing newline. Two documents with the same
  semantic content must serialize byte-identically, so a saved file's diff shows only
  what changed.
- **Round-trip property (write this test first):** for every shipped variant,
  `loadVariantToml(text)` → document → `writeVariant` → `loadVariantToml` yields the
  **same `VariantId`**. This is the acceptance test for the writer and the reason the
  writer is built before any UI.
- **Gotcha, state it in the code:** `VariantId` is a hash over the *semantic* fields
  in `VariantSpec::finalize()` (atoms via `toString`, fields, regions, policy) — not
  over the file text, and **not** over `shape`, `heightPermille`, `description`,
  `difficulty`, or the geometry/icon assets. So a model or icon change must not change
  `VariantId`; a move, field, or rule change must. The round-trip test asserts exactly
  this split, with a second test that editing a cosmetic field leaves the id alone.
- Idempotence: writing a document, loading it, and writing again is byte-identical.

### M7.0.3 One validator, surfaced at the control that broke **[INVARIANT]**

The editor is a *front end to `finalize()`*, never a second set of rules.

- Every edit is followed by a dry-run `finalize()` on a copy of the built `VariantSpec`
  (and, where relevant, the rule `validate()` and the direction-budget check). Failure
  is shown inline against the field that caused it, using the loader's own message and
  line where it has one.
- The save button is disabled while the document does not finalize, with the first
  error shown; there is no "save anyway".
- The M9 package validator will reuse this path; do not invent a second error format.

### M7.0.4 Save / load as a package

- A saved variant is a directory: `variant.toml`, `model/piece-<n>.model` (or an
  `[assets]` reference), `icon/piece-<n>.icon`, `README.md` optional. This is the M9
  package shape, built here so M9 only adds publishing and the security hardening.
- Load = `loadVariantDoc`. "Open shipped variant as base" = `loadVariantDoc` of the
  shipped file, then detach the name.
- File dialogs are avoided in tests: the shell injects a "storage" seam (an interface
  with a real-filesystem implementation and an in-memory test implementation), the
  same way the shell injects a variant loader today.

### M7.0.5 Start-from-anything **[the user's explicit ask]**

- "New piece from base…" lists every piece in the current variant and every shipped
  piece, and copies its atoms, referenced fields, shape/height, model and icon into
  the new document. Editing a pawn into a pawn variant is then a few controls.
- For this to cover the shipped pieces, M7.3.4 ports the shipped primitive
  archetypes to the vector model, so a shipped piece always has an editable model.

### M7.0.6 ADRs and skills

- **ADR-0014 "The authoring document and canonical variant serialization"** — why the
  editable model is separate from `VariantSpec`, why the serializer is canonical, why
  `VariantId` covers semantics only, and how the format is versioned.
- **ADR-0015 "Piece geometry as data"** — the profile/revolve/symmetry/element model,
  its integer representation, and how the renderer assembles it; states that geometry
  is cosmetic and outside `VariantId`.
- New skills (after the third repetition, per `skills.md`): `cb-authoring-document`,
  `cb-variant-serializer`, `cb-piece-geometry`, `cb-editor-screen`.

### M7.0.7 Where the code lives (layering)

- `assets::PieceModel`/`IconModel`: new module, integer-only, below `app` so both the
  editor document and the renderer can see it (see ADR-0015 for the exact home).
- `io::PieceDoc`/`VariantDoc` + `writeVariant`: L9, beside `variant_toml.cpp`.
- `app::Editor`: the document, undo/redo, dirty state, save/load — in `app/`, owning
  no window and no Vulkan, driven by the same action-log seam as `app::Session`, so it
  is unit-testable headlessly.
- `render/ui_editor_*`: ImGui screens that read the document and emit editor actions,
  exactly as `ui.cpp` reads `Session` and emits `UiRequest`. Every pixel size goes
  through `Ui::px()`.
- `render/piece_mesh.cpp`: gains the assembly path from `assets::PieceModel` to a
  `MeshLibrary` range; the primitive archetypes remain the fallback.

---

## M7.1 Piece editor — movement (the move algebra as UI)

The engine has exactly one atom list per piece. The editor presents it in the shape a
person thinks in.

### M7.1.1 The atom grid

One row per atom, each row editing the fields of `MoveAtom`:

| Control | `MoveAtom` field | Notes |
|---|---|---|
| Magnitudes | `mags` | one small stepper per active axis, e.g. `[1,2]`; show the canonical sorted form |
| Distance | `minK`/`maxK` | a single "leaper / exact-n / rider" control that maps to min/max/`kUnlimited` |
| Traversal | `mode` | slide / leap / hop, with the one-line meanings from `atom.hpp` |
| On occupied | `capture` | may / must / cannot |
| Direction | `oriented` | "forward half-space only", per the orientation axis |
| Where it works | `fromRegion[color]` | "only from the home rank" etc., per colour; inactive = anywhere |
| Side effect | `leavesEnPassant` | "passing this leaves an en-passant target" |

### M7.1.2 Move set and eat set are two views of one list **[the user's ask]**

A pawn is not a special case in the engine; it is atoms with different `capture`
policies. The editor makes that legible:

- A **quiet-moves** group (rows with `capture = cannot`) and a **captures** group
  (`may`/`must`), presented side by side. Both compile to the single atom list.
- A **pawn preset** fills the three canonical rows: forward push `[1]` cannot,
  oriented; the double step `[1] min=2 max=2` cannot, oriented, `fromRegion` = home
  rank, `leavesEnPassant`; the diagonal capture `[1,1]` must, oriented. Show the
  preset's expansion so the author sees it is the shipped pawn.

### M7.1.3 Compose by union, with named bundles **[the user's ask]**

- A palette of bundles: rook `[1]∞`, bishop `[1,1]∞`, knight `[1,2]¹`, king
  `[1]¹+[1,1]¹`, and any piece already in the variant. Dropping a bundle unions its
  atoms into the piece; dropping twice is a no-op (dedupe against canonical form).
- **The teaching moment, stated in the UI:** rook + bishop is a queen in 2-D, but it is
  the *same union of atoms in every dimension* — it is not the "line in any direction"
  piece. A "line in any direction" piece is `⋃ₖ [1]ᵏ` (rook, then diagonal, then the
  3-axis and higher diagonals), which only differs above 2-D. The preview below makes
  the difference visible, and a one-line note explains it.

### M7.1.4 Per-dimension preview **[essential, and cheap]**

- A reachability diagram: from a centre cell, mark every cell the current atoms reach
  on the variant's board, rendered per 2-D slice with a slice pager (reuse the game's
  board projection and `--shot` capture).
- The predicted direction count from `expectedDirectionCount(mags, dims)` beside the
  live `VariantSpec::directionCount()` after finalize, so the budget warning is real.
- An atom of order `r > dims` expands to **nothing**; the row shows "needs N axes" and
  is greyed, not an error — that is the engine's deliberate behaviour, and the editor
  teaches it rather than hides it.

### M7.1.5 Tests

- App-layer unit tests via the action log: build the shipped pawn, rook, bishop,
  knight, king from controls and assert the produced `MoveAtom` list equals the
  shipped variant's (canonicalized).
- A property test: for random atom lists at D ∈ {2,3,4,8}, the editor's displayed
  count equals `expandAtom(...).size()`.
- Compose test: rook ∪ bishop on the 8×8 standard board yields exactly the shipped
  queen's move set; at D = 3 the two pieces' move sets differ, and a test asserts that
  (the teaching claim, pinned).

---

## M7.2 Piece editor — custom fields and rules

### M7.2.1 Fields

- Declare piece fields and cell fields (name, default, min, max, hashed) with the same
  validation the loader applies.
- The editor explains the field-vs-region choice with the two shipped examples:
  - **"first move" the FEN way** — a pawn's double step is a `fromRegion` on the atom,
    position-derivable and FEN-round-tripping (`5d`/standard). No field needed.
  - **"first move" the state way** — a per-piece `moved` field survives and is hashed.
    Offer both; say which one the author is choosing and why.
- **The `charged` piece** is the worked example: a `charge` piece field, a rule that
  increments it on moving, and a rule that transforms at a threshold, in that order.

### M7.2.2 Rule builder

- A form over the VM catalogue: pick a trigger (`on_move_filter`, `on_capture`,
  `on_move_end`, `on_turn_end`, `on_result_query`), add ordered effects, and fill each
  effect's arguments. The effect/expression catalogue is read from the same source the
  validator uses, so the form cannot offer a combination `validate.cpp` rejects.
- Ordered list with drag-reorder, because order is meaning (`charged` proves it); the
  UI says so.
- **Advanced mode:** edit the raw S-expression text with the parser's error messages.
- **Dry run:** "apply to the current position and show what changed", using the same
  VM the game uses, on a scratch position, so a rule can be understood before it is
  saved.

### M7.2.3 Tests

- A rule set built through the form serializes to the same TOML as the hand-written
  `charged`/`atomic` equivalent, and the resulting variant behaves identically in a
  short scripted game (hash-for-hash).
- Every rejection message `validate.cpp` can produce is reachable from the form and is
  shown as-is.

---

## M7.3 Piece editor — geometry (3-D in two steps, then 2-D) **[the user's ask]**

The geometry is a small **vector description**, stored as data, never a mesh file and
never code. It is cosmetic: it does not enter `VariantId`.

### M7.3.1 The model (ADR-0015)

- **`profile`** — a closed polyline in the *(radius, height)* plane, integer permille,
  authored like a glyph: straight and quadratic segments, snapped.
- **`revolve`** — sweep the profile about the height axis with `N` angular segments and
  a **symmetry**: `full` (radially symmetric), `kfold(k)`, or `mirror`.
- **`elements`** — zero or more non-radially-symmetric parts. Each is a small
  profile/extrusion placed in the piece's frame; the **symmetry group replicates it**
  (a mirror makes a pair for a knight's ears; `kfold(k)` makes a ring of spikes).
- This is the "figure in vector form": profiles + a rotation + a symmetry group +
  placed elements. The renderer turns it into a mesh the same way `MeshLibrary` turns
  primitives into a mesh today.

### M7.3.2 Step 1 — the side profile

- A 2-D editor (the same drawing surface as M7.3.5's icon editor): draw and edit the
  closed profile in the (radius, height) plane, with the foot pinned to `height = 0`
  and a snap grid; the area is shaded so the silhouette reads immediately.
- For a radially symmetric piece (a pawn), this **is** the whole model — the step-1
  preview shows the revolved solid ghosted so the author sees what the profile will
  become.
- Editing an existing profile: point insert/move/delete, segment-curve toggle, mirror
  the profile left/right as an editing aid (not a model operation).

### M7.3.3 Step 2 — revolve and symmetry

- A segment slider `N` (min 3, capped; a stepped preview at low N shows the facets) and
  a symmetry picker.
- With `full`, the model is complete; the "add element" tool stays disabled and the UI
  says why (a radially symmetric piece cannot carry a non-radial element).
- With `kfold(k)` or `mirror`, the element tool enables: place a part, and it is
  replicated by the group. A knight is `mirror` + one ear/head element; a crown with
  evenly spaced points is `kfold(k)` + one point.
- A live 3-D preview built by the real assembly path (not a separate viewer), so what
  the author sees is what the game draws. Offscreen capture (`--shot`) makes it
  reviewable in CI.

### M7.3.4 Ship the current pieces as editable models

The primitive archetypes (`Dome`, `Tower`, `Wedge`, `Spire`, `Crown`, `Monolith`,
`Horn`) are built from primitives in `piece_mesh.cpp`. Port them to the profile/element
format so "start from the pawn/knight/…" always yields an editable model and there is
**one** geometry pipeline:

- The radially symmetric ones (pawn, rook, bishop, queen, king) port to a profile +
  `full`/`kfold`; the knight and horn need `mirror` + elements.
- Keep the primitive fallback in `archetypeFor` for a piece with no model, so an
  incomplete or missing asset never makes a piece unplayable (the existing invariant).
- A render golden per ported piece: before/after images compared, or a vertex-count
  and bounding-box assertion plus the mesh test that every archetype is closed.

### M7.3.5 The 2-D icon

- Auto-derive the icon from the model's top-down silhouette as a first draft.
- An editable straight-segment outline in the existing 100×100 glyph box (the icon
  language is straight segments only — that is also exactly what `ImDrawList` fills).
- **Optional mirror** as an authoring aid: draw the right half, mirror to the left;
  the stored outline is the full result, so the icon stays as dumb as the shipped
  tables.
- The existing icon test ("every archetype has one, nothing escapes its box, the cheap
  set is actually cheap") extends to authored icons; the Primitive set is either
  auto-simplified (convex hulls) or the author is asked for it explicitly.

### M7.3.6 Tests

- Determinism and closure of the assembled mesh: same model → same vertices; the mesh
  is a closed manifold for `full` and `kfold`, checked by an edge-pairing test.
- Symmetry replication: `kfold(4)` places exactly four elements; `mirror` places two.
- A profile that self-intersects or leaves the box is rejected with a message, not
  rendered badly.
- Offscreen render of an authored piece is validation-clean and changes when the model
  changes.

---

## M7.4 Piece editor — finishing

- Identity: name, symbol, `royal`, `resetsDrawClock`, `promotesTo` (from the variant's
  piece list), and the display metadata `shape`/`heightPermille` (a slider; the model
  supplies the visual, height is the reading).
- "Save piece" into the in-progress variant; "duplicate from base"; "revert to base".
- A piece is playable the moment it has atoms; the editor offers a "try it" that drops
  it into the scratch position and highlights its moves (the M7.1.4 diagram).

---

## M7.5 Game editor — assemble the variant

### M7.5.1 Axes

Add/remove axes; per axis: name, extent, kind (spatial / temporal / multiverse),
`pitch`. Live re-preview of the board; the temporal fields (`white_sign`,
`black_sign`, `branch_advance`) appear when a temporal/multiverse axis exists.

### M7.5.2 Geometry (topology)

The M3 identifications as UI: add an identification per axis (`periodic`, mirrored,
with a twist for the non-orientable cases). Preview seams and a wrapped-move trace, so
"what does a rook do on this?" is answered before saving.

### M7.5.3 Start position

- A board editor over the current pieces and axes: place/erase, colour and type
  palettes, side-to-move, then read back as FEN-N. Paste a FEN-N as the other entry
  path. The origin (`[start] origin` / `[start] board`) is chosen here.

### M7.5.4 Pieces and rules

- The piece list: add from a shipped piece, from a piece document (M7.4), or duplicate;
  remove; reorder (the order is the `PieceTypeId`, so say so).
- The rule list from M7.2, ordered, shared across pieces.

### M7.5.5 Policy and metadata

Promotion regions per colour, en passant, stalemate policy, halfmove draw limit,
orientation axis, name, description, difficulty.

### M7.5.6 Validate, test-drive, save

- A **Validate** step runs `finalize()` + rule `validate()` + the direction budget +
  the M9 smoke play-out (N random games), reporting the first failure precisely.
- A **Test drive** button starts a real game with the in-progress variant without
  leaving the editor. The variant must be finalized and owned at a **stable address**
  for the life of the game (the `Session`/`VariantSpec` pointer gotcha in AGENTS).
- **Save** writes the package (M7.0.4) and can also export a single
  `variants/<name>.toml`. Saved variants appear in the library immediately, ordered by
  the existing difficulty rule.

### M7.5.7 Tests

- An "authored standard": build standard chess through the editor controls and assert
  its perft node counts equal the shipped `standard`'s.
- An "authored charged": reproduce the shipped `charged` and assert a scripted game
  matches hash-for-hash.
- Save → reload → play, over a temp package, headlessly.
- A migration test: open every shipped variant, save it unchanged, reload, same
  `VariantId` (this is M7.0.2's property, exercised through the whole game editor).

---

## M7.6 Documentation and skills

- `docs/variants/authoring.md` — a human walkthrough of the two editors with the two
  worked examples (pawn, charged) and the geometry two-step.
- A page under `docs/` for the model format (the ADR-0015 text plus a format table).
- Update `AGENTS.md`: the editor screens, the serializer, the asset module, and the new
  skills; update the "Where to add what" table with "A piece's model" → the editor or
  the asset file.
- Update `docs/ARCHITECTURE.md` §10 with the authored-geometry pipeline and §11 with
  the "`VariantId` covers semantics, not cosmetics" rule.

---

## Dependencies and ordering

- Depends on M5 (rule VM + fields) and M6 (temporal axes as data) being done; both
  are. Independent of M8/M9/M10.
- M9 (Workshop) wraps M7's output: M7 builds the package shape and the serializer, M9
  adds the security validator, signing and distribution. Do not duplicate the
  serializer in M9.
- M7 must not add engine features. If authoring needs one (a rule that cannot be
  expressed), **stop**: that is a `cb-new-effect` task with its own tests, recorded as
  such, not smuggled in as an editor convenience.

## Acceptance facts

1. A piece authored entirely in the GUI — atoms, a field, a rule, a 3-D model and a
   2-D icon — plays in a real game.
2. Composing rook + bishop yields the shipped queen's move set in 2-D, and the editor
   shows (and a test asserts) that it differs from a "line in any direction" piece
   above 2-D.
3. A pawn authored from the quiet/capture controls and the pawn preset contributes the
   shipped pawn's perft counts.
4. A `charged`-like field + ordered-rules variant authored in the GUI behaves
   identically to the shipped `charged`.
5. Every shipped variant survives open → save → reopen with an unchanged `VariantId`;
   a cosmetic change does not alter it.
6. A non-radially-symmetric piece (knight) is authored as a profile + mirror + one
   element and renders.
7. The editor's errors are the loader's errors — one validator, no second dialect.
8. "Start from the pawn" produces an edited pawn with new moves, a new model and a new
   icon, and the shipped pawn is untouched.

## Risks and non-goals

- **Open-ended geometry is the risk.** Ship the radial revolve + mirror subset first
  and gate the general element system behind it; a piece is never unplayable for want
  of a model.
- **The serializer is the linchpin.** The round-trip property test lands before any UI
  writes a file.
- **GUI testing without a display.** All editor *logic* is in `app::Editor`, tested
  through the action log; the ImGui layers are thin and reviewed by `--shot` capture.
- **Non-goals:** no mesh import/export, no boolean modelling, no scripting, no
  real-time sculpting. The model is a profile, a revolution and a symmetry — that is
  the whole vocabulary.

## Status

In progress. Shipped: the document and its canonical TOML writer (M7.0.2), with the
round-trip property; `app::Editor` with dirty tracking, snapshot undo/redo and save;
the piece designer (pieces, moves as `- n +` steppers with cycling kind/capture
buttons, a colour palette, a live move preview on a scratch board with placeable
pawns, and a help page); and the board designer (axis name/extent, kind, and a
glued/open toggle per axis, add/remove axis, save).

Not yet built: the geometry editor's twists and swaps, the rule builder and custom
fields (M7.2), the 3-D/2-D model editors (M7.3), the game editor's start position,
pieces list, rules and policy (M7.5), and the package/validation-before-save flow
(the save button does not yet gate on `finalize()`). The serializer and the move and
axis editing are the parts the rest will hang off.

**Wave-1 priority (2026-10-16).** Within the piece editor, what the pull-forward actually
buys is **M7.3**: the 3-D profile editor, the revolve/symmetry step and the 2-D icon
editor, plus M7.3.4 porting the shipped primitive archetypes to editable models. That is
the surface a human needs to replace the placeholder models every clip currently shows.
M7.1/M7.2 (movement, then fields/rules) already have their first cut and complete the
editor around it; the board and gamemode editors (M7.5) are not part of Wave 1 and stay in
Wave 2.


## Revision 2026-10-29 - M7.3.4, elements, save gating, M7.2

- **Authored models are part of the variant (M7.3.4).** `[piece.model]` / `[piece.icon]`
  in the variant TOML (`src/io/piece_model_toml.{hpp,cpp}`, shared by the loader and the
  document), on `PieceTypeDef::model/icon` (cosmetic, outside `VariantId`).
  `VariantDoc`/`Editor` read and write them; the body and icon tabs now keep a working copy
  of the document's model and commit it as one undo step when the mouse is up - the
  session-only `designs_` map is gone, and RESET clears the document's model.
- **The renderer draws them.** `MeshLibrary::appendModel` sweeps the profile (faceted, one
  normal per facet) and extrudes each element in its (radial, up) plane, replicated by the
  symmetry group (k-fold: i/k of a turn; mirror: half a turn). Each authored piece gets a
  shape slot past `Archetype::Count` (`shapeSlots`, up to 32); `BoardRenderer::record`
  rebuilds and re-uploads the library when `authoredKey` changes. The shape view's puck
  and disc for an authored piece are its archetype's. The flat board draws the authored
  icon (`pieceIconFor`). A piece with no model keeps its primitive archetype - the shipped
  look is unchanged until someone draws a piece. **Deviation from M7.3.4:** the archetypes
  are *not* rebuilt through the profile path, so there are still two pipelines; doing that
  changes every shipped frame and should be its own reviewed step.
- **Element placement (M7.3.3).** Step 3 of the body tab: add / remove / next element,
  drag its ember anchor on the profile, edit its outline (shifted half a box so it can reach
  both sides of the anchor). Disabled under `full` symmetry, with the reason shown. The
  revolve preview draws every element at every copy.
- **Save is gated on the loader (M7.0.3).** `Editor::validate()` loads the serialized text
  exactly as the game would (cached per document state); `saveFile` refuses a document that
  does not load; both SAVE buttons are disabled with the loader's message shown. After a
  save the shell's cached preview is dropped so the game reads the new file.
- **Fields and rules (M7.2).** A RULES tab: piece and cell fields (name, default, min, max,
  hashed) and the rules as a form - trigger, S-expression condition, ordered effects whose
  ops and keys come from `io/rule_catalogue` (the same keys the loader reads; a test builds
  one rule per op and per trigger through the form and loads it), with up/down reordering.
  The loader's own message is shown live. `--screen rules` captures it.
- Tests: `tests/unit/io/test_piece_model_toml.cpp`, `tests/unit/io/test_rule_form.cpp`
  (charged/atomic/mustcapture rebuilt through the form keep their `VariantId`),
  `tests/render/test_authored_mesh.cpp` (closed, deterministic sweep; k copies per k-fold).
- **Still open:** M7.2's dry run, M7.2's raw S-expression editor beyond the condition
  field, M7.0.4's package directory (saving still overwrites `variants/<name>.toml`), and
  "start from anything" (M7.0.5).

## Revision 2026-10-18 - piece designer fixes

- **Red box on "rook".** The piece-name chips and the "add a move" bundles are both
  labelled rook / bishop / knight / king, so they shared an ImGui ID and Dear ImGui drew
  its "conflicting ID" error over the editor. Each row now has its own `PushID` scope.
  Reproduced headlessly with the new `--ui-click "x,y;..."` and `--piece NAME` capture
  flags (pointer events injected before the shot), which is how the designer is reviewed.
- **`fillPolygon` filled the convex hull.** It copied the caller's points but left them
  in `dl->_Path`, and `AddTriangleFilled` appends to the pending path, so the first
  triangle was the hull of the whole outline: the wedges across the profile editor and
  the closed-over crenellations. It now clears the path first.
- **Menu animations ignored cut-outs.** `overture_scene.cpp` and `deco.cpp` drew only an
  icon's `fills`, so a faceted rook's crown was a solid rectangle. They now paint `cuts`.
- **Queen = every straight line.** A queen is `straightLineAtoms(kUnlimited, dims)`;
  `archetypeFor` calls a piece a unicorn (Horn) only when it has no one-axis slider.
- Body tab: the live revolve preview that its text promised is now drawn; bodies and
  icons authored in a session are kept per piece; move steps cannot be 0; a failed edit
  shows its message in red; the add-a-move row has a "queen" bundle.
- Still open: bodies and icons are not yet written into the variant file or used by the
  renderer (M7.3.4 onward), and there is no element-placement tool.
