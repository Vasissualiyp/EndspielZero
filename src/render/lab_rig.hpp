// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <vector>

#include "render/lab_light.hpp"
#include "render/lab_scene.hpp"
#include "variant/variant.hpp"
#include "view/light_bridge.hpp"
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
  std::vector<view::SlabPathSpec> paths;  ///< per slab, with the deploy departures
  std::vector<std::uint32_t> slabGroup;   ///< group of each slab
  std::vector<int> bay;                   ///< parked bay slot of each slab, or -1
  std::vector<std::uint32_t> cellSlab;    ///< slab of each cell (by CellId)
  bool travels{true};  ///< every layout travels (M24.6 removed M24.5's static fallback)
  std::vector<int> groupSlot;   ///< departure slot of each group (view::departureOrder)
  int slots{1};                 ///< number of departure slots
  std::vector<view::Vec3> arm;  ///< each slab's own mast axis (x, y): a rack's arm
  float lastLock{0};
  view::Bounds room;  ///< the room's inner box
  view::Bounds layoutBounds;
  std::string bayPrefix;
  float bayWidth{0},
      bayDepth{0};  ///< a parked slab's face, board units (edge-on in its bay)
};
/// Null when the lab has no `[deploy]` table or the layout is empty. `xform` places the
/// lab explicitly (M24.6); null fits it to the layout (`view::placeLayout`).
[[nodiscard]] std::optional<RigLayout> rigLayout(const LabScene& lab,
                                                 const std::vector<view::Placement>& p,
                                                 const view::RigXform* xform = nullptr);
/// The rig after a layout change (M24.6): the lab stays where `previous` stood while the
/// new layout still fits there at the same scale, so only slabs that change move.
[[nodiscard]] std::optional<RigLayout> rigLayoutAfter(
    const LabScene& lab, const RigLayout& previous,
    const std::vector<view::Placement>& p);

/// Rest is the deployed game; a deploy at `age` seconds from the move start; a stow at
/// `age` seconds after its shutdown (departure slots leave last first).
enum class RigPhase : std::uint8_t { Rest, Deploy, Stow };
[[nodiscard]] std::vector<view::SlabPose> rigPoses(const RigLayout& layout,
                                                   RigPhase phase, float age,
                                                   float intensity);

