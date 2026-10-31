# M24 — Materials, step one: the material id and console's wood

**Goal:** the default look (`console`, since 2026-10-30) stops being flat-painted slabs and reads
as a **real wooden set**: maple and walnut squares with visible grain, a darker wooden plinth, and
turned boxwood / ebonised pieces under a soft varnish highlight. This is the first half of the
material work M23 describes: it builds the shared **material id** (M23.1, specified precisely here
and superseding M23.1's sketch wherever the two differ) and the **wood** material on top of it.
M23.2 (glass, crystal, glow for `manifold`) builds on exactly this plumbing afterwards.

Presentation only. Nothing here enters `VariantId`, a game file, `Settings` or the engine. No
floats below `src/view`. No new textures, no new vertex attributes, no new pipelines, no new
passes: one varying is added and one shader gains a branch.

## M24.0 What was found (why it looks unpolished)

- `render/shaders/board.frag` has **one material**: Lambert with a warm key and a cool fill,
  multiplied into a flat per-instance colour, plus a `fragMetal > 0.5` branch for mirror rails.
  Every square is one uniform colour, so at any size the board reads as a diagram, not an
  object. Pieces are the same: a flat colour with a foot-darkening term.
- `Instance::metal` (`board_renderer.hpp:58`, vertex location 9, `instMetal`) is the only
  per-instance shading switch; it is set in exactly one place (`board_renderer.cpp:~1383`, mirror
  seam rails = 1). Everything else is 0.
- `BoardRenderer` already owns the theme (`theme_`, `setTheme`), so the material choice can come
  from the theme with **no** change to `BoardOptions` or `gui/main.cpp`'s `optionsFrom` (this
  replaces M23.1's `BoardOptions::Materials` idea - one fewer plumbing path, same effect).
- `BoardRenderer`'s default theme is `Theme::manifold()` (`board_renderer.hpp:297`), so every
  existing `[render]` unit test that builds instances without calling `setTheme` runs under
  manifold - which stays all-matte in this milestone. Those tests must pass untouched.
- The `--shot`/`--clip` captures use the default `Settings`, whose theme is now `console`, so the
  `gui-*` ctest captures will exercise wood automatically.
- The board's vertex shader already has the mesh-local position (`inPosition`, rolled into
  `spun`) and the instance scale; nothing passes the *object-space* position to the fragment
  shader today. Wood must be sampled in object space: sampled in world space, the grain would
  slide over a piece as it moves and over the board as the camera's layout changes.

## M24.1 The material id (implements M23.1)

### Data

`src/view/theme.hpp` (view layer: names only, no render ids leak upward or downward):

```cpp
/// How a surface is shaded. The renderer maps each to its own shader branch; a theme
/// only says which one each part of the board uses.
enum class Material : std::uint8_t { Matte = 0, Metal = 1, Wood = 2, Glass = 3, Crystal = 4, Glow = 5 };

/// Which material each part of the board is made of.
struct Materials {
  Material cells{Material::Matte};
  Material pieces{Material::Matte};
  Material plinth{Material::Matte};
};
```

