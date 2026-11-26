// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
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
std::size_t appendVrBridges(InstanceSet& set,
                            const std::vector<view::LightBridge>& bridges, float power);

}  // namespace cb::render
