# M19 - Generalized moves: one data model for every piece, including the irregulars

**Status: step 1 of section 10 is built (the three kinds, pins, flip, expansion, loader and editor round trip); everything else is spec.** Written 2026-10-18 at the project owner's direction ("we want a
very generalizable engine"). It changes the move model, the position state and the editor, so
it is written down first, with the tests that will prove it, before any of it is implemented.

## Why

The move model today is a `MoveAtom`: a multiset of magnitudes `[a,b,...]`, expanded over
**every** assignment to distinct axes and **every** sign, repeated `k` times. That is exactly
right for the 90 % case (rook, bishop, queen, knight, unicorn) and wrong for everything that
has a *direction*. The irregulars are bolted on beside it:

| Irregular | Today | Problem |
|---|---|---|
| "Forward" | `forward = true`: keep the forward half-space of the expansion | One half-space on one axis. Cannot say "only along the file", "only the two forward diagonals", "a lance", "5 steps forward". |
| Pawn's first move | `from_rank = n` (a `Region` on the orientation axis) | Position-derived, white-defined and mirrored; not "has this piece moved". |
| En passant | `leaves_en_passant` + an engine special case | The capture lands on one cell and removes a piece on another; that is hard-wired, not a property a move can have. |
| Castling | a separate `[[castle]]` template with fixed cells and `rights_bit` | Fixed coordinates, no editor, not dimension-agnostic, rights are a bitmask apart from the pieces. |

The principle: **nothing about chess is in the engine; chess is data.** The standard ruleset
must be authorable entirely in the piece designer, and so must any variant's irregulars.

## 1. Three move types, one expansion

A move is a **template**: components, a step range, a flip, a mode and a capture policy.

| Type | Meaning | Example (board axes `file, rank`, forward = `+rank`) |
|---|---|---|
| **exact** | The single vector, once. | `[0,1]` - one cell forward. `[1,0]` - one cell to the right. |
| **expanded** | The vector times `k`, `k` in a signed range. | `[1,0]` - right *or* left, any distance. `[0,1]`, `k >= 1` - a lance. |
| **extrapolated** | The vector's magnitudes over every axis assignment and sign (today's atom). | `[1,0]` - a rook. `[1,2]` - a knight. |

Every component of the vector is either **pinned** to a named axis with a signed value, or
**free** (a magnitude to be permuted over the axes that are not pinned, both signs):

- exact and expanded: every component pinned.
- extrapolated: every component free - *unless the author pins some*. A pawn's capture is
  "pin forward `+1`, free `[1]`": in two dimensions that is `(+/-1, +1)`, in four it is `+/-1`
  on any one of the three other axes with `+1` forward. That is the whole of what
  `forward = true` was trying to say, and it generalizes to any dimension without a flag.

**Step range.** For expanded and extrapolated, `k` has bounds, `[kmin, kmax]`, default
`[1, inf)` for extrapolated and `(-inf, inf) \ {0}` for expanded (so "up to 5 steps forward" is
expanded `[0,1]`, `k` in `[1,5]`). Exact has no `k`.

**Flip.** A flag on exact and expanded templates, **on by default**: for Black the pinned
components along the variant's orientation axis are negated (`[0,1]` is forward for White and
backward-in-board-terms for Black). Off for a piece that moves the same way for both sides.

