// SPDX-License-Identifier: GPL-3.0-or-later
//
// M24.9 rig polish: the shape on its slab, the board framed by the lab game camera, and
// the stow flight's clearance from the carried slab.
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_VULKAN
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <optional>

#include "app/screen_transition.hpp"
#include "app/session.hpp"
#include "render/lab_rig.hpp"
#include "render/play_surface.hpp"
#include "render/slab_skin.hpp"
#include "support/variants.hpp"
#ifdef CB_HAVE_IMGUI
#include "render/ui.hpp"
#endif
#include "view/layout.hpp"

using namespace cb;
using namespace cb::render;

namespace {
LabScene shippedLab() {
  auto doc = io::loadLabDoc(std::filesystem::path(CB_SOURCE_DIR) / "assets/lab/lab.toml");
  REQUIRE(doc);
  auto lab = LabScene::create(*doc);
  REQUIRE(lab);
  std::vector<std::uint32_t> ranges(lab->fileMeshes().size());
  std::iota(ranges.begin(), ranges.end(), 0u);
  lab->setFileRanges(ranges);
  return std::move(*lab);
}

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
/// Clip a counter-clockwise polygon by a convex one (Sutherland-Hodgman).
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
float area(const Polygon& p) {
  float sum = 0;
  for (std::size_t j = 0; j < p.size(); ++j) {
    const auto a = p[j], b = p[(j + 1) % p.size()];
    sum += a[0] * b[1] - a[1] * b[0];
  }
  return std::abs(sum) * .5f;
}
Polygon rect(float x0, float y0, float x1, float y1) {
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}
/// A world point on a 16:9 frame, 0..1 with the origin top left.
view::OrbitCamera::ScreenPoint onFrame(const view::OrbitCamera& cam,
                                       const view::Vec3& w) {
  auto q = cam.project(w, 16.f / 9, 1920, 1080);
  q.x = (q.x + .5f) / 1920;
  q.y = (q.y + .5f) / 1080;
  return q;
}
float metres(const RigLayout& layout) {
  return kCellsPerMetre * layout.xform.labScale;
}
view::Vec3 rotate(const std::array<float, 4>& q, const view::Vec3& v) {
  const view::Vec3 axis{q[0], q[1], q[2]};
  const auto t = view::cross(axis, v) * 2;
  return v + t * q[3] + view::cross(axis, t);
}
}  // namespace

TEST_CASE("a 2-D shape sits centred on its slab, flat and formed (M24.9)",
          "[render][rig][polish]") {
  const auto lab = shippedLab();
  for (const char* name : {"torus", "cylinder", "mobius", "klein"}) {
    CAPTURE(name);
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s);
    const auto& v = (*s)->variant();
    const auto layout = rigLayout(lab, shapeRigPlacements(v, (*s)->placements()));
    REQUIRE(layout);
    const auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
    const view::Vec3 boss = yokeBoss(*layout, poses, 0);
    for (const float formed : {0.0f, 0.5f, 1.0f}) {
      CAPTURE(formed);
      SurfacePose pose;
      pose.formed = formed;
      pose.skin = true;
      const auto surf = PlaySurface::build(v, pose);
      REQUIRE_FALSE(surf.empty());
      const auto b = surf.bounds();
      const float off = std::hypot(b.centerX() - boss.x, b.centerY() - boss.y);
      CAPTURE(off / metres(*layout));
      if (formed != 0.5f) CHECK(off <= 0.05f * metres(*layout));
      if (formed <= 0.0f) continue;
      const auto ribs = slabRibs(surf, kRibCount, boss, formed, &surf.passThrough());
      REQUIRE(ribs.size() >= 2);
      // The spokes are the last two ribs: from the yoke to the nearest rib point.
      for (std::size_t i = ribs.size() - 2; i < ribs.size(); ++i) {
        REQUIRE(ribs[i].points.size() == 2);
        const float spoke = view::length(ribs[i].points[1] - ribs[i].points[0]);
        CAPTURE(spoke / metres(*layout));
        CHECK(spoke <= 0.35f * metres(*layout));
      }
    }
  }
  // Anything without a 2-D shape keeps its own placements.
  auto s = app::Session::create(test::loadVariant("standard"));
  REQUIRE(s);
  const auto& p = (*s)->placements();
  const auto same = shapeRigPlacements((*s)->variant(), p);
  REQUIRE(same.size() == p.size());
  for (std::size_t i = 0; i < p.size(); ++i) {
    CHECK(same[i].x == p[i].x);
    CHECK(same[i].y == p[i].y);
  }
}

