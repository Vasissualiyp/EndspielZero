// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <vector>

#include "render/lab_light.hpp"
#include "render/lab_scene.hpp"
#include "view/rig_deploy.hpp"

namespace cb::app {
class Session;
}

namespace cb::render {

/// The rig around a deployed game (M24.5): where the lab sits, the slabs, their masts
/// and their paths out of the magazine. Everything in board space.
inline constexpr float kRoomInsetMetres = 0.15f;
inline constexpr float kWallFadePadMetres = 0.4f;
inline constexpr float kFocusLevel = 0.15f;  ///< room lights while a game runs
inline constexpr float kFocusEaseSeconds = 1.0f;
inline constexpr int kMaxSlabGlowLights = 2;

struct RigLayout {
  view::RigXform xform;
  view::DeployVolume volume;
  std::vector<view::SlabSpec> slabs;
  std::vector<view::MastGroup> groups;
  std::vector<view::SlabPathSpec> paths;    ///< per slab, with the deploy departures
  std::vector<std::uint32_t> slabGroup;     ///< group of each slab
  std::vector<int> bay;                     ///< parked bay slot of each slab, or -1
  std::vector<std::uint32_t> cellSlab;      ///< slab of each cell (by CellId)
  bool travels{true};                       ///< at most four mast groups
  float lastLock{0};
  view::Bounds room;                        ///< the room's inner box
  view::Bounds layoutBounds;
  std::string bayPrefix;
  float bayWidth{0}, bayDepth{0};  ///< a parked slab's face, board units (edge-on in its bay)
};
/// Null when the lab has no `[deploy]` table or the layout is empty.
[[nodiscard]] std::optional<RigLayout> rigLayout(const LabScene& lab,
                                                 const std::vector<view::Placement>& p);

/// Rest is the deployed game; a deploy at `age` seconds from the move start; a stow at
/// `age` seconds after its shutdown (groups leave last first, 0.12 s apart).
enum class RigPhase : std::uint8_t { Rest, Deploy, Stow };
[[nodiscard]] std::vector<view::SlabPose> rigPoses(const RigLayout& layout, RigPhase phase,
                                                   float age, float intensity);

/// One piece of rig hardware drawn, for tests and the `rig` console dump.
struct RigPiece {
  enum class Kind : std::uint8_t {
    Carriage,
    Bridge,
    MastOuter,
    MastInner,
    Clamp,
    Yoke,
    Back
  } kind;
  int group{-1};
  int slab{-1};
  view::Bounds bounds;
};
/// Carriages, bridges, masts, clamps, yokes and slab backs, appended to the
/// lab batches as copies of their templates.
std::vector<RigPiece> appendRig(InstanceSet& set, const LabScene& lab,
                                const RigLayout& layout,
                                const std::vector<view::SlabPose>& poses);
/// The board rides its travelling slabs (M24.5): each board instance (cells, pieces,
/// shadows, flourishes, the standby LED) of a slab that is not clamped moves rigidly with
/// the slab's pose, shrunk to the bay's size while it is edge-on; parked in the bay it is
/// not drawn. Clamped slabs are untouched, so the lock swaps nothing. Render-only:
/// picking and every board-space system stay where they are.
void carryBoard(InstanceSet& set, const RigLayout& layout,
                const std::vector<view::SlabPose>& poses);
/// The parked carriage and bridges are replaced by the per-group copies; a parked
/// slab is not in its bay while it is out.
[[nodiscard]] bool hiddenInGame(const io::LabPart& part, const RigLayout& layout,
                                const std::vector<view::SlabPose>& poses);
/// Which slabs draw the real board (clamped), by slab index.
[[nodiscard]] std::vector<bool> slabsShown(const RigLayout& layout,
                                           const std::vector<view::SlabPose>& poses);
/// Room-light focus: 1 in the menus, kFocusLevel while a game runs.
[[nodiscard]] float deployFocus(float age, float lock, float total,
                                float intensity) noexcept;
[[nodiscard]] float stowFocus(float age, float shutdown, float arrive, float total,
                              float intensity) noexcept;
/// One point light per mast group (at most two; the rest merge into the nearest), 0.12 m
/// above the group's top slab, coloured by its cells' mean emission and scaled by power.
[[nodiscard]] std::vector<GpuLight> slabGlowLights(const RigLayout& layout,
                                                   const InstanceSet& board);
/// Clamp the session's stored orbit so the eye stays inside the room.
void constrainGameCamera(app::Session& session, const RigLayout& layout);
/// Walls, shelves, boxes and the bench fade when they stand between eye and target.
[[nodiscard]] float labPartFade(const io::LabPart& part, const view::Bounds& bounds,
                                const view::Vec3& eye, const view::Vec3& target,
                                const RigLayout& layout) noexcept;
/// The lab's station camera seen from board space.
[[nodiscard]] view::OrbitCamera toBoardCamera(const view::OrbitCamera& station,
                                              const view::RigXform& x) noexcept;
/// Move lab-world instances (projector geometry) into board space.
void placeInstances(std::vector<Instance>& list, const view::RigXform& x) noexcept;

}  // namespace cb::render