**Mode and capture** (`slide`, `leap`, `hop`; `may`, `must`, `cannot`) are unchanged. An exact
move with mode `slide` walks unit steps along the vector (so a two-cell step needs the first
cell empty, which is the pawn's double step).

### Expansion and what does not change

Templates expand at load into the **same** direction table the engine already uses. Movegen,
perft and every existing golden are untouched. The expanded atoms - not the templates - are
what `VariantSpec::finalize()` hashes, so `VariantId` does not change for a variant whose
moves are unchanged.

Migration is mechanical and is the acceptance test: every shipped atom has a template whose
expansion is identical (rook = extrapolated `[1]`; knight = extrapolated `[1,2]`; shipped pawn =
exact forward `1` + exact forward `2` + `pin forward 1 / free [1]`), and the loader keeps
reading `vector`, `forward`, `from_rank`, `leaves_en_passant` as aliases that desugar to the
new form. The writer emits the new form.

## 2. "Has this piece moved": a built-in piece state

A piece carries one built-in bit, **`moved`**, cleared when the piece is placed by the start
position and set by the engine when it moves. It is hashed, restored by undo, and shown by
FEN-N. A template, a compound move or a rule may require `unmoved`.

- **Pawn double step**: exact forward `2`, mode `slide`, `cannot`, requires `unmoved`.
- **Castling**: both pieces `unmoved` (section 4).
- The old `from_rank` stays available (it is position-derived, and some variants want
  exactly that), but the editor offers `unmoved` by default.

**FEN-N.** Gains an `unmoved` field listing the cells of unmoved pieces. If it is absent, a
piece is unmoved iff it stands on the square the variant's `[start]` put a piece of its type
on - which is what standard FEN already assumes of pawns. A standard castling field
(`KQkq`) is read as "the king and that rook are unmoved", so standard FENs load unchanged and
`VariantId`-equal games hash equal.

This replaces `castleRights_` as a separate bitmask. It is the one place this spec changes
position state, so it needs the full treatment: hash, undo, FEN round trip, replay.

## 3. Capturing somewhere other than where you land

A template may declare a **capture offset**: the cell a capture removes is the landing cell
plus a (flipped) vector. With no offset it is the landing cell, as now. Two small parts make
en passant a property of a move instead of an engine special case:

1. **Marks.** A template may `leaves_mark`: the cells it passed over are recorded on the
   position for one turn (this is today's ep target, generalized to any set of cells and any
   dimension). Marks are hashed and undone.
2. **`capture_offset` with `needs_mark`.** The move is legal only if the landing cell carries
   an opponent mark and the cell at the offset holds an enemy piece; it removes *that* piece.

En passant is then: pawn double step `leaves_mark`; pawn diagonal capture with
`capture_offset = [0,-1]` (flipped), `needs_mark`. The same offset without `needs_mark` is a
checkers jump (exact `[0,2]`, `capture_offset = [0,-1]`, `must`).

## 4. Compound moves: castling and any two-piece move

A **compound move** is a mover template plus a **partner**:

- the mover: an exact (or any) template, typically `[2,0]` with `unmoved` (the king);
- the partner: *found*, not placed - the first piece along the mover's direction of travel,
  on the side it moved towards, whose type is in a list and which is `unmoved`; its
  destination is a vector from the mover's landing cell (castling: the cell the mover
  crossed). Searching rather than naming cells is what makes it work on any board size, in
  any dimension, along any axis, and for either wing, with no fixed coordinates;
- **conditions**: `path_empty` between mover and partner, and `not_attacked` over a list of
  mover-relative cells (`from`, `crossed`, `to`).

`not_attacked` needs one new expression the rule VM does not have, **`attacked(cell)`**: is
the cell attacked by the side not to move. Movegen already answers this (it is how check is
detected and how `[[castle]].safe` works), so the work is exposing it as an `ExprOp`
answered by the orchestrating layer, exactly like `HasCaptureFrom` is today. The same
expression is what lets a rule say "you may not end a move on an attacked cell".

`[[castle]]` stays as loader sugar that desugars to a compound move, and a shipped variant's
moves and `VariantId` do not change. The point of the generalization is chess960-style
variants, castling along a file in 3-D, and "the king and any unmoved rook" without engine
code.

## 5. Editor

The move table's columns become: **type** (exact / expanded / extrapolated), the **vector**
(one cell per axis: a signed number, plus a free/pinned choice for extrapolated), the **step
range** (`min`, `max`, infinite), **flip**, **mode**, **capture**, and the conditions
(`unmoved`, plus an expression box for anything else). The forward checkbox goes.

- Palette bundles are re-expressed as templates: pawn = exact push + exact double step
  (unmoved) + the pinned capture with its en passant offset and mark.
- A **compound move** editor: mover template, partner types, destination vector, the three
  condition lists, with the preview board showing the partner's move as well.
- Standard chess authored end to end in the editor, perft-equal to the shipped `standard`,
  is the acceptance test for the whole milestone.

## 6. Tests (written first)

1. **Expansion equivalence.** For every shipped variant and piece: the template the migration
   gives expands to the identical atom list, direction counts and `variantId`.
2. **Property.** Random templates at `D` in {2,3,4,8}: displayed direction count equals
   `expandAtom(...).size()`; `flip` on a pinned orientation component is the exact negation
   for Black; a pinned+free capture in `D` dimensions has `2(D-1)` directions.