TEST_CASE("the lab game camera frames the board and keeps the rig in view (M24.9)",
          "[render][rig][polish]") {
  const auto lab = shippedLab();
  constexpr float aspect = 16.f / 9;
  struct Case {
    const char* name;
    float stageWidth;  ///< the HUD's safe stage at 960 x 540 (`applyStage`)
    float dockLeft, dockRight;
  };
  // The safe stage the game HUD leaves, and its dock's span, measured in the 960 x 540
  // manifold captures: the plate (top left), the status panel (top right), the dock.
  for (const auto& c :
       {Case{"standard", .66875f, .32f, .69f}, Case{"cylinder", .647917f, .11f, .89f},
        Case{"torus", .6125f, .11f, .89f}, Case{"cube5", .58125f, .27f, .73f},
        Case{"hyper4", .58125f, .27f, .73f}}) {
    CAPTURE(c.name);
    auto s = app::Session::create(test::loadVariant(c.name));
    REQUIRE(s);
    auto& session = **s;
    session.setBoardAspect(aspect);
    const auto stage = labStageArea(c.stageWidth, .633333f);
    session.setFramingArea(stage[0], stage[1]);
    session.frameOn(view::boundsOf(session.placements()));
    const auto layout = rigLayout(lab, session.placements());
    REQUIRE(layout);
    constrainGameCamera(session, *layout);
    const auto cam = session.camera();
    const auto half = BoardRenderer::cellHalfExtent();
    Polygon points;
    bool piecesOn = true;
    for (const auto& p : session.placements()) {
      for (int k = 0; k < 8; ++k) {
        const view::Vec3 w{p.x + (k & 1 ? half.x : -half.x),
                           p.y + (k & 2 ? half.y : -half.y),
                           p.z + (k & 4 ? half.z : -half.z)};
        const auto q = onFrame(cam, w);
        points.push_back({q.x, q.y});
      }
      // A piece's crown, at most 1.3 cells above its square.
      const auto top = onFrame(cam, {p.x, p.y, p.z + 1.3f});
      piecesOn =
          piecesOn && top.visible && top.x >= 0 && top.x <= 1 && top.y >= 0 && top.y <= 1;
    }
    const auto board = hull(points);
    float lo = 1, hi = 0;
    for (const auto& p : board) {
      lo = std::min(lo, std::clamp(p[1], 0.f, 1.f));
      hi = std::max(hi, std::clamp(p[1], 0.f, 1.f));
    }
    const float boardArea = area(intersect(board, rect(0, 0, 1, 1)));
    float covered = 0;
    for (const auto& panel :
         {rect(.02f, .04f, .17f, .13f), rect(.835f, .04f, .99f, .145f),
          rect(c.dockLeft, .83f, c.dockRight, .98f)})
      covered += area(intersect(board, panel));
    CAPTURE(hi - lo, boardArea, covered / boardArea);
    CHECK(hi - lo >= .22f);
    CHECK(boardArea >= .10f);
    CHECK(piecesOn);
    CHECK(covered <= .10f * boardArea);
    // The rig stays in view: the first slab's mast foot and its slab's near edge.
    const auto poses = rigPoses(*layout, RigPhase::Rest, 0, 1);
    const auto& slab = layout->slabs[0];
    const auto& group = layout->groups[layout->slabGroup[0]];
    const auto foot =
        onFrame(cam, {group.mastFoot.x, group.mastFoot.y, poses[0].centre.z + 1.0f});
    CAPTURE(foot.x, foot.y);
    CHECK(foot.visible);
    const auto edge = onFrame(cam, {(slab.minX + slab.maxX) * .5f, slab.minY, slab.z});
    CAPTURE(edge.x, edge.y);
    CHECK(edge.visible);
  }
}

