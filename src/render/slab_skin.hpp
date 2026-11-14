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
[[nodiscard]] std::vector<Rib> slabRibs(const PlaySurface& shaped, int count,
                                        const view::Vec3& yoke, float formed,
                                        const std::vector<CellId>* skip = nullptr);

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
std::size_t appendRibs(InstanceSet& set, const std::vector<Rib>& ribs);

}  // namespace cb::render