3. **Unmoved.** Hash, undo, FEN round trip, and replay with the flag; a pawn that moves and
   returns cannot double-step; standard FENs load identically.
4. **En passant.** Perft-equal on the shipped positions that contain it, through marks and
   `capture_offset` rather than the special case; the same in a 3-D variant.
5. **Castling.** Authored through a compound move: perft-equal to the shipped `standard`,
   `atomic`, `charged`, `cylinder`, `mustcapture`; refused through check, through an
   attacked cell, after either piece has moved, with a piece between.
6. **Authored standard.** Standard chess built only from the editor's controls has the shipped
   perft (20, 400, 8902, ...).
7. Every rejection the validator can produce is reachable from the editor and shown as-is.

## 7. Order of work (superseded by section 10)

1. Templates and expansion, with the loader aliases and the equivalence test (no behaviour
   change; this is the part that can ship alone).
2. `moved` state with its hash/undo/FEN, then `unmoved` conditions; retarget the pawn.
3. Marks and `capture_offset`; retarget en passant.
4. `attacked(cell)` and compound moves; desugar `[[castle]]`.
5. Editor: move table, compound editor, palette.

Steps 2-4 change position state and must each land with the golden suite green; if the
`moved`-bit migration proves too deep for FEN round trips, the fallback is to keep
`castleRights_` and `from_rank` as the storage and expose `unmoved` as a derived condition
in the editor - recorded as a deviation, not hidden.

## 8. Decisions (owner, 2026-10-18)

- **`unmoved` replaces `from_rank`** everywhere, shipped variants included. A shipped
  variant's pawns start on their home squares, so every shipped perft is unchanged; what
  changes is the meaning for a custom position, where "unmoved" is now a fact about the
  piece (stored, listed in FEN-N) and not about the rank it stands on. `from_rank` remains
  readable and desugars to `unmoved` plus a region expression only for files that still use it.
- **Marks are configurable.** A mark is no longer "the en passant target". It is a named
  **tag** on a cell with a **duration**: `N` plies, or forever. A move may leave marks on any
  set of cells - the cells it passed over, or the surroundings of where it landed - so
  "when a piece lands it marks the cells around it destroyed, for 3 moves" and "a double step
  marks the passed cell for 1 ply" are the same mechanism. Tags are declared by the variant
  (`[[mark]]` with `name` and `blocks = true|false`; a blocking tag makes the cell
  impassable and un-enterable to movegen), and read by `marked(cell, tag)` in expressions.
  Marks are hashed, counted down by the engine each ply, and restored by undo. En passant is
  the tag `ep` with duration 1; section 3 is the special case of this.
- **Compound moves move two pieces for now.** The data shape is a list of partners, so more
  can be added later without a format change; the validator rejects a list longer than one
  until the engine supports it.

## 9. The moveset language (symbolic move definitions)

Typing every queen move on every axis is the problem this solves. A moveset is **text**: a
small, typed, mathematical notation for sets of moves, so a piece's movement is one line and
a file of them can be shared. It is a *data* language in the sense of ADR-0005 - declarative,
no loops, no recursion, structural termination, no I/O beyond importing another moveset file
by name - so a downloaded moveset is as safe to load as a variant.

```
rook      = [1]^k                        -- k is the free step count, 1..inf by default
bishop    = [1,1]^k
knight    = [1,2]^1                      -- ^1 is exactly one step
nightrider = [1,2]^k
queen     = U r in 1..D : [1 x r]^k      -- the union over r of "r ones", D = board dimension
king      = U r in 1..2 : [1 x r]^1
unicorn   = [1,1,1]^k
queen4    = [1]^k U [1,1]^k U [1,1,1]^k U [1,1,1,1]^k      -- the same, written out
lance     = expanded [rank:+1]^1..inf    -- k from 1 to inf, one direction only
step5     = expanded [rank:+1]^1..5      -- at most five cells forward
pawnPush  = exact [rank:+1] flip cannot
pawnTwo   = exact [rank:+2] flip cannot slide unmoved marks ep(path, 1)
pawnTake  = [rank:+1, 1]^1 flip must     -- pinned forward, one free axis: the capture
dragon    = rook \ [1]^1                 -- a rook that cannot make the one-step move
```

