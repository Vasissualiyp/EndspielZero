// SPDX-License-Identifier: GPL-3.0-or-later
//
// M25.3 boards with weight: a slab leaves and enters the rack only as a flat screen, and
// the shape's own sheet rides its slab while it travels.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#ifdef CB_HAVE_VULKAN
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <optional>
#include <string_view>

#include "app/screen_transition.hpp"
#include "app/session.hpp"
#include "render/lab_rig.hpp"
#include "support/variants.hpp"
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
/// The placements the rig stands in for a new game, with the shape view's centring for a
/// glued 2-D board (`shapeRigPlacements`, as `main.cpp` does in shape view).
std::vector<view::Placement> rigView(const char* name, bool shape) {
  auto s = app::Session::create(test::loadVariant(name));
  REQUIRE(s);
  return shape ? shapeRigPlacements((*s)->variant(), (*s)->placements())
               : (*s)->placements();
}
std::vector<view::Placement> afterAction(const char* variant, const char* line) {
  auto s = app::Session::create(test::loadVariant(variant));
  REQUIRE(s);
  auto a = app::parseAction((*s)->variant(), line);
  REQUIRE(a);
  REQUIRE((*s)->apply(*a));
  return (*s)->placements();
}
view::Vec3 rotate(const std::array<float, 4>& q, const view::Vec3& v) {
  const view::Vec3 axis{q[0], q[1], q[2]};
  const auto t = view::cross(axis, v) * 2;
  return v + t * q[3] + view::cross(axis, t);
}
/// Locked: clamped at its hang, or latched in its bay.
bool locked(const view::SlabPose& p) {
  return p.clamped || p.inBay;
}
}  // namespace

TEST_CASE("physical gantry loads obey speed and acceleration caps at 60 Hz (M25.3b)",
          "[render][rig][weight][gantryspeed]") {
  const auto lab = shippedLab();
  for (const auto& file :
       std::filesystem::directory_iterator(test::repoRoot() / "variants")) {
    if (file.path().extension() != ".toml") continue;
    const auto name = file.path().stem().string();
    for (bool shape : {false, true}) {
      CAPTURE(shape);
      const auto layout = rigLayout(lab, rigView(name.c_str(), shape));
      REQUIRE(layout);
      if (layout->slabs.size() > 8) continue;
      CAPTURE(name);
      CHECK(layout->lastLock + 1 <= 9.0f);
      for (const auto phase : {RigPhase::Deploy, RigPhase::Stow}) {
        const float end = phase == RigPhase::Deploy
                              ? layout->lastLock
                              : view::stowTravelSeconds(layout->slots);
        float speed = 0, vertical = 0, acceleration = 0;
        auto previous = rigPoses(*layout, phase, -1.0f / 60, 1);
        std::vector<view::Vec3> velocity(previous.size());
        const float metre = kCellsPerMetre * layout->xform.labScale;
        for (int i = 0; i <= static_cast<int>((end + .5f) * 60); ++i) {
          const auto poses = rigPoses(*layout, phase, static_cast<float>(i) / 60, 1);
          for (std::size_t k = 0; k < poses.size(); ++k) {
            const auto v = (poses[k].centre - previous[k].centre) * (60 / metre);
            speed = std::max(speed, view::length(v));
            vertical = std::max(vertical, std::abs(v.z));
            acceleration = std::max(acceleration, view::length(v - velocity[k]) * 60);
            velocity[k] = v;
          }
          previous = poses;
        }
        CAPTURE(phase, speed, vertical, acceleration);
        std::printf("GANTRY %s %s speed=%.5f vertical=%.5f acceleration=%.5f lock=%.3f\n",
                    name.c_str(), phase == RigPhase::Deploy ? "deploy" : "stow",
                    static_cast<double>(speed), static_cast<double>(vertical),
                    static_cast<double>(acceleration),
                    static_cast<double>(layout->lastLock));
        CHECK(speed <= .8f);
        CHECK(vertical <= .5f);
        CHECK(acceleration <= 1.5f);
      }
    }
  }
}