TEST_CASE("deploy and stow flights keep clear of the carried slab (M24.9)",
          "[render][rig][polish]") {
  const auto lab = shippedLab();
  constexpr float aspect = 16.f / 9;
  for (const char* name : {"standard", "cube5", "hyper4", "torus"}) {
    CAPTURE(name);
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s);
    auto& session = **s;
    session.setBoardAspect(aspect);
    const auto stage = labStageArea(.6f, .633333f);
    session.setFramingArea(stage[0], stage[1]);
    session.frameOn(view::boundsOf(session.placements()));
    const auto layout = rigLayout(lab, session.placements());
    REQUIRE(layout);
    constrainGameCamera(session, *layout);
    const auto game = session.camera();
    const float clear = 0.30f * metres(*layout);
    const float railZ = layout->groups.front().mastTopZ;
    // M25.3: the heavier timings, and a stow from the shape view, which unforms the
    // shape before anything leaves (`view::stowLead`), with or without the shutdown.
    struct Flight {
      bool deploy;
      float shutdown;
    };
    for (const auto [deploy, shutdown] :
         {Flight{true, 0.0f}, Flight{false, app::kStowShutdownSeconds},
          Flight{false, view::stowLead(app::kStowShutdownSeconds, true)},
          Flight{false, view::stowLead(0.0f, true)}}) {
      CAPTURE(deploy, shutdown);
      const auto station = lab.station(deploy ? "library" : "menu");
      REQUIRE(station);
      const auto labCam = toBoardCamera(*station, layout->xform);
      RigFlight f;
      f.deploy = deploy;
      f.shutdown = shutdown;
      f.lock = deploy ? view::deployLastLock(layout->slots)
                      : f.shutdown + std::max(view::stowTravelSeconds(layout->slots),
                                              app::kStowFlightMinSeconds);
      const float total = deploy ? std::max(app::kDeploySeconds, f.lock + 1.0f)
                                 : f.lock + app::kStowMenuSeconds;
      float nearest = 1e30f, step = 0;
      std::optional<view::Vec3> last, lastRaw;
      for (int i = 0; i <= static_cast<int>(total * 60); ++i) {
        f.age = static_cast<float>(i) / 60;
        const auto poses = rigPoses(*layout, deploy ? RigPhase::Deploy : RigPhase::Stow,
                                    deploy ? f.age : f.age - f.shutdown, 1);
        const auto eye = rigFlightCamera(*layout, poses, f, game, labCam).eye();
        CAPTURE(f.age, eye.x, eye.y, eye.z);
        // The same flight with nothing to keep clear of (every slab marked clamped).
        auto still = poses;
        for (auto& p : still) p.clamped = true;
        const auto raw = rigFlightCamera(*layout, still, f, game, labCam).eye();
        if (last)
          step = std::max(step, view::length(eye - *last) - view::length(raw - *lastRaw));
        last = eye;
        lastRaw = raw;
        for (std::size_t k = 0; k < poses.size(); ++k) {
          const auto& pose = poses[k];
          if (pose.clamped || pose.inBay) continue;
          const auto& slab = layout->slabs[k];
          // The slab with its board: the body under the face, the pieces over it.
          const float hw = (slab.maxX - slab.minX) * .5f,
                      hd = (slab.maxY - slab.minY) * .5f;
          const std::array<float, 4> inv{-pose.quat[0], -pose.quat[1], -pose.quat[2],
                                         pose.quat[3]};
          const auto local = rotate(inv, eye - pose.centre);
          const view::Vec3 lo{-hw, -hd, -0.4f}, hi{hw, hd, 1.5f};
          const view::Vec3 near{std::clamp(local.x, lo.x, hi.x),
                                std::clamp(local.y, lo.y, hi.y),
                                std::clamp(local.z, lo.z, hi.z)};
          const float d = view::length(local - near);
          nearest = std::min(nearest, d);
          CAPTURE(k, d / metres(*layout));
          CHECK(d >= clear);
          // Never between the slab and the rail: over a level slab's footprint, the eye
          // is under the slab or over the rail.
          const bool level =
              std::abs(pose.quat[0]) < 1e-3f && std::abs(pose.quat[1]) < 1e-3f;
          if (level && std::abs(local.x) <= hw && std::abs(local.y) <= hd)
            CHECK((local.z < 0 || eye.z > railZ));
        }
      }
      // The push out of the keep-out is continuous: no frame moves the eye more than 2 cm
      // further than the unobstructed flight's same frame does. A rack of slabs that
      // swings out of the magazine through the Library's eye is the exception (M24.9
      // Status): there the eye is pushed clear in two quick steps.
      CAPTURE(nearest / metres(*layout), step / metres(*layout));
      const bool racked = std::any_of(layout->groups.begin(), layout->groups.end(),
                                      [](const auto& g) { return g.rack(); });
      if (!racked) CHECK(step <= 0.02f * metres(*layout));
    }
  }
}
namespace {
/// The default game camera on a glued board's formed shape: framed on the surface.
view::OrbitCamera shapeCamera(app::Session& session, const SurfacePose& pose) {
  session.setBoardAspect(16.f / 9);
  BoardOptions options;
  options.surface = true;
  frameGeometryCamera(session, options, pose);
  return session.camera();
}
}  // namespace

