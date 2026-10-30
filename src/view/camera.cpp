// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/camera.hpp"

#include <limits>

namespace cb::view {

Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 out{};
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      float sum = 0;
      for (int k = 0; k < 4; ++k)
        sum += a[static_cast<std::size_t>(k * 4 + r)] *
               b[static_cast<std::size_t>(c * 4 + k)];
      out[static_cast<std::size_t>(c * 4 + r)] = sum;
    }
  }
  return out;
}

Vec3 transformPoint(const Mat4& m, const Vec3& p) {
  const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
  const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
  const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
  const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
  const float inv = w != 0 ? 1.0f / w : 1.0f;
  return {x * inv, y * inv, z * inv};
}

Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
  const Vec3 f = normalize(center - eye);
  const Vec3 s = normalize(cross(f, up));
  const Vec3 u = cross(s, f);
  Mat4 m{};
  m[0] = s.x;
  m[4] = s.y;
  m[8] = s.z;
  m[12] = -dot(s, eye);
  m[1] = u.x;
  m[5] = u.y;
  m[9] = u.z;
  m[13] = -dot(u, eye);
  m[2] = -f.x;
  m[6] = -f.y;
  m[10] = -f.z;
  m[14] = dot(f, eye);
  m[3] = 0;
  m[7] = 0;
  m[11] = 0;
  m[15] = 1;
  return m;
}

Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float t = std::tan(fovYRadians * 0.5f);
  Mat4 m{};
  m[0] = 1.0f / (aspect * t);
  // Negative so that clip-space Y points down, as Vulkan expects; doing it here means
  // no viewport flip and no surprise mirror later.
  m[5] = -1.0f / t;
  m[10] = farZ / (nearZ - farZ);
  m[11] = -1.0f;
  m[14] = (nearZ * farZ) / (nearZ - farZ);
  return m;
}

Mat4 orthographic(float halfHeight, float aspect, float nearZ, float farZ) {
  Mat4 m{};
  m[0] = 1.0f / (aspect * halfHeight);
  m[5] = -1.0f / halfHeight;  // clip-space Y down, as in perspective() above
  m[10] = 1.0f / (nearZ - farZ);
  m[14] = nearZ / (nearZ - farZ);
  m[15] = 1.0f;
  return m;
}

OrbitCamera OrbitCamera::frame(const Bounds& b, float aspect, float headroom) {
  OrbitCamera cam;
  cam.target = Vec3{b.centerX(), b.centerY(), b.centerZ() + headroom * 0.5f};

  // Fit exactly rather than by the usual bounding-sphere approximation, which is what
  // was clipping the near rank: the board is a wide flat slab seen at a steep angle, so
  // a sphere around it is a poor stand-in for what actually reaches the frustum edges.
  //
  // Moving the camera straight back along its own view direction changes a point's
  // depth and nothing else, so the exact distance to add can be solved in one pass over
  // the corners instead of iterated.
  const float t = std::tan(cam.fovY * 0.5f);
  // The bounds describe cell *centres*, but a cell is half a cell wide either side of
  // its centre - without this margin the near rank is clipped.
  constexpr float kCellMargin = 0.55f;
  const float minX = b.minX - kCellMargin;
  const float maxX = b.maxX + kCellMargin;
  const float minY = b.minY - kCellMargin;
  const float maxY = b.maxY + kCellMargin;
  const float corners[8][3] = {{minX, minY, b.minZ},
                               {maxX, minY, b.minZ},
                               {minX, maxY, b.minZ},
                               {maxX, maxY, b.minZ},
                               {minX, minY, b.maxZ + headroom},
                               {maxX, minY, b.maxZ + headroom},
                               {minX, maxY, b.maxZ + headroom},
                               {maxX, maxY, b.maxZ + headroom}};

  const float radius = std::max(
      1.0f, length(Vec3{b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ}) * 0.5f);
  cam.distance = radius / t;  // a starting point; the pass below makes it exact

  const Vec3 eye = cam.eye();
  const Vec3 forward = normalize(cam.target - eye);
  const Vec3 right = normalize(cross(forward, cam.upHint()));
  const Vec3 up = cross(right, forward);

  float extra = 0.0f;
  for (const auto& c : corners) {
    const Vec3 rel = Vec3{c[0], c[1], c[2]} - eye;
    const float depth = dot(rel, forward);
    const float x = std::abs(dot(rel, right));
    const float y = std::abs(dot(rel, up));
    extra = std::max(extra, x / (aspect * t) - depth);
    extra = std::max(extra, y / t - depth);
  }
  // A little air so nothing sits flush against the edge of the frame.
  cam.distance = (cam.distance + std::max(0.0f, extra)) * 1.04f;
  cam.farZ = cam.distance + radius * 4.0f + 20.0f;
  return cam;
}

