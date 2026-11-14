// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/projector_hologram.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include "render/lab_scene.hpp"
#include "render/quintic.hpp"
namespace cb::render {
std::array<float, 4> quatFromBasis(view::Vec3 t, view::Vec3 b, view::Vec3 n) {
  std::array<float, 4> q{};
  const float trace = t.x + b.y + n.z;
  if (trace > 0) {
    const float s = 2 * std::sqrt(trace + 1);
    q = {(b.z - n.y) / s, (n.x - t.z) / s, (t.y - b.x) / s, s / 4};
  } else if (t.x > b.y && t.x > n.z) {
    const float s = 2 * std::sqrt(1 + t.x - b.y - n.z);
    q = {s / 4, (b.x + t.y) / s, (n.x + t.z) / s, (b.z - n.y) / s};
  } else if (b.y > n.z) {
    const float s = 2 * std::sqrt(1 + b.y - t.x - n.z);
    q = {(b.x + t.y) / s, s / 4, (n.y + b.z) / s, (n.x - t.z) / s};
  } else {
    const float s = 2 * std::sqrt(1 + n.z - t.x - b.y);
    q = {(n.x + t.z) / s, (n.y + b.z) / s, s / 4, (t.y - b.x) / s};
  }
  return q;
}
namespace {
using view::Vec3;
constexpr float kPi = std::numbers::pi_v<float>;
Vec3 vec(OvVec3 p) {
  return {p.x, p.y, p.z};
}
Vec3 turn(Vec3 p, float yaw) {
  return {p.x * std::cos(yaw) - p.y * std::sin(yaw),
          p.x * std::sin(yaw) + p.y * std::cos(yaw), p.z};
}
// The permitted rank-sign fallback keeps a1 on the near left at the Library station.
Vec3 axes(Vec3 p) {
  return {p.x, p.z, p.y};
}
struct Bounds {
  Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
          std::numeric_limits<float>::max()};
  Vec3 hi{-lo.x, -lo.y, -lo.z};
  bool any{false};
  void add(Vec3 p) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    any = true;
  }
  Vec3 centre() const { return any ? (lo + hi) * .5f : Vec3{}; }
};
struct Mapping {
  Vec3 centre;
  float reach;
  float yaw;
  const HologramPlacement& at;
  float tilt{0};
  Vec3 point(Vec3 p) const {
    auto world = at.centre + direction(p - centre) * (at.radius * at.scale / reach);
    if (at.squashZ != 1) {
      const auto axis = view::normalize(at.centre - at.lens);
      world = world + axis * (view::dot(world - at.lens, axis) * (at.squashZ - 1));
    }
    return world;
  }
  Vec3 direction(Vec3 p) const {
    const auto v = turn(axes(p), yaw);
    if (tilt == 0) return v;
    // The wall lens faces -X. Tip about the subject centre, after its own yaw,
    // so its normal faces into the room; pieces and trails share this rotation.
    return {v.x * std::cos(tilt) - v.z * std::sin(tilt), v.y,
            v.x * std::sin(tilt) + v.z * std::cos(tilt)};
  }
};
Instance instance(Vec3 p, Vec3 size, view::Rgba color, float alpha) {
  Instance out;
  out.center[0] = p.x;
  out.center[1] = p.y;
  out.center[2] = p.z;
  out.scale[0] = size.x;
  out.scale[1] = size.y;
  out.scale[2] = size.z;
  out.color[0] = color.r;
  out.color[1] = color.g;
  out.color[2] = color.b;
  out.color[3] = color.a * alpha;
  out.material = shaderMaterial(view::Material::Hologram, Cut::Plank, 0);
  // Material-4-only per-instance opacity flag. Captured board flourishes also use
  // neutral fxCell=0, so that index alone cannot distinguish projector geometry.
  out.panelSignal[2] = -1;
  return out;
}
void orient(Instance& out, Vec3 t, Vec3 b, Vec3 n) {
  const auto q = quatFromBasis(t, b, n);
  std::copy(q.begin(), q.end(), out.quat);
}
void alongZ(Instance& out, Vec3 n) {
  n = view::normalize(n);
  if (view::length(n) < 1e-6f) n = {0, 0, 1};
  const auto t = view::normalize(
      view::cross(std::abs(n.z) < .9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, n));
  orient(out, t, view::cross(n, t), n);
}
Instance tile(const OvQuad& q, const Mapping& map, view::Rgba color, float alpha) {
  Vec3 p[4];
  for (int j = 0; j < 4; ++j) p[j] = map.point(vec(q.p[j]));
  const auto e1 = p[1] - p[0], e2 = p[3] - p[0];
  auto out =
      instance((p[0] + p[1] + p[2] + p[3]) * .25f,
               {view::length(e1), view::length(e2), .02f * map.at.radius * map.at.scale},
               color, alpha * map.at.alpha);
  // Collapsed overture tiles retain their count but have no visible area. Give
  // their orientation a finite identity instead of normalising a zero cross product.
  if (view::length(e1) > 1e-7f && view::length(view::cross(e1, e2)) > 1e-7f) {
    const auto t = view::normalize(e1), n = view::normalize(view::cross(e1, e2));
    orient(out, t, view::cross(n, t), n);
  }
  return out;
}
Instance line(Vec3 a, Vec3 b, float width, view::Rgba color, float alpha) {
  auto out = instance((a + b) * .5f, {width, width, view::length(b - a)}, color, alpha);
  alongZ(out, b - a);
  return out;
}
view::Rgba toneColour(const view::Theme& th, OvTone tone) {
  switch (tone) {
    case OvTone::Light:
      return th.boardLight;
    case OvTone::Dark:
      return th.boardDark;
    case OvTone::Lit:
      return th.ember;
    case OvTone::Scorch:
      return th.blood;
    case OvTone::Mirror:
      return th.mirrorEdge;
    case OvTone::Scrim:
      return th.light ? view::Rgba::hex(0xFFFFFF) : view::Rgba::hex(0x000000);
  }
  return th.boardLight;
}
Archetype shape(char glyph) {
  switch (glyph) {
    case 'P':
      return Archetype::Dome;
    case 'N':
      return Archetype::Wedge;
    case 'B':
      return Archetype::Spire;
    case 'R':
      return Archetype::Tower;
    case 'Q':
      return Archetype::Crown;
    case 'K':
      return Archetype::Monolith;
    case 'U':
      return Archetype::Horn;
    default:
      return Archetype::Tower;
  }
}
struct QuinticTiles {
  std::vector<OvQuad> quads;
  Vec3 centre;
  float radius{.001f};
  QuinticTiles() {
    Bounds bounds;
    constexpr int steps = 6;
    for (int k1 = 0; k1 < kQuinticN; ++k1)
      for (int k2 = 0; k2 < kQuinticN; ++k2) {
        OvVec3 grid[steps + 1][steps + 1];
        for (int i = 0; i <= steps; ++i)
          for (int j = 0; j <= steps; ++j) {
            const auto p =
                quinticPoint(k1, k2, static_cast<float>(i) / steps * (kPi * .5f),
                             -1 + 2 * static_cast<float>(j) / steps);
            grid[i][j] = {p[0], p[1], p[2]};
            bounds.add(vec(grid[i][j]));
          }
        for (int i = 0; i < steps; ++i)
          for (int j = 0; j < steps; ++j)
            quads.push_back(
                {{grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]},
                 (i + j + k1 + k2) % 2 == 0 ? OvTone::Dark : OvTone::Light});
      }
    centre = bounds.centre();
    for (const auto& q : quads)
      for (const auto& p : q.p) radius = std::max(radius, view::length(vec(p) - centre));
  }
};
}  // namespace
Hologram overtureHologram(const OvertureScene& scene, const view::Theme& theme,
                          const HologramPlacement& at) {
  Bounds bounds;
  const auto visiblePoints = [&](auto add) {
    for (const auto& q : scene.quads)
      if (!at.wallSubject || q.fade > 0)
        for (const auto& p : q.p) add(vec(p));
    for (const auto& t : scene.tokens)
      if (!at.wallSubject || t.fade > 0) add(vec(t.at));
    if (at.wallSubject)
      for (const auto& trail : scene.trails)
        if (trail.fade > 0)
          for (const auto& p : trail.pts) add(vec(p));
  };
  visiblePoints([&](Vec3 p) { bounds.add(p); });
  float reach = std::max(scene.cam.reach, .001f), tilt = 0;
  if (at.wallSubject && bounds.any) {
    // OvCamera::reach includes pane-camera padding (particularly for hyper4).
    // A wall projection fits the actual subject, independently of that old zoom.
    reach = .001f;
    visiblePoints(
        [&](Vec3 p) { reach = std::max(reach, view::length(p - bounds.centre())); });
    const auto extent = bounds.hi - bounds.lo;
    const float flatness =
        1 -
        std::clamp(extent.y / (.35f * std::max({extent.x, extent.z, .001f})), 0.f, 1.f);
    tilt = kWallSubjectTiltDegrees * kPi / 180 * flatness * flatness * (3 - 2 * flatness);
  }
  const Mapping map{bounds.centre(), reach, at.yaw + scene.cam.yaw, at, tilt};
  Hologram out;
  out.quads.reserve(scene.quads.size());
  for (const auto& q : scene.quads)
    out.quads.push_back(
        tile(q, map, q.hasColour ? q.colour : toneColour(theme, q.tone), q.fade));
  std::array<std::vector<Instance>, kShapeSlots> byShape;
  for (const auto& t : scene.tokens) {
    const float scale = t.height * at.radius * at.scale * .10f;
    auto i = instance(map.point(vec(t.at)), {scale, scale, scale},
                      t.white ? theme.whitePiece : theme.blackPiece, t.fade * at.alpha);
    alongZ(i, map.direction(vec(t.normal)));
    byShape[static_cast<std::size_t>(shape(t.glyph))].push_back(i);
  }
  for (std::size_t s = 0; s < byShape.size(); ++s) {
    out.tokenBatches[s] = {static_cast<std::uint32_t>(out.tokens.size()),
                           static_cast<std::uint32_t>(byShape[s].size())};
    out.tokens.insert(out.tokens.end(), byShape[s].begin(), byShape[s].end());
  }
  for (const auto& trail : scene.trails)
    for (std::size_t j = 1; j < trail.pts.size(); ++j) {
      if (trail.dashed && j % 2 == 0) continue;
      out.lines.push_back(line(map.point(vec(trail.pts[j - 1])),
                               map.point(vec(trail.pts[j])), .006f * at.radius * at.scale,
                               trail.colour, trail.fade * at.alpha));
    }
  return out;
}
Hologram manifoldHologram(float seconds, const view::Theme& theme,
                          const HologramPlacement& at) {
  static const QuinticTiles model;
  const float yaw = at.yaw + seconds * .09f;
  const Mapping map{model.centre, model.radius, yaw, at};
  Hologram out;
  out.quads.reserve(model.quads.size());
  for (const auto& q : model.quads) {
    float depth = 0;
    const float pitch = .45f + std::sin(seconds * .05f) * .22f;
    for (const auto& p : q.p) {
      const float z = -p.x * std::sin(yaw) + p.z * std::cos(yaw);
      depth += (p.y * std::sin(pitch) + z * std::cos(pitch)) * .25f;
    }
    const float alpha = .13f + .26f * std::clamp((depth + 1.1f) / 2.2f, 0.f, 1.f);
    const auto dark = theme.light ? theme.bone : theme.soot;
    const auto pale = theme.light ? theme.panel : theme.boneDim;
    auto color = q.tone == OvTone::Dark ? dark : pale;
    color.a = alpha;
    out.quads.push_back(tile(q, map, color, 1));
  }
  return out;
}
Hologram tesseractHologram(float seconds, const view::Theme& theme,
                           const HologramPlacement& at) {
  Vec3 cube[16];
  Bounds bounds;
  const float a = seconds * .14f, b = seconds * .09f;
  for (int i = 0; i < 16; ++i) {
    const float x = static_cast<float>((i & 1) * 2 - 1),
                y = static_cast<float>(((i >> 1) & 1) * 2 - 1);
    const float z = static_cast<float>(((i >> 2) & 1) * 2 - 1),
                w = static_cast<float>(((i >> 3) & 1) * 2 - 1);
    const float nx = x * std::cos(a) - w * std::sin(a),
                nw = x * std::sin(a) + w * std::cos(a);
    const float ny = y * std::cos(b) - z * std::sin(b),
                nz = y * std::sin(b) + z * std::cos(b);
    const float k = 2.2f / (2.4f - nw * .85f);
    cube[i] = {nx * k, ny * k, nz * k};
    bounds.add(cube[i]);
  }
  float radius = .001f;
  for (const auto p : cube) radius = std::max(radius, view::length(p - bounds.centre()));
  const Mapping map{bounds.centre(), radius, at.yaw, at};
  Hologram out;
  for (int i = 0; i < 16; ++i)
    for (int d = 0; d < 4; ++d) {
      const int j = i ^ (1 << d);
      if (j < i) continue;
      out.lines.push_back(line(map.point(cube[i]), map.point(cube[j]),
                               .01f * at.radius * at.scale, theme.ember, at.alpha));
    }
  return out;
}
Instance projectorBeam(view::Vec3 lens, const HologramPlacement& at,
                       const view::Theme& theme) {
  const auto axis = view::normalize(at.centre - lens);
  const auto base = lens + axis *
                               std::max(0.f, view::length(at.centre - lens) - at.radius) *
                               at.squashZ;
  return line(lens, base, .10f * kCellsPerMetre, theme.ember, .12f * at.alpha);
}
}  // namespace cb::render
