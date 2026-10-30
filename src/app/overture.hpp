// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace cb {
struct VariantSpec;
}  // namespace cb

namespace cb::app {

/// What a variant's geometry looks like to an overture - the derived scene's key.
///
/// A small, finite catalogue selected from the variant's own identifications, never from
/// its name: two variants with the same gluing get the same surface, and a variant that
/// did not exist when this build was made still gets one (M13).
enum class SurfaceKind : std::uint8_t {
  FlatGrid,   ///< no gluing: the board extruded into its own grid
  Tube,       ///< one periodic axis: the files roll into a cylinder
  Torus,      ///< two or more periodic axes
  Band,       ///< a periodic axis with a sign flip: a Moebius band
  Klein,      ///< a flip on one axis plus a periodic axis: the figure-eight
  MirrorBox,  ///< a reflecting wall somewhere, so nothing is glued
};

/// The facts an overture needs, derived from the resolved spec.
struct OvertureSignature {
  SurfaceKind surface{SurfaceKind::FlatGrid};
  std::uint8_t dims{2};
  bool temporal{false};  ///< a turn or multiverse axis exists
};

/// Derive the signature from the identifications. Pure, and a pure function of the
/// geometry: a table test pins every shipped variant's surface.
[[nodiscard]] OvertureSignature overtureSignature(const VariantSpec& v) noexcept;

/// One lap of slide on `axis` (0 = slideU along the files, 1 = slideV along the ranks), in
/// cells: the smallest slide that gives the identical picture once the shape is formed
/// (docs/plan/M21-trailer-transitions.md). 0 where that axis does not slide on the shape.
/// Keyed by SurfaceKind, never by name.
[[nodiscard]] float slideLap(const VariantSpec& v, int axis) noexcept;

/// True when the variant's name has a hand-authored scene. Everything else - a Workshop
/// package, a variant added as data - animates with the derived scene instead.
[[nodiscard]] bool hasBespokeOverture(std::string_view variantName) noexcept;

/// The animation the library screen plays for a variant.
///
/// Every one of them opens on the same 8x8 board and becomes the thing that makes its
/// variant different - a tube, a donut, a cube, a detonation - and then takes it back.
/// The geometry lives in `render::overtureScene`; this is only the name of it, because
/// which overture a variant asks for is a fact about the variant, not about the renderer.
enum class Overture : std::uint8_t {
  None,
  Standard,     ///< the board itself, built once, then an opening
  Cylinder,     ///< the files roll into a tube
  Torus,        ///< and then the tube closes into a ring
  Mobius,       ///< the tube cannot absorb a rank flip, so it becomes a band
  Klein,        ///< and a circle cannot absorb a file flip, so it pinches to a figure-8
  Mirrorbox,    ///< two silvered walls and a billiard
  Cube5,        ///< cut to 5x5, then extruded into the cube
  Hyper4,       ///< cut to 4x4, extruded, then a hypercube that unfolds
  Atomic,       ///< a triangle of pawns, and nine cells taken at once
  AtomicTorus,  ///< the same blast, on a surface where the nine cells are four corners
  MustCapture,  ///< the refusal: a quiet move that is not a move
  Multiverse,   ///< boards appending along time, and a timeline branching
  Torus3d,      ///< a cube whose third gluing has nowhere in space to go
  T6,           ///< three axes too many, resolved onto the quintic
};

/// The overture a variant's name asks for.
///
/// `charged` borrows the standard board: it has no bespoke overture, and the nearest
/// family is a better answer than a blank pane. An unknown name - a Workshop package -
/// gets the standard board for the same reason. A library entry always animates.
[[nodiscard]] Overture overtureFor(std::string_view variantName) noexcept;

/// The overture's own name, for a caption and for a test's failure message.
[[nodiscard]] std::string_view overtureName(Overture o) noexcept;

/// Where an overture is in its cycle, and what happens when the player picks a
/// different variant while one is mid-flight.
///
/// The rule this class exists to enforce: **an overture is never cut off.** A formed
/// torus does not vanish because the player moved down the list - it unwinds to the flat
/// board first, and only then does the next one begin. That is a statement about
/// transitions, so it lives beside the shell's other transitions rather than in the
/// interface, and is testable with no window and no clock.
///
/// Every scene is a pure function of `progress()`, so reverse playback is not a second
/// animation: it is this class counting down.
class OverturePlayer {
 public:
  enum class Phase : std::uint8_t {
    Forward,  ///< t climbing to the formed shape
    Held,     ///< dwelling on it, so it can be read
    Reverse,  ///< t falling back to the flat board
    Settled,  ///< flat, briefly, so the 8x8 registers as the common ground
  };

