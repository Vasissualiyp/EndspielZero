// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "variant/variant.hpp"
#include "view/camera.hpp"
#include "view/layout.hpp"
#include "view/theme.hpp"

namespace cb::view {

/// The bezel a slab's face carries beyond its outer cells' half pitch: the renderer's
/// `render::kBezelMargin` (a render test pins the two together).
inline constexpr float kSlabBezelMargin = 0.28f;
/// Two slabs whose facing edges are further apart than this (cells) cannot touch.
inline constexpr float kBridgeMinGap = 1.5f;

/// A light bridge (M24.6): two slabs the variant glues along an axis that is not one of
/// the two drawn screen axes (a wrapping level stack, a periodic grid axis), which
/// cannot touch in 3-D, joined by a beam of light in that seam's axis colour.
struct LightBridge {
  std::uint32_t sliceA{0}, sliceB{0};  ///< the layout slices of the two slabs
  std::uint32_t slabA{0}, slabB{0};    ///< their slabs (one per slice and face height)
  std::uint8_t axis{0};                ///< the glued axis
  Rgba color{};
  Vec3 a{}, b{};   ///< board space: the two edge midpoints, A's on the side it leaves by
  Vec3 control{};  ///< the quadratic Bezier's control point
};

/// Every light bridge of a layout, one per glued pair of slabs, sorted by (slabA, slabB,
/// axis). A pair glued along a level axis (a stacked board) is joined on the slabs' near
/// (-Y) edges and arches out towards the player (-Y) by a quarter of its span (the mast
/// stands on the far side); any other pair is joined on
/// the facing edges of the gluing's own direction and arches upward (+Z) by a quarter of
/// its span, so it never cuts through a slab between them.
[[nodiscard]] std::vector<LightBridge> lightBridges(const VariantSpec& v,
                                                    const ViewConfig& cfg,
                                                    const std::vector<Placement>& p,
                                                    const Theme& theme);

/// The bridge's curve at `u` in [0, 1]: `a` at 0, `b` at 1.
[[nodiscard]] Vec3 bridgePoint(const LightBridge& b, float u) noexcept;

/// A move across a light bridge (M24.6): the piece runs from its start cell to `a` over
/// the first 20 % of the move, along the curve over the next 60 % and from `b` to its
/// end cell over the last 20 %.
inline constexpr float kBridgeEnter = 0.2f;
inline constexpr float kBridgeLeave = 0.8f;
struct BridgeRide {
  Vec3 start{}, a{}, control{}, b{}, end{};
};
[[nodiscard]] Vec3 bridgeRide(const BridgeRide& r, float progress) noexcept;

}  // namespace cb::view