TEST_CASE("relayout loads obey gantry limits in lab metres at 60 Hz (M25.3b)",
          "[render][rig][weight][gantryspeed]") {
  const auto lab = shippedLab();
  for (const char* name : {"standard", "cube5", "hyper4", "hyper4_8x8"}) {
    const auto before = rigView(name, false);
    const auto after = afterAction(
        name, std::string_view(name) == "standard" ? "axes rank,file"
              : std::string_view(name) == "cube5"  ? "axes file,level"
                                                   : "axes file,aeon vertical");
    const auto from = rigLayout(lab, before);
    REQUIRE(from);
    const auto to = rigLayoutAfter(lab, *from, after);
    REQUIRE(to);
    const auto move = relayoutOf(lab, *from, before, *to, after);
    auto previous = relayoutFrame(lab, move, after, -1.f / 60, 1);
    std::vector<view::Vec3> velocities(previous.poses.size());
    float speed = 0, vertical = 0, acceleration = 0;
    for (int i = 0; i <= static_cast<int>((move.seconds + .5f) * 60); ++i) {
      const auto frame = relayoutFrame(lab, move, after, static_cast<float>(i) / 60, 1);
      REQUIRE(frame.poses.size() == previous.poses.size());
      for (std::size_t k = 0; k < frame.poses.size(); ++k) {
        const auto at = frame.layout.xform.toLab(frame.poses[k].centre);
        const auto was = previous.layout.xform.toLab(previous.poses[k].centre);
        const auto v = (at - was) * (60 / kCellsPerMetre);
        speed = std::max(speed, view::length(v));
        vertical = std::max(vertical, std::abs(v.z));
        acceleration = std::max(acceleration, view::length(v - velocities[k]) * 60);
        velocities[k] = v;
      }
      previous = frame;
    }
    CAPTURE(name, speed, vertical, acceleration);
    std::printf(
        "RELAYOUT %s speed=%.5f vertical=%.5f acceleration=%.5f seconds=%.3f "
        "scale=%.3f\n",
        name, static_cast<double>(speed), static_cast<double>(vertical),
        static_cast<double>(acceleration), static_cast<double>(move.seconds),
        static_cast<double>(move.timeScale));
    CHECK(speed <= .8f);
    CHECK(vertical <= .5f);
    CHECK(acceleration <= 1.5f);
  }
}

TEST_CASE("scale-changing rack arrivals and returns obey the caps (M25.3b)",
          "[render][rig][weight][gantryspeed]") {
  const auto lab = shippedLab();
  const auto small = rigView("standard", false);
  auto large = small;
  for (auto p : small) {
    p.cell += 100000;
    p.slice += 1;
    p.x += 80;
    large.push_back(p);
  }
  const auto from = rigLayout(lab, small);
  REQUIRE(from);
  const auto to = rigLayoutAfter(lab, *from, large);
  REQUIRE(to);
  REQUIRE(to->xform.labScale > from->xform.labScale + .1f);
  for (bool reverse : {false, true}) {
    auto move = reverse ? relayoutOf(lab, *to, large, *from, small)
                        : relayoutOf(lab, *from, small, *to, large);
    REQUIRE((reverse ? move.leaving : move.arrivals).size() == 1);
    move.lead = view::kRigUnformSeconds;
    const auto& placements = reverse ? small : large;
    auto previous = relayoutFrame(lab, move, placements, -1.f / 60, 1);
    std::vector<view::Vec3> velocities(previous.poses.size());
    float speed = 0, vertical = 0, acceleration = 0;
    for (int i = 0; i <= static_cast<int>((move.lead + move.seconds + .5f) * 60); ++i) {
      const auto frame =
          relayoutFrame(lab, move, placements, static_cast<float>(i) / 60, 1);
      REQUIRE(frame.poses.size() == previous.poses.size());
      for (std::size_t k = 0; k < frame.poses.size(); ++k) {
        const auto at = frame.layout.xform.toLab(frame.poses[k].centre);
        const auto was = previous.layout.xform.toLab(previous.poses[k].centre);
        const auto v = (at - was) * (60 / kCellsPerMetre);
        speed = std::max(speed, view::length(v));
        vertical = std::max(vertical, std::abs(v.z));
        acceleration = std::max(acceleration, view::length(v - velocities[k]) * 60);
        velocities[k] = v;
      }
      previous = frame;
    }
    CAPTURE(reverse, move.timeScale, speed, vertical, acceleration);
    std::printf(
        "RESCALE %s speed=%.5f vertical=%.5f acceleration=%.5f seconds=%.3f scale=%.3f\n",
        reverse ? "return" : "arrival", static_cast<double>(speed),
        static_cast<double>(vertical), static_cast<double>(acceleration),
        static_cast<double>(move.seconds), static_cast<double>(move.timeScale));
    CHECK(speed <= .8f);
    CHECK(vertical <= .5f);
    CHECK(acceleration <= 1.5f);
    const float lock =
        move.lead +
        (move.to.paths.back().departure + view::kSlabLockSeconds) * move.timeScale;
    if (!reverse) {
      CHECK(relayoutBootAge(move, lock - .01f, 1) == Catch::Approx(0));
      CHECK(relayoutBootAge(move, lock + .5f, 1) == Catch::Approx(.5f));
    }
  }
}

