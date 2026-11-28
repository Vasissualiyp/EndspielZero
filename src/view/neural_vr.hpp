// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <vector>

#include "variant/variant.hpp"
#include "view/layout.hpp"
#include "view/rig_deploy.hpp"

namespace cb::view {

/// M25.5 the neural VR. When a game needs more slabs than the rack physically holds, the
/// room cannot hold it: the implant takes over. Everything here is a pure function of the
/// layout, or of an age in seconds the caller passes in.

/// The rack's bays: the authored lab parks its stand-ins in `magazine.slab.0..7` (a
/// render test counts them in `lab.toml`), and M24.6 reuses them modulo 8.
inline constexpr int kRackBays = 8;

/// The physical slabs a layout deploys: one per flat level of a slice, as `slabsOf`.
[[nodiscard]] int deployedSlabCount(const std::vector<Placement>& placements);
/// True when the deployed slab count exceeds the rack's bays. A temporal game deploys
/// only its visible boards, so the front end asks with the session's placements: a 5D
/// game crosses into VR when it appends its ninth board.
[[nodiscard]] bool needsVr(const std::vector<Placement>& placements,
                           int rackBays = kRackBays);
/// M25.7 whether the variant's rules can create boards during a game: any temporal or
/// multiverse axis (time travel appends boards and timelines). Such a game starts in VR.
[[nodiscard]] bool canAddSlabs(const VariantSpec& variant);
/// The same for every board of the variant's layout under `cfg`, or a variant that can
/// add slabs (`canAddSlabs`), whatever the starting count.
[[nodiscard]] bool needsVr(const VariantSpec& variant, const ViewConfig& cfg,
                           int rackBays = kRackBays);

/// One frame of a handoff between the room and the implant's space.
struct VrState {
  /// The stand-in slab's deploy age (seconds from the move start), or negative while it
  /// hangs at rest.
  float slabAge{-1};
  bool room{false};     ///< the lab and the slab are drawn; else the implant's space
  float code{0};        ///< the neural code on the slab's LEDs, 0..1
  float handshake{-1};  ///< seconds into the implant handshake (M24.7), or -1
  float lidOpen{1};     ///< 0 closed .. 1 no lids (M24.7's eyelids)
  float dark{0};        ///< black over the whole picture, 0..1
  float horizon{1};     ///< how far the lucid horizon has opened, 0..1
  bool done{true};
};

/// Entering, from the start of the game's deploy: one slab deploys to the centre exactly
/// as a standard game's (one departure slot) and locks; its LEDs power on into the neural
/// code, which pulses; the implant's handshake answers it; the eyes close; darkness; the
/// lucid horizon opens.
inline constexpr float kVrSlabLock = kDeployFirstDeparture + kSlabLockSeconds;
inline constexpr float kVrCodeRise = 0.35f;  ///< the code powers on with a flicker
inline constexpr float kVrLidsCloseAt = kVrSlabLock + 1.5f;  ///< ~1.5 s of code
inline constexpr float kVrHandshakeAt = kVrSlabLock + 0.8f;
inline constexpr float kVrHandshakeSeconds = 1.2f;
inline constexpr float kVrLidsCloseSeconds = 0.5f;
inline constexpr float kVrDarkSeconds = 0.4f;
inline constexpr float kVrHorizonSeconds = 1.2f;
inline constexpr float kVrEnterSeconds =
    kVrLidsCloseAt + kVrLidsCloseSeconds + kVrDarkSeconds + kVrHorizonSeconds;
/// Calm: the slab hangs at once and the picture crosses through darkness.
inline constexpr float kVrEnterCalmSeconds = 0.6f;
[[nodiscard]] VrState vrEnter(float age, float intensity) noexcept;

/// Leaving, from the start of the game's stow: the implant's space fades to dark; the
/// eyes open on the room, with the slab hanging at the centre showing the neural code;
/// the implant activates again; the code fades and the board turns off. The slab then
/// goes back into the rack (the stow's travel, after this lead).
inline constexpr float kVrLeaveDark = 0.4f;
inline constexpr float kVrLidsOpenSeconds = 0.8f;
inline constexpr float kVrLeaveHandshakeAt = 0.8f;
inline constexpr float kVrCodeFadeAt = 1.6f;
inline constexpr float kVrCodeFadeSeconds = 0.8f;
inline constexpr float kVrLeaveSeconds = kVrCodeFadeAt + kVrCodeFadeSeconds;
inline constexpr float kVrLeaveCalmSeconds = 0.6f;
[[nodiscard]] VrState vrLeave(float age, float intensity) noexcept;

/// Choosing 2D (or 3D again) in a lab game: the eyes close briefly and open on the other
/// picture. From the room the slab first shows the neural code (`codeStep`); inside the
/// implant's space there is no code step. The picture switches at `vrBlinkSwitch`, behind
/// closed lids. Calm: a short dip through darkness.
inline constexpr float kVrBlinkCode = 0.6f;
inline constexpr float kVrBlinkClose = 0.25f;
inline constexpr float kVrBlinkDark = 0.1f;
inline constexpr float kVrBlinkOpen = 0.3f;
[[nodiscard]] float vrBlinkSwitch(bool codeStep, float intensity = 1) noexcept;
[[nodiscard]] float vrBlinkSeconds(bool codeStep, float intensity = 1) noexcept;
[[nodiscard]] VrState vrBlink(float age, bool codeStep, float intensity) noexcept;

/// A mode request changes its shown value once, behind closed eyelids. Input remains
/// locked until reopening ends. Callers apply the change only when advance returns true.
struct VrSwitch {
  bool shown{false};
  bool target{false};
  bool codeStep{false};
  double started{-1e9};
  [[nodiscard]] bool request(bool next, bool code, double now) noexcept;
  [[nodiscard]] bool advance(double now, float intensity) noexcept;
  [[nodiscard]] bool active(double now, float intensity) const noexcept;
  void skip(double now, float intensity) noexcept;
};

/// The neural code: an organic, QR-like pattern the implant reads, `n` x `n` modules.
/// Three QR finder squares stand in the corners; the rest is a slowly drifting
/// Turing-like labyrinth of blobs and stripes, structured but alive.
inline constexpr int kNeuralCodeModules = 40;
[[nodiscard]] bool neuralCodeModule(int i, int j, int n, float seconds,
                                    std::uint32_t seed = 0) noexcept;

/// A relayout in the implant's space moves every board's light from where it was to where
/// it goes: an eased glide that rises a little on the way. Calm: placed at once.
inline constexpr float kVrRelayoutSeconds = 0.9f;
inline constexpr float kVrRelayoutLift = 1.2f;  ///< cells, at the middle of the glide
[[nodiscard]] Vec3 vrRelayoutPoint(const Vec3& from, const Vec3& to, float age,
                                   float intensity) noexcept;

/// Interpolated placements keyed by cell, preserving the destination's slice identity.
/// Retarget from this result to avoid jumping to an unfinished move's destination.
[[nodiscard]] std::vector<Placement> vrRelayoutPlacements(
    const std::vector<Placement>& from, const std::vector<Placement>& to, float age,
    float intensity);

}  // namespace cb::view