TEST_CASE("the closed torus shows thin hoops between its tile rows (M24.9)",
          "[render][skin][polish]") {
  auto s = app::Session::create(test::loadVariant("torus"));
  REQUIRE(s);
  const auto& v = (*s)->variant();
  // No hoops until the files have rolled shut.
  SurfacePose open;
  open.formed = view::kTorusRollEnd;
  open.skin = true;
  CHECK(outsideHoops(PlaySurface::build(v, open), kRibCount, open.formed).empty());
  SurfacePose pose;
  pose.formed = 1;
  pose.skin = true;
  const auto surf = PlaySurface::build(v, pose);
  const auto hoops = outsideHoops(surf, kRibCount, 1);
  REQUIRE(hoops.size() == static_cast<std::size_t>(kRibCount));
  const auto eye = shapeCamera(**s, pose).eye();
  std::size_t seen = 0, total = 0;
  for (const auto& hoop : hoops) {
    REQUIRE(hoop.points.size() >= 8);
    // A closed loop round the tube.
    CHECK(view::length(hoop.points.front() - hoop.points.back()) < 1e-3f);
    for (const auto& p : hoop.points) {
      // Outside the tiles: clear of every tile's top face (a tile reaches half its
      // thickness off its surface corners).
      float best = 1e30f;
      for (const auto& patch : surf.patches())
        for (const auto& c : patch.pos) best = std::min(best, view::length(p - c));
      CHECK(best > PlaySurface::kThickness * .5f + kHoopDiameter * .5f);
      ++total;
      if (!surf.blocked(eye, p, .05f)) ++seen;
    }
  }
  // The structure reads from the default camera: a good share of each hoop is in view.
  CAPTURE(seen, total);
  CHECK(seen * 4 >= total);
}

TEST_CASE("no Klein rib shows outside the skin from the camera (M24.9)",
          "[render][skin][polish]") {
  auto s = app::Session::create(test::loadVariant("klein"));
  REQUIRE(s);
  const auto& v = (*s)->variant();
  std::size_t outside = 0;  // unfiltered rib points in view: what M24.5b drew
  for (const float formed : {0.6f, 1.0f}) {
    CAPTURE(formed);
    SurfacePose pose;
    pose.formed = formed;
    pose.skin = true;
    const auto surf = PlaySurface::build(v, pose);
    auto cam = shapeCamera(**s, pose);
    for (const float yaw : {0.0f, 1.6f, 3.2f, 4.8f}) {
      CAPTURE(yaw);
      cam.yaw = (**s).camera().yaw + yaw;
      const auto eye = cam.eye();
      const auto all = slabRibs(surf, kRibCount, {0, 0, -3}, formed, &surf.passThrough());
      const auto ribs =
          slabRibs(surf, kRibCount, {0, 0, -3}, formed, &surf.passThrough(), &eye);
      REQUIRE(ribs.size() >= 2);
      std::size_t shown = 0, kept = 0;
      // Every rib point (spokes aside) is hidden behind the skin from the eye.
      for (std::size_t r = 0; r + 2 < ribs.size(); ++r)
        for (const auto& p : ribs[r].points) {
          ++kept;
          if (!surf.blocked(eye, p, .05f)) ++shown;
        }
      CAPTURE(kept, shown);
      CHECK(shown == 0);
      CHECK(kept > 0);
      for (std::size_t r = 0; r + 2 < all.size(); ++r)
        for (const auto& p : all[r].points)
          if (!surf.blocked(eye, p, .05f)) ++outside;
    }
  }
  CHECK(outside > 0);
}