TEST_CASE("heavy shipped slabs keep moving and retain visible lean (M25.3b)",
          "[render][rig][weight][gantryspeed]") {
  const auto lab = shippedLab();
  for (const char* name : {"standard", "cube5"}) {
    const auto layout = rigLayout(lab, rigView(name, false));
    REQUIRE(layout);
    for (auto path : layout->paths) {
      for (bool reverse : {false, true}) {
        path.reversed = reverse;
        float lean = 0;
        for (int i = 0; i <= static_cast<int>(view::kSlabLockSeconds * 60); ++i) {
          const float u = static_cast<float>(i) / 60;
          const auto p = view::slabDeploy(path, path.departure + u, 1);
          if (u >= view::kSlabTiltEnd) {
            const auto up = rotate(p.quat, {0, 0, 1});
            lean = std::max(lean, std::atan2(std::hypot(up.x, up.y), up.z) * 57.29578f);
          }
          // After break-free and before the final settle there are no rest-to-rest
          // phase joins. Both playback directions follow these same centres.
          if (u > view::kSlabBreakFreeEnd + .2f && u < view::kSlabTravelEnd - .2f) {
            const auto prev = view::slabDeploy(path, path.departure + u - 1.f / 60, 1);
            const auto speed = view::length(p.centre - prev.centre) * 60 /
                               (kCellsPerMetre * layout->xform.labScale);
            CAPTURE(name, reverse, u, speed);
            CHECK(speed > .001f);
          }
        }
        CAPTURE(name, reverse, lean);
        std::printf("LEAN %s %s %.4f degrees\n", name, reverse ? "stow" : "deploy",
                    static_cast<double>(lean));
        CHECK(lean >= 1);
        CHECK(lean <= 2);
      }
    }
  }
}

TEST_CASE("a slab leaves and enters the rack only flat (M25.3)",
          "[render][rig][weight]") {
  const auto lab = shippedLab();
  struct Case {
    const char* name;
    bool shape;
  };
  for (const auto& c : {Case{"standard", false}, Case{"torus", true}, Case{"klein", true},
                        Case{"cube5", false}, Case{"hyper4", false}}) {
    CAPTURE(c.name);
    const auto layout = rigLayout(lab, rigView(c.name, c.shape));
    REQUIRE(layout);
    // Deploy: every sample at 60 Hz, through the form after the last lock.
    float formed = 0;
    for (int i = 0; i <= static_cast<int>((layout->lastLock + 2) * 60); ++i) {
      const float age = static_cast<float>(i) / 60;
      const auto poses = rigPoses(*layout, RigPhase::Deploy, age, 1);
      const float gate = view::deployFormGate(age, layout->lastLock, 1);
      for (std::size_t k = 0; k < poses.size(); ++k) {
        CAPTURE(age, k);
        if (!locked(poses[k])) CHECK(gate == 0.0f);
      }
      formed = gate;
    }
    CHECK(formed == 1.0f);
    // Stow from the shape view, both with the shutdown and for a finished game: the
    // shape unforms over the lead, before the first slab leaves.
    for (const float shutdown : {app::kStowShutdownSeconds, 0.0f}) {
      const float lead = view::stowLead(shutdown, true);
      const float end = lead + view::stowTravelSeconds(layout->slots) + .5f;
      for (int i = 0; i <= static_cast<int>(end * 60); ++i) {
        const float age = static_cast<float>(i) / 60;
        const auto poses = rigPoses(*layout, RigPhase::Stow, age - lead, 1);
        const float gate = view::stowFormGate(age, 1);
        for (std::size_t k = 0; k < poses.size(); ++k) {
          CAPTURE(shutdown, age, k);
          if (!locked(poses[k])) CHECK(gate == 0.0f);
        }
      }
    }
  }
}

