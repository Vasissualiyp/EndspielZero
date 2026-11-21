// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <vector>

#include "render/board_renderer.hpp"
#include "render/lab_scene.hpp"
#include "render/play_surface.hpp"
#include "variant/variant.hpp"
#include "view/camera.hpp"

namespace cb::render {

/// The slab skin's hardware (M24.5b): the ribs that hold the skin into its shape, the
/// seam latches that click shut as an axis closes, and the band a Klein bottle cannot
/// form physically. All pure in the shape and in explicit time; see
/// `docs/plan/M24.5b-slab-skin.md`.

/// The rib's offset behind the skin, and its spacing, in cells (one cell = one board
/// unit). The diameter is 0.012 m.
inline constexpr float kRibBehindCells = 0.6f;
inline constexpr int kRibCount = 6;
inline constexpr float kRibDiameter = 0.012f * kCellsPerMetre;
/// How near the skin a sight line may pass and still count as hidden by it (cells).
inline constexpr float kRibEyeEps = 0.05f;
/// Two corners nearer than this (cells) that the lattice keeps more than two cells apart
/// mark a pass-through cell.
inline constexpr float kPassThroughGap = 0.35f;
/// The membrane sits this far behind its tile, in cells, along the patch normals.
inline constexpr float kMembraneOffset = 0.025f;

struct Rib {
  std::vector<view::Vec3> points;  ///< a polyline, board space
};

/// Ribs behind the skin for the current shape: `count` lattice lines across each rolled
/// axis (evenly spaced, snapped to the nearest cell-centre line), one point per cell at
/// the cell centres, offset 0.6 cells behind the skin (against the surface normal), plus
/// two spokes from `yoke` to the nearest rib point of the two ribs closest to it. Empty
/// when `formed` is 0. Each rib shows from its middle outwards over
/// smoothstep(0, 0.35, formed) of its length. Spokes come last. A rib point whose nearest
/// seat is in `skip` (the Klein light band, sorted) is left out, splitting its rib.
///
/// M24.9: with `eye`, a rib point (or the chord to the one before it) that the skin does
/// not hide from `eye` is left out too - on a Klein bottle the playing side faces into
/// the bottle, so "behind the skin" is outside it, and a rib piece there would stand in
/// the open. Such a piece vanishes exactly as it would come into view, so nothing pops.
[[nodiscard]] std::vector<Rib> slabRibs(const PlaySurface& shaped, int count,
                                        const view::Vec3& yoke, float formed,
                                        const std::vector<CellId>* skip = nullptr,
                                        const view::Vec3* eye = nullptr);

/// M24.9: on the closed torus the ribs are inside the tube, so the structure shows as
/// thin hoops round the tube on the outside instead: `count` loops along the files at
/// rank boundaries (between two rows of tiles, evenly spaced), two points per cell, clear
/// of the tiles' physical outside by `kHoopLiftCells`, regardless of playing-side
/// inversion. They grow from their middle once the files have rolled shut
/// (`view::kTorusRollEnd`), over the next 0.2 of the morph. Empty for any other shape.
inline constexpr float kHoopDiameter = 0.006f * kCellsPerMetre;
inline constexpr float kHoopLiftCells =
    PlaySurface::kThickness * 0.5f + kHoopDiameter * 0.5f + 0.02f;
[[nodiscard]] std::vector<Rib> outsideHoops(const PlaySurface& shaped, int count,
                                            float formed);

/// The flexible skin (M25.4). In shape view the squares themselves bend into the shape
/// and the slab, its ribs and the membrane are not drawn; the flat board stays the
/// M24.5b slab. `flexBlend` is how far the tiles have lifted off the slab into the skin:
/// 0 at `formed` 0 (the slab), 1 from `kFlexFormEnd` on, smoothstep between. It sets
/// `SurfacePose::flex`, and the slab body dims away over the same window
/// (`slabBackFade`, through the lab's screen door).
inline constexpr float kFlexFormEnd = 0.15f;
[[nodiscard]] float flexBlend(float formed) noexcept;
[[nodiscard]] float slabBackFade(float formed) noexcept;

/// The one point at which the yoke holds the flexible skin (M25.4): a short round stem
/// from `yoke` (the yoke's boss) to the back face of the nearest square that faces away
/// from it, ending in a foot along that square's normal. Three points, yoke first;
/// empty when the shape has no seats.
inline constexpr float kHoldDiameter = 0.02f * kCellsPerMetre;
[[nodiscard]] Rib skinHold(const PlaySurface& shaped, const view::Vec3& yoke);

/// The `formed` value at which each lattice axis (0 files, 1 ranks) closes, read off the
/// same stage table the seam rails use; NaN for an axis that never closes.
[[nodiscard]] std::array<float, 2> wrapCloseAt(const VariantSpec& v);
[[nodiscard]] std::array<float, 2> wrapCloseAt(app::SurfaceKind kind);

/// Did `formed` cross `closeAt` upward between two frames? Opening never latches.
[[nodiscard]] bool latchCrossed(float before, float after, float closeAt) noexcept;

/// The rail boost `age` seconds after a latch: x2.5 to 0.06 s, x1.6 to 0.14 s, x1.2 to
/// 0.25 s, then x1, in hard steps. Calm intensity (< 0.5) never flashes.
[[nodiscard]] float latchBoost(float age, float intensity) noexcept;

/// Cells whose skin would have to pass through another part of the skin: any patch
/// corner closer than `gap` (cells) to a corner of a patch at lattice distance > 2
/// (with wraps). Pure in the shape; sorted by CellId.
[[nodiscard]] std::vector<CellId> passThroughCells(const PlaySurface& shaped, float gap);

/// The same test on a given patch list, for `PlaySurface::build` (its unshrunk sheet).
[[nodiscard]] std::vector<CellId> passThroughOf(const std::vector<SurfacePatch>& patches,
                                                const PlaySurface& lattice, float gap);

/// Draw the ribs as a chain of thin lab cylinders (metal, #2A2E35) in the lab batch.
/// Returns the number of cylinder instances added.
std::size_t appendRibs(InstanceSet& set, const std::vector<Rib>& ribs,
                       float diameter = kRibDiameter);
/// A lab `cylinder` instance from `a` to `b` (its axis is local +Z), `diameter` across,
/// a touch longer than the segment so consecutive ones meet without a crack. Colour,
/// material and signals are the caller's.
[[nodiscard]] Instance cylinderBetween(const view::Vec3& a, const view::Vec3& b,
                                       float diameter);
/// Insert instances into the lab cylinder batch, keeping every lab batch contiguous.
std::size_t insertLabCylinders(InstanceSet& set, const std::vector<Instance>& bars);

}  // namespace cb::render