`Theme` gains `Materials materials{Material::Wood, Material::Wood, Material::Wood};` (the struct
defaults are console's, as for every other member). `Theme::manifold()` sets
`t.materials = Materials{};` (all matte) explicitly. Update the stale doc comment on `Theme`
(it still says manifold is the default) to say console is the default and manifold the
alternative.

`src/render/board_renderer.hpp`: rename `Instance::metal` to `Instance::material` (still a
`float`, still last, `static_assert(sizeof(Instance) == 84)` unchanged, attribute location 9
unchanged, the `offsetof` at `board_renderer.cpp:~206` follows the rename). Doc comment:

> The shader material: its integer part picks the branch (0 matte, 1 metal, 2 plank wood,
> 6 turned wood; 3-5 reserved for M23.2), its fractional part in [0, 0.99] is a per-instance
> variation the branch may use (wood: grain direction and offset). 0 is today's shading exactly.

Shader ids are **renderer-internal**: `view::Material::Wood` becomes shader id 2 on a plank
(cells, plinth, the surface mesh) and shader id 6 on a turned object (pieces). One function in
the anonymous namespace of `board_renderer.cpp` does the mapping and nothing else names a number:

```cpp
enum class Cut : std::uint8_t { Plank, Turned };
/// The `Instance::material` value for a theme material on a given kind of object.
/// `variation` is clamped to [0, 0.99] so the integer part never changes.
float shaderMaterial(view::Material m, Cut cut, float variation);
// Matte -> 0, Metal -> 1, Wood -> (cut == Plank ? 2 : 6) + variation,
// Glass/Crystal/Glow -> 0 for now (M23.2 will give them 3/4/5); variation is ignored
// (returns the bare integer) for every id except wood.
```

And a deterministic integer hash for variations (no floats in the hashing; float only at the end):

```cpp
/// A stable value in [0, 1) from an integer key - same on every machine, every frame.
float hash01(std::uint32_t key) {
  std::uint32_t x = key * 0x9E3779B1u;
  x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
  return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}
```

### Who gets which material (every `Instance` emission site in `buildInstances`)

| Site (current line, approx.) | Gets |
|---|---|
| ordinary/flat cell `cell` (~1366) | `shaderMaterial(theme_.materials.cells, Plank, 0.5f*(parity&1) + 0.49f*hash01(pl.cell))` |
| `plinthInstance` (~75; called at ~1117 and ~1429) | `shaderMaterial(t.materials.plinth, Plank, 0.25f)` - set inside `plinthInstance` so both callers agree |
| `emitPiece` body (~1326), the captured-piece flourish body (~1753), `emitSurfacePiece`'s body (~990-1036) | `shaderMaterial(theme_.materials.pieces, Turned, 0.99f*hash01(2u*piece.type + (white?0:1)))` - keyed on type and colour, **never** on cell or position, so a travelling or shrinking piece keeps its grain |
| `emitSurfacePiece`'s puck disc (the token, `shapePieceCylinders`) | the pieces material, `Turned`, same variation; its **icon decal** stays matte (0) |
| the surface mesh's identity instance (~1243) | `shaderMaterial(theme_.materials.cells, Plank, 0.25f)` (one instance for the whole curved board; its object space is world space, see Risks) |
| seam rails (~1383, ~1588) | unchanged: mirror = 1 (via `shaderMaterial(Metal, ...)`), portal = 0 |
| timeline rails/connectors/arrows/fillets, portal iris, flash rings, shadows, scaffold | 0 (matte), unchanged |

### Invariant (the M23.1 promise)

With `theme.materials` all `Matte`, `buildInstances` produces an instance list **bit-identical**
to the pre-M24 one (the `material` field is 0 everywhere except 1 on mirror rails, exactly as
`metal` was), and the shader's matte and metal paths compute exactly what they compute today.
Do not re-order, re-associate or "tidy" any expression in the existing matte/metal/seam-edge code
in `board.frag`; new code goes in new branches only.

## M24.2 The shader plumbing

`board.vert`:

- `layout(location = 9) in float instMaterial;` (rename of `instMetal`).
- `layout(location = 7) flat out float fragMaterial;` (rename of `fragMetal`, now `flat`: a
  material must not be interpolated, and `flat` makes the integer/fraction split exact).
- New `layout(location = 9) out vec3 fragObject;` = `vec3(spun, inPosition.z) * instScale` -
  the position in the object's own frame (rolled and scaled, **before** `instQuat` and before
  `instCenter`), in world units. A cell's is its slab, a piece's its own body, the plinth's the
  whole table, the surface mesh's is world space (its instance is identity).

`board.frag`: matching `flat in float fragMaterial` (location 7) and `in vec3 fragObject`
(location 9). Decode once at the top of `main`:

```glsl
int matId = int(fragMaterial);
float matVar = fragMaterial - float(matId);
```

The existing `if (fragMetal > 0.5)` becomes `if (matId == 1)` with its body unchanged. Wood
inserts **after** `vec3 rgb = fragColor.rgb * shade;` and **before** the metal branch / the seam
edge overlay, so the plinth's lit rim and any seam band still draw on top of the grain:

```glsl
if (matId == 2 || matId == 6) {
    rgb *= woodFactor(fragObject, matVar, matId == 6);
    vec3 toEye = normalize(push.eyePos.xyz - fragWorld);
    vec3 halfV = normalize(normalize(-push.lightDir.xyz) + toEye);
    float spec = pow(max(dot(n, halfV), 0.0), kVarnishPower);
    rgb += vec3(1.00, 0.93, 0.82) * spec * (matId == 6 ? kVarnishPiece : kVarnishBoard);
}
```

Update the file's header comment ("One material, one light") to describe the dispatch.

### The wood function (write it exactly like this, then tune only the constants)

Value noise from an integer-lattice hash. **Do not use a `sin`-based hash**: its result depends
on the driver's `sin` precision, and the captures must be reproducible across machines.

```glsl
const float kWoodScale     = 3.0;   // noise lattice cells per world unit, along the board
const float kRingsBoard    = 14.0;  // growth rings per world unit across a plank
const float kRingsPiece    = 18.0;  // ... across a turned piece
const float kRingAmp       = 0.26;  // how dark a latewood band is
const float kRingMean      = 0.80;  // the mean of the ring profile below; keeps the average at 1
const float kFibreAmp      = 0.10;  // fine fibre streaks
const float kVarnishPower  = 48.0;
const float kVarnishBoard  = 0.10;
const float kVarnishPiece  = 0.18;

// "Hash without sine" (Dave Hoskins): only fract and arithmetic, so every driver agrees.
float hash13(vec3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

// A multiplier around 1: the wood's figure laid over the instance's own colour, so the
// theme's square and piece colours stay what they are on average (test_theme pins those).
float woodFactor(vec3 obj, float variation, bool turned) {
    // Into grain space: g.x runs along the grain, (g.y, g.z) across it.
    vec3 g;
    float s;
    if (turned) {
        g = vec3(obj.z, obj.x, obj.y);             // turned from a vertical blank
        s = variation;
    } else {
        // A plank's variation carries its grain direction in its upper half, so the
        // squares of an inlaid board alternate the way a real one does.
        g = variation < 0.5 ? obj : vec3(obj.y, obj.x, obj.z);
        s = fract(variation * 2.0);
    }
    // The pith (the rings' centre) sits well off the object, so a visible face cuts the
    // rings at a shallow angle: long wavy stripes, never a bullseye.
    vec2 pith = vec2(1.7 + 1.3 * s, -0.9 - 1.1 * fract(s * 7.13));
    vec3 q = g * kWoodScale + vec3(s * 41.0, s * 13.0, 0.0);
    float warp = 0.65 * valueNoise(vec3(q.x * 0.35, q.y * 2.0, q.z * 2.0))
               + 0.35 * valueNoise(vec3(q.x * 0.70, q.y * 4.0, q.z * 4.0) + 17.0) - 0.5;
    float r = length(g.yz - pith) * (turned ? kRingsPiece : kRingsBoard) + warp * 1.6;
    float t = fract(r);
    // Earlywood is wide and pale, latewood a thin dark band.
    float ring = smoothstep(0.0, 0.12, t) * (1.0 - smoothstep(0.72, 1.0, t));
    float fibre = valueNoise(vec3(q.x * 1.2, q.y * 38.0, q.z * 38.0));
    return 1.0 + kRingAmp * (ring - kRingMean) + kFibreAmp * (fibre - 0.5);
}
```

Tuning: the constants may be adjusted by eye against the M24.5 captures, but keep the
mean-preserving structure (`ring - kRingMean`, `fibre - 0.5`) and record the final values in the
status section. If grain shimmers (aliases) on far squares at 1080p, lower `kFibreAmp` with
distance (`kFibreAmp * clamp(1.5 - 0.08 * length(push.eyePos.xyz - fragWorld), 0.0, 1.0)`)
rather than adding mip-style filtering.

## M24.3 Capture switches (for A/B, never persisted)

`src/gui/main.cpp`:

- `--theme <name>`: overrides the theme for this run (`themeFromName`), so a capture can be
  taken under either theme without a settings file. It does not write `Settings`.
- `--matte`: after the theme is resolved, set `theme.materials = view::Materials{}` - the
  pre-M24 look, for before/after comparison.

The theme is resolved at three sites (`main.cpp:~742`, `~1438`, `~2056`, each
`view::themeFromName(shell->settings().theme)`); replace all three by one local helper
`resolveTheme(const app::Settings&)` that applies both overrides, so the three cannot disagree.
Add both flags to the commands block in `AGENTS.md`.

## M24.4 Tests (write them first)

1. `tests/unit/view/test_theme.cpp`:
   - `Theme::console().materials` is `{Wood, Wood, Wood}`; `Theme::manifold().materials` is all
     `Matte`.
   - The existing contrast tests stay green untouched (the palette does not change in M24).
2. `tests/render/test_offscreen_render.cpp`, `buildInstances` only (no device), on `standard`:
   - **Invariant:** a renderer given `console()` with `materials = Materials{}` produces, for
     every instance, `material == 0.0f` except mirror rails (`mirrorbox`: those are exactly
     `1.0f`), and its instances are bytewise equal (`std::memcmp` on the vector) to a renderer
     given the same theme twice - and every non-`material` field is bytewise equal to the
     wood-themed list (materials change nothing but the material field).
   - Under `console()`: every one of the 64 cells has `floor(material) == 2`, the plinth has
     exactly `2.25f`, every piece body `floor == 6`; light and dark squares fall in different
     halves (`fract(material) < 0.5` iff the square's parity is even - assert via the same
     `squareParity` the renderer uses); all 64 cell variations are not all equal.
   - **Stable grain on a moving piece:** in the existing move-animation fixture (the one at
     ~line 197 that passes `&s.animation()`), the travelling piece's `material` equals the same
     piece's `material` before the move, at several `t`.
   - Under `console()` on `torus` with `surface = true`: the surface identity instance's
     material is `2.25f`; seam rails, shadows (`set.shadows`) and the flourish rings are 0.
   - `hash01` is in [0, 1) for keys 0..10000 and is not constant (put it in a header-visible
     spot only if the test needs it; otherwise test through the instance values above).
3. Capture tests (`tests/CMakeLists.txt`, the existing `gui-*` pattern - non-uniform frame,
   validation layers silent): add `gui-wood` (`standard --shot ...`, default theme) and
   `gui-wood-geometry` (`torus --geometry --shot ...`). Add one CMake-script test in the style
   of `tests/check_cinema_grade.cmake`, `tests/check_wood.cmake`, which shoots `standard` at
   640x360 twice - default and `--matte` - and asserts on a central window of the board
   (the middle 40% x 40% of the frame): (a) mean luminance of wood within 8% of matte's (the
   grain does not repaint the board), and (b) the mean absolute horizontal neighbour
   difference in that window is at least 1.3x matte's (grain is actually visible). Parse PPMs
   the way `check_cinema_grade.cmake` does.
4. The existing `gui-play-deterministic` / `check_clip_deterministic` tests must stay green -
   they now exercise wood and prove it is a pure function of the frame.

## M24.5 Verification against the real artifact

Not done until these are looked at, not just generated. Write them to `out/m24/` (untracked):

```bash
G=./build/dev/src/gui/endspielzero_gui
$G standard --shot out/m24/standard-wood.ppm
$G standard --shot out/m24/standard-matte.ppm --matte
$G standard --script $'click e2\nclick e4' --follow route --move-t 0.5 --cinema --shot out/m24/move.ppm
$G torus --geometry --shot out/m24/torus-wood.ppm
$G klein --geometry --shot out/m24/klein-wood.ppm
$G cube5 --shot out/m24/cube5-wood.ppm
$G 5d --shot out/m24/5d-wood.ppm
$G standard --flat --shot out/m24/flat-wood.ppm
$G standard --theme manifold --shot out/m24/standard-manifold.ppm   # must look exactly as before
$G standard --width 1920 --height 1080 --shot out/m24/standard-1080.ppm  # look for shimmer
```