TEST_CASE("a relayout unforms the shape before any slab moves (M25.3)",
          "[render][rig][weight]") {
  const auto lab = shippedLab();
  for (const auto& [variant, line] : std::vector<std::pair<const char*, const char*>>{
           {"hyper4", "axes file,aeon vertical"},
           {"cube5", "axes file,level"},
           {"standard", "axes rank,file"}}) {
    CAPTURE(variant, line);
    auto s = app::Session::create(test::loadVariant(variant));
    REQUIRE(s);
    const auto before = (*s)->placements();
    const auto after = afterAction(variant, line);
    const auto from = rigLayout(lab, before);
    REQUIRE(from);
    const auto to = rigLayoutAfter(lab, *from, after);
    REQUIRE(to);
    auto move = relayoutOf(lab, *from, before, *to, after);
    move.lead = view::kRigUnformSeconds;  // the shape view was formed when it began
    const float total = move.lead + move.seconds + view::kRigFormSeconds;
    int moving = 0;
    for (int i = 0; i <= static_cast<int>((total + .5f) * 60); ++i) {
      const float age = static_cast<float>(i) / 60;
      const auto frame = relayoutFrame(lab, move, after, age, 1);
      const float gate = view::relayoutFormGate(age, move.lead, move.seconds, 1);
      for (std::size_t k = 0; k < frame.poses.size(); ++k) {
        CAPTURE(age, k);
        if (!locked(frame.poses[k])) {
          ++moving;
          CHECK(gate == 0.0f);
        }
      }
      // Through the lead nothing has moved yet: the frame is the move's start.
      if (age < move.lead) {
        const auto start = relayoutFrame(
            lab,
            [&] {
              auto m = move;
              m.lead = 0;
              return m;
            }(),
            after, 0, 1);
        REQUIRE(start.poses.size() == frame.poses.size());
        for (std::size_t k = 0; k < frame.poses.size(); ++k)
          CHECK(view::length(frame.poses[k].centre - start.poses[k].centre) < 1e-5f);
      }
    }
    // A mirror (standard's rank,file) turns nothing: its slab never leaves its clamp.
    if (std::string_view(variant) != "standard") CHECK(moving > 0);
    CHECK(relayoutFrame(lab, move, after, move.lead + move.seconds + 1e-3f, 1).done);
    // The move ends only after the lead and the slabs' own travel.
    CHECK_FALSE(relayoutFrame(lab, move, after, move.lead + move.seconds - .05f, 1).done);
  }
}

TEST_CASE("the shape's flat sheet rides its travelling slab (M25.3)",
          "[render][rig][weight]") {
  const auto lab = shippedLab();
  const auto placements = rigView("torus", true);
  const auto layout = rigLayout(lab, placements);
  REQUIRE(layout);
  REQUIRE(layout->slabs.size() == 1);
  const auto& slab = layout->slabs[0];
  const view::Vec3 at{(slab.minX + slab.maxX) * .5f + 1.5f,
                      (slab.minY + slab.maxY) * .5f - 2.0f, slab.z};
  const auto setAt = [&] {
    InstanceSet set;
    Instance i;
    i.center[0] = at.x;
    i.center[1] = at.y;
    i.center[2] = at.z;
    i.scale[0] = i.scale[1] = i.scale[2] = 1;
    i.quat[3] = 1;
    set.instances.push_back(i);
    // The mesh's identity instance: its vertices are already in board space.
    Instance identity;
    identity.scale[0] = identity.scale[1] = identity.scale[2] = 1;
    identity.quat[3] = 1;
    set.surfaceInstance = static_cast<std::uint32_t>(set.instances.size());
    set.instances.push_back(identity);
    MeshVertex v{};
    v.pos[0] = at.x;
    v.pos[1] = at.y;
    v.pos[2] = at.z;
    v.normal[2] = 1;
    set.surfaceVertices.push_back(v);
    // A second vertex a cell away on the same sheet.
    v.pos[0] += 1;
    set.surfaceVertices.push_back(v);
    return set;
  };
  const auto& path = layout->paths[0];
  // Mid-travel, the vertex moves exactly as a board instance at the same place does.
  const float mid = path.departure + .5f * (view::kSlabTiltEnd + view::kSlabTravelEnd);
  for (const float age : {mid, path.departure + view::kSlabLockSeconds - .17f}) {
    CAPTURE(age);
    auto set = setAt();
    const auto poses = rigPoses(*layout, RigPhase::Deploy, age, 1);
    REQUIRE_FALSE(poses[0].clamped);
    carryBoard(set, *layout, poses);
    const auto& i = set.instances[0];
    const auto& v = set.surfaceVertices[0];
    CHECK(v.pos[0] == Catch::Approx(i.center[0]).margin(1e-4));
    CHECK(v.pos[1] == Catch::Approx(i.center[1]).margin(1e-4));
    CHECK(v.pos[2] == Catch::Approx(i.center[2]).margin(1e-4));
    CHECK(view::length(view::Vec3{v.pos[0], v.pos[1], v.pos[2]} - at) > 1e-2f);
    // Carried once: the mesh's identity instance stays the identity.
    const auto& id = set.instances[set.surfaceInstance];
    CHECK(id.center[0] == 0.0f);
    CHECK(id.center[1] == 0.0f);
    CHECK(id.center[2] == 0.0f);
    CHECK(id.quat[3] == 1.0f);
    // The normal turns with the slab and stays unit.
    const auto n = rotate(poses[0].quat, view::Vec3{0, 0, 1});
    CHECK(v.normal[0] == Catch::Approx(n.x).margin(1e-4));
    CHECK(v.normal[1] == Catch::Approx(n.y).margin(1e-4));
    CHECK(v.normal[2] == Catch::Approx(n.z).margin(1e-4));
  }
  // In its bay the sheet is not drawn: every vertex collapses to one point.
  {
    auto set = setAt();
    carryBoard(set, *layout, rigPoses(*layout, RigPhase::Deploy, 0, 1));
    const auto& a = set.surfaceVertices[0];
    const auto& b = set.surfaceVertices[1];
    CHECK(a.pos[0] == b.pos[0]);
    CHECK(a.pos[1] == b.pos[1]);
    CHECK(a.pos[2] == b.pos[2]);
  }
  // Clamped, nothing moves.
  {
    auto set = setAt();
    const auto before = set.surfaceVertices;
    carryBoard(set, *layout, rigPoses(*layout, RigPhase::Rest, 0, 1));
    CHECK(set.surfaceVertices[0].pos[0] == before[0].pos[0]);
    CHECK(set.surfaceVertices[0].pos[2] == before[0].pos[2]);
  }
}
#endif

