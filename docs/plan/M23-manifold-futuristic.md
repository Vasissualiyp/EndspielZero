# M23 — The manifold theme: futuristic

**Goal:** make `manifold` a real second look for the game: **futuristic**, not just console's pale
opposite. `console` (dark ground, warm wood, amber accent) becomes the default and the main
style (2026-10-30); `manifold` is the alternative a player chooses, and it should be one worth
choosing: a dark, cool, glassy, glowing board where the gluing is the light source.

Presentation only: nothing here enters `VariantId`, a game file or the engine. Both themes keep
working through the one `view::Theme` struct and the one renderer.

## M23.0 Why it does not look futuristic today (what was found)

Rendered side by side (torus, 3-D board and shape view, both themes):

- `Theme::manifold()` (`src/view/theme.hpp:~142`) is a **light** theme: a soft blue page
  (`ink 0xB4C4DB`, `soot 0xE6EAF0`), slate board (`boardLight 0x98A4BE`, `boardDark 0x59637E`),
  electric-blue accent (`ember 0x2E4BFF`). It reuses console's whole shading, so it reads as
  "console, recoloured to daylight". White pieces on the pale ground lose their edge; the grey
  board is low-contrast.
- The renderer has **one material** (`render/shaders/board.frag`: Lambert, a warm key and a cool
  fill, `fragMetal` for mirror faces). Futuristic needs glass, glow and rim light, which the
  shader has no way to express, and no bloom: there is a blur pass (`blur.frag`,
  `BoardRenderer::setBlur`) but it only defocuses the board for the pause menu.
- The ambient background is the same confetti-of-wireframes (`deco`, `imgui_field`) in both.

So this milestone is partly palette and partly **a small material system**, which the planned
wood material (console) needs too - build that once (M23.1), use it for both.

## M23.1 A material id on every instance (shared with the wood material)

> **Specified precisely and built under [M24](M24-wood-material.md) (M24.1-M24.2).** Where the
> two differ, M24 wins: the materials come from `Theme::materials` read by the renderer (no
> `BoardOptions::Materials`), the fractional part of `Instance::material` is a per-instance
> variation, and wood uses shader ids 2 (plank) and 6 (turned).

`Instance` carries `float metal` (`board_renderer.hpp:~58`, shader input `instMetal`, location 9).
Generalise it to a **material**: rename the field `material` (keep the vertex layout and the 84-byte
`static_assert`), values:

