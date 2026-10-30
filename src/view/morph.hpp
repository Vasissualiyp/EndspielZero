// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace cb::view {

/// The base speed of a flat<->shape morph's own ramp (M18.6): at 1.0 the live value
/// crosses the whole way in half a second, and a per-caller speed multiplies that rate.
/// It lives here, one layer below both the renderer that poses the board and the app
/// shell that drives the main-menu attract sequence, so the two cannot drift apart
/// (M13.9).
inline constexpr float kFormMorphRate = 2.0f;

/// One frame's advance of a linear morph: move `formed` toward `target` by
/// `dt * kFormMorphRate * formSpeed`, stopping exactly on the target rather than
/// approaching it asymptotically, so the caller settles in a definite time and a settle
/// frame is exact (M17.24). Pure, so the per-frame step is testable without a live
/// window. `formSpeed` of 1 is the shipped pacing; larger is faster, smaller slower.
[[nodiscard]] inline float advanceFormMorph(float formed, float target, float dt,
                                            float formSpeed) noexcept {
  const float step = dt * kFormMorphRate * formSpeed;
  if (formed < target) return std::min(target, formed + step);
  if (formed > target) return std::max(target, formed - step);
  return formed;
}

/// A morph that eases: slow away from its start, quick through the middle, slow into its
/// end (smoothstep), so a shape change begins and finishes without an instant where the
/// motion "switches on". The duration is the span over `kFormMorphRate * formSpeed`, the
/// same time `advanceFormMorph` takes for the same distance, so speeds mean what they
/// did.
struct EasedMorph {
  float from{0.0f};
  float to{0.0f};
  float t{1.0f};
  float last{0.0f};
  bool started{false};
};

/// One frame of an eased morph of `current` toward `target`. A new target, or a `current`
/// somebody else has changed since the last call (a seek, a keyframe), restarts the ease
/// from where the value actually is.
[[nodiscard]] inline float advanceEasedMorph(EasedMorph& m, float current, float target,
                                             float dt, float formSpeed) noexcept {
  if (!m.started || target != m.to || std::abs(current - m.last) > 1e-4f) {
    m.started = true;
    m.from = current;
    m.to = target;
    m.t = 0.0f;
  }
  const float span = std::abs(m.to - m.from);
  float out = m.to;
  if (span > 1e-6f && m.t < 1.0f) {
    m.t = std::min(1.0f, m.t + dt * kFormMorphRate * formSpeed / span);
    out =
        m.t >= 1.0f ? m.to : m.from + (m.to - m.from) * (m.t * m.t * (3.0f - 2.0f * m.t));
  } else {
    m.t = 1.0f;
  }
  m.last = out;
  return out;
}

/// The shape morphs' own stage boundaries, in `formed` - one source of truth, shared by
/// the renderer that stages the surface functions and the app layer that choreographs a
/// cut landing exactly on one (M13.9 revision: the main-menu trailer's cylinder ->
/// torus -> Klein sequence lands each cut on the previous shape's own end-of-stage
/// waypoint, so the two sides of a cut are the same picture). Lives here, below both
/// `render` and `app`, for the same reason `kFormMorphRate` does.
///
/// `kTorusRollEnd` is the Torus morph's own boundary: the files roll into an open tube
/// first (0 .. this), then that tube bends into a closed ring (this .. 1).
/// `kKleinRingEnd` and `kKleinFoldEnd` are the Klein morph's own three-stage boundaries
/// (M18 revision, item 3): the ring forms first (0 .. `kKleinRingEnd`, bit-for-bit the
/// Torus morph's own end state), then the cross-section folds into the figure-eight
/// (`kKleinRingEnd` .. `kKleinFoldEnd`), then the twist settles the identifications
/// (`kKleinFoldEnd` .. 1).
inline constexpr float kTorusRollEnd = 0.5f;
inline constexpr float kKleinRingEnd = 0.40f;
inline constexpr float kKleinFoldEnd = 0.72f;

/// `value` moved by whole `lap`s to the equivalent nearest `reference`:
/// value + lap * round((reference - value) / lap). `lap <= 0` returns `value` unchanged
/// (that axis does not slide). With reference 0 this is the canonical slide in
/// [-lap/2, lap/2]. A slide is periodic, so interpolating between two values takes the
/// short way round rather than sweeping spurious laps (M21).
[[nodiscard]] inline float nearestLap(float value, float reference, float lap) noexcept {
  if (!(lap > 0.0f)) return value;
  return value + lap * std::round((reference - value) / lap);
}

/// How long the trailer takes to hand its pose from a beat's own reveal to the keyframe
/// spline, in trailer units (one unit = one move).
inline constexpr float kPoseHandoverUnits = 1.0f;

/// The keyframe spline's weight in the trailer pose at unshifted trailer time `g`, for a
/// beat whose first frame was at `beatStart` and whose first keyframe is at `firstKey`
/// (infinity when the beat has none -> 0). The window is
/// end = max(firstKey, beatStart + kPoseHandoverUnits), start = end - kPoseHandoverUnits;
/// smoothstep between, 0 before, 1 after.
[[nodiscard]] inline float poseHandoverWeight(float g, float beatStart,
                                              float firstKey) noexcept {
  if (!std::isfinite(firstKey)) return 0.0f;
  const float end = std::max(firstKey, beatStart + kPoseHandoverUnits);
  const float start = end - kPoseHandoverUnits;
  if (g <= start) return 0.0f;
  if (g >= end) return 1.0f;
  const float t = (g - start) / kPoseHandoverUnits;
  return t * t * (3.0f - 2.0f * t);
}

}  // namespace cb::view
