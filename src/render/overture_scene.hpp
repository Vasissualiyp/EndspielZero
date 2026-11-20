// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/overture.hpp"
#include "render/piece_icon.hpp"
#include "space/dims.hpp"
#include "view/morph.hpp"
#include "view/theme.hpp"

namespace cb::render {

/// A point in the overture's own world. Y is up, the flat board lies in y = 0, files
/// run along x and ranks along z - the same handedness the board renderer uses, so a
/// pose that looks right here looks right there.
struct OvVec3 {
  float x{0}, y{0}, z{0};
};

/// What a quad is made of. A tone rather than a colour, so every palette decision stays
/// in `drawOverture` and a retheme cannot be defeated by a scene function that thought
/// it knew better.
enum class OvTone : std::uint8_t {
  Light,   ///< a light square
  Dark,    ///< a dark square
  Lit,     ///< called out by a rule - the accent
  Scorch,  ///< what a detonation left
  Mirror,  ///< a reflecting wall: bright metal, deliberately unhued
  Scrim,   ///< a dimming wash over a cell a rule has taken out of play
};

struct OvQuad {
  OvVec3 p[4];
  OvTone tone{OvTone::Light};
  float fade{1.0f};
  /// An explicit colour, for a face that is a portal rather than a board cell. A tone
  /// cannot name one: the seam ramp is computed from the theme, not stored in it. When
  /// set, `tone` is ignored and the face is drawn flat in this colour.
  bool hasColour{false};
  view::Rgba colour{};
};

/// A piece, standing along its surface's normal - which is what lets one stand on the
/// outside of a torus and another on the underside without either lying down.
struct OvToken {
  OvVec3 at;
  OvVec3 normal{0.0f, 1.0f, 0.0f};
  char glyph{'P'};
  bool white{true};
  float height{1.0f};
  float fade{1.0f};
  /// Drawn mirrored, for the bishop that comes home off a Moebius band.
  bool mirrored{false};
};

struct OvTrail {
  std::vector<OvVec3> pts;
  view::Rgba colour{};
  float width{2.0f};
  float fade{1.0f};
  bool dashed{false};
};

struct OvBurst {
  OvVec3 at;
  float radius{1.0f};
  float fade{1.0f};
  view::Rgba colour{};
  /// A cross rather than a ring: a refusal, not an event.
  bool cross{false};
};

/// Where the overture is seen from. `reach` is the world radius the pane must hold, so
/// growing it is how an overture zooms out as its object gets bigger - and because it is
/// a function of t like everything else, the zoom cannot drift out of step with the
/// shape.
struct OvCamera {
  float yaw{0.0f};
  float elev{1.2f};
  float reach{9.0f};
  float persp{0.10f};
};

/// One frame of an overture: everything to draw, and nothing about how.
struct OvertureScene {
  std::vector<OvQuad> quads;
  std::vector<OvToken> tokens;
  std::vector<OvTrail> trails;
  std::vector<OvBurst> bursts;
  OvCamera cam;
  std::string caption;
};

/// The scene, as a pure function of progress.
///
/// `t` runs 0 at the flat 8x8 to 1 at the formed shape. It reads no clock and no
/// previous frame, which is what makes reverse playback free - it is this function
/// called with a falling `t` - and a screenshot reproducible.
///
/// `intro` is honoured only by `Overture::Standard`, where it builds the board out of
/// nothing; see `app::OverturePlayer::intro`.
[[nodiscard]] OvertureScene overtureScene(app::Overture which, float t, bool intro,
                                          const view::Theme& theme);

/// The scene for a variant with no hand-authored overture, derived from its spec (M13).
///
/// Pure in `t`, like its sibling: it opens on the flat board, forms the surface the
/// variant's identifications imply, and unwinds. A data-only variant animates without a
/// line of scene code written for it.
[[nodiscard]] OvertureScene derivedOvertureScene(const VariantSpec& v, float t,
                                                 const view::Theme& theme);

/// How far a surface has been turned through itself, and nothing else about it (M17).
///
/// A shape is rigid; a surface is not. `evert` runs 0 to 1 and pulls the embedding
/// through its own middle: a torus's hole closes and reopens with what was inside it now
/// outside, a Moebius band is drawn through its own loop, and a cylinder - which no
/// rotation can turn inside out, being open at both ends - is rolled back over itself
/// the way a sock is. At 0 the surface is exactly the one the library screen draws, so
/// every existing capture still holds; the whole family is a pure function of the
/// number, like an overture, which is what makes a drag reversible and a still
/// reproducible.
struct SurfacePose {
  /// Where the board sits *on* the surface, in lattice cells - the whole board slid
  /// along, not the shape moved. Sliding one cell along the files puts a1 where b1 was;
  /// keep going and a1 arrives back at a1 having been all the way round, which on a
  /// Moebius band takes two laps and comes home mirrored. The surface functions are
  /// defined for every real `u` and `v` and already satisfy the variant's gluing there,
  /// so this is sampling, not a special case.
  float slideU{0.0f};
  float slideV{0.0f};
  /// The `hyper4` tesseract's own two live fourth-dimension rotation angles, in radians -
  /// the middle-drag's 4D turn, where a glued board has a slide. `hyperSpinU` is the
  /// file/aeon plane: the library's fixed spin, so its default is that same `0.9` and the
  /// opening, undragged picture is unchanged to the bit. `hyperSpinV` is a second, rank/
  /// aeon plane, identity (`0`) at rest, so the two angles together turn the board
  /// through its fourth dimension instead of only orbiting the camera. A pure function of
  /// the two numbers, so a drag retraces and a still reproduces. Ignored by every other
  /// shape.
  float hyperSpinU{0.9f};
  float hyperSpinV{0.0f};
  /// The Klein bottle ring's twist multiplier: 1 is the value the shipped surface uses.
  /// A diagnostic knob; ignored by every other warp.
  float twist{1.0f};
  /// Which side is out, for the play board: past the halfway point `PlaySurface` swaps
  /// the outward normal, so the pieces stand on the other face while the squares stay
  /// exactly where they were (M17.7, revised). The surface functions still *carry* a
  /// geometric eversion - the overtures use it - but the play board does not take it: it
  /// mirrors the whole shape about the origin and moves every cell, which is not a board.
  float evert{0.0f};
  /// How much of the Moebius ribbon's stretch to keep, 0 to 1, where 1 is the shape the
  /// library screen draws. The ribbon is the one surface in the catalogue whose two axes
  /// can be traded against each other freely - area is preserved either way - and the
  /// library spends all of it on the twist being unmistakable, which leaves each cell
  /// nine times longer than it is wide. A board has to be played on as well as looked at,
  /// so `PlaySurface` spends less. Nothing else reads it.
  float stretch{1.0f};
  /// The ring or loop radius as a multiple of the shipped one, the tube/ribbon's own
  /// cross-section left unchanged: a larger value opens a tight torus/Klein bottle, or
  /// lengthens the Moebius loop, without thickening or thinning the material. 1 is the
  /// shipped shape.
  float openness{1.0f};
  /// The cross-section size as a multiple of the shipped one - the tube's thickness, the
  /// outer-to-inner radius - leaving the ring radius alone. A legibility cheat like
  /// `openness`: the cells stretch around the fatter tube. 1 is the shipped shape.
  float thickness{1.0f};
  /// For the Klein bottle: the total rotation of one rank end relative to the other, as a
  /// fraction of the file loop (one file = one `nx`th). It ramps from 0 at rank 0 to this
  /// value at rank 1, so the square lines spiral round the tube rather than the whole
  /// cylinder turning rigidly. This is not the half-turn twist. 0 is the shipped shape.
  float collapsePhase{0.0f};
  /// How far the surface has formed, 0 to 1 (M18.6): 0 is the surface flat, 1 is the
  /// fully-formed shipped shape. It scales the underlying surface functions' own roll
  /// parameters (`th`/`ph` and the Klein pinch), which are 0 at the flat board - so a
  /// morph is sampling the same functions the overtures already animate, not a second
  /// embedding blended in. Default 1, so every existing caller that does not set it keeps
  /// today's always-fully-formed behaviour and every captured frame is unchanged.
  float formed{1.0f};
  /// For a D >= 3 nested-shell shape (`torus3d`/`torus3d_8x8`): how far the levels have
  /// spread into their own radii, 0 to 1. At 0 every level sits at the *same* radius -
  /// the mean of the shipped ones - so the whole stack reads as a single ring, exactly
  /// the 2-D torus `formed = 1` already draws; at 1 the levels are at their shipped,
  /// distinct radii, nested shells. It is a pure function of the same `shellTube` the
  /// shape already uses, not a second shape blended in: a single ring is not a special
  /// case, it is every level's `d` collapsed to one value. Default 1 (the shipped,
  /// always-spread look), so an existing caller that never sets it is unchanged
  /// (M13.9's trailer attract sequence: "klein folds back into a torus, which then
  /// visibly copies itself into the nested shells").
  float shellSpread{1.0f};
  /// The lab's slab skin (M24.5b): `PlaySurface::build` keeps each tile at its flat size
  /// inside a stretched cell, and adds the membrane behind it. Off by default, so every
  /// existing caller is unchanged; only the lab game path turns it on. Ignored by the
  /// overtures and by the stacked D >= 3 shapes.
  bool skin{false};
};

/// A sequential sub-window of a morph's own 0..1 progress (M18 revision, items 2 and 3).
///
/// The flat<->shape morph is one number, `SurfacePose::formed`, but a shape's
/// construction is several stages that must not overlap: the Mobius ribbon has to stretch
/// before it curls, and the Klein bottle has to form its ring, then fold its
/// cross-section, then apply its twist. `stageForm(form, begin, end)` clamps `form` to
/// `[begin, end]` and rescales it to 0..1 within that window, linear like every other
/// ramp in the morph - so a caller can spend disjoint slices of the morph on disjoint
/// parameters. At or before `begin` it is exactly 0 and at or after `end` exactly 1,
/// which is what keeps `formed = 1` landing bit-for-bit on the shipped shape.
[[nodiscard]] float stageForm(float form, float begin, float end) noexcept;

/// The Klein bottle morph's three real construction stages (M18 revision, item 3): the
/// ring forms as a plain circular-cross-section tube, then the cross-section folds into
/// the figure-eight, then the cross-section twist settles the identifications. Exposed so
/// the staging can be tested directly against `form`, and so the surface function and any
/// future caller share one definition.
struct KleinMorph {
  float th;     ///< ring roll, 0 .. 2*pi
  float pinch;  ///< figure-eight fold, 0 .. 1
  float ph;     ///< rank ring roll, 0 .. 2*pi
  float tau;    ///< cross-section twist, 0 .. `twist`
};

/// The staged parameters `derivedSurfaceAt` passes to `kleinSurf` at `form`, where
/// `twist` is the cross-section twist the caller asked for (`SurfacePose::twist`). At
/// `form = 1` this is exactly `th = ph = 2*pi`, `pinch = 1`, `tau = twist` - the unstaged
/// call.
[[nodiscard]] KleinMorph kleinMorph(float form, float twist) noexcept;

/// The shape morphs' own stage boundaries - `view::kTorusRollEnd`/`kKleinRingEnd`/
/// `kKleinFoldEnd` - re-exported here because the renderer names them through this
/// header (and the app layer's attract sequence names them through `view::morph.hpp`
/// directly, which is where they live: below both layers, so neither duplicates the
/// other's copy).
using view::kKleinFoldEnd;
using view::kKleinRingEnd;
using view::kTorusRollEnd;

/// The Torus morph's two real construction stages: roll the files into an open tube
/// (`th` 0 -> 2*pi), then bend that tube into a closed ring (`ph` 0 -> 2*pi). Sequential,
/// not simultaneous, for the same reason the Klein morph is staged: at `form = 1` this is
/// exactly `th = ph = 2*pi`, the unstaged call, so a formed shape never moves - but an
/// *open* tube (`th = 2*pi, ph = 0`) is now a real, nameable waypoint at
/// `form = kTorusRollEnd`, which is what lets a cylinder cut seamlessly into a torus mid-
/// morph instead of jumping straight to the fully closed ring (M13.9).
struct TorusMorph {
  float th;  ///< file roll, 0 .. 2*pi
  float ph;  ///< ring close, 0 .. 2*pi
};
[[nodiscard]] TorusMorph torusMorph(float form) noexcept;

/// Where a lattice point lands on a derived overture's fully-formed surface, keyed by the
/// surface the geometry implies. Exposed so the differential test can assert it equals
/// the hand-authored surface for the same topology - the shipped scenes are the oracle
/// the generated ones are checked against.
[[nodiscard]] OvVec3 derivedSurfaceAt(app::SurfaceKind kind, float u, float v);

/// The same surface, posed. `SurfacePose{}` is the line above, to the bit.
[[nodiscard]] OvVec3 derivedSurfaceAt(app::SurfaceKind kind, float u, float v,
                                      SurfacePose pose);

/// Where a lattice point lands on an overture's fully-formed surface.
///
/// Exposed for one reason: seam closure is the property these warps exist to satisfy and
/// the only one that can be got wrong silently. `klein` joins its rank edges with the
/// file *reversed*, so (u, 0) must land on (1 - u, 1) - and a circular cross-section
/// cannot do that, it comes back shifted by four files instead. A test that cannot reach
/// the surface cannot tell those two apart, and the first version of this code shipped
/// the wrong one.
///
/// Overtures that form no closed surface return the flat board.
[[nodiscard]] OvVec3 overtureSurfaceAt(app::Overture which, float u, float v);

/// Whether a variant's play board has a surface to become (M17). True exactly when the
/// geometry names a non-flat 2-D surface; `standard`, mirrors and every non-glued variant
/// return false, and the interface must offer no toggle for them - a setting that did
/// nothing would be a lie.
[[nodiscard]] bool hasPlaySurface(const VariantSpec& v) noexcept;

/// Where a point given by fractional lattice coordinates lands on an authored D >= 3 play
/// shape - the continuous sibling of `playShapePosition`, and the one place the
/// `shellTube`/tesseract math is written. `axis[k]` is the coordinate along board axis
/// `k` measured in cells, so an integer cell's centre sits at `axis[k] + 0.5`.
/// `pose.slideU`/ `pose.slideV` shift the periodic axes before sampling (the same
/// cells-unit offset the 2-D pose uses) and `pose.formed` scales the shape's own roll,
/// exactly as the 2-D surfaces do. `playShapePosition` is this function at a whole cell;
/// `PlaySurface:: buildStacked` samples it at a sub-grid to cut each tile from the same
/// surface the cell centre comes from, rather than approximating it with a tangent quad.
/// False for anything without an authored shape.
[[nodiscard]] bool playShapeAt(const VariantSpec& v,
                               const std::array<float, kMaxDims>& axis, OvVec3& out,
                               SurfacePose pose = {});

/// A cell of a three- or four-dimensional variant placed on its hand-authored play shape
/// - the nested shells of `torus3d`, the tesseract of `hyper4` (M17.12) - in the same
/// Y-up world the two-dimensional surfaces use. False for anything without one.
///
/// Keyed by the variant's own identity, unlike the derived 2-D surfaces: a shape above
/// two dimensions is *authored*, not read off the gluing - `hyper4` is a plain 4-D box
/// with no identifications at all, and its shape is the projection a tesseract is always
/// drawn with. `PlaySurface` turns these positions into a tile per cell.
///
/// `pose.slideU`/`pose.slideV` are in lattice cells and shift the periodic axes before
/// the surface is sampled, exactly as the 2-D pose does - one cell along the files puts
/// a1 where b1 was. A shape with no periodic axis (`hyper4`) ignores them, honestly: it
/// has nowhere to slide (M17.22).
[[nodiscard]] bool playShapePosition(const VariantSpec& v, CellId cell, OvVec3& out,
                                     SurfacePose pose = {});

/// Draw one, sorted far to near, into the rectangle it has been given.
///
/// `zoom` scales it about the rectangle's centre and `alpha` fades every colour, so the
/// shell's screen change can push the object away and take it with it - the same
/// contract `drawDeco` keeps. At alpha 0 it emits nothing at all.
void drawOverture(ImDrawList* dl, const OvertureScene& scene, ImVec2 min, ImVec2 max,
                  const view::Theme& theme, IconStyle iconStyle, float zoom = 1.0f,
                  float alpha = 1.0f);

}  // namespace cb::render
