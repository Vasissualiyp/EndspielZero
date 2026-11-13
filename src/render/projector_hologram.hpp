// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "render/board_renderer.hpp"
#include "render/overture_scene.hpp"
namespace cb::render {
/// The projector's world geometry, batched for the existing board pipeline.
struct Hologram {
  std::vector<Instance> quads;
  std::vector<Instance> tokens;
  std::array<InstanceSet::Batch, kShapeSlots> tokenBatches{};
  std::vector<Instance> lines;
  Instance beam{};
  bool any() const noexcept {
    return !quads.empty() || !tokens.empty() || !lines.empty();
  }
};
struct HologramPlacement {
  view::Vec3 centre{};
  float radius{1};
  float yaw{0};
  float alpha{1};
  float scale{1};
  float squashZ{1};  ///< Historical name: squash along lens-to-centre, about the lens.
  view::Vec3 lens{};
};
/// Columns of a right-handed orthonormal frame; quaternion order is x,y,z,w.
[[nodiscard]] std::array<float, 4> quatFromBasis(view::Vec3 t, view::Vec3 b,
                                                 view::Vec3 n);
[[nodiscard]] Hologram overtureHologram(const OvertureScene&, const view::Theme&,
                                        const HologramPlacement&);
[[nodiscard]] Hologram manifoldHologram(float seconds, const view::Theme&,
                                        const HologramPlacement&);
[[nodiscard]] Hologram tesseractHologram(float seconds, const view::Theme&,
                                         const HologramPlacement&);
[[nodiscard]] Instance projectorBeam(view::Vec3 lens, const HologramPlacement&,
                                     const view::Theme&);
}  // namespace cb::render
