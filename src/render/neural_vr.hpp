// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <optional>
#include <vector>

#include "io/lab_doc.hpp"
#include "render/board_renderer.hpp"
#include "render/lab_rig.hpp"
#include "view/layout.hpp"
#include "view/theme.hpp"

namespace cb::render {

/// M25.5 the neural VR, render side. The switch and the handoff's timing are pure view
/// functions (`view/neural_vr.hpp`); this draws them.

/// The rack's bays as authored: the parts named `<deploy.slab_bay>.<digit>`.
[[nodiscard]] int rackBayCount(const io::LabDoc& doc);

/// The stand-in slab a VR game deploys and stows in the room: a standard 8 x 8 board's
/// layout, one slab, exactly as a standard game deploys.
[[nodiscard]] const std::vector<view::Placement>& vrStandIn();

/// Front-on implant picture, leaving a small margin for cell labels. The session owns
/// this camera so rendering and picking use the same projection.
[[nodiscard]] view::OrbitCamera vrPictureCamera(const view::Bounds& bounds, float aspect);

/// The neural code (`view::neuralCodeModule`) on the face of every clamped slab, as LED
/// modules: small glow boxes in the theme's LED colour scaled by `strength` (0..1), a
/// hair above the cell tops. Nothing at strength 0 or on a slab that is not clamped.
/// Returns the modules added (lab box batch).
std::size_t appendNeuralCode(InstanceSet& set, const RigLayout& layout,
                             const std::vector<view::SlabPose>& poses, float strength,
                             float seconds, const view::Theme& theme);

/// A relayout in the implant's space: every board instance moves by its cell's offset
/// (`offset` by CellId; a cell from `Instance::fxCell`). Instances with no cell of their
/// own (shadows, flourishes) follow the nearest cell of `placements` in x and y.
void carryVr(InstanceSet& set, const std::vector<view::Placement>& placements,
             const std::vector<view::Vec3>& offset);

/// The same bridge curves as the physical rig, sampled into thin emissive streaks.
/// One box per segment avoids tessellating tubes for subpixel light in large layouts.
/// M25.6: in the implant's space the streaks are faint guides, not the subject - half
/// the physical glow core across, their seam colour normalised to `kVrBridgeShare` of the
/// dimmest light tile (`vrLightColor(theme.boardDark)`), and, given the eye, fading to
/// `kVrBridgeFar` of that from the nearest streak to the farthest. Every bridge and
/// segment is still drawn.
inline constexpr float kVrBridgeWidth = 0.5f;
inline constexpr float kVrBridgeShare = 0.4f;
inline constexpr float kVrBridgeFar = 0.3f;
std::size_t appendVrBridges(InstanceSet& set,
                            const std::vector<view::LightBridge>& bridges, float power,
                            const view::Theme& theme,
                            const std::optional<view::Vec3>& eye = std::nullopt);

/// Whether this frame's boards follow the lattice layout, so a VR relayout glides them
/// and its bridges join them (M25.6). A drawn shape is placed by its own sampling, never
/// by the layout: carrying its mesh by lattice offsets tears it apart mid-glide, and the
/// lattice's bridges would join cells that are not where the shape draws them (the
/// physical rig parks its slabs and draws no bridges there either).
[[nodiscard]] bool vrCarriesLayout(const BoardOptions& options, const VariantSpec& v);

}  // namespace cb::render
