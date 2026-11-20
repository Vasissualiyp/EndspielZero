// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_IMGUI
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include "render/lab_scene.hpp"
#include "render/projector_hologram.hpp"
#include "render/ui.hpp"
using namespace cb;
using namespace cb::render;
namespace {
using Point = std::array<float, 2>;
using Polygon = std::vector<Point>;
float cross(Point a, Point b, Point c) {
  return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
}
Polygon hull(Polygon points) {
  std::sort(points.begin(), points.end());
  points.erase(std::unique(points.begin(), points.end()), points.end());
  if (points.size() < 3) return points;
  Polygon out;
  for (const auto p : points) {
    while (out.size() >= 2 && cross(out[out.size() - 2], out.back(), p) <= 0)
      out.pop_back();
    out.push_back(p);
  }
  const auto lower = out.size();
  for (auto i = points.rbegin() + 1; i != points.rend(); ++i) {
    while (out.size() > lower && cross(out[out.size() - 2], out.back(), *i) <= 0)
      out.pop_back();
    out.push_back(*i);
  }
  out.pop_back();
  return out;
}
float area(Polygon points) {
  // Clip to the actual viewport; offscreen hardware does not cover any pixels.
  for (std::size_t axis = 0; axis < 2; ++axis)
    for (float boundary : {0.f, 1.f}) {
      Polygon next;
      if (points.empty()) return 0;
      auto a = points.back();
      const auto inside = [&](Point p) {
        return boundary == 0 ? p[axis] >= 0 : p[axis] <= 1;
      };
      for (const auto b : points) {
        if (inside(a) != inside(b)) {
          const float t = (boundary - a[axis]) / (b[axis] - a[axis]);
          next.push_back({std::lerp(a[0], b[0], t), std::lerp(a[1], b[1], t)});
        }
        if (inside(b)) next.push_back(b);
        a = b;
      }
      points = std::move(next);
    }
  float sum = 0;
  for (std::size_t j = 0; j < points.size(); ++j) {
    const auto a = points[j], b = points[(j + 1) % points.size()];
    sum += a[0] * b[1] - a[1] * b[0];
  }
  return std::abs(sum) * .5f;
}
Polygon intersect(Polygon p, const Polygon& clip) {
  for (std::size_t j = 0; j < clip.size(); ++j) {
    if (p.empty()) break;
    const auto a = clip[j], b = clip[(j + 1) % clip.size()];
    Polygon next;
    auto v = p.back();
    float dv = cross(a, b, v);
    for (const auto w : p) {
      const float dw = cross(a, b, w);
      if ((dv >= 0) != (dw >= 0)) {
        const float t = dv / (dv - dw);
        next.push_back({std::lerp(v[0], w[0], t), std::lerp(v[1], w[1], t)});
      }
      if (dw >= 0) next.push_back(w);
      v = w;
      dv = dw;
    }
    p = std::move(next);
  }
  return p;
}
float unionArea(const std::vector<Polygon>& polygons) {
  // Inclusion/exclusion counts overlapping rails and ceiling panels only once.
  const auto remaining = [&](auto&& self, const Polygon& p, std::size_t start) -> float {
    float result = area(p);
    for (std::size_t j = start; j < polygons.size(); ++j) {
      const auto common = intersect(p, polygons[j]);
      if (common.size() >= 3 && area(common) > 0) result -= self(self, common, j + 1);
    }
    return result;
  };
  float result = 0;
  for (std::size_t j = 0; j < polygons.size(); ++j)
    result += remaining(remaining, polygons[j], j + 1);
  return result;
}
view::Vec3 rotate(view::Vec3 p, const float* q) {
  const view::Vec3 axis{q[0], q[1], q[2]};
  const auto t = view::cross(axis, p) * 2;
  return p + t * q[3] + view::cross(axis, t);
}
struct Bounds {
  view::Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
  void add(view::Vec3 p) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
  }
};
void boxPoints(Polygon& out, const view::OrbitCamera& cam, const Bounds& bounds,
               const Instance& instance) {
  std::array<view::Vec3, 8> corners;
  for (std::size_t j = 0; j < 8; ++j) {
    view::Vec3 p{j & 1 ? bounds.hi.x : bounds.lo.x, j & 2 ? bounds.hi.y : bounds.lo.y,
                 j & 4 ? bounds.hi.z : bounds.lo.z};
    p = {p.x * instance.scale[0], p.y * instance.scale[1], p.z * instance.scale[2]};
    corners[j] = rotate(p, instance.quat) +
                 view::Vec3{instance.center[0], instance.center[1], instance.center[2]};
  }
  const auto forward = view::normalize(cam.target - cam.eye());
  const auto project = [&](view::Vec3 p) {
    const auto q = view::transformPoint(cam.viewProj(16.f / 9), p);
    out.push_back({.5f + .5f * q.x, .5f + .5f * q.y});
  };
  for (std::size_t j = 0; j < 8; ++j)
    for (std::size_t bit : {1u, 2u, 4u})
      if (!(j & bit)) {
        auto a = corners[j], b = corners[j | bit];
        const float da = view::dot(a - cam.eye(), forward),
                    db = view::dot(b - cam.eye(), forward);
        if (da < cam.nearZ && db < cam.nearZ) continue;
        if (da < cam.nearZ) a = a + (b - a) * ((cam.nearZ - da) / (db - da));
        if (db < cam.nearZ)
          b = corners[j] + (b - corners[j]) * ((cam.nearZ - da) / (db - da));
        project(a);
        project(b);
      }
}
Polygon subjectHull(const Hologram& h, const view::OrbitCamera& cam) {
  Polygon points;
  const Bounds unit{{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}};
  for (const auto* group : {&h.quads, &h.lines})
    for (const auto& i : *group)
      if (i.color[3] > 0) boxPoints(points, cam, unit, i);
  static const auto meshes = MeshLibrary::build();
  for (std::size_t s = 0; s < h.tokenBatches.size(); ++s) {
    const auto batch = h.tokenBatches[s];
    if (!batch.count) continue;
    Bounds b;
    const auto range = meshes.ranges[s];
    for (std::uint32_t j = 0; j < range.indexCount; ++j) {
      const auto& v = meshes.vertices[static_cast<std::size_t>(range.vertexOffset) +
                                      meshes.indices[range.firstIndex + j]];
      b.add({v.pos[0], v.pos[1], v.pos[2]});
    }
    for (std::uint32_t j = 0; j < batch.count; ++j) {
      const auto& i = h.tokens[batch.first + j];
      if (i.color[3] > 0) boxPoints(points, cam, b, i);
    }
  }
  return hull(std::move(points));
}
HologramPlacement wallPlacement(const LabScene& lab) {
  const auto lens = *lab.doc().anchor("wallprojector.lens");
  return {lab.toWorld(wallHologramCentre(lens, kWallHologramRadiusMetres)),
          kWallHologramRadiusMetres * kCellsPerMetre,
          0,
          1,
          1,
          1,
          lab.toWorld(lens),
          true};
}
std::array<view::Vec3, 8> corners(const Instance& i) {
  std::array<view::Vec3, 8> out;
  for (std::size_t j = 0; j < 8; ++j) {
    const view::Vec3 p{(j & 1 ? .5f : -.5f) * i.scale[0],
                       (j & 2 ? .5f : -.5f) * i.scale[1],
                       (j & 4 ? .5f : -.5f) * i.scale[2]};
    out[j] = rotate(p, i.quat) + view::Vec3{i.center[0], i.center[1], i.center[2]};
  }
  return out;
}
Polygon partHull(const LabScene& lab, const view::OrbitCamera& cam,
                 const io::LabPart& part) {
  Bounds b{{-.5f, -.5f, -.5f}, {.5f, .5f, .5f}};
  if (part.mesh == io::LabMesh::File) {
    std::ifstream file(part.file);
    const std::string text((std::istreambuf_iterator<char>(file)), {});
    auto mesh = io::parseObj(text, part.file.string());
    REQUIRE(mesh);
    b = Bounds{};
    for (const auto& p : mesh->positions) b.add({p[0], p[1], p[2]});
  }
  Instance i;
  const auto centre = lab.toWorld(part.pos);
  i.center[0] = centre.x;
  i.center[1] = centre.y;
  i.center[2] = centre.z;
  for (std::size_t j = 0; j < 3; ++j) i.scale[j] = part.size[j] * kCellsPerMetre;
  const float rad = std::numbers::pi_v<float> / 180;
  const auto q =
      quatFromYawPitchRoll(part.rot[0] * rad, part.rot[1] * rad, part.rot[2] * rad);
  std::copy(q.begin(), q.end(), i.quat);
  Polygon points;
  boxPoints(points, cam, b, i);
  return hull(std::move(points));
}
}  // namespace
TEST_CASE("Library framing measures subjects and obstructing hardware",
          "[unit][render][lab][library-framing]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  const auto cam = *lab->station("library");
  float rackArea = 0;
  std::vector<Polygon> ceiling;
  for (const auto& p : doc->parts) {
    if (p.name == "magazine.frame") rackArea = area(partHull(*lab, cam, p));
    if (p.name.starts_with("lamp.")) {
      const float coverage = area(partHull(*lab, cam, p));
      CAPTURE(p.name, coverage);
      CHECK(coverage <= .01f);
    }
    if (p.name.starts_with("ceiling.") || p.name.starts_with("gantry.rail."))
      ceiling.push_back(partHull(*lab, cam, p));
  }
  const float ceilingArea = unionArea(ceiling);
  CAPTURE(rackArea, ceilingArea);
  CHECK(rackArea >= .07f);
  CHECK(rackArea <= .25f);
  CHECK(ceilingArea <= .08f);
  const auto theme = view::Theme::manifold();
  for (auto which : {app::Overture::Standard, app::Overture::Cylinder,
                     app::Overture::Torus, app::Overture::Cube5, app::Overture::Hyper4}) {
    const auto scene = overtureScene(which, 1, false, theme);
    const auto h = overtureHologram(scene, theme, wallPlacement(*lab));
    const auto polygon = subjectHull(h, cam);
    REQUIRE(!polygon.empty());
    Point lo{1, 1}, hi{0, 0};
    for (auto p : polygon)
      for (std::size_t j = 0; j < 2; ++j) {
        lo[j] = std::min(lo[j], p[j]);
        hi[j] = std::max(hi[j], p[j]);
      }
    const float coverage = area(polygon), height = hi[1] - lo[1];
    CAPTURE(which, coverage, height, lo[0], hi[0], lo[1], hi[1]);
    // M24.4e amends 9 % to 8.5 %: from a standing eye in front of the bench the
    // subject is further away than from M24.4c-3d-fix's eye on the bench.
    CHECK(coverage >= .085f);
    CHECK(height >= .22f);
    CHECK(lo[0] >= .02f);
    CHECK(hi[0] <= .56f);
    // A standing eye looks down, so the subject must not slip off the frame's foot.
    CHECK(lo[1] >= .02f);
    CHECK(hi[1] <= .98f);
    // M24.4e drops "larger than the rack": from in front of the bench the rack is
    // seen face-on to the right, mostly behind the Library pane.
  }
}
TEST_CASE("the Library's standing eye looks down on the wall board (M24.4e)",
          "[unit][render][lab][library-framing]") {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  const auto eye = lab->station("library")->eye();
  const auto at = wallPlacement(*lab);
  const auto theme = view::Theme::manifold();
  constexpr float deg = 180 / std::numbers::pi_v<float>;
  // Flat subjects: every 2-D board, before it forms. The line of sight to the board's
  // centre meets its plane at 20-40 degrees from the top side, and the centre sits below
  // the eye by at least 0.15 of the board's projected (vertical) height.
  for (auto which :
       {app::Overture::Standard, app::Overture::Cylinder, app::Overture::Torus}) {
    CAPTURE(which);
    const auto h = overtureHologram(overtureScene(which, 0, false, theme), theme, at);
    REQUIRE_FALSE(h.quads.empty());
    auto n = rotate({0, 0, 1}, h.quads.front().quat);
    if (n.z < 0) n = n * -1.f;
    float lo = 1e9f, hi = -1e9f;
    for (const auto& q : h.quads) {
      CHECK(std::abs(view::dot(rotate({0, 0, 1}, q.quat), n)) > .999f);
      for (const auto& c : corners(q)) {
        lo = std::min(lo, c.z);
        hi = std::max(hi, c.z);
      }
    }
    const float angle = std::asin(view::dot(n, view::normalize(eye - at.centre))) * deg;
    CAPTURE(angle, eye.z, at.centre.z, hi - lo);
    CHECK(angle >= 20);
    CHECK(angle <= 40);
    CHECK(at.centre.z <= eye.z - .15f * (hi - lo));
  }
  // Stacked and 3-D subjects, formed: the top layer is seen from above, at >= 10 degrees.
  for (auto which : {app::Overture::Cube5, app::Overture::Hyper4, app::Overture::Torus}) {
    CAPTURE(which);
    const auto h = overtureHologram(overtureScene(which, 1, false, theme), theme, at);
    REQUIRE_FALSE(h.quads.empty());
    float top = -1e9f;
    for (const auto& q : h.quads)
      if (q.color[3] > 0)
        for (const auto& c : corners(q)) top = std::max(top, c.z);
    const view::Vec3 topCentre{at.centre.x, at.centre.y, top};
    const float angle = std::asin((eye.z - top) / view::length(eye - topCentre)) * deg;
    CAPTURE(angle, eye.z, top);
    CHECK(angle >= 10);
  }
}
#endif
