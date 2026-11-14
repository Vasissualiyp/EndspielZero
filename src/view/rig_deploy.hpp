// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "view/camera.hpp"

namespace cb::view {

/// Board cell centre to the underside of its slab: the cell's half height plus the
/// plinth's full depth (`BoardRenderer::cellHalfExtent().z` + 2 x 0.13; a render test
/// pins the two together).
inline constexpr float kSlabBelowFace = 0.315f;

/// The `[deploy]` table of lab.toml (M24.5), in lab-world cells (`LabScene::toWorld`
/// for points; metres x 20 for the gap and drop).
struct DeployVolume {
  Vec3 min{}, max{};
  float railZ{0};        ///< carriage underside
  float mastEdgeGap{0};  ///< slab's far bezel edge to the mast axis
  float yokeDrop{0};     ///< the yoke's centre below the slab's back face
};
using DeployInfo = DeployVolume;

/// Lab world (cells, LabScene::toWorld) -> board space, during a game (M24.5).
struct RigXform {
  Vec3 labOrigin{};   ///< board-space position of the lab-world origin (gantry.centre)
  float labScale{1};  ///< board units per lab unit; >= 1
  [[nodiscard]] Vec3 toBoard(const Vec3& labWorld) const noexcept;
  [[nodiscard]] Vec3 toLab(const Vec3& board) const noexcept;
};

/// Place a layout: its bounds' centre (x, y) and its bounds' **bottom** z sit at the
/// deploy anchor; the lab scales up only if the bounds (plus a 0.08 m margin per side,
/// in cells) exceed the deployment volume.
[[nodiscard]] RigXform placeLayout(const Bounds& layout,
                                   const DeployVolume& volume) noexcept;

struct SlabSpec {  ///< one flat level of a slice: a physical slab, in board space
  std::uint32_t slice{0};
  float minX{0}, maxX{0}, minY{0}, maxY{0};  ///< bezel extents (cells + bezel margin)
  float z{0};                                ///< the slice's face height
};
struct MastGroup {
  Vec3 mastFoot{};   ///< board space: lowest yoke attachment on the mast axis
  float mastTopZ{0};  ///< board space: the carriage underside (rail_z mapped)
  std::vector<std::uint32_t> slabs;  ///< indices into the SlabSpec list, top to bottom
};
/// One slab per (slice, face height), ordered by slice then height: a 3-D board's
/// levels are one layout slice but separate slabs.
[[nodiscard]] std::vector<SlabSpec> slabsOf(const std::vector<Placement>& placements,
                                            float bezelMargin);
/// The mast stands `mastEdgeGap` beyond each slab's far (+Y) bezel edge; slabs whose
/// axes lie within half a cell share one mast, in order of their first slice.
[[nodiscard]] std::vector<MastGroup> mastGroups(const std::vector<SlabSpec>& slabs,
                                                const RigXform& x, const DeployInfo& d);

/// Phase ends of the slab path, in seconds after the slab's departure.
inline constexpr float kSlabSlideEnd = 0.30f;
inline constexpr float kSlabTiltEnd = 0.55f;
inline constexpr float kSlabTravelEnd = 1.05f;
inline constexpr float kSlabLowerEnd = 1.30f;
inline constexpr float kSlabLockSeconds = 1.42f;  ///< the lock: the path's full length
inline constexpr float kSlabOvershoot = 0.05f;    ///< 2.5 mm, in cells
inline constexpr float kClampPulseRise = 0.03f;
inline constexpr float kClampPulseEnd = 0.25f;
/// Departures overlap: group i leaves at kDeployFirstDeparture + kDeployStagger * i.
inline constexpr float kDeployFirstDeparture = 0.2f;
inline constexpr float kDeployStagger = 0.12f;
inline constexpr int kMaxTravellingGroups = 4;

struct SlabPathSpec {
  Vec3 bayCentre{};               ///< board space: the parked stand-in's centre
  std::array<float, 4> bayQuat{};  ///< its orientation (edge-on, rot [90,90,0])
  Vec3 pickCentre{};  ///< where the carriage takes it: bay slid out to x = rail limit
  Vec3 hangCentre{};  ///< the deployed slab centre at its face height
  float departure{0};  ///< seconds after the move starts that this slab leaves
  Vec3 mastOffset{};   ///< mast axis minus slab centre (x, y), board units
  float yokeBelow{0};  ///< slab centre z minus its yoke z
  float railZ{0};      ///< board space: the carriage underside
  float liftZ{0};      ///< slab centre z while it is carried just under the rail
  Vec3 parkCarriage{};  ///< the parked carriage (x, y) the move starts from
};
struct SlabPose {
  Vec3 centre{};
  std::array<float, 4> quat{};  ///< slab orientation
  Vec3 carriage{};              ///< carriage underside centre (x, y; z = rail)
  float mastLength{0};          ///< from the carriage to the yoke
  float clampPulse{0};          ///< 0..1, the short glow when it locks
  bool clamped{false};          ///< true from the lock on (deploy)
  bool inBay{true};
};
/// Deploy pose at `t` seconds after the move start. Stow is the same path reversed
/// in time with its own departure order.
[[nodiscard]] SlabPose slabDeploy(const SlabPathSpec& s, float t, float intensity) noexcept;

/// The lock time of the last of `groups` mast groups (deploy), or the boot gate a
/// static layout keeps (`kStaticLockSeconds`) when there are more than four.
inline constexpr float kStaticLockSeconds = 1.4f;
[[nodiscard]] float deployLastLock(int groups) noexcept;
/// Total seconds of the stow's slab travel for `groups` groups (no shutdown).
[[nodiscard]] float stowTravelSeconds(int groups) noexcept;

/// Largest orbit distance that keeps the eye inside the room box shrunk by `inset`
/// (all in board space). Returns `wanted` when it already fits.
[[nodiscard]] float clampOrbitDistance(const OrbitCamera& c, float wanted,
                                       const Bounds& roomInBoard, float inset) noexcept;
/// Screen-door fade for a room part whose bounds intersect the segment eye -> target
/// (both board space), grown by `pad`: 1 = fully drawn, down to 0.15 when blocking.
inline constexpr float kWallFadeFloor = 0.15f;
[[nodiscard]] float wallFade(const Bounds& part, const Vec3& eye, const Vec3& target,
                             float pad) noexcept;

}  // namespace cb::view
