// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

#include "view/layout.hpp"

namespace cb::view {

// The camera lives in the view layer rather than in the renderer: it is presentation
// maths with no Vulkan in it, so framing and picking can be tested - and driven by a
// front end - on a machine with no graphics device at all.

/// Column-major 4x4, matching what Vulkan expects from a uniform or push constant.
using Mat4 = std::array<float, 16>;

struct Vec3 {
  float x{0}, y{0}, z{0};
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator+(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator*(const Vec3& a, float s) {
  return {a.x * s, a.y * s, a.z * s};
}
inline float dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) {
  return std::sqrt(dot(a, a));
}
inline Vec3 normalize(const Vec3& a) {
  const float l = length(a);
  return l > 0 ? a * (1.0f / l) : a;
}

Mat4 multiply(const Mat4& a, const Mat4& b);
Vec3 transformPoint(const Mat4& m, const Vec3& p);

/// Right-handed look-at.
Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up);

/// Perspective projection with Vulkan's conventions: depth in [0,1] and Y down in
/// clip space, so no flip is needed anywhere else.
Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ);

/// Parallel projection, same conventions. Used by the flat board view, where a
/// perspective would make the far rank smaller than the near one for no reason: a 2-D
/// board is a diagram, and a diagram has no vanishing point.
Mat4 orthographic(float halfHeight, float aspect, float nearZ, float farZ);

/// The distance, in cells, inside which nothing is drawn: the camera's near clipping plane
/// as a play setting. A camera zoomed or slid into a shape (the inside of a torus tube) would
/// otherwise have polygons a hair in front of the lens flicker and fill the screen. 0.1 was
/// the fixed value before this was a setting.
inline constexpr float kMinNearClip = 0.05f;
inline constexpr float kMaxNearClip = 3.0f;
inline constexpr float kDefaultNearClip = 0.5f;

/// An orbit camera, which is the only control scheme that makes sense for a board you
/// are looking at rather than standing in.
struct OrbitCamera {
  Vec3 target{};
  float distance{10.0f};
  float yaw{0.6f};
  float pitch{0.9f};
  float fovY{0.9f};
  float nearZ{0.1f};
  float farZ{1000.0f};
  /// Look straight down with no perspective. The flat view sets this; everything else
  /// - framing, zoom, picking - keeps working, which is why it is a flag on the camera
  /// rather than a second camera class.
  bool orthographic{false};
  /// Roll about the view direction, radians. Zero for every ordinary view - the board is
  /// looked at with world up on screen. The geometry view's chase camera sets it so the
  /// piece's own up (its surface normal) is the view's up, and the piece stands
  /// vertically in the middle of the frame (M17.16 revision).
  float roll{0.0f};

  /// Frame the whole laid-out scene, whatever its dimensionality.
  ///
  /// `headroom` is how far the pieces stand above the cells the bounds were measured
  /// from - a king's crown should not be clipped - and `aspect` is the shape of the
  /// area the board will actually be drawn into, which is narrower than the window
  /// once the interface takes its rails.
  static OrbitCamera frame(const Bounds& b, float aspect = 1.3f, float headroom = 1.4f);

  [[nodiscard]] Vec3 eye() const;
  /// World up for the view basis. Looking straight down, the usual +Z is parallel to
  /// the view direction and gives a degenerate basis, so the board's +Y takes over.
  [[nodiscard]] Vec3 upHint() const;
  /// The camera's view basis (forward, right, up) with `roll` applied - the same basis
  /// `viewProj` and `pickRay` use. Exposed so a caller blending two cameras (M17.19) uses
  /// the renderer's basis and not a second derivation of it.
  void basis(Vec3& forward, Vec3& right, Vec3& up) const;
  [[nodiscard]] Mat4 viewProj(float aspect) const;
  /// This camera with its near plane at least `nearClip` away: geometry closer to the eye
  /// than that is not drawn. Never moves the plane nearer than the camera already had it.
  [[nodiscard]] OrbitCamera withNearClip(float nearClip) const noexcept {
    OrbitCamera c = *this;
    c.nearZ = nearZ > nearClip ? nearZ : nearClip;
    return c;
  }

  /// A world point in pixels, origin top left - the inverse of pickRay. `visible` is
  /// false when the point is behind the camera or outside the viewport, which is exactly
  /// when nothing should be drawn there.
  struct ScreenPoint {
    float x{0}, y{0};
    bool visible{false};
  };
  [[nodiscard]] ScreenPoint project(const Vec3& world, float aspect, float width,
                                    float height) const;

  /// Screen-space pick ray. `px`,`py` are pixel coordinates with the origin at the
  /// top left, matching the rendered image.
  struct Ray {
    Vec3 origin;
    Vec3 direction;
  };
  [[nodiscard]] Ray pickRay(float px, float py, float width, float height) const;
};

/// Nearest box hit along a ray, or -1. Boxes are the laid-out cells, which are all
/// axis-aligned - so picking is an exact slab test rather than a depth-buffer readback,
/// and it works with no GPU at all, which is what makes interaction testable headlessly.
int pickBox(const OrbitCamera::Ray& ray, const std::vector<Placement>& boxes,
            const Vec3& halfExtent);

/// Blend two orbit cameras: the target travels in a straight line between them and the
/// orientation is a quaternion slerp, so the view turns the shortest way and never swings
/// the long way round (M17.19). Interpolating yaw/pitch/roll separately - or the eye
/// *direction* by a plain lerp, which walks through the origin when the two views nearly
/// oppose - is what produced the 720-degree spins this replaces. At `t = 0` this is `a`,
/// at `t = 1` it is `b`.
[[nodiscard]] OrbitCamera slerpCamera(const OrbitCamera& a, const OrbitCamera& b,
                                      float t);

/// One keyframe of a camera path: the camera at a moment on any monotonic clock.
struct CameraWaypoint {
  float time{0.0f};
  OrbitCamera camera;
};

/// A smooth camera path through `keys` (sorted by time, strictly increasing), evaluated
/// at `t`. Position, distance and lens go through a Catmull-Rom spline and the
/// orientation through a squad spline - the quaternion analogue, which is C1 where a
/// chain of slerps has a corner at every keyframe - so the motion has no jerk at a
/// keyframe and, between them, no stop-and-start. It follows the keyframes closely rather
/// than exactly in between. Held at the first and last keyframe outside the range; an
/// empty list gives a default camera.
[[nodiscard]] OrbitCamera splineCamera(const std::vector<CameraWaypoint>& keys, float t);

/// A scalar through `points` (time, value; sorted by time), by cubic Hermite with
/// Catmull-Rom tangents: the same smoothness for the pose scalars (morph, ghost ...).
/// Held at the end values outside the range.
[[nodiscard]] float splineScalar(const std::vector<std::pair<float, float>>& points,
                                 float t);

}  // namespace cb::view