  /// Forward and reverse each take this long. The two dwells are shorter on purpose:
  /// the formed shape is the thing worth looking at, and the flat board is a junction.
  static constexpr float kSweep = 2.4f;
  static constexpr float kHoldFormed = 0.7f;
  static constexpr float kHoldFlat = 0.25f;

  [[nodiscard]] Overture current() const noexcept { return current_; }
  /// What the player has picked but not yet been shown, or `None`.
  [[nodiscard]] Overture pending() const noexcept { return pending_; }
  /// 0 at the flat board, 1 at the formed shape. The only input a scene needs.
  [[nodiscard]] float progress() const noexcept { return t_; }
  [[nodiscard]] Phase phase() const noexcept { return phase_; }

  /// True while the standard overture should still build its board out of nothing.
  ///
  /// The construction runs on the first entry to the library in a session and never
  /// again; after that the same slot plays the opening on a board that is simply there.
  /// Only the standard overture has one, so this is false for every other.
  [[nodiscard]] bool intro() const noexcept {
    return intro_ && current_ == Overture::Standard;
  }

  /// The player is now looking at this variant.
  ///
  /// Mid-flight this does not switch the scene and does not interrupt it: the shape on
  /// screen finishes forming, dwells and unwinds on its own clock, and the stored choice
  /// begins when it reaches the flat board. Selecting the variant already running
  /// cancels a stored one. Only the latest selection is kept - running down the library
  /// must not enqueue every entry it passes, and a variant glanced at for a second is
  /// never one whose overture plays.
  void select(std::string_view variantName);
  void select(Overture o);

  /// Move the cycle on. `dt` is seconds, scaled by the playback speed.
  void advance(float dt);

  /// Multiplier on playback: the forward and reverse sweeps and the dwells all scale
  /// together, so "faster" keeps the shape legible rather than skipping it. A value of
  /// zero or less is ignored - the cycle always runs, only its rate is a setting.
  void setSpeed(float speed) noexcept { speed_ = speed > 0.0f ? speed : 1.0f; }
  [[nodiscard]] float speed() const noexcept { return speed_; }

  /// Switch outright, with no unwind.
  ///
  /// For `endspielzero_gui --shot` only. A screenshot is not a player moving through the
  /// library, so the rule that a shape must unwind first does not apply to it - and
  /// making a capture sit through an unwind it will never draw would only make the
  /// render tests slower.
  void jumpTo(Overture o) noexcept;

  /// Put the cycle at an exact point and hold it there.
  ///
  /// For `endspielzero_gui --shot`. The capture path feeds a fixed timestep on purpose so
  /// that a screenshot is the same picture every time it is taken - which means a
  /// capture has to be able to *state* its `t`, and have it stay stated: the interface
  /// hands that same synthetic timestep to the overture as its dt, so pinning without
  /// holding put the capture most of half a cycle past where it was asked for.
  ///
  /// `advance` does nothing while pinned. `select` and `jumpTo` release it, because both
  /// mean a player is driving again.
  void setProgress(float t) noexcept;
  [[nodiscard]] bool pinned() const noexcept { return pinned_; }

 private:
  Overture current_{Overture::Standard};
  Overture pending_{Overture::None};
  Phase phase_{Phase::Forward};
  float t_{0.0f};
  float dwell_{0.0f};
  float speed_{1.0f};
  bool intro_{true};
  bool pinned_{false};
};

}  // namespace cb::app