| id | material | used by |
|---|---|---|
| 0 | matte (today's shading, unchanged) | everything, by default |
| 1 | mirror / polished metal (today's `metal == 1`) | mirror faces |
| 2 | wood (procedural grain; separate plan, console) | board cells, plinth, pieces |
| 3 | glass | manifold board cells and plinth |
| 4 | crystal | manifold pieces |
| 5 | glow | seam rails, the hovered/selected cell outline |

`BoardOptions` gains `Materials{ cells, pieces, plinth }` (three ids), filled from the theme
(`Theme::materials`), so a theme chooses its look and nothing else in the renderer names a theme.
**Invariant:** with every id 0 or 1 the instance list and the image are bit-identical to today,
so every existing capture and golden test still holds. A test pins it.

## M23.2 The manifold look (shader and palette)

**Palette** (`Theme::manifold()` rewritten; the light flag becomes false, which also flips the
interface text colours):

- Ground: a deep blue-black with a slight violet bias (`ink ~0x05070F`, `soot ~0x0A0E1A`), never
  pure black. Panels a step up, rules as thin cool lines.
- Accent (`ember`): electric cyan (~`0x35E0FF`); `emberDeep` a violet (~`0x7A5CFF`). The seam
  ramp (`seamHueBegin/End`) shifts to cyan-to-magenta so the gluing colours glow against the dark.
- Board: dark translucent glass, two tones far enough apart to read the chequer
  (`boardLight ~0x1C2A44`, `boardDark ~0x0F1830`), with **thin emissive grid lines** between cells
  (the existing groove `rim` colour made emissive, not a dark gap).
- Pieces: white = bright icy `0xDDF4FF`, black = deep indigo `0x2A2F6B` with a cyan rim, so both
  colours read on the dark ground (today's failure on the pale one). `check` stays `blood`
  (a hot red-pink), `moss`/`peek` retuned to cyan-green / amber so legal moves and the hover
  hints (M20) stay distinct from the accent; `test_theme.cpp`'s "not confusable" test must stay
  green for both themes.

**`board.frag`**, material-dispatched (matte path untouched):

- **Glass (3):** low base alpha (blended pass), a Fresnel term `pow(1 - dot(n, v), 3)` adding a
  cyan rim, a faint two-tone vertical gradient, and the emissive grid line on the cell border
  (`fragLocal` near +-0.5). Opaque cells beneath it are not needed: the plinth is the dark floor.
- **Crystal (4):** flat-shaded facets (derive the face normal from `dFdx/dFdy` of `fragWorld`, so
  the faceted piece meshes read as cut gems), a sharp specular, a Fresnel rim in the accent. No
  warm key light: a cool key and a cyan fill (a `push.lightDir`-independent constant pair per
  material, so wood and crystal can light themselves differently).
- **Glow (5):** unlit, `rgb = colour * intensity`, alpha 1; the seam rails use it, so the gluing
  is the brightest thing on the board.
- The 5-vertex-attribute plumbing already exists (`fragWorld`, `fragNormal`, `fragLocal`); no new
  vertex data is needed. The new shader branches read one extra push-constant vec4
  `push.material` (rim colour, glow intensity) so the theme sets the palette, not the shader.

**Bloom** (cheap, reusing what exists): after the board pass, a threshold + the existing blur
into a half-resolution target, added back at ~0.35, gated by `Theme::bloom` (0 for console, so
console is unchanged). Pieces are never blurred in the *pause* defocus, only in the glow add.
Budget: one extra pass at half resolution; measure it (`cb-bench-baseline` does not cover the
GUI, so use the `fps` readout and record numbers in the status section).

## M23.3 The ground and the interface

- **Background:** replace the confetti of wireframe solids (`deco.cpp`, `backdrop.frag`) under
  manifold by a slow **perspective grid floor** and a few drifting cyan/violet light streaks; the
  parameters come from the theme (`Theme::backdrop` enum `Confetti | Grid`), so console keeps its
  look. Must stay a pure function of the clock so `--shot --t N` reproduces.
- **Interface:** thin 1 px rules in the accent at low alpha, a mono face for numbers and labels
  (the theme already names fonts for `fontMono_`), corner brackets on panels instead of fills. All
  of this is `applyStyle`/`ui_widgets` colour and geometry, driven by the theme; nothing is
  hard-coded to manifold.
- **Shape view:** the torus/Klein in glass shows the far side through the near side naturally
  (it is already a ghost option, `geometryGhost`); under manifold the default ghost is lower and
  the seam rails glow, which also fixes the muddy overlap seen in the 3-D torus trailer beat.

## M23.4 Tests

1. `tests/unit/view/test_theme.cpp`: manifold is a dark theme (`light == false`, ink luminance
   < 0.02), its pieces and board tones all read on its ground (the existing "both piece colours
   read on both squares" tests over `{manifold, console}` pass unchanged), peek/hint/ember are
   not confusable.
2. `tests/render/test_offscreen_render.cpp`, `buildInstances` only (no device): with materials
   0/1 the instance list is bit-identical to the pre-M23 one; with manifold's materials, cells
   carry id 3, pieces id 4, seam rails id 5, the plinth id 3, and nothing else changes.
3. A `[render]` capture test for each new material that the frame is non-uniform and the
   validation layers are silent (the existing `--shot` gate).

## M23.5 Verification against the real artifact

- Side-by-side filmstrip, console vs manifold, of: the standard board (3-D and flat), the torus
  shape view, the Klein bottle, the main menu, the paused game. Judge on contrast first (can both
  colours of piece be told apart at a glance on both squares?), then mood.
- A mid-size window and 1080p `--shot`; the interface text at both sizes.
- The trailer under manifold (`--trailer-clip`): the 3-D torus beat's overlapping translucent
  panels, which are the worst-looking thing today.
- FPS with bloom on a mid machine; if it costs more than a few ms, ship manifold without bloom and
  record it.

## M23.6 Order of work, and when

Before the demo is published (roadmap Wave 2, alongside M22): **M23.1 first** (the material id,
bit-identical default), then the manifold palette (M23.2 colours only, a quick look at it), then
glass/crystal/glow, then the background and interface, then bloom last (the most optional). The
**wood material for console** is a separate small plan that uses M23.1 and should be done first
because console is the default.

**Cut line if it slips:** palette + glass cells + glow seams + crystal pieces (M23.1, M23.2
without bloom). The grid-floor background, the interface restyle and bloom follow the demo.

## Status

Planned (2026-10-30); nothing built. `console` became the default theme the same day.