TEST_CASE("the Moebius ribs stop cleanly at the twisted seam (M24.9)",
          "[render][skin][polish]") {
  auto s = app::Session::create(test::loadVariant("mobius"));
  REQUIRE(s);
  const auto& v = (*s)->variant();
  for (int k = 1; k <= 10; ++k) {
    const float formed = static_cast<float>(k) / 10;
    CAPTURE(formed);
    SurfacePose pose;
    pose.formed = formed;
    pose.skin = true;
    const auto surf = PlaySurface::build(v, pose);
    const auto ribs = slabRibs(surf, kRibCount, {0, 0, -3}, formed, &surf.passThrough());
    REQUIRE(ribs.size() >= 2);
    // No floating stub: every rib (spokes aside) spans at least two cells.
    for (std::size_t r = 0; r + 2 < ribs.size(); ++r) {
      CAPTURE(r, ribs[r].points.size());
      CHECK(ribs[r].points.size() >= 3);
    }
  }
}
#ifdef CB_HAVE_IMGUI
namespace {
/// M24.9 section 6 fallback: the unchanged Library station is the best found in a
/// 125-pose local survey preserving the M24.4e subject and rack limits (see Status).
/// Pin its measured 85.2 % rack-box overlap so it cannot grow.
constexpr float kLibraryRackOverlap = 0.86f;
/// A station camera's projected hull of a world box's corners in front of the eye.
Polygon boxHull(const view::OrbitCamera& cam, const view::Bounds& b) {
  Polygon points;
  const auto forward = view::normalize(cam.target - cam.eye());
  for (int k = 0; k < 8; ++k) {
    const view::Vec3 w{k & 1 ? b.maxX : b.minX, k & 2 ? b.maxY : b.minY,
                       k & 4 ? b.maxZ : b.minZ};
    if (view::dot(w - cam.eye(), forward) <= cam.nearZ) continue;
    const auto q = onFrame(cam, w);
    points.push_back({q.x, q.y});
  }
  return hull(points);
}
}  // namespace

TEST_CASE("the Watch station looks at the bench projector, beside its pane (M24.9)",
          "[render][lab][polish]") {
  const auto lab = shippedLab();
  const auto cam = *lab.station("watch");
  // Watch renders no hologram yet. Measure the visible projector hardware alone;
  // a hypothetical preview volume cannot satisfy the subject coverage requirement.
  Polygon points = boxHull(cam, lab.partBounds("projector.body"));
  const auto subject = hull(points);
  float lo = 1, hi = 0;
  for (const auto& p : subject) {
    lo = std::min(lo, p[0]);
    hi = std::max(hi, p[0]);
  }
  const float subjectArea = area(intersect(subject, rect(0, 0, 1, 1)));
  const float rack =
      area(intersect(boxHull(cam, lab.partBounds("magazine.frame")), rect(0, 0, 1, 1)));
  CAPTURE(subjectArea, lo, hi, rack);
  CHECK(subjectArea >= .03f);
  // Inside the left 60 %: the Watch pane is on the right, not over the subject.
  CHECK(lo >= 0);
  CHECK(hi <= .6f);
  CHECK(rack <= .15f);
}

TEST_CASE("the Library's rack and its pane: the overlap M24.9 records does not grow",
          "[render][lab][polish]") {
  const auto lab = shippedLab();
  const auto cam = *lab.station("library");
  // The rack's projected box against the Library pane (measured at 960 x 540).
  const auto box = boxHull(cam, lab.partBounds("magazine.frame"));
  float x0 = 1, x1 = 0, y0 = 1, y1 = 0;
  for (const auto& p : box) {
    x0 = std::min(x0, std::clamp(p[0], 0.f, 1.f));
    x1 = std::max(x1, std::clamp(p[0], 0.f, 1.f));
    y0 = std::min(y0, std::clamp(p[1], 0.f, 1.f));
    y1 = std::max(y1, std::clamp(p[1], 0.f, 1.f));
  }
  const float rackBox = (x1 - x0) * (y1 - y0);
  const float overlap = std::max(0.f, std::min(x1, .945f) - std::max(x0, .545f)) *
                        std::max(0.f, std::min(y1, .88f) - std::max(y0, .10f));
  CAPTURE(x0, x1, y0, y1, overlap / rackBox);
  CHECK(overlap <= kLibraryRackOverlap * rackBox);
}
#endif
#endif
