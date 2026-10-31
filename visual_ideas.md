<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Manifold visual ideas — choose what to add

The intended look is a physical LCD/LED board carrying holographic chess pieces in
cyberspace. The board supplies structure; the pieces supply light. Effects should
support a game that remains readable for an entire session, not just a trailer.

**Already requested:** rare tiny panel-interference strips and a
barely visible LCD settling response when a square's gameplay colour changes.
Both effects are implemented.
See `docs/plan/M23.8-led-ambient-and-response.md`. These are not extra choices below.
The existing LED matrix, hologram scan, moving ground and hover-glitch buttons are
also already part of the design. No optional item below is approved for implementation.

## Optional choices

| ID | Idea and visible treatment | What it contributes | Readability/intensity tradeoff | Implementation weight |
|---|---|---|---|---|
| 1 | **Recessed display modules.** Give each square a thin dark gasket and a restrained inner bevel highlight; corners read as manufactured panels instead of painted checks. | A tangible screen assembly beneath the holograms, visible even in a still. | Static and quiet; border must not shrink the playable-looking area or compete with portal rails. | Small: refine existing cell-local LED border shading; inspect curved/inverted surfaces. |
| 2 | **Hologram projector contact.** A very faint neutral light footprint immediately under each standing piece, roughly the shape of its base, never a tall beam. | Makes the piece look projected *by* its square and visually seats it on the display. | Can resemble selection if too bright; keep ordinary footprints much weaker and smaller than any move cue, preserving both armies' identity. | Medium: existing base/disc geometry or local panel emission; geometry surfaces and moving pieces need careful anchoring. |
| 3 | **Off-axis LCD response.** Very gently lower panel brightness when seen at a grazing angle, while the front view retains current contrast. | Gives the board a screen surface rather than self-lit paint. | Too much makes far ranks disappear; strong lower bound and portal/piece exemptions essential. No rainbow shift. | Small: one bounded view-normal term in the LED shader, tested on rotated/inside-out boards. |
| 4 | **Emission restricted to the pixel grid.** On a close zoom, bright highlights strengthen existing emitter centres while their tiny black gaps stay black. | More convincing display colour changes and close-up LCD physicality. | Little normal-play cost; can produce moiré if it bypasses current derivative filtering. | Small–medium: extend the existing matrix shading and close/far sampling tests. |
| 5 | **Quiet display edge hardware.** A few low-contrast ventilation slots or connector marks on the plinth's side, plus one tiny neutral power light. | Turns the whole board into a deliberate device without filling it with decoration. | Keep detail off the squares and away from seam hues; useful in oblique views, mostly invisible from above. | Small–medium: procedural glass/plinth treatment, distinct from LED cells. |
| 6 | **Arrival-only hologram reacquisition.** Once a moved piece reaches its legal destination, one short, faint scan stabilizes its silhouette, then the ordinary scan resumes. | Suggests projection being re-established at the receiving panel. | Must not delay or obscure the actual move or mimic a capture; no repeated flicker, no disappearing figure. | Medium: explicit move-event timing and object-local material control; arbitrary still captures need defined event age. |
| 7 | **Very faint directional halo on piece silhouettes.** A narrow cool-neutral halo strengthens only along selected viewing angles; solid base silhouette and army colour remain clear. | A stronger holographic light impression without making pieces glassy again. | Broad glow washes out nearby pieces and portal hues; avoid whole-screen bloom and large halos. | Medium: shader rim refinement first; a genuine outside-silhouette halo would need extra geometry and more testing. |
| 8 | **Topology links as data conduits.** Existing matching portal rails carry an occasional very dim brightness packet, consistently related at their two ends. | Makes non-Euclidean connections feel like active circuitry rather than arbitrary coloured markings. | The portal's original hue and continuous identity must dominate; moving packets must not be mistaken for legal routes. | Medium–large: use stable portal identity and seam parameterization, including twists and geometry view; not a per-cell random stripe. |
| 9 | **Layer brackets for higher-dimensional boards.** Quiet neutral corner marks or coordinate ticks frame each visible board layer, shown mainly near the focused layer. | Makes multiple boards feel like organized display surfaces in a shared space. | Avoid dense labels or implying a new move relationship; keep unrelated layers subdued and picking unchanged. | Medium: layout-aware presentation geometry using existing layer information. |
| 10 | **Soft spatial depth cues behind the board.** A few slow, dim points at different depths drift behind the existing moving ground, fading near the board silhouette. | Strengthens cyberspace depth without changing board materials. | Can compete with the existing ground and chess terminals; only worth adding if the current background still feels flat. | Medium: extend the existing backdrop, no separate particle framework. |
| 11 | **A calm accessibility presentation.** Optional reduction of ambient glitches/scan motion while retaining static LED pixels and hologram silhouettes. | Keeps the aesthetic usable for players who find continuous motion distracting. | Less animated, equally legible. This is a preference, not a new visual effect forced on everyone. | Medium: one coherent presentation setting propagated to shader/UI effects and captures, rather than many scattered switches. |

## Suggested first choices

**1 + 2** would do the most to make the display-and-projection idea understandable
in a single still image. **3 or 4** would then improve the material's close-up
credibility without adding visual traffic. **6** adds a meaningful moment of life
during play. **8** is distinctive to this game's geometry, but deserves its own
careful design rather than being bundled with general decoration.

Avoid a full-screen CRT filter, permanent RGB splitting, strong bloom, constantly
corrupted pieces or random flashes across entire ranks. Those would obscure the
LCD/hologram distinction and compete with gameplay signals. Select IDs or describe
a combination; the remaining ideas stay unimplemented.
