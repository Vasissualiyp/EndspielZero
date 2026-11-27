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

/// Whether `layout` (board space), with the 0.08 m margin per side, lies inside the
/// deployment volume when the lab stands at `x` (M24.6: a layout change keeps the lab
/// where it is while this holds).
[[nodiscard]] bool layoutFits(const Bounds& layout, const RigXform& x,
                              const DeployVolume& volume) noexcept;

struct SlabSpec {  ///< one flat level of a slice: a physical slab, in board space
  std::uint32_t slice{0};
  float minX{0}, maxX{0}, minY{0}, maxY{0};  ///< bezel extents (cells + bezel margin)
  float z{0};                                ///< the slice's face height
};
struct MastGroup {
  Vec3 mastFoot{};    ///< board space: lowest yoke attachment on the mast axis
  float mastTopZ{0};  ///< board space: the carriage underside (rail_z mapped)
  std::vector<std::uint32_t> slabs;  ///< indices into the SlabSpec list, top to bottom
  /// M24.6: a rack mast. The feet of the masts merged into it (board space); empty for a
  /// plain mast. Horizontal T-bars join them at every yoke height, and each slab's yoke
  /// hangs from its own arm rather than from the mast.
  std::vector<Vec3> arms;
  [[nodiscard]] bool rack() const noexcept { return !arms.empty(); }
};
/// One slab per (slice, face height), ordered by slice then height: a 3-D board's
/// levels are one layout slice but separate slabs.
[[nodiscard]] std::vector<SlabSpec> slabsOf(const std::vector<Placement>& placements,
                                            float bezelMargin);
/// The mast stands `mastEdgeGap` beyond each slab's far (+Y) bezel edge; slabs whose
/// axes lie within half a cell share one mast, in order of their first slice.
[[nodiscard]] std::vector<MastGroup> mastGroups(const std::vector<SlabSpec>& slabs,
                                                const RigXform& x, const DeployInfo& d);

/// Phase ends of the slab path, in seconds after the slab's departure (M25.3: a heavy
/// slab). The carriage arrives over the pick-up and takes up the load; the slab hesitates
/// while it breaks free of the bay, dropping kSlabDrop as the latch lets go; it slides
/// out, tilts face-up as it rises under the rail, travels, lowers and settles. Every
/// displacement is C2, with overlapping lift, travel and lowering (M25.3b).
inline constexpr float kSlabTakeUpEnd = 0.40f;
inline constexpr float kSlabBreakFreeEnd = 0.60f;
inline constexpr float kSlabLiftStart = 0.70f;
inline constexpr float kSlabLiftEnd = 5.20f;
inline constexpr float kSlabTravelStart = 2.90f;
inline constexpr float kSlabLowerStart = 3.10f;
inline constexpr float kSlabSlideEnd = 3.90f;
inline constexpr float kSlabTiltEnd = 4.60f;
inline constexpr float kSlabTravelEnd = 7.00f;
inline constexpr float kSlabLowerEnd = 7.05f;
inline constexpr float kSlabLockSeconds = 7.30f;  ///< the lock: the path's full length
/// The settle: one dip below the hang and back, from 0.1 s before the lowering ends to
/// the lock. 2 mm, in cells.
inline constexpr float kSlabOvershoot = 0.04f;
inline constexpr float kSlabDrop = 0.14f;  ///< 7 mm as the bay's latch lets go, in cells
/// Inertia: the slab rocks on its yoke against the carriage's acceleration, at most this
/// many degrees (a 1 m travel's peak braking, before the damping), critically damped.
inline constexpr float kSlabSwingDegrees = 2.5f;
inline constexpr float kSlabSwingOmega = 20.0f;  ///< rad/s: a stiff hanger
inline constexpr float kClampPulseRise = 0.03f;
inline constexpr float kClampPulseEnd = 0.25f;
/// Departures overlap: slot i leaves at kDeployFirstDeparture + departureStep(n) * i.
inline constexpr float kDeployFirstDeparture = 0.2f;
inline constexpr float kDeployStagger = 0.12f;
/// The last departure is at most this long after the first (M24.6).
inline constexpr float kDeployDepartureSpan = 0.5f;

/// M24.6 routing, in metres of the lab (board distances divide by 20 x labScale).
/// Carriages on different bridges stand this far apart in x; ...
inline constexpr float kColumnSpacingMetres = 0.34f;
/// ... carriages sharing one bridge (one x) this far apart in y.
inline constexpr float kBridgeSpacingMetres = 0.32f;
/// True when every pair of masts is either on one bridge (the same x) at least
/// kBridgeSpacingMetres apart in y, or on two bridges at least kColumnSpacingMetres
/// apart in x, and no two racks' arm spans overlap.
[[nodiscard]] bool spacingHolds(const std::vector<MastGroup>& groups,
                                float labScale) noexcept;
/// Merge the closest offending pair of masts into one rack mast, again and again, until
/// `spacingHolds`. A rack's mast stands at the middle of its arms' x span, on the
/// farthest (+Y) arm line; its foot is the lowest arm. Groups come back ordered by
/// their first slab. `labScale` converts the metre rules to board units (an added
/// parameter: the volume alone does not know it).
[[nodiscard]] std::vector<MastGroup> routeGroups(std::vector<MastGroup> groups,
                                                 const DeployInfo& d, float labScale = 1);

/// Which departure slot each group takes: columns (masts on one bridge, the same x)
/// leave together, nearest the magazine first.
struct Departures {
  std::vector<int> slot;  ///< per group
  int count{0};           ///< number of slots
};
[[nodiscard]] Departures departureOrder(const std::vector<MastGroup>& groups,
                                        const Vec3& magazine);