Vec3 OrbitCamera::eye() const {
  const float cp = std::cos(pitch);
  return target +
         Vec3{std::sin(yaw) * cp, -std::cos(yaw) * cp, std::sin(pitch)} * distance;
}

namespace {

/// The camera's view basis (forward, right, up), with `roll` applied about the view
/// direction. The one place the roll lives, so `viewProj` and `pickRay` cannot disagree
/// about which way is up on screen.
void cameraBasis(const OrbitCamera& cam, Vec3& f, Vec3& r, Vec3& u) {
  f = normalize(cam.target - cam.eye());
  r = normalize(cross(f, cam.upHint()));
  u = cross(r, f);
  if (cam.roll != 0.0f) {
    const float c = std::cos(cam.roll);
    const float s = std::sin(cam.roll);
    const Vec3 r2 = r * c + u * s;
    const Vec3 u2 = u * c - r * s;
    r = r2;
    u = u2;
  }
}

Mat4 lookFrom(const Vec3& eye, const Vec3& f, const Vec3& r, const Vec3& u) {
  Mat4 m{};
  m[0] = r.x;
  m[4] = r.y;
  m[8] = r.z;
  m[12] = -dot(r, eye);
  m[1] = u.x;
  m[5] = u.y;
  m[9] = u.z;
  m[13] = -dot(u, eye);
  m[2] = -f.x;
  m[6] = -f.y;
  m[10] = -f.z;
  m[14] = dot(f, eye);
  m[15] = 1;
  return m;
}

}  // namespace

Vec3 OrbitCamera::upHint() const {
  return std::abs(std::cos(pitch)) < 1e-3f ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}

void OrbitCamera::basis(Vec3& forward, Vec3& right, Vec3& up) const {
  cameraBasis(*this, forward, right, up);
}

namespace {

/// A unit quaternion, camera-local to world. Only what the camera blend needs.
struct Quat {
  float x{0}, y{0}, z{0}, w{1};
};

/// The rotation whose images of the local axes are `right`, `up`, `back` (the columns of
/// a camera-to-world matrix). Branches on the largest diagonal, like `quatOf` in the play
/// surface: the naive trace formula loses precision near a half-turn.
Quat quatFromBasis(const Vec3& right, const Vec3& up, const Vec3& back) {
  const float m00 = right.x, m01 = up.x, m02 = back.x;
  const float m10 = right.y, m11 = up.y, m12 = back.y;
  const float m20 = right.z, m21 = up.z, m22 = back.z;
  const float trace = m00 + m11 + m22;
  Quat q;
  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
  } else if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
  } else if (m11 > m22) {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
  } else {
    const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
  }
  const float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (len > 1e-8f) {
    q.x /= len;
    q.y /= len;
    q.z /= len;
    q.w /= len;
  }
  return q;
}

Vec3 rotate(const Quat& q, const Vec3& v) {
  const Vec3 u{q.x, q.y, q.z};
  const Vec3 t = cross(u, v) * 2.0f;
  return v + t * q.w + cross(u, t);
}

/// Shortest-path slerp. The dot-product sign flip is what picks the short way round.
Quat slerp(Quat a, Quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0.0f) {
    b = {-b.x, -b.y, -b.z, -b.w};
    d = -d;
  }
  if (d > 0.9995f) {  // nearly parallel: a normalized lerp is stable and equivalent
    Quat out{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
             a.w + (b.w - a.w) * t};
    const float len =
        std::sqrt(out.x * out.x + out.y * out.y + out.z * out.z + out.w * out.w);
    if (len > 1e-8f) {
      out.x /= len;
      out.y /= len;
      out.z /= len;
      out.w /= len;
    }
    return out;
  }
  const float theta = std::acos(std::clamp(d, -1.0f, 1.0f));
  const float s = std::sin(theta);
  const float wa = std::sin((1.0f - t) * theta) / s;
  const float wb = std::sin(t * theta) / s;
  return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb,
          a.w * wa + b.w * wb};
}

}  // namespace