#ifdef CB_HAVE_VULKAN
// Not a check: `endspielzero_tests '[.weightplot]'` writes the first slab's height, speed
// and lean through a deploy and a stow to $CB_WEIGHT_CSV, for the M25.3 plot.
TEST_CASE("M25.3 weight plot data", "[.weightplot]") {
  const char* path = std::getenv("CB_WEIGHT_CSV");
  if (path == nullptr) return;
  std::FILE* out = std::fopen(path, "w");
  REQUIRE(out != nullptr);
  std::fprintf(out, "variant,phase,t,height_m,speed_ms,lean_deg,acceleration_ms2\n");
  const auto lab = shippedLab();
  for (const char* name : {"standard", "cube5"}) {
    const auto layout = rigLayout(lab, rigView(name, false));
    REQUIRE(layout);
    const auto slab = layout->groups.front().slabs.front();
    const float m = kCellsPerMetre * layout->xform.labScale;
    const float hang = layout->paths[slab].hangCentre.z;
    for (const bool deploy : {true, false}) {
      const float lead = deploy ? 0.0f : app::kStowShutdownSeconds;
      const float end = deploy ? layout->lastLock + .5f
                               : lead + view::stowTravelSeconds(layout->slots) + .5f;
      std::optional<view::Vec3> last;
      view::Vec3 previousVelocity{};
      for (int i = 0; i <= static_cast<int>(end * 60); ++i) {
        const float t = static_cast<float>(i) / 60;
        const auto p = rigPoses(*layout, deploy ? RigPhase::Deploy : RigPhase::Stow,
                                deploy ? t : t - lead, 1)[slab];
        const auto velocity = last ? (p.centre - *last) * (60 / m) : view::Vec3{};
        const float speed = view::length(velocity);
        const float acceleration = view::length(velocity - previousVelocity) * 60;
        previousVelocity = velocity;
        last = p.centre;
        // Only face-up poses lean meaningfully; edge-on reports the tilt into the bay.
        const float lean = 2 * std::acos(std::min(1.0f, std::abs(p.quat[3]))) * 57.29578f;
        std::fprintf(out, "%s,%s,%.5f,%.5f,%.5f,%.4f,%.5f\n", name,
                     deploy ? "deploy" : "stow", static_cast<double>(t),
                     static_cast<double>((p.centre.z - hang) / m),
                     static_cast<double>(speed), static_cast<double>(lean),
                     static_cast<double>(acceleration));
      }
    }
  }
  std::fclose(out);
}
#endif