/// The stagger between departure slots: min(0.12, 0.5 / max(1, n - 1)).
[[nodiscard]] float departureStep(int slots) noexcept;

struct SlabPathSpec {
  Vec3 bayCentre{};                ///< board space: the parked stand-in's centre
  std::array<float, 4> bayQuat{};  ///< its orientation (edge-on, rot [90,90,0])
  Vec3 pickCentre{};    ///< where the carriage takes it: bay slid out to x = rail limit
  Vec3 hangCentre{};    ///< the deployed slab centre at its face height
  float departure{0};   ///< seconds after the move starts that this slab leaves
  Vec3 mastOffset{};    ///< mast axis minus slab centre (x, y), board units
  float yokeBelow{0};   ///< slab centre z minus its yoke z
  float railZ{0};       ///< board space: the carriage underside
  float liftZ{0};       ///< slab centre z while it is carried just under the rail
  Vec3 parkCarriage{};  ///< the parked carriage (x, y) the move starts from
  /// M25.3: the path is played backwards in time (a stow), so the slab's inertia answers
  /// the reversed motion: its swing is causal in the stow's own time.
  bool reversed{false};
  float labScale{1};  ///< board units per lab unit, for physical acceleration/swing
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
[[nodiscard]] SlabPose slabDeploy(const SlabPathSpec& s, float t,
                                  float intensity) noexcept;

/// The lock time of the last of `slots` departure slots (deploy). Every layout travels
/// (M24.6 removed M24.5's static fallback).
[[nodiscard]] float deployLastLock(int slots) noexcept;
/// Total seconds of the stow's slab travel for `slots` departure slots (no shutdown).
[[nodiscard]] float stowTravelSeconds(int slots) noexcept;

/// M25.3: a slab leaves and enters the rack only as a flat screen. These gates are the
/// share of the shape's morph (`geometryFormed`) a rig move lets show: 0 whenever any
/// slab is neither clamped at its hang nor latched in its bay. The shape forms over
/// kRigFormSeconds once the last slab has locked, and unforms over kRigUnformSeconds
/// before the first one leaves.
inline constexpr float kRigFormSeconds = 0.8f;
inline constexpr float kRigUnformSeconds = 0.8f;
/// Deploy, `age` seconds from the move start; `lastLock` is the last slab's lock. Calm
/// hangs the slabs at once: 1.
[[nodiscard]] float deployFormGate(float age, float lastLock, float intensity) noexcept;
/// Stow, `age` seconds from the move start: unformed by kRigUnformSeconds. Calm puts the
/// slabs straight back in their bays: 0.
[[nodiscard]] float stowFormGate(float age, float intensity) noexcept;
/// How long a stow waits before its slabs leave: the shutdown, and at least the unform
/// when the shape is formed.
[[nodiscard]] float stowLead(float shutdown, bool formed) noexcept;
/// A relayout, `age` seconds from its start: unform over `lead` (0 when the shape was
/// flat), flat while the slabs move for `seconds`, then form again. Calm: 1.
[[nodiscard]] float relayoutFormGate(float age, float lead, float seconds,
                                     float intensity) noexcept;

/// M24.6: a layout change moves the hardware. One slab from its old pose to its new one,
/// in lab-world cells: lift, travel along the rails (turning to the new yaw on the way),
/// lower and settle with the deploy's overshoot.
inline constexpr float kRelayoutLiftSeconds = 1.0f;
inline constexpr float kRelayoutLowerSeconds = 1.0f;
inline constexpr float kRelayoutSettleSeconds = 0.3f;
struct RelayoutSpec {
  Vec3 from{}, to{};           ///< slab centre
  float fromYaw{0}, toYaw{0};  ///< radians about +Z
  float lift{0.08f * 20};      ///< 0.08 m
};
struct RelayoutPose {
  Vec3 centre{};
  float yaw{0};
  bool settled{false};  ///< the move is over (or never moves it): clamped at `to`
};
/// Distance-limited rail travel in lab cells; longer paths take longer, without a cap.
[[nodiscard]] float relayoutTravelSeconds(float distanceCells) noexcept;
[[nodiscard]] float relayoutSeconds(const RelayoutSpec& s) noexcept;
/// The pose `t` seconds into the move; calm (intensity < 0.5) is the final pose. A slab
/// whose pose does not change does not move at all.
[[nodiscard]] RelayoutPose slabRelayout(const RelayoutSpec& s, float t,
                                        float intensity) noexcept;
/// Which old slab each new slab was: the one sharing the most cells (each old slab used
/// once), with the quarter-turn-or-any yaw that best maps its cells' offsets onto the
/// new ones. New slabs with no old one have `from = -1`; old slabs with no new one come
/// last with `to = -1`.
struct SlabMatch {
  int from{-1};
  int to{-1};
  float yaw{0};  ///< radians: new offsets = R(yaw) old offsets
};
[[nodiscard]] std::vector<SlabMatch> matchSlabs(
    const std::vector<Placement>& oldPlacements, const std::vector<SlabSpec>& oldSlabs,
    const std::vector<Placement>& newPlacements, const std::vector<SlabSpec>& newSlabs);
/// The slab (index into `slabs`) of every placement, by slice and face height.
[[nodiscard]] std::vector<std::uint32_t> slabOfPlacements(
    const std::vector<Placement>& placements, const std::vector<SlabSpec>& slabs);

/// M24.6: a light-guide cable between two masts, a catenary of `segments` straight
/// pieces whose middle hangs `sag` x the span below the chord.
[[nodiscard]] std::vector<Vec3> cablePoints(const Vec3& a, const Vec3& b,
                                            int segments = 12, float sag = 0.08f);

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