namespace {

/// An orbit camera looking along the orientation `q`, from `target` at `distance`, taking
/// everything else (lens, clip planes, projection) from `proto`. Pitch and yaw come from
/// the eye direction and the remaining twist goes in `roll`, through the same basis the
/// renderer uses (after the pitch clamp and with the camera's own up-hint).
OrbitCamera orbitFromQuat(const Quat& q, const Vec3& target, float distance,
                          const OrbitCamera& proto) {
  const Vec3 back = rotate(q, Vec3{0.0f, 0.0f, 1.0f});  // target -> eye
  const Vec3 wantUp = rotate(q, Vec3{0.0f, 1.0f, 0.0f});
  Vec3 e = back;
  if (length(e) > 1e-6f) e = normalize(e);

  OrbitCamera out;
  out.fovY = proto.fovY;
  out.nearZ = proto.nearZ;
  out.farZ = proto.farZ;
  out.orthographic = proto.orthographic;
  out.distance = std::max(0.5f, distance);
  out.pitch = std::asin(std::clamp(e.z, -0.999f, 0.999f));
  out.yaw = std::atan2(e.x, -e.y);
  // `target` is the authored, primary state everywhere else, so it is given, and `eye()`
  // derives from it along the orientation's direction - matching `OrbitCamera`'s own
  // representation instead of inverting it. Interpolating the eye and back-solving a
  // target aimed the in-between camera away from both subjects whenever their targets
  // differ, which is every real caller (M17.23).
  out.target = target;
  const Vec3 viewDir = e * -1.0f;
  const Vec3 r0 = normalize(cross(viewDir, out.upHint()));
  const Vec3 up0 = cross(r0, viewDir);
  Vec3 nPerp = wantUp - viewDir * dot(wantUp, viewDir);
  if (length(nPerp) > 1e-5f) {
    nPerp = normalize(nPerp);
    out.roll = -std::atan2(dot(cross(up0, nPerp), viewDir), dot(up0, nPerp));
  }
  return out;
}

Quat quatOfCamera(const OrbitCamera& c) {
  Vec3 f{}, r{}, u{};
  c.basis(f, r, u);
  return quatFromBasis(r, u, f * -1.0f);
}

Quat mul(const Quat& a, const Quat& b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quat conj(const Quat& q) {
  return {-q.x, -q.y, -q.z, q.w};
}

float dotq(const Quat& a, const Quat& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

/// Log of a unit quaternion: a pure quaternion (x, y, z, 0) of half the rotation angle.
Quat qlog(const Quat& q) {
  const float vlen = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
  if (vlen < 1e-7f) return {0.0f, 0.0f, 0.0f, 0.0f};
  const float k = std::atan2(vlen, q.w) / vlen;
  return {q.x * k, q.y * k, q.z * k, 0.0f};
}

Quat qexp(const Quat& v) {
  const float vlen = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (vlen < 1e-7f) return {0.0f, 0.0f, 0.0f, 1.0f};
  const float k = std::sin(vlen) / vlen;
  return {v.x * k, v.y * k, v.z * k, std::cos(vlen)};
}

/// Squad's inner control point at `q`, given its neighbours (already hemisphere-aligned
/// to `q`): the point that makes the spline's tangent continuous through `q`.
Quat squadControl(const Quat& prev, const Quat& q, const Quat& next) {
  const Quat qi = conj(q);
  const Quat a = qlog(mul(qi, next));
  const Quat b = qlog(mul(qi, prev));
  const Quat sum{-(a.x + b.x) * 0.25f, -(a.y + b.y) * 0.25f, -(a.z + b.z) * 0.25f, 0.0f};
  return mul(q, qexp(sum));
}

/// Cubic Hermite on [0,1] with end values `p0`,`p1` and tangents `m0`,`m1` (already
/// scaled by the interval length).
float hermite(float p0, float p1, float m0, float m1, float h) {
  const float h2 = h * h;
  const float h3 = h2 * h;
  return (2 * h3 - 3 * h2 + 1) * p0 + (h3 - 2 * h2 + h) * m0 + (-2 * h3 + 3 * h2) * p1 +
         (h3 - h2) * m1;
}

/// Catmull-Rom tangent (value per unit time) at index `i` of a non-uniform series.
float tangentAt(const std::vector<float>& t, const std::vector<float>& v, std::size_t i) {
  const std::size_t lo = i == 0 ? 0 : i - 1;
  const std::size_t hi = i + 1 < t.size() ? i + 1 : i;
  const float dt = t[hi] - t[lo];
  return dt > 1e-9f ? (v[hi] - v[lo]) / dt : 0.0f;
}

/// The segment holding `t`: index `i` with `times[i] <= t < times[i+1]`, and `h` in
/// [0,1].
bool locate(const std::vector<float>& times, float t, std::size_t& i, float& h) {
  if (times.size() < 2 || t <= times.front() || t >= times.back()) return false;
  i = 0;
  while (i + 2 < times.size() && times[i + 1] <= t) ++i;
  h = (t - times[i]) / (times[i + 1] - times[i]);
  return true;
}

}  // namespace

OrbitCamera slerpCamera(const OrbitCamera& a, const OrbitCamera& b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const Quat q = slerp(quatOfCamera(a), quatOfCamera(b), t);
  return orbitFromQuat(q, a.target + (b.target - a.target) * t,
                       a.distance + (b.distance - a.distance) * t, a);
}

OrbitCamera splineCamera(const std::vector<CameraWaypoint>& keys, float t) {
  if (keys.empty()) return OrbitCamera{};
  std::vector<float> times;
  times.reserve(keys.size());
  for (const CameraWaypoint& k : keys) times.push_back(k.time);
  std::size_t i = 0;
  float h = 0.0f;
  if (!locate(times, t, i, h))
    return t <= keys.front().time ? keys.front().camera : keys.back().camera;

  // Orientation: hemisphere-align the whole chain first so every neighbour is the short
  // way round, then squad between keys i and i+1.
  std::vector<Quat> q;
  q.reserve(keys.size());
  for (const CameraWaypoint& k : keys) {
    Quat cur = quatOfCamera(k.camera);
    if (!q.empty() && dotq(q.back(), cur) < 0.0f) cur = {-cur.x, -cur.y, -cur.z, -cur.w};
    q.push_back(cur);
  }
  const auto control = [&](std::size_t k) {
    const Quat& prev = k == 0 ? q[k] : q[k - 1];
    const Quat& next = k + 1 < q.size() ? q[k + 1] : q[k];
    return squadControl(prev, q[k], next);
  };
  // Each control point stays on the hemisphere of the key it belongs to, so `inner` is the
  // short way between them, and the blend below can never flip: `slerp` picks the short way
  // by negating one end, and when `outer` and `inner` are a quarter turn of quaternion
  // space apart that choice changes sign from one frame to the next - the camera used to
  // spin half a turn in a single frame there (a key added between two far-apart poses).
  // Weighting the squad term by |dot| makes it vanish exactly where the sign would flip, so
  // the path is continuous whichever way it is signed.
  Quat s0 = control(i);
  Quat s1 = control(i + 1);
  if (dotq(s0, q[i]) < 0.0f) s0 = {-s0.x, -s0.y, -s0.z, -s0.w};
  if (dotq(s1, q[i + 1]) < 0.0f) s1 = {-s1.x, -s1.y, -s1.z, -s1.w};
  const Quat outer = slerp(q[i], q[i + 1], h);
  Quat inner = slerp(s0, s1, h);
  const float d = dotq(outer, inner);
  if (d < 0.0f) inner = {-inner.x, -inner.y, -inner.z, -inner.w};
  const Quat orient = slerp(outer, inner, 2.0f * h * (1.0f - h) * std::fabs(d));

  // Position, distance and lens: Catmull-Rom on each component.
  const float span = times[i + 1] - times[i];
  const auto channel = [&](auto get) {
    std::vector<float> v;
    v.reserve(keys.size());
    for (const CameraWaypoint& k : keys) v.push_back(get(k.camera));
    return hermite(v[i], v[i + 1], tangentAt(times, v, i) * span,
                   tangentAt(times, v, i + 1) * span, h);
  };
  const Vec3 target{channel([](const OrbitCamera& c) { return c.target.x; }),
                    channel([](const OrbitCamera& c) { return c.target.y; }),
                    channel([](const OrbitCamera& c) { return c.target.z; })};
  const float distance = channel([](const OrbitCamera& c) { return c.distance; });
  OrbitCamera out = orbitFromQuat(orient, target, distance, keys[i].camera);
  out.fovY = channel([](const OrbitCamera& c) { return c.fovY; });
  return out;
}

float splineScalar(const std::vector<std::pair<float, float>>& points, float t) {
  if (points.empty()) return 0.0f;
  std::vector<float> times, vals;
  for (const auto& p : points) {
    times.push_back(p.first);
    vals.push_back(p.second);
  }
  std::size_t i = 0;
  float h = 0.0f;
  if (!locate(times, t, i, h)) return t <= times.front() ? vals.front() : vals.back();
  const float span = times[i + 1] - times[i];
  return hermite(vals[i], vals[i + 1], tangentAt(times, vals, i) * span,
                 tangentAt(times, vals, i + 1) * span, h);
}

OrbitCamera::ScreenPoint OrbitCamera::project(const Vec3& world, float aspect,
                                              float width, float height) const {
  const Mat4 vp = viewProj(aspect);
  const float cx = vp[0] * world.x + vp[4] * world.y + vp[8] * world.z + vp[12];
  const float cy = vp[1] * world.x + vp[5] * world.y + vp[9] * world.z + vp[13];
  const float cw = vp[3] * world.x + vp[7] * world.y + vp[11] * world.z + vp[15];
  ScreenPoint out;
  if (cw <= 0.0f) return out;
  // Half a pixel back, because pickRay works in pixel centres; without it a round trip
  // through the two would drift by half a pixel every time.
  out.x = (cx / cw * 0.5f + 0.5f) * width - 0.5f;
  out.y = (cy / cw * 0.5f + 0.5f) * height - 0.5f;
  out.visible = out.x >= 0.0f && out.y >= 0.0f && out.x < width && out.y < height;
  return out;
}

Mat4 OrbitCamera::viewProj(float aspect) const {
  // Z is the board's "up" for 3-D boards, and the extra axes lay out in X and Y, so the
  // camera treats Z as up too - looking at a 2-D board then gives the familiar
  // over-the-table view with no special case.
  // `farZ` was set when the board was framed; a player can zoom out far past it, and the
  // whole shape would then sit beyond the far plane and vanish. The far plane follows the
  // eye's distance, never closer than the framed value.
  const float far = std::max(farZ, distance * 2.0f + 50.0f);
  const Mat4 proj =
      orthographic
          ? cb::view::orthographic(distance * std::tan(fovY * 0.5f), aspect, nearZ, far)
          : perspective(fovY, aspect, nearZ, far);
  Vec3 f{}, r{}, u{};
  cameraBasis(*this, f, r, u);
  return multiply(proj, lookFrom(eye(), f, r, u));
}

OrbitCamera::Ray OrbitCamera::pickRay(float px, float py, float width,
                                      float height) const {
  // Reconstruct the ray from the camera basis rather than by inverting the matrix:
  // fewer operations, no near-singular cases, and it is obvious what it does.
  const Vec3 e = eye();
  Vec3 forward{}, right{}, up{};
  cameraBasis(*this, forward, right, up);

  const float aspect = width > 0 ? width / height : 1.0f;
  const float t = std::tan(fovY * 0.5f);
  // Pixel centre to normalised device coordinates; Y is flipped because the image's
  // origin is at the top left.
  const float ndcX = (px + 0.5f) / width * 2.0f - 1.0f;
  const float ndcY = 1.0f - (py + 0.5f) / height * 2.0f;

  if (orthographic) {
    // Parallel rays: the pixel picks the point, not the angle.
    const float halfH = distance * std::tan(fovY * 0.5f);
    const Vec3 origin = e + right * (ndcX * halfH * aspect) + up * (ndcY * halfH);
    return Ray{origin, forward};
  }
  const Vec3 dir = normalize(forward + right * (ndcX * t * aspect) + up * (ndcY * t));
  return Ray{e, dir};
}

int pickBox(const OrbitCamera::Ray& ray, const std::vector<Placement>& boxes,
            const Vec3& halfExtent) {
  int best = -1;
  float bestT = std::numeric_limits<float>::max();
  const Vec3 inv{ray.direction.x != 0 ? 1.0f / ray.direction.x : 0,
                 ray.direction.y != 0 ? 1.0f / ray.direction.y : 0,
                 ray.direction.z != 0 ? 1.0f / ray.direction.z : 0};

  for (std::size_t i = 0; i < boxes.size(); ++i) {
    const Placement& p = boxes[i];
    // Standard slab test, skipping axes the ray is parallel to.
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    const float center[3]{p.x, p.y, p.z};
    const float half[3]{halfExtent.x, halfExtent.y, halfExtent.z};
    const float origin[3]{ray.origin.x, ray.origin.y, ray.origin.z};
    const float d[3]{ray.direction.x, ray.direction.y, ray.direction.z};
    const float invd[3]{inv.x, inv.y, inv.z};

    bool hit = true;
    for (int a = 0; a < 3; ++a) {
      if (d[a] == 0.0f) {
        if (origin[a] < center[a] - half[a] || origin[a] > center[a] + half[a]) {
          hit = false;
          break;
        }
        continue;
      }
      float t1 = (center[a] - half[a] - origin[a]) * invd[a];
      float t2 = (center[a] + half[a] - origin[a]) * invd[a];
      if (t1 > t2) std::swap(t1, t2);
      tMin = std::max(tMin, t1);
      tMax = std::min(tMax, t2);
      if (tMin > tMax) {
        hit = false;
        break;
      }
    }
    if (hit && tMin < bestT) {
      bestT = tMin;
      best = static_cast<int>(i);
    }
  }
  return best;
}

}  // namespace cb::view