/// One piece of rig hardware drawn, for tests and the `rig` console dump.
struct RigPiece {
  enum class Kind : std::uint8_t {
    Carriage,
    Bridge,
    MastOuter,
    MastInner,
    Clamp,
    Yoke,
    Back,
    TBar,   ///< a rack's horizontal bar from the mast to a slab's arm (M24.6)
    Cable,  ///< a 5D light-guide cable segment (M24.6)
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
/// picking and every board-space system stay where they are. M25.3: so does the shape
/// view's surface mesh (flat while it travels), vertex by vertex, its normals turned.
void carryBoard(InstanceSet& set, const RigLayout& layout,
                const std::vector<view::SlabPose>& poses);
/// The parked carriage and bridges are replaced by the per-group copies; a parked
/// slab is not in its bay while it is out.
[[nodiscard]] bool hiddenInGame(const io::LabPart& part, const RigLayout& layout,
                                const std::vector<view::SlabPose>& poses);
/// Which slabs draw the real board (clamped), by slab index.
[[nodiscard]] std::vector<bool> slabsShown(const RigLayout& layout,
                                           const std::vector<view::SlabPose>& poses);
/// M24.6: a layout change in flight. Every slab of the new layout either moves from the
/// old slab it was (`view::slabRelayout`, lifted, carried along the rails and turned, in
/// lab world while the lab eases from the old placement to the new over 1 s) or arrives
/// from the rack (`view::slabDeploy`); old slabs with no new one go back to the rack.
inline constexpr float kLabScaleEaseSeconds = 1.0f;
struct Relayout {
  RigLayout from, to;
  std::vector<view::SlabMatch> matches;   ///< `view::matchSlabs`: new slabs, then leaving
  std::vector<view::RelayoutSpec> specs;  ///< per new slab, lab world (matched ones)
  std::vector<int> arrivals;              ///< new slabs from the rack
  std::vector<int> leaving;               ///< old slabs back into it
  float seconds{0};  ///< until every slab is clamped and the lab placed
  /// M25.3: seconds before anything moves, while a formed shape unforms to flat
  /// (`view::kRigUnformSeconds`; 0 when the shape was flat). Set by the front end.
  float lead{0};
};
[[nodiscard]] Relayout relayoutOf(const LabScene& lab, const RigLayout& from,
                                  const std::vector<view::Placement>& fromPlacements,
                                  const RigLayout& to,
                                  const std::vector<view::Placement>& toPlacements);
/// One frame of a relayout `age` seconds in (calm: the end at once). Through the move's
/// `lead` every slab holds its starting pose, clamped (M25.3).
struct RelayoutFrame {
  /// The new rig in the eased lab, plus one extra single-slab group per leaving slab
  /// (indices from `to.slabs.size()` on), so `appendRig` draws them all.
  RigLayout layout;
  std::vector<view::SlabPose> poses;  ///< per `layout` slab
  /// Per new slab: the rigid carry of its board (translation + yaw about its new centre).
  struct Carry {
    bool moves{false};
    bool arriving{false};     ///< from the rack: rides its deploy path instead
    view::Vec3 from{}, to{};  ///< pivot (the new hang centre) and where it is now
    float yaw{0};             ///< radians, relative to the new pose
  };
  std::vector<Carry> carry;
  bool done{false};
};
[[nodiscard]] RelayoutFrame relayoutFrame(
    const LabScene& lab, const Relayout& move,
    const std::vector<view::Placement>& toPlacements, float age, float intensity);
/// The board rides its slabs through a relayout: a moving slab's instances follow its
/// translation and yaw; an arriving slab's ride its deploy path (`carryBoard`). Render
/// only, like `carryBoard`.
void carryRelayout(InstanceSet& set, const RelayoutFrame& frame);
/// The arriving slabs' boot age: 0 (dark) until the last of them locks, then the age of
/// their boot; empty once it is over or when nothing arrives.
[[nodiscard]] std::optional<float> relayoutBootAge(const Relayout& move, float age,
                                                   float intensity) noexcept;

/// M24.6 light bridges as beams of light: each bridge's curve as `kBridgeSegments`
/// hologram `cylinder` segments, 0.01 m across, its seam colour at alpha 0.55 x `power`
/// (the slab edges emit it, so it fades with the board), around a glow core 0.004 m
/// across. Board space. Returns the instances added.
inline constexpr int kBridgeSegments = 16;
inline constexpr float kBridgeBeamMetres = 0.01f;
inline constexpr float kBridgeCoreMetres = 0.004f;
inline constexpr float kBridgeAlpha = 0.55f;
std::size_t appendBridges(InstanceSet& set, const std::vector<view::LightBridge>& bridges,
                          float power);

/// M24.6: a 5D light-guide cable, from the mast of the board a timeline branched from to
/// the mast of the new timeline's first board, 0.15 m above the higher of the two slab
/// faces, a 12-piece catenary sagging 8 % of its span.
inline constexpr float kCableAboveMetres = 0.15f;
inline constexpr float kCableMetres = 0.012f;
inline constexpr float kCablePulseSpeed = 0.6f;  ///< m/s
inline constexpr int kCablePulses = 3;
struct RigCable {
  int slabA{-1}, slabB{-1};  ///< parent's and child's slab
  std::vector<view::Vec3> points;
};
[[nodiscard]] std::vector<RigCable> rigCables(
    const RigLayout& layout, const std::vector<view::SlabPose>& poses,
    const VariantSpec& v, const view::ViewConfig& cfg,
    const std::vector<view::TimelineLink>& links);
/// Glass `#9FE8FF`, with `kCablePulses` glow dots per cable running along it at 0.6 m/s
/// while `power` > 0. `seconds` is explicit presentation time.
std::size_t appendCables(InstanceSet& set, const std::vector<RigCable>& cables,
                         float seconds, float power);

/// M24.6: a stacked shape view cannot hang on slabs, so they rise to the gantry's top -
/// each group lifted rigidly until its top slab's mast is 0.15 m long - by `f` (0 at
/// their hang, 1 parked), and stand by.
inline constexpr float kParkMastMetres = 0.15f;
/// The stacked shape's tiles as light (`BoardOptions::stackedLight`).
inline constexpr float kStackedLightOpacity = 0.8f;
/// How far the slabs have risen for a stacked shape view at morph point `formed`: they
/// leave as the shape starts to form and are parked by a third of the way.
[[nodiscard]] float parkFraction(float formed) noexcept;
void parkHigh(const RigLayout& layout, std::vector<view::SlabPose>& poses, float f);
/// The standby light of each parked slab (amber, at its near-left corner).
std::size_t appendStandby(InstanceSet& set, const RigLayout& layout,
                          const std::vector<view::SlabPose>& poses);

/// M24.9: a glued 2-D board shown as its shape (`render::PlaySurface`) is drawn round the
/// surface's own centre, not the flat layout's, so the rig is built from the placements
/// moved in x and y onto the shape's centre (at `formed` 0; the shapes keep it as they
/// form). Anything else - a flat-only board, a stacked D >= 3 shape - is returned as is.
[[nodiscard]] std::vector<view::Placement> shapeRigPlacements(
    const VariantSpec& v, const std::vector<view::Placement>& p);
/// Where slab `slab`'s yoke meets its back: the carrier's centre at the yoke's height.
/// The skin's spokes start here (M24.9).
[[nodiscard]] view::Vec3 yokeBoss(const RigLayout& layout,
                                  const std::vector<view::SlabPose>& poses,
                                  std::size_t slab) noexcept;

/// M24.9: the lab game frames its board on more of the screen than the HUD's safe stage.
/// The lab's HUD is a few small panels at the edges, so the board may run behind their
/// corners; `width`/`height` are the safe stage's fractions (`applyStage`), and the
/// result is what the session frames on.
inline constexpr float kLabStageGrow = 1.35f;
[[nodiscard]] std::array<float, 2> labStageArea(float width, float height) noexcept;

/// A deploy's or stow's camera flight (M24.5), at `age` seconds into the move. `lock` is
/// the last slab's lock (deploy) or the stow's arrival; `shutdown` the stow's shutdown.
struct RigFlight {
  bool deploy{true};
  float age{0}, lock{0}, shutdown{0};
};
/// The flight from the lab's station camera `lab` to `game` (deploy) or back (stow), in
/// board space. The eye rides `view::blendCamera` with `rigEase`; the aim watches the
/// travelling slab - the first group's out of the rack, the last group's back in -
/// turning onto it and off it smoothly, so every endpoint is held exactly.
[[nodiscard]] view::OrbitCamera rigFlightCamera(const RigLayout& layout,
                                                const std::vector<view::SlabPose>& poses,
                                                const RigFlight& flight,
                                                const view::OrbitCamera& game,
                                                const view::OrbitCamera& lab);

/// M24.9: a flight keeps the eye at least `kFlightClearMetres` from every carried slab -
/// its body and the board riding it (`kCarriedBelowCells` under the hang centre to
/// `kCarriedAboveCells` over it, a piece's crown) - pushing it straight out of that
/// margin, and out underneath when it is over the slab, never up between the slab and
/// its rail. Clamped and parked slabs are not carried.
inline constexpr float kFlightClearMetres = 0.32f;
inline constexpr float kCarriedBelowCells = 0.4f;
inline constexpr float kCarriedAboveCells = 1.5f;
[[nodiscard]] view::Vec3 clearOfSlabs(const RigLayout& layout,
                                      const std::vector<view::SlabPose>& poses,
                                      view::Vec3 eye) noexcept;

/// The game camera backed off along its view ray until its eye is `kFlightClearMetres`
/// clear of every slab's lift column (each slab at its hang, swept up to its rail), no
/// further than the room allows. A deploy's flight lands there and settles onto `game`
/// over `kFlightSettleSeconds` once every slab is clamped; a stow backs off to it over
/// the shutdown first (M24.9).
inline constexpr float kFlightSettleSeconds = 0.6f;
inline constexpr float kLiftMarginMetres = 0.08f;
[[nodiscard]] view::OrbitCamera clearOfLifts(const RigLayout& layout,
                                             const view::OrbitCamera& game);

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
