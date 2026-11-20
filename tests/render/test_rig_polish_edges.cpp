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

float metres(const RigLayout& layout) {
  return kCellsPerMetre * layout.xform.labScale;
}
view::Vec3 rotate(const std::array<float, 4>& q, const view::Vec3& v) {
  const view::Vec3 axis{q[0], q[1], q[2]};
  const auto t = view::cross(axis, v) * 2;
  return v + t * q[3] + view::cross(axis, t);
}
}  // namespace

TEST_CASE("torus hardware stays outside when the playing side is inverted (M24.9)",
          "[render][skin][polishedges]") {
  auto session = app::Session::create(test::loadVariant("torus"));
  REQUIRE(session);
  for (const float formed : {0.8f, 1.0f}) {
    for (const float slide : {0.0f, 1.3f}) {
      CAPTURE(formed, slide);
      SurfacePose pose;
      pose.skin = true;
      pose.formed = formed;
      pose.slideU = slide;
      const auto outside = outsideHoops(PlaySurface::build((*session)->variant(), pose),
                                        kRibCount, formed);
      pose.evert = 1;
      const auto inverted = outsideHoops(PlaySurface::build((*session)->variant(), pose),
                                         kRibCount, formed);
      REQUIRE_FALSE(outside.empty());
      REQUIRE(outside.size() == inverted.size());
      for (std::size_t r = 0; r < outside.size(); ++r) {
        REQUIRE(outside[r].points.size() == inverted[r].points.size());
        for (std::size_t p = 0; p < outside[r].points.size(); ++p)
          CHECK(view::length(outside[r].points[p] - inverted[r].points[p]) < 1e-4f);
      }
    }
  }
}

TEST_CASE("finished-game stow keeps clear without a shutdown lead-in (M24.9)",
          "[render][rig][polishedges]") {
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
    for (const bool deploy : {false}) {
      CAPTURE(deploy);
      const auto station = lab.station(deploy ? "library" : "menu");
      REQUIRE(station);
      const auto labCam = toBoardCamera(*station, layout->xform);
      RigFlight f;
      f.deploy = deploy;
      f.shutdown = 0.0f;
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
#endif