Judge, in this order: (1) both piece colours still read at a glance on both squares; (2) the
grain looks like wood (long, wavy, alternating direction square to square), not noise, stripes
or a bullseye; (3) the plinth reads as one darker piece of wood with its lit rim on top; (4)
pieces show vertical grain and a soft highlight, and a piece mid-move does not swim; (5)
manifold is visually unchanged. Record what was looked at and the final constants in the
status section below, plus a frame-time note from the `fps` readout before/after on the
interactive window (the wood is ~11 value-noise lookups per fragment; if it costs more than
~1 ms at 1080p, drop the second warp octave first and record it).

## Risks and non-goals

- **The surface mesh's grain is world-space**: the geometry view's board is one mesh whose
  instance is identity, so sliding the board along its surface (`--slide`, middle-drag) or
  forming/everting it moves the squares *through* a fixed block of wood. Accepted for now and
  stated here so nobody "fixes" it by sampling in world space everywhere. A proper fix would put
  a per-vertex wood coordinate in `MeshVertex`, which is out of scope (vertex layout change).
- Not PBR, no normal maps, no textures, no shadows, no bloom, no change to the light. The
  palette is untouched; a richer maple/walnut retune is a possible follow-up, but it must go
  through `test_theme`'s contrast tests.
- Flat (`--flat`) view gets the wood on its squares too (it is the same cell instance); its
  pieces are interface tokens and are unaffected.
- `manifold` stays matte until M23.2, which reuses `shaderMaterial`, the decode, `fragObject`
  and the dispatch built here and fills ids 3-5.

## Order of work

1. M24.4 test 1 and test 2's invariant (red), then the M24.1 rename + `Material`/`Materials` +
   `shaderMaterial` with all-matte behaviour (green, captures unchanged).
2. Remaining test-2 cases (red), the emission-site table (green).
3. M24.2 shader plumbing and `woodFactor`; M24.3 flags; M24.4 capture tests.
4. M24.5 captures, tune constants, record. `tools/test.sh --build render app view`, then
   `ctest --preset dev -R gui-`, then `tools/precommit.sh`.
5. Update: this file's status, the M24 row in `00-roadmap.md`, M23's status line ("M23.1 built
   under M24"), `AGENTS.md` (the two flags; a "Where to add what" row: *A surface's material
   (wood, metal, ...) → `Theme::materials` + `shaderMaterial` + the dispatch in `board.frag`*;
   and a gotcha: *a material is sampled in object space (`fragObject`) and keyed on what the
   object is, never where it is, or the grain swims on a moving piece*).

## Status

Built (2026-10-30): `Theme::materials`, the preserved 84-byte instance material
field, object-space shader coordinates, stable type/colour grain variations, console
wood on cells/plinth/pieces, and the `--theme`/`--matte` A/B flags. Flat icons remain
matte; shape-view pucks are turned wood with matte icon decals. GPU and instance tests
cover both themes, moving pieces, mirror rails, surface glass, and the grain-vs-matte
luminance/edge measurement. The matte/metal shader expressions were left untouched.

Looked at 960x540 console wood vs matte, mid-move, torus and Klein surface, cube5,
5d and flat tokens; manifold standard/flat/torus/Klein, menu and pause (filmstrip in
untracked `out/m24/`). The first grain looked like tightly spaced etching, so its
rings and fibre were reduced. Final constants: wood scale 3; board/piece rings 7/9;
ring amp 0.19, mean 0.80; fibre amp 0.055; varnish power 48, board/piece strength
0.10/0.18. The second warp octave was dropped after a software Vulkan 1080p
measurement: 41.5 ms wood vs 33.4 ms matte; after removal 39.6 vs 34.3 ms over
20 frames. These are llvmpipe numbers, **not a GPU performance claim**; real GPU
timing and interactive FPS still need a display. The wood remains visibly grained
without repainting the square's mean luminance (`gui-wood-contrast`). The 1080p
capture was checked for aliased fibre; no obvious pixel crawl can be assessed in a
still image, so motion on a real display is an outstanding visual check.