**Terms.** `[ ... ]` is a vector: a comma list of **components**, a bare magnitude (free) or
`axis:signed` (pinned). `x n` repeats a component `n` times (`1 x r`). `^` gives the step
range: `^k` (all `k >= 1`), `^n` (exactly `n`), `^a..b`, `^a..inf`. A leading `exact` or
`expanded` selects the other two move types; the default is extrapolated. Trailing words
set the rest of the template: `slide | leap | hop`, `may | must | cannot`, `flip`, `unmoved`,
`marks tag(where, N)` with `N` a count or `forever`, `captures at [vector]`, `needs mark tag`.

**Operators.** `U` (union), `\` (difference), parentheses, and `U v in a..b : body` for a
union over a bounded integer (the only repetition in the language; its bounds must be
constants or `D`, so it always terminates). Names are bound with `=` and may refer to
earlier names, giving every move set a readable definition made of other move sets.

**Types.** The checker runs at load and says what is wrong in the author's own words:

| Type | Is | Examples |
|---|---|---|
| `Int`, `Range` | a number; a step range | `3`, `1..5`, `1..inf` |
| `Component` | a free magnitude or a pinned axis with a sign | `2`, `rank:+1` |
| `Vector` | a list of components | `[1,2]`, `[rank:+1, 1]` |
| `Template` | a vector with a range, a type and the flags | `exact [rank:+1] flip` |
| `MoveSet` | a set of templates | everything above |

Errors it must give, with the position in the text: a pinned axis that does not exist; a
vector with more free components than the board has axes left (reported as "needs N axes",
never silently empty - the engine's deliberate rule); an empty range; `exact` with a `^`;
`flip` with no orientation axis; a union variable used outside its range; an unbound name.

**Relationship to the model.** A moveset *denotes* the section 1 templates and nothing else:
parsing and printing are inverse (`print(parse(t)) == canonical(t)`), so the editor can show
any piece's moves as this text and accept this text back, and the editor's table and the
text are two views of one thing. The text of a moveset is canonical (sorted, normalized
whitespace) so two authors' files for the same piece are byte-identical. Shared files are
plain text (`*.moveset`), referenced from a variant as `moves = "queen"` or inline.

**Tests.** Round trip over every shipped piece in every variant; `D` in {2,3,4,8} for
`queen`, with direction counts from `expectedDirectionCount`; every error above with its
position; the union is order-independent and idempotent; difference removes exactly the
removed templates' directions; a fuzz of malformed text never crashes or loops.

## 10. Order of work (replaces section 7's list)

1. Templates and expansion, loader aliases, equivalence test (no behaviour change).
2. The moveset language: parser, printer, type checker, `*.moveset` files; the editor gains a
   text view of the move table. This needs only step 1, so it ships before any state change.
3. `moved` state, `unmoved`, and the retarget of `from_rank`.
4. Marks as tagged, timed cell state; blocking marks in movegen; en passant on top of them.
5. `attacked(cell)`, `capture_offset`, compound moves; `[[castle]]` desugars to one.
6. Editor: the table's new columns, the compound editor, marks, the palette.

## 11. Step 1 as built (2026-10-18)

- `MoveAtom` gained `kind` (`Extrapolated | Exact | Expanded`), `pins` (axis + signed value),
  `flip` and `sign`; `expandAtomFor(atom, dims, orientAxis, colour)` is the one expansion the
  engine, the preview and the tests share, and `stepRange` / `exactSteps` give the steps an
  atom walks (an exact slide of n cells is the unit step n times, so the cells between are
  checked). Classic atoms keep their original expansion and `toString` byte for byte, so
  `VariantId` is unchanged for every shipped variant.
- File spelling, shared by loader and editor document (`io/atom_toml.*`): `type = "exact" |
  "expanded"`, a literal signed `vector` with one entry per axis, `sign`, `flip` (on by default
  wherever a move names the orientation axis), and `pin = ["rank:+1"]` on extrapolated moves.
  `forward`, `from_rank` and `leaves_en_passant` still read as before.
- Proved: the new kinds reproduce every shipped pawn's directions and step ranges for both
  colours on every shipped board that has one (golden); expansion counts (a pin plus one free
  magnitude is `2(D-1)` directions); a small variant with an exact one-cell piece, a lance and
  a flipped push generates exactly the expected moves; the editor writes the new spelling back
  and the variant keeps its id.
- Not yet: the move table's own columns for the new kinds (a literal move is read out as text
  for now), the moveset language, `unmoved`, marks, `attacked`, compound moves.
