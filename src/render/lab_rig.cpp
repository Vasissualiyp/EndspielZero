// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/lab_rig.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "app/session.hpp"
#include "render/play_surface.hpp"
#include "render/slab_skin.hpp"
#include "view/board_boot.hpp"
#include "view/rig_move.hpp"

namespace cb::render {
namespace {
constexpr float kBodyBelowFace = 0.13f;  ///< cell centre to the slab body's centre
constexpr float kBodyHalf = 0.185f;      ///< half the slab's thickness, back to cell top
constexpr float kMinMastCells = 0.62f * kCellsPerMetre;
constexpr float kTubeCells = 0.6f * kCellsPerMetre;    ///< one tube's length
constexpr float kReachCells = 1.15f * kCellsPerMetre;  ///< both tubes, overlapped
constexpr float kGlowLiftCells = 0.12f * kCellsPerMetre;
constexpr float kGlowRadiusMetres = 2.5f;
constexpr std::array<float, 4> kIdentity{0, 0, 0, 1};
constexpr float kTBarSection = 1.3f;  ///< a rack's T-bar, x the yoke's 0.03 m section

float smooth(float x) noexcept {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * (3 - 2 * x);
}
view::Vec3 rotate(const std::array<float, 4>& q, const view::Vec3& v) noexcept {
  const view::Vec3 axis{q[0], q[1], q[2]};
  const auto t = view::cross(axis, v) * 2;
  return v + t * q[3] + view::cross(axis, t);
}
float radians(float d) noexcept {
  return d * std::numbers::pi_v<float> / 180;
}
view::Bounds boardBounds(const view::Bounds& lab, const view::RigXform& x) noexcept {
  const auto lo = x.toBoard({lab.minX, lab.minY, lab.minZ});
  const auto hi = x.toBoard({lab.maxX, lab.maxY, lab.maxZ});
  return {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z};
}
/// The world box of a local box (lo..hi) turned by `q` and centred at `c`.
view::Bounds turnedBox(const view::Vec3& c, const view::Vec3& lo, const view::Vec3& hi,
                       const std::array<float, 4>& q) noexcept {
  view::Bounds b{1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f};
  for (int i = 0; i < 8; ++i) {
    const auto w =
        c + rotate(q, {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z});
    b = {std::min(b.minX, w.x), std::min(b.minY, w.y), std::min(b.minZ, w.z),
         std::max(b.maxX, w.x), std::max(b.maxY, w.y), std::max(b.maxZ, w.z)};
  }
  return b;
}
/// A template's own box relative to its origin (templates are authored unrotated).
std::pair<view::Vec3, view::Vec3> localBox(const LabScene& lab, std::string_view name,
                                           const std::array<float, 3>& mul) {
  const auto* part = lab.part(name);
  const auto b = lab.partBounds(name);
  const auto o = lab.toWorld(part->pos);
  return {{(b.minX - o.x) * mul[0], (b.minY - o.y) * mul[1], (b.minZ - o.z) * mul[2]},
          {(b.maxX - o.x) * mul[0], (b.maxY - o.y) * mul[1], (b.maxZ - o.z) * mul[2]}};
}
std::string bayName(const LabScene& lab, int slot) {
  return lab.doc().deploy->slabBay + "." + std::to_string(slot);
}
view::Vec3 partCentre(const LabScene& lab, const view::RigXform& x,
                      std::string_view name) {
  const auto* p = lab.part(name);
  return p ? x.toBoard(lab.toWorld(p->pos)) : view::Vec3{};
}
}  // namespace

namespace {
/// The rig for `placements` with the lab at `xform` (placed to fit when null) and, when
/// given, these mast groups (a relayout keeps its groups while the lab eases).
std::optional<RigLayout> build(const LabScene& lab,
                               const std::vector<view::Placement>& placements,
                               const view::RigXform* xform,
                               const std::vector<view::MastGroup>* fixedGroups) {
  const auto& doc = lab.doc();
  if (!doc.deploy || placements.empty()) return std::nullopt;
  const auto& d = *doc.deploy;
  RigLayout out;
  out.volume.min = lab.toWorld(d.min);
  out.volume.max = lab.toWorld(d.max);
  out.volume.railZ = lab.toWorld({0, 0, d.railZ}).z;
  out.volume.mastEdgeGap = d.mastEdgeGap * kCellsPerMetre;
  out.volume.yokeDrop = d.yokeDrop * kCellsPerMetre;
  out.layoutBounds = view::boundsOf(placements);
  out.xform = xform ? *xform : view::placeLayout(out.layoutBounds, out.volume);
  out.slabs = view::slabsOf(placements, kBezelMargin);
  // Masts too close for their carriages merge into racks (M24.6); every layout travels.
  if (fixedGroups) {
    out.groups = *fixedGroups;
    for (auto& g : out.groups) g.mastTopZ = out.xform.toBoard({0, 0, out.volume.railZ}).z;
  } else {
    out.groups = view::routeGroups(view::mastGroups(out.slabs, out.xform, out.volume),
                                   out.volume, out.xform.labScale);
  }
  out.room = boardBounds(lab.roomBounds(), out.xform);
  out.bayPrefix = d.slabBay;
  if (const auto* bay = lab.part(bayName(lab, 0))) {
    out.bayWidth = bay->size[0] * kCellsPerMetre * out.xform.labScale;
    out.bayDepth = bay->size[1] * kCellsPerMetre * out.xform.labScale;
  }

  std::map<std::pair<std::uint32_t, long>, std::uint32_t> slabOf;
  for (std::uint32_t i = 0; i < out.slabs.size(); ++i)
    slabOf[{out.slabs[i].slice,
            std::lround(static_cast<double>(out.slabs[i].z) * 1000)}] = i;
  CellId maxCell = 0;
  for (const auto& p : placements) maxCell = std::max(maxCell, p.cell);
  out.cellSlab.assign(static_cast<std::size_t>(maxCell) + 1, 0);
  for (const auto& p : placements) {
    // A level's slab is the one with its face height; cells below it in a column of
    // levels belong to their own level.
    std::uint32_t best = 0;
    float bestDz = 1e30f;
    for (std::uint32_t i = 0; i < out.slabs.size(); ++i)
      if (out.slabs[i].slice == p.slice && std::abs(out.slabs[i].z - p.z) < bestDz) {
        bestDz = std::abs(out.slabs[i].z - p.z);
        best = i;
      }
    out.cellSlab[p.cell] = best;
  }

  out.slabGroup.assign(out.slabs.size(), 0);
  for (std::uint32_t g = 0; g < out.groups.size(); ++g)
    for (const auto s : out.groups[g].slabs) out.slabGroup[s] = g;

  // Rails: the frame's x extent from the Y rails, its y extent from the X rails.
  const auto& x = out.xform;
  float railMinY = x.toBoard(out.volume.min).y, railMaxY = x.toBoard(out.volume.max).y;
  if (lab.part("gantry.rail.x.a") && lab.part("gantry.rail.x.b")) {
    railMinY = partCentre(lab, x, "gantry.rail.x.a").y;
    railMaxY = partCentre(lab, x, "gantry.rail.x.b").y;
  }
  const float pickX = x.toBoard(out.volume.max).x;
  const auto park = partCentre(lab, x, "gantry.carriage.0");
  const float yokeBelow = view::kSlabBelowFace - kBodyBelowFace + out.volume.yokeDrop;

  // A rack's frame runs in the gaps between its slabs: each slab's arm stands beyond its
  // far edge (halfway to the next slab behind it), its T-bar runs along the row of arms
  // to a spine in the column gap nearest the rack's middle, and the mast stands on the
  // spine beyond the farthest row. Every bar stays out from under the slabs.
  out.arm.assign(out.slabs.size(), {});
  for (auto& group : out.groups) {
    for (const auto s : group.slabs) {
      const auto& slab = out.slabs[s];
      float next = 1e30f;
      for (const auto o : group.slabs)
        if (o != s && out.slabs[o].minY > slab.maxY && out.slabs[o].minX < slab.maxX &&
            out.slabs[o].maxX > slab.minX)
          next = std::min(next, out.slabs[o].minY);
      out.arm[s] = {
          (slab.minX + slab.maxX) * .5f,
          std::min(slab.maxY + out.volume.mastEdgeGap, (slab.maxY + next) * .5f), 0};
    }
    if (!group.rack()) {
      for (const auto s : group.slabs)
        out.arm[s] = {group.mastFoot.x, group.mastFoot.y, 0};
      continue;
    }
    if (fixedGroups) continue;
    std::vector<std::pair<float, float>> spans;
    float far = -1e30f, mid = 0;
    for (const auto s : group.slabs) {
      spans.emplace_back(out.slabs[s].minX, out.slabs[s].maxX);
      far = std::max(far, out.arm[s].y);
      mid += out.arm[s].x / static_cast<float>(group.slabs.size());
    }
    std::sort(spans.begin(), spans.end());
    // The free x gaps between the slabs' x spans; beside the rack if there is none.
    float spine = spans.back().second + out.volume.mastEdgeGap, best = 1e30f;
    float reach = spans.front().second;
    for (std::size_t i = 1; i < spans.size(); ++i) {
      if (spans[i].first > reach) {
        const float gap = (reach + spans[i].first) * .5f;
        if (std::abs(gap - mid) < best) {
          best = std::abs(gap - mid);
          spine = gap;
        }
      }
      reach = std::max(reach, spans[i].second);
    }
    group.mastFoot.x = spine;
    group.mastFoot.y = far;
  }

  // Departures: a column of carriages (one bridge) leaves together, nearest the
  // magazine first, the slots spread over at most 1.2 s (M24.6).
  const auto departures =
      view::departureOrder(out.groups, partCentre(lab, x, bayName(lab, 0)));
  out.groupSlot = departures.slot;
  out.slots = std::max(1, departures.count);
  out.lastLock = view::deployLastLock(out.slots);
  const float step = view::departureStep(out.slots);

  out.bay.assign(out.slabs.size(), -1);
  out.paths.resize(out.slabs.size());
  for (std::uint32_t g = 0; g < out.groups.size(); ++g) {
    const auto& group = out.groups[g];
    const auto& top = out.slabs[group.slabs.front()];
    const int topSlot = static_cast<int>(group.slabs.front() % 8);
    const auto topBay = partCentre(lab, x, bayName(lab, topSlot));
    // The lowest slab's yoke rides just under the rail; the stack keeps its spacing.
    const float bottomZ = out.slabs[group.slabs.back()].z;
    const float liftBottom = group.mastTopZ - kMinMastCells + yokeBelow;
    // A rack is carried rigidly: each slab keeps its offset from the rack's mast, and the
    // farthest +X slab is picked up where a single slab would be.
    const auto offsetOf = [&](std::uint32_t s) {
      const auto& slab = out.slabs[s];
      return view::Vec3{(slab.minX + slab.maxX) * .5f - group.mastFoot.x,
                        (slab.minY + slab.maxY) * .5f - group.mastFoot.y, 0};
    };
    float shiftX = -1e30f;
    for (const auto s : group.slabs) shiftX = std::max(shiftX, offsetOf(s).x);
    const auto topOffset = offsetOf(group.slabs.front());
    for (const auto s : group.slabs) {
      const auto& slab = out.slabs[s];
      const int slot = static_cast<int>(s % 8);
      out.bay[s] = s < 8 ? static_cast<int>(s) : -1;
      const auto* bayPart = lab.part(bayName(lab, slot));
      auto& path = out.paths[s];
      path.bayCentre = partCentre(lab, x, bayName(lab, slot));
      path.bayQuat = bayPart ? quatFromYawPitchRoll(radians(bayPart->rot[0]),
                                                    radians(bayPart->rot[1]),
                                                    radians(bayPart->rot[2]))
                             : kIdentity;
      path.hangCentre = {(slab.minX + slab.maxX) * .5f, (slab.minY + slab.maxY) * .5f,
                         slab.z - kBodyBelowFace};
      path.mastOffset = {group.mastFoot.x - path.hangCentre.x,
                         group.mastFoot.y - path.hangCentre.y, 0};
      path.yokeBelow = yokeBelow;
      path.railZ = group.mastTopZ;
      // A group is carried as a stack: every slab keeps its height below the top one.
      const float below = top.z - slab.z;
      path.liftZ = liftBottom + (slab.z - bottomZ);
      const auto off = offsetOf(s);
      path.pickCentre = {pickX + off.x - shiftX,
                         std::clamp(topBay.y, railMinY - path.mastOffset.y,
                                    railMaxY - path.mastOffset.y) +
                             off.y - topOffset.y,
                         topBay.z - below};
      path.departure =
          view::kDeployFirstDeparture + step * static_cast<float>(out.groupSlot[g]);
      path.parkCarriage = park;
    }
  }
  return out;
}
}  // namespace

namespace {
/// A point (an instance's centre, a surface vertex) belongs to the slab under it: inside
/// its bezel, on the highest face at or below it (a plinth hangs just under its own
/// face). -1 when none.
int slabUnder(const RigLayout& layout, const float* c) {
  int best = -1;
  float bestDz = 1e30f;
  for (std::size_t s = 0; s < layout.slabs.size(); ++s) {
    const auto& slab = layout.slabs[s];
    if (c[0] < slab.minX - .05f || c[0] > slab.maxX + .05f || c[1] < slab.minY - .05f ||
        c[1] > slab.maxY + .05f)
      continue;
    const float dz = c[2] - slab.z;
    const float rank = dz >= -view::kSlabBelowFace - .05f ? dz : 1e6f - dz;
    if (rank < bestDz) {
      bestDz = rank;
      best = static_cast<int>(s);
    }
  }
  if (best >= 0) return best;
  // Outside every bezel (a timeline's rail and arrow): the nearest slab at its height
  // carries it, so nothing the board draws is left hanging (M24.6).
  float bestD = 1e30f;
  for (std::size_t s = 0; s < layout.slabs.size(); ++s) {
    const auto& slab = layout.slabs[s];
    const float dx = std::max({0.0f, slab.minX - c[0], c[0] - slab.maxX});
    const float dy = std::max({0.0f, slab.minY - c[1], c[1] - slab.maxY});
    const float dz = std::abs(c[2] - slab.z);
    const float d = dx * dx + dy * dy + dz * dz;
    if (d < bestD) {
      bestD = d;
      best = static_cast<int>(s);
    }
  }
  return best;
}
int slabUnder(const RigLayout& layout, const Instance& i) {
  return slabUnder(layout, i.center);
}
/// Rotate `i` rigidly about `pivot` by `q`, then move the pivot to `to`.
void turnInstance(Instance& i, const view::Vec3& pivot, const view::Vec3& to,
                  const std::array<float, 4>& a) {
  const view::Vec3 local{i.center[0] - pivot.x, i.center[1] - pivot.y,
                         i.center[2] - pivot.z};
  const auto w = to + rotate(a, local);
  i.center[0] = w.x;
  i.center[1] = w.y;
  i.center[2] = w.z;
  const std::array<float, 4> b{i.quat[0], i.quat[1], i.quat[2], i.quat[3]};
  i.quat[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  i.quat[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  i.quat[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  i.quat[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
std::array<float, 4> yawQuat(float yaw) noexcept {
  return {0, 0, std::sin(yaw / 2), std::cos(yaw / 2)};
}
view::RigXform easeXform(const view::RigXform& a, const view::RigXform& b,
                         float f) noexcept {
  return {a.labOrigin + (b.labOrigin - a.labOrigin) * f,
          std::lerp(a.labScale, b.labScale, f)};
}
}  // namespace

std::optional<RigLayout> rigLayout(const LabScene& lab,
                                   const std::vector<view::Placement>& placements,
                                   const view::RigXform* xform) {
  return build(lab, placements, xform, nullptr);
}

std::optional<RigLayout> rigLayoutAfter(const LabScene& lab, const RigLayout& previous,
                                        const std::vector<view::Placement>& placements) {
  auto fresh = build(lab, placements, nullptr, nullptr);
  if (!fresh) return fresh;
  // The lab only moves when the layout outgrows the volume where it stands, or when it
  // would scale differently.
  if (std::abs(fresh->xform.labScale - previous.xform.labScale) < 1e-4f &&
      view::layoutFits(fresh->layoutBounds, previous.xform, fresh->volume))
    return build(lab, placements, &previous.xform, nullptr);
  return fresh;
}

std::vector<view::SlabPose> rigPoses(const RigLayout& layout, RigPhase phase, float age,
                                     float intensity) {
  std::vector<view::SlabPose> out;
  out.reserve(layout.paths.size());
  const float step = view::departureStep(layout.slots);
  for (std::size_t i = 0; i < layout.paths.size(); ++i) {
    const auto& path = layout.paths[i];
    if (phase == RigPhase::Rest) {
      out.push_back(view::slabDeploy(path, 0, 0));
      continue;
    }
    if (phase == RigPhase::Deploy) {
      out.push_back(view::slabDeploy(path, age, intensity));
      continue;
    }
    // Stow: the path reversed in time, the last group first.
    auto reversed = path;
    reversed.departure = 0;
    reversed.reversed = true;
    if (intensity < .5f) {
      out.push_back(view::slabDeploy(reversed, -1, 1));
      continue;
    }
    const int slot = layout.groupSlot[layout.slabGroup[i]];
    const float u = age - step * static_cast<float>(layout.slots - 1 - slot);
    if (u <= 0) {
      out.push_back(view::slabDeploy(path, 0, 0));
      out.back().clampPulse = 0;
    } else {
      out.push_back(view::slabDeploy(reversed, view::kSlabLockSeconds - u, 1));
    }
  }
  return out;
}

std::vector<RigPiece> appendRig(InstanceSet& set, const LabScene& lab,
                                const RigLayout& layout,
                                const std::vector<view::SlabPose>& poses) {
  std::vector<RigPiece> pieces;
  const auto add = [&](RigPiece::Kind kind, int group, int slab, std::string_view name,
                       const view::Vec3& centre, const std::array<float, 4>& q,
                       const std::array<float, 3>& mul, float alpha = 1) {
    if (!lab.part(name)) return;
    lab.appendCopyAt(set, name, centre, q, mul, alpha);
    const auto [lo, hi] = localBox(lab, name, mul);
    pieces.push_back({kind, group, slab, turnedBox(centre, lo, hi, q)});
  };
  // A horizontal bevel bar (the yoke template) from `a` to `b`, `section` x its own.
  const auto bar = [&](RigPiece::Kind kind, int group, int slab, const view::Vec3& a,
                       const view::Vec3& b, float section) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::hypot(dx, dy);
    if (len < 1e-4f) return;
    add(kind, group, slab, "rig.yoke", (a + b) * .5f,
        quatFromYawPitchRoll(std::atan2(-dx, dy), 0, 0),
        {section, len / (0.03f * kCellsPerMetre), section});
  };
  const auto& x = layout.xform;
  const auto carriageTemplate = partCentre(lab, x, "gantry.carriage.0");
  for (std::size_t g = 0; g < layout.groups.size(); ++g) {
    const auto& group = layout.groups[g];
    const int gi = static_cast<int>(g);
    const auto& top = poses[group.slabs.front()];
    float reach = 0, pulse = 0;
    for (const auto s : group.slabs) {
      reach = std::max(reach, poses[s].mastLength);
      pulse = std::max(pulse, poses[s].clampPulse);
    }
    const view::Vec3 carriage{top.carriage.x, top.carriage.y, group.mastTopZ};
    add(RigPiece::Kind::Carriage, gi, -1, "gantry.carriage.0", carriage, kIdentity,
        {1, 1, 1});
    for (const auto* bridge : {"gantry.bridge.a", "gantry.bridge.b"}) {
      auto at = partCentre(lab, x, bridge);
      at.x = carriage.x + (at.x - carriageTemplate.x) / x.labScale;
      at.z = carriage.z + (at.z - carriageTemplate.z) / x.labScale;
      add(RigPiece::Kind::Bridge, gi, -1, bridge, at, kIdentity, {1, x.labScale, 1});
    }
    // Two telescoping tubes; beyond their 1.15 m reach both stretch in z.
    const float stretch = std::max(1.0f, reach / kReachCells);
    const float foot = carriage.z - reach;
    add(RigPiece::Kind::MastOuter, gi, -1, "rig.mast.outer", carriage, kIdentity,
        {1, 1, stretch});
    add(RigPiece::Kind::MastInner, gi, -1, "rig.mast.inner",
        {carriage.x, carriage.y, std::min(carriage.z, foot + kTubeCells * stretch)},
        kIdentity, {1, 1, stretch});
    if (pulse > 0)
      add(RigPiece::Kind::Clamp, gi, -1, "rig.clamp.glow", {carriage.x, carriage.y, foot},
          kIdentity, {1, 1, 1}, pulse);
    for (const auto s : group.slabs) {
      const auto& pose = poses[s];
      if (pose.inBay) continue;
      const int si = static_cast<int>(s);
      const auto& slab = layout.slabs[s];
      const auto& path = layout.paths[s];
      const float width = slab.maxX - slab.minX, depth = slab.maxY - slab.minY;
      // How far from edge-on to face-up: the back grows from the bay's size with it, as
      // the board it carries does (carryBoard).
      const float w0 = std::abs(path.bayQuat[3]);
      const float f =
          w0 >= 1 ? 1.0f : std::clamp((std::abs(pose.quat[3]) - w0) / (1 - w0), 0.f, 1.f);
      const float backSize = 0.44f * kCellsPerMetre;
      const float bayW = layout.bayWidth > 0 ? layout.bayWidth : width;
      const float bayD = layout.bayDepth > 0 ? layout.bayDepth : depth;
      add(RigPiece::Kind::Back, gi, si, "rig.slab.back",
          pose.centre + rotate(pose.quat, {0, 0, -kBodyHalf}), pose.quat,
          {std::lerp(bayW, width, f) / backSize, std::lerp(bayD, depth, f) / backSize,
           1});
      // Face-up, give or take the few degrees it rocks on its yoke (M25.3).
      if (pose.quat[3] > .999f) {
        // The yoke runs under the slab from its mast axis (a rack's arm) to the back's
        // boss; on a rack a T-bar joins the arm to the mast at the yoke's height.
        const auto& arm = layout.arm[s];
        const view::Vec3 armAt{
            pose.centre.x + path.mastOffset.x + arm.x - group.mastFoot.x,
            pose.centre.y + path.mastOffset.y + arm.y - group.mastFoot.y,
            pose.centre.z - path.yokeBelow};
        bar(RigPiece::Kind::Yoke, gi, si, {pose.centre.x, pose.centre.y, armAt.z}, armAt,
            1);
        if (group.rack()) {
          const view::Vec3 mast{pose.centre.x + path.mastOffset.x,
                                pose.centre.y + path.mastOffset.y, armAt.z};
          const view::Vec3 spine{mast.x, armAt.y, armAt.z};
          bar(RigPiece::Kind::TBar, gi, si, armAt, spine, kTBarSection);
          bar(RigPiece::Kind::TBar, gi, si, spine, mast, kTBarSection);
        }
      }
    }
  }
  return pieces;
}

namespace {
/// Per slab: its pivot (the deployed centre), its pose and its size from edge-on in the
/// bay (the parked slab's footprint) to its own once face-up.
struct SlabCarry {
  bool moves{false}, hidden{false};
  view::Vec3 pivot, centre, scale{1, 1, 1};
  std::array<float, 4> quat{};
};
std::vector<SlabCarry> carriesOf(const RigLayout& layout,
                                 const std::vector<view::SlabPose>& poses) {
  std::vector<SlabCarry> carry(layout.slabs.size());
  for (std::size_t s = 0; s < carry.size(); ++s) {
    const auto& pose = poses[s];
    auto& c = carry[s];
    if (pose.clamped) continue;
    c.moves = true;
    c.hidden = pose.inBay;
    c.pivot = layout.paths[s].hangCentre;
    c.centre = pose.centre;
    c.quat = pose.quat;
    const auto& slab = layout.slabs[s];
    const auto& path = layout.paths[s];
    const float w0 = std::abs(path.bayQuat[3]);
    const float f =
        w0 >= 1 ? 1.0f : std::clamp((std::abs(pose.quat[3]) - w0) / (1 - w0), 0.f, 1.f);
    if (layout.bayWidth > 0 && layout.bayDepth > 0) {
      c.scale.x = std::lerp(layout.bayWidth / (slab.maxX - slab.minX), 1.0f, f);
      c.scale.y = std::lerp(layout.bayDepth / (slab.maxY - slab.minY), 1.0f, f);
      c.scale.z = std::lerp(std::min(c.scale.x, c.scale.y), 1.0f, f);
    }
  }
  return carry;
}
void carryInstance(Instance& i, const SlabCarry& c) {
  if (!c.moves) return;
  if (c.hidden) {
    i.scale[0] = i.scale[1] = i.scale[2] = 0;
    return;
  }
  const view::Vec3 local{(i.center[0] - c.pivot.x) * c.scale.x,
                         (i.center[1] - c.pivot.y) * c.scale.y,
                         (i.center[2] - c.pivot.z) * c.scale.z};
  const auto w = c.centre + rotate(c.quat, local);
  i.center[0] = w.x;
  i.center[1] = w.y;
  i.center[2] = w.z;
  i.scale[0] *= c.scale.x;
  i.scale[1] *= c.scale.y;
  i.scale[2] *= c.scale.z;
  // q = carry * own (Hamilton product, x y z w).
  const auto& a = c.quat;
  const std::array<float, 4> b{i.quat[0], i.quat[1], i.quat[2], i.quat[3]};
  i.quat[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  i.quat[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  i.quat[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  i.quat[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}
/// A surface-mesh vertex rides its slab like an instance (M25.3): its position through
/// the carry, its normal turned (through the inverse of the edge-on shrink), and every
/// vertex of a slab still in its bay on one point, so its triangles vanish.
void carryVertex(MeshVertex& v, const SlabCarry& c) {
  if (!c.moves) return;
  if (c.hidden) {
    v.pos[0] = c.centre.x;
    v.pos[1] = c.centre.y;
    v.pos[2] = c.centre.z;
    return;
  }
  const view::Vec3 local{(v.pos[0] - c.pivot.x) * c.scale.x,
                         (v.pos[1] - c.pivot.y) * c.scale.y,
                         (v.pos[2] - c.pivot.z) * c.scale.z};
  const auto w = c.centre + rotate(c.quat, local);
  v.pos[0] = w.x;
  v.pos[1] = w.y;
  v.pos[2] = w.z;
  auto n = rotate(c.quat, {v.normal[0] / c.scale.x, v.normal[1] / c.scale.y,
                           v.normal[2] / c.scale.z});
  if (const float l = view::length(n); l > 0) n = n * (1 / l);
  v.normal[0] = n.x;
  v.normal[1] = n.y;
  v.normal[2] = n.z;
}
/// Every board instance list a carry moves. The surface mesh's identity instances are
/// left alone: its vertices are carried themselves (M25.3), so moving those too would
/// carry the sheet twice.
template <typename F>
void eachBoardInstance(InstanceSet& set, F&& f) {
  std::vector<bool> surface;
  if (!set.surfaceVertices.empty()) {
    surface.assign(set.instances.size(), false);
    const auto mark = [&](std::uint32_t i) {
      if (i < surface.size()) surface[i] = true;
    };
    mark(set.surfaceInstance);
    for (const auto& d : set.surfaceDraws) mark(d.instance);
  }
  for (std::size_t k = 0; k < set.instances.size(); ++k)
    if (surface.empty() || !surface[k]) f(set.instances[k]);
  for (auto* list : {&set.shadows, &set.flourish})
    for (auto& i : *list) f(i);
  if (set.standbyLed) f(*set.standbyLed);
}
}  // namespace

void carryBoard(InstanceSet& set, const RigLayout& layout,
                const std::vector<view::SlabPose>& poses) {
  if (layout.slabs.empty() || poses.size() != layout.slabs.size()) return;
  if (std::all_of(poses.begin(), poses.end(), [](const auto& p) { return p.clamped; }))
    return;
  const auto carry = carriesOf(layout, poses);
  eachBoardInstance(set, [&](Instance& i) {
    const int s = slabUnder(layout, i);
    if (s >= 0) carryInstance(i, carry[static_cast<std::size_t>(s)]);
  });
  for (auto& v : set.surfaceVertices) {
    const int s = slabUnder(layout, v.pos);
    if (s >= 0) carryVertex(v, carry[static_cast<std::size_t>(s)]);
  }
}

bool hiddenInGame(const io::LabPart& part, const RigLayout& layout,
                  const std::vector<view::SlabPose>& poses) {
  if (part.name == "gantry.carriage.0" || part.name == "gantry.bridge.a" ||
      part.name == "gantry.bridge.b")
    return true;
  const auto dot = part.name.rfind('.');
  if (dot == std::string::npos || dot + 2 != part.name.size()) return false;
  const char c = part.name.back();
  if (c < '0' || c > '7') return false;
  if (std::string_view(part.name).substr(0, dot) != layout.bayPrefix) return false;
  const int slot = c - '0';
  for (std::size_t i = 0; i < layout.bay.size(); ++i)
    if (layout.bay[i] == slot && !poses[i].inBay) return true;
  return false;
}

Relayout relayoutOf(const LabScene& lab, const RigLayout& from,
                    const std::vector<view::Placement>& fromPlacements,
                    const RigLayout& to,
                    const std::vector<view::Placement>& toPlacements) {
  (void)lab;
  Relayout out;
  out.from = from;
  out.to = to;
  out.matches = view::matchSlabs(fromPlacements, from.slabs, toPlacements, to.slabs);
  out.specs.resize(to.slabs.size());
  out.seconds = kLabScaleEaseSeconds;
  for (const auto& m : out.matches) {
    if (m.to < 0) {
      out.leaving.push_back(m.from);
      out.seconds = std::max(out.seconds, view::kSlabLockSeconds);
      continue;
    }
    const auto t = static_cast<std::size_t>(m.to);
    if (m.from < 0) {
      out.arrivals.push_back(m.to);
      out.seconds = std::max(out.seconds, to.paths[t].departure + view::kSlabLockSeconds);
      continue;
    }
    auto& spec = out.specs[t];
    spec.from = from.xform.toLab(from.paths[static_cast<std::size_t>(m.from)].hangCentre);
    spec.to = to.xform.toLab(to.paths[t].hangCentre);
    spec.fromYaw = -m.yaw;
    spec.toYaw = 0;
    spec.lift = 0.08f * kCellsPerMetre;
    out.seconds = std::max(out.seconds, view::relayoutSeconds(spec));
  }
  return out;
}

RelayoutFrame relayoutFrame(const LabScene& lab, const Relayout& move,
                            const std::vector<view::Placement>& toPlacements,
                            float sinceStart, float intensity) {
  RelayoutFrame f;
  const bool calm = intensity < .5f;
  // M25.3: nothing moves while the shape unforms; the slabs then move from age 0.
  const float age = std::max(0.0f, sinceStart - move.lead);
  const bool holding = !calm && sinceStart < move.lead;
  f.done = calm || age >= move.seconds;
  const float ease = calm ? 1.0f : view::rigEase(age / kLabScaleEaseSeconds);
  const auto xform = easeXform(move.from.xform, move.to.xform, ease);
  auto live = build(lab, toPlacements, &xform, &move.to.groups);
  f.layout = live ? std::move(*live) : move.to;
  const std::size_t n = f.layout.slabs.size();
  f.carry.resize(n);
  f.poses.reserve(n + move.leaving.size());
  std::vector<bool> arriving(n, false);
  for (const int a : move.arrivals) arriving[static_cast<std::size_t>(a)] = true;
  for (std::size_t i = 0; i < n; ++i) {
    const auto& path = f.layout.paths[i];
    if (arriving[i]) {
      // From the rack, on its own path in the (eased) lab.
      f.carry[i].arriving = true;
      f.poses.push_back(view::slabDeploy(path, f.done ? 1e3f : age, intensity));
      continue;
    }
    const auto& spec = move.specs[i];
    const auto p = view::slabRelayout(spec, f.done ? 1e3f : age, intensity);
    view::SlabPose pose;
    pose.centre = f.done ? path.hangCentre : xform.toBoard(p.centre);
    pose.quat = yawQuat(f.done ? 0.0f : p.yaw);
    const auto arm = rotate(pose.quat, path.mastOffset);
    pose.carriage = {pose.centre.x + arm.x, pose.centre.y + arm.y, path.railZ};
    pose.mastLength = std::max(0.0f, path.railZ - (pose.centre.z - path.yokeBelow));
    pose.clamped = f.done || p.settled;
    pose.inBay = false;
    f.poses.push_back(pose);
    auto& c = f.carry[i];
    c.moves = !f.done && (view::length(pose.centre - path.hangCentre) > 1e-6f ||
                          std::abs(p.yaw) > 1e-6f);
    c.from = path.hangCentre;
    c.to = pose.centre;
    c.yaw = p.yaw;
  }
  // The leaving slabs, back to the rack along their old path in the old lab, as extra
  // groups of their own.
  for (const int j : move.leaving) {
    const auto o = static_cast<std::size_t>(j);
    auto path = move.from.paths[o];
    path.departure = 0;
    path.reversed = true;
    auto pose = view::slabDeploy(path, f.done ? -1.0f : view::kSlabLockSeconds - age, 1);
    const auto toHere = [&](const view::Vec3& b) {
      return xform.toBoard(move.from.xform.toLab(b));
    };
    pose.centre = toHere(pose.centre);
    pose.carriage = toHere(pose.carriage);
    const float railZ = xform.toBoard({0, 0, f.layout.volume.railZ}).z;
    pose.carriage.z = railZ;
    pose.mastLength = std::max(0.0f, railZ - (pose.centre.z - path.yokeBelow));
    pose.clamped = age <= 0.0f;  // through the lead it still hangs, clamped
    path.hangCentre = toHere(path.hangCentre);
    path.railZ = railZ;
    view::MastGroup g;
    g.mastFoot = path.hangCentre + path.mastOffset;
    g.mastTopZ = railZ;
    g.slabs = {static_cast<std::uint32_t>(f.layout.slabs.size())};
    f.layout.groups.push_back(g);
    f.layout.slabs.push_back(move.from.slabs[o]);
    f.layout.paths.push_back(path);
    f.layout.arm.push_back({g.mastFoot.x, g.mastFoot.y, 0});
    f.layout.bay.push_back(move.from.bay[o]);
    f.layout.slabGroup.push_back(static_cast<std::uint32_t>(f.layout.groups.size() - 1));
    f.layout.groupSlot.push_back(0);
    f.poses.push_back(pose);
  }
  if (holding)
    for (auto& p : f.poses)
      if (!p.inBay) p.clamped = true;
  return f;
}

void carryRelayout(InstanceSet& set, const RelayoutFrame& frame) {
  const std::size_t n = frame.carry.size();
  if (n == 0 || frame.done) return;
  // Arrivals ride their deploy path like a deploy's slabs; a matched slab moves rigidly
  // with its relayout. Each instance's slab is found before anything moves.
  RigLayout own = frame.layout;
  own.slabs.resize(n);
  own.paths.resize(n);
  std::vector<view::SlabPose> poses(frame.poses.begin(),
                                    frame.poses.begin() + static_cast<std::ptrdiff_t>(n));
  for (std::size_t i = 0; i < n; ++i)
    if (!frame.carry[i].arriving) poses[i].clamped = true;
  const auto deploy = carriesOf(own, poses);
  eachBoardInstance(set, [&](Instance& i) {
    const int s = slabUnder(own, i);
    if (s < 0) return;
    const auto& c = frame.carry[static_cast<std::size_t>(s)];
    if (c.moves)
      turnInstance(i, c.from, c.to, yawQuat(c.yaw));
    else
      carryInstance(i, deploy[static_cast<std::size_t>(s)]);
  });
  for (auto& v : set.surfaceVertices) {
    const int s = slabUnder(own, v.pos);
    if (s < 0) continue;
    const auto& c = frame.carry[static_cast<std::size_t>(s)];
    if (!c.moves) {
      carryVertex(v, deploy[static_cast<std::size_t>(s)]);
      continue;
    }
    const auto q = yawQuat(c.yaw);
    const auto w =
        c.to + rotate(q, {v.pos[0] - c.from.x, v.pos[1] - c.from.y, v.pos[2] - c.from.z});
    const auto normal = rotate(q, {v.normal[0], v.normal[1], v.normal[2]});
    v.pos[0] = w.x;
    v.pos[1] = w.y;
    v.pos[2] = w.z;
    v.normal[0] = normal.x;
    v.normal[1] = normal.y;
    v.normal[2] = normal.z;
  }
}

std::optional<float> relayoutBootAge(const Relayout& move, float age,
                                     float intensity) noexcept {
  if (move.arrivals.empty()) return std::nullopt;
  age -= intensity >= .5f ? move.lead : 0.0f;
  float lock = 0;
  if (intensity >= .5f)
    for (const int a : move.arrivals)
      lock = std::max(lock, move.to.paths[static_cast<std::size_t>(a)].departure +
                                view::kSlabLockSeconds);
  if (age < lock) return 0.0f;
  if (age - lock >= view::kBootSeconds) return std::nullopt;
  return age - lock;
}

std::size_t appendBridges(InstanceSet& set, const std::vector<view::LightBridge>& bridges,
                          float power) {
  std::vector<Instance> bars;
  const float alpha = kBridgeAlpha * std::clamp(power, 0.0f, 1.0f);
  if (alpha <= 0.0f) return 0;
  for (const auto& b : bridges) {
    for (int i = 0; i < kBridgeSegments; ++i) {
      const auto p = view::bridgePoint(b, static_cast<float>(i) / kBridgeSegments);
      const auto q = view::bridgePoint(b, static_cast<float>(i + 1) / kBridgeSegments);
      Instance beam = cylinderBetween(p, q, kBridgeBeamMetres * kCellsPerMetre);
      beam.color[0] = b.color.r;
      beam.color[1] = b.color.g;
      beam.color[2] = b.color.b;
      beam.color[3] = alpha;
      beam.material = shaderMaterial(view::Material::Hologram, Cut::Plank, 0.0f);
      beam.panelSignal[2] = -1.0f;  // the projector's opacity path: alpha is opacity
      bars.push_back(beam);
      Instance core = cylinderBetween(p, q, kBridgeCoreMetres * kCellsPerMetre);
      for (int c = 0; c < 3; ++c)
        core.color[c] = std::min(1.0f, beam.color[c] * 1.4f) * power;
      core.color[3] = 1.0f;
      core.material = shaderMaterial(view::Material::Glow, Cut::Plank, 0.0f);
      bars.push_back(core);
    }
  }
  return insertLabCylinders(set, bars);
}

std::vector<RigCable> rigCables(const RigLayout& layout,
                                const std::vector<view::SlabPose>& poses,
                                const VariantSpec& v, const view::ViewConfig& cfg,
                                const std::vector<view::TimelineLink>& links) {
  std::vector<RigCable> out;
  if (links.empty() || poses.size() < layout.slabs.size()) return out;
  int turnGrid = -1, lineGrid = -1;
  for (std::size_t i = 0; i < cfg.gridAxes.size(); ++i) {
    if (v.dims.kind(cfg.gridAxes[i]) == AxisKind::Temporal)
      turnGrid = static_cast<int>(i);
    if (v.dims.kind(cfg.gridAxes[i]) == AxisKind::Multiverse)
      lineGrid = static_cast<int>(i);
  }
  if (turnGrid < 0 || lineGrid < 0) return out;
  const auto slices = view::enumerateSlices(v.dims, cfg);
  // The slab of (line, turn), and each line's earliest board.
  std::map<std::pair<int, int>, int> slabAt;
  std::map<int, std::pair<int, int>> first;  // line -> (turn, slab)
  for (std::size_t s = 0; s < layout.slabs.size(); ++s) {
    const auto slice = layout.slabs[s].slice;
    if (slice >= slices.size()) continue;
    const int turn = slices[slice].at[static_cast<std::size_t>(turnGrid)];
    const int line = slices[slice].at[static_cast<std::size_t>(lineGrid)];
    slabAt[{line, turn}] = static_cast<int>(s);
    const auto it = first.find(line);
    if (it == first.end() || turn < it->second.first)
      first[line] = {turn, static_cast<int>(s)};
  }
  const float above = kCableAboveMetres * kCellsPerMetre;
  for (const auto& link : links) {
    const auto a = slabAt.find({link.fromLine, link.atTurn});
    const auto b = first.find(link.toLine);
    if (a == slabAt.end() || b == first.end()) continue;
    const auto sa = static_cast<std::size_t>(a->second);
    const auto sb = static_cast<std::size_t>(b->second.second);
    const auto& pa = poses[sa];
    const auto& pb = poses[sb];
    if (pa.inBay || pb.inBay) continue;
    // The slab's face: its body centre plus the body-below-face offset.
    const float face = std::max(pa.centre.z, pb.centre.z) + kBodyBelowFace;
    const view::Vec3 from{pa.carriage.x, pa.carriage.y, face + above};
    const view::Vec3 to{pb.carriage.x, pb.carriage.y, face + above};
    if (view::length(to - from) < 1.0f) continue;
    out.push_back({a->second, b->second.second, view::cablePoints(from, to)});
  }
  return out;
}

std::size_t appendCables(InstanceSet& set, const std::vector<RigCable>& cables,
                         float seconds, float power) {
  std::vector<Instance> bars;
  const view::Rgba glass = view::Rgba::hex(0x9FE8FF);
  const float d = kCableMetres * kCellsPerMetre;
  for (const auto& cable : cables) {
    float length = 0;
    std::vector<float> at{0};
    for (std::size_t i = 1; i < cable.points.size(); ++i) {
      Instance bar = cylinderBetween(cable.points[i - 1], cable.points[i], d);
      bar.color[0] = glass.r;
      bar.color[1] = glass.g;
      bar.color[2] = glass.b;
      bar.color[3] = 1.0f;
      bar.material = shaderMaterial(view::Material::Glass, Cut::Plank, 0.0f);
      bars.push_back(bar);
      length += view::length(cable.points[i] - cable.points[i - 1]);
      at.push_back(length);
    }
    if (power <= 0.0f || length <= 0.0f) continue;
    // Light pulses running from the parent to the child.
    for (int k = 0; k < kCablePulses; ++k) {
      const float s = std::fmod(seconds * kCablePulseSpeed * kCellsPerMetre +
                                    length * static_cast<float>(k) / kCablePulses,
                                length);
      std::size_t i = 1;
      while (i + 1 < at.size() && at[i] < s) ++i;
      const float f = (s - at[i - 1]) / std::max(1e-5f, at[i] - at[i - 1]);
      const auto p = cable.points[i - 1] + (cable.points[i] - cable.points[i - 1]) * f;
      const auto dir = view::normalize(cable.points[i] - cable.points[i - 1]);
      Instance dot =
          cylinderBetween(p - dir * (d * 1.5f), p + dir * (d * 1.5f), d * 1.6f);
      const auto g = glass.array();
      for (std::size_t c = 0; c < 3; ++c)
        dot.color[c] = std::min(1.0f, g[c] * 1.2f) * power;
      dot.color[3] = 1.0f;
      dot.material = shaderMaterial(view::Material::Glow, Cut::Plank, 0.0f);
      bars.push_back(dot);
    }
  }
  return insertLabCylinders(set, bars);
}

float parkFraction(float formed) noexcept {
  return smooth(formed / 0.3f);
}

void parkHigh(const RigLayout& layout, std::vector<view::SlabPose>& poses, float f) {
  if (f <= 0.0f) return;
  const float park = kParkMastMetres * kCellsPerMetre;
  for (const auto& group : layout.groups) {
    if (group.slabs.empty()) continue;
    const auto top = group.slabs.front();
    if (top >= poses.size()) continue;
    const auto& path = layout.paths[top];
    const float yoke = poses[top].centre.z - path.yokeBelow;
    const float rise = (path.railZ - park - yoke) * std::clamp(f, 0.0f, 1.0f);
    for (const auto s : group.slabs) {
      if (s >= poses.size()) continue;
      poses[s].centre.z += rise;
      poses[s].mastLength = std::max(0.0f, poses[s].mastLength - rise);
    }
  }
}

std::size_t appendStandby(InstanceSet& set, const RigLayout& layout,
                          const std::vector<view::SlabPose>& poses) {
  std::vector<Instance> leds;
  const view::Rgba amber = view::Rgba::hex(0xFF9F43);
  for (std::size_t s = 0; s < layout.slabs.size() && s < poses.size(); ++s) {
    const auto& slab = layout.slabs[s];
    const auto& p = poses[s];
    if (p.inBay) continue;
    const float hw = (slab.maxX - slab.minX) * .5f - .25f;
    const float hd = (slab.maxY - slab.minY) * .5f - .25f;
    const auto at = p.centre + rotate(p.quat, {-hw, -hd, kBodyHalf});
    Instance led = cylinderBetween(at, at + view::Vec3{0, 0, .08f}, .22f);
    const auto a = amber.array();
    for (std::size_t c = 0; c < 3; ++c) led.color[c] = a[c];
    led.color[3] = 1.0f;
    led.material = shaderMaterial(view::Material::Glow, Cut::Plank, 0.0f);
    leds.push_back(led);
  }
  return insertLabCylinders(set, leds);
}

std::vector<bool> slabsShown(const RigLayout& layout,
                             const std::vector<view::SlabPose>& poses) {
  std::vector<bool> out(layout.slabs.size(), true);
  for (std::size_t i = 0; i < out.size() && i < poses.size(); ++i)
    out[i] = poses[i].clamped;
  return out;
}

std::vector<view::Placement> shapeRigPlacements(const VariantSpec& v,
                                                const std::vector<view::Placement>& p) {
  if (p.empty() || v.dims.dims() != 2 || !hasPlaySurface(v)) return p;
  SurfacePose pose;
  pose.formed = 0.0f;
  const PlaySurface flat = PlaySurface::build(v, pose);
  if (flat.empty() || flat.stacked()) return p;
  const auto shape = flat.bounds();
  const auto board = view::boundsOf(p);
  const float dx = shape.centerX() - board.centerX(),
              dy = shape.centerY() - board.centerY();
  auto out = p;
  for (auto& c : out) {
    c.x += dx;
    c.y += dy;
  }
  return out;
}

view::Vec3 yokeBoss(const RigLayout& layout, const std::vector<view::SlabPose>& poses,
                    std::size_t slab) noexcept {
  const auto& c = poses[slab].centre;
  return {c.x, c.y, c.z - layout.paths[slab].yokeBelow};
}

std::array<float, 2> labStageArea(float width, float height) noexcept {
  return {std::min(1.0f, width * kLabStageGrow), std::min(1.0f, height * kLabStageGrow)};
}

namespace {
/// The distance from `eye` to slab `s`'s keep-out at `pose` - its body and board swept
/// `sweep` straight up - and the keep-out's nearest point. The distance to a box along a
/// vertical line is convex, so the sweep's nearest point is found by a ternary search
/// over the lift.
float keepOutDistance(const RigLayout& layout, std::size_t s, const view::SlabPose& pose,
                      float sweep, const view::Vec3& eye, view::Vec3& nearest) {
  const auto& slab = layout.slabs[s];
  const float hw = (slab.maxX - slab.minX) * .5f, hd = (slab.maxY - slab.minY) * .5f;
  const std::array<float, 4> inv{-pose.quat[0], -pose.quat[1], -pose.quat[2],
                                 pose.quat[3]};
  const auto at = [&](float lift, view::Vec3& onBox) {
    const auto local = rotate(inv, eye - view::Vec3{0, 0, lift} - pose.centre);
    const view::Vec3 n{std::clamp(local.x, -hw, hw), std::clamp(local.y, -hd, hd),
                       std::clamp(local.z, -kCarriedBelowCells, kCarriedAboveCells)};
    onBox = pose.centre + rotate(pose.quat, n) + view::Vec3{0, 0, lift};
    return view::length(local - n);
  };
  float lo = 0, hi = std::max(0.0f, sweep);
  for (int i = 0; i < 40 && hi - lo > 1e-4f; ++i) {
    view::Vec3 unused{};
    const float a = lo + (hi - lo) / 3, b = hi - (hi - lo) / 3;
    if (at(a, unused) <= at(b, unused))
      hi = b;
    else
      lo = a;
  }
  return at((lo + hi) * .5f, nearest);
}
}  // namespace

namespace {
/// The camera at `eye` aimed at `aim`.
view::OrbitCamera lookFrom(view::OrbitCamera c, const view::Vec3& eye,
                           const view::Vec3& aim) {
  const auto back = eye - aim;
  c.target = aim;
  c.distance = std::max(1e-3f, view::length(back));
  c.pitch = std::asin(std::clamp(back.z / c.distance, -1.f, 1.f));
  c.yaw = std::atan2(back.x, -back.y);
  return c;
}
/// The camera re-aimed at `aim` from where it stands.
view::OrbitCamera lookAt(const view::OrbitCamera& c, const view::Vec3& aim) {
  return lookFrom(c, c.eye(), aim);
}
/// The flight before it keeps clear of the carried slabs (M24.5).
view::OrbitCamera flightOf(const RigLayout& layout,
                           const std::vector<view::SlabPose>& poses, const RigFlight& m,
                           const view::OrbitCamera& game, const view::OrbitCamera& lab) {
  if (m.deploy) {
    constexpr float kFlyStart = 0.4f;  // the menu has dissolved; the slab is out
    // The flight lands clear of the slabs' lift columns, then settles onto the game
    // camera once every slab is clamped (M24.9).
    const auto clearGame = clearOfLifts(layout, game);
    if (m.age >= m.lock)
      return view::blendCamera(clearGame, game,
                               view::rigEase((m.age - m.lock) / kFlightSettleSeconds));
    if (m.age <= kFlyStart) return lab;
    const auto slab = layout.groups.front().slabs.front();
    // On the slab until it has run along the rail, then onto the game's target.
    const float overhead = layout.paths[slab].departure + view::kSlabTravelEnd;
    const auto follow =
        poses[slab].centre +
        (game.target - poses[slab].centre) *
            smooth((m.age - overhead) / std::max(.01f, m.lock - overhead));
    const auto aim =
        lab.target + (follow - lab.target) * smooth((m.age - kFlyStart) / 0.3f);
    return lookAt(
        view::blendCamera(lab, clearGame,
                          view::rigEase((m.age - kFlyStart) / (m.lock - kFlyStart))),
        aim);
  }
  if (m.age >= m.lock) return lab;
  // First back off the game camera, clear of the slabs' lift columns, over the shutdown
  // and the slabs' unclamp (M24.9).
  const auto start =
      view::blendCamera(game, clearOfLifts(layout, game),
                        view::rigEase(m.age / (m.shutdown + kFlightSettleSeconds * .5f)));
  if (m.age <= m.shutdown) return start;
  const float u = m.age - m.shutdown, span = m.lock - m.shutdown;
  const auto slab = layout.groups.back().slabs.front();
  // Onto the slab as it leaves its mast, off it once it is tilting into its bay.
  const float inBay = view::stowTravelSeconds(layout.slots) -
                      (view::kSlabLockSeconds - view::kSlabTiltEnd);
  const float off = smooth((u - inBay) / std::max(.01f, span - inBay));
  const auto follow = poses[slab].centre + (lab.target - poses[slab].centre) * off;
  const auto aim = game.target + (follow - game.target) * smooth(u / 0.3f);
  return lookAt(view::blendCamera(start, lab, view::rigEase(u / span)), aim);
}
}  // namespace

view::OrbitCamera rigFlightCamera(const RigLayout& layout,
                                  const std::vector<view::SlabPose>& poses,
                                  const RigFlight& flight, const view::OrbitCamera& game,
                                  const view::OrbitCamera& lab) {
  const auto c = flightOf(layout, poses, flight, game, lab);
  const auto eye = c.eye();
  const auto clear = clearOfSlabs(layout, poses, eye);
  // Untouched unless a carried slab is near: every endpoint is still held exactly.
  if (view::length(clear - eye) <= 0.0f) return c;
  return lookFrom(c, clear, c.target);
}

view::Vec3 clearOfSlabs(const RigLayout& layout, const std::vector<view::SlabPose>& poses,
                        view::Vec3 eye) noexcept {
  const float metre = kCellsPerMetre * layout.xform.labScale;
  const float clear = kFlightClearMetres * metre;
  // A few rounds, so a push out of one slab's keep-out cannot leave the eye in another's.
  for (int round = 0; round < 4; ++round) {
    bool moved = false;
    for (std::size_t s = 0; s < poses.size() && s < layout.slabs.size(); ++s) {
      const auto& pose = poses[s];
      if (pose.clamped || pose.inBay) continue;
      // The keep-out is the slab swept straight up to its rail: the eye leaves it
      // sideways or underneath, never up between the slab and the rail. The sweep grows
      // over the slab's first 0.3 m off its hang (and shrinks over its last), so clamping
      // or releasing a slab moves nothing.
      const float off =
          view::length(pose.centre - layout.paths[s].hangCentre) / (0.3f * metre);
      const float sweep = (layout.paths[s].railZ - pose.centre.z) * smooth(off);
      view::Vec3 onBox{};
      const float d = keepOutDistance(layout, s, pose, sweep, eye, onBox);
      if (d >= clear) continue;
      if (d > 1e-4f) {
        eye = onBox + (eye - onBox) * (clear / d);
      } else {
        // Inside it: straight down, under the slab's lowest corner.
        const auto& slab = layout.slabs[s];
        const float hw = (slab.maxX - slab.minX) * .5f,
                    hd = (slab.maxY - slab.minY) * .5f;
        float bottom = 1e30f;
        for (int k = 0; k < 8; ++k)
          bottom = std::min(
              bottom,
              (pose.centre +
               rotate(pose.quat, {k & 1 ? hw : -hw, k & 2 ? hd : -hd,
                                  k & 4 ? kCarriedAboveCells : -kCarriedBelowCells}))
                  .z);
        eye.z = bottom - clear;
      }
      moved = true;
    }
    if (!moved) break;
  }
  return eye;
}

view::OrbitCamera clearOfLifts(const RigLayout& layout, const view::OrbitCamera& game) {
  // A little wider than the flight's own margin, so a slab running off along its rail
  // past the backed-off eye barely has to push it.
  const float clear =
      (kFlightClearMetres + kLiftMarginMetres) * kCellsPerMetre * layout.xform.labScale;
  // Every slab at its hang, swept up to its rail: the column it is lifted through.
  const auto rest = rigPoses(layout, RigPhase::Rest, 0, 1);
  const auto clearAt = [&](float distance) {
    auto c = game;
    c.distance = distance;
    const auto eye = c.eye();
    for (std::size_t s = 0; s < rest.size() && s < layout.slabs.size(); ++s) {
      view::Vec3 unused{};
      if (keepOutDistance(layout, s, rest[s], layout.paths[s].railZ - rest[s].centre.z,
                          eye, unused) < clear)
        return false;
    }
    return true;
  };
  if (clearAt(game.distance)) return game;
  // Back along the view ray, at most to the room's wall (constrainGameCamera's limit).
  const float inset = kRoomInsetMetres * kCellsPerMetre * layout.xform.labScale;
  const float far =
      std::max(game.distance,
               view::clampOrbitDistance(game, game.distance * 3, layout.room, inset));
  if (!clearAt(far)) return game;
  float lo = game.distance, hi = far;
  for (int i = 0; i < 30; ++i) {
    const float mid = (lo + hi) * .5f;
    (clearAt(mid) ? hi : lo) = mid;
  }
  auto c = game;
  c.distance = hi;
  return c;
}

float deployFocus(float age, float lock, float total, float intensity) noexcept {
  if (intensity < .5f) return age < total ? 1.0f : kFocusLevel;
  if (age < lock) return 1;
  return 1 + (kFocusLevel - 1) * smooth((age - lock) / kFocusEaseSeconds);
}

float stowFocus(float age, float shutdown, float arrive, float total,
                float intensity) noexcept {
  if (intensity < .5f) return age < total ? kFocusLevel : 1.0f;
  if (age < shutdown) return kFocusLevel;
  return kFocusLevel +
         (1 - kFocusLevel) * smooth((age - shutdown) / std::max(.01f, arrive - shutdown));
}

std::vector<GpuLight> slabGlowLights(const RigLayout& layout, const InstanceSet& board) {
  const auto n = layout.groups.size();
  std::vector<std::array<float, 4>> sum(n, {0, 0, 0, 0});
  std::vector<float> cells(n, 0);
  for (const auto& i : board.instances) {
    const auto rec = static_cast<std::uint32_t>(i.fxCell);
    if (rec < 2 || rec % 2 != 0 || i.fxCell != static_cast<float>(rec)) continue;
    const std::uint32_t cell = rec / 2 - 1;
    if (cell >= layout.cellSlab.size()) continue;
    const auto g = layout.slabGroup[layout.cellSlab[cell]];
    float power = 1;
    if (cell + 1 < board.cellFx.size())
      power = board.cellFx[cell + 1].power * board.cellFx[cell + 1].flicker;
    cells[g] += 1;
    if (power <= 0) continue;
    for (int c = 0; c < 3; ++c) sum[g][static_cast<std::size_t>(c)] += i.color[c];
    sum[g][3] += power;
  }
  std::vector<GpuLight> out;
  for (std::size_t g = 0; g < n; ++g) {
    GpuLight l{};
    const auto& top = layout.slabs[layout.groups[g].slabs.front()];
    l.posRadius[0] = (top.minX + top.maxX) * .5f;
    l.posRadius[1] = (top.minY + top.maxY) * .5f;
    l.posRadius[2] = top.z + kGlowLiftCells;
    l.posRadius[3] = kGlowRadiusMetres;
    const float lit = std::max(1.0f, cells[g]);
    const float powered = sum[g][3];
    for (int c = 0; c < 3; ++c)
      l.colorIntensity[c] =
          powered > 0 ? sum[g][static_cast<std::size_t>(c)] / std::max(1.0f, powered) : 0;
    l.colorIntensity[3] = 0.6f * powered / lit;
    l.dirCone[3] = -2;
    out.push_back(l);
  }
  // At most two: the rest merge into the nearest kept light by summing intensity.
  while (out.size() > static_cast<std::size_t>(kMaxSlabGlowLights)) {
    const auto last = out.back();
    out.pop_back();
    std::size_t best = 0;
    float bestD = 1e30f;
    for (std::size_t k = 0; k < static_cast<std::size_t>(kMaxSlabGlowLights); ++k) {
      const float d = std::hypot(out[k].posRadius[0] - last.posRadius[0],
                                 out[k].posRadius[1] - last.posRadius[1],
                                 out[k].posRadius[2] - last.posRadius[2]);
      if (d < bestD) {
        bestD = d;
        best = k;
      }
    }
    out[best].colorIntensity[3] += last.colorIntensity[3];
  }
  return out;
}

void constrainGameCamera(app::Session& session, const RigLayout& layout) {
  const auto c = session.playerCamera();
  const float inset = kRoomInsetMetres * kCellsPerMetre * layout.xform.labScale;
  const float limit = view::clampOrbitDistance(c, c.distance, layout.room, inset);
  if (limit < c.distance) session.limitOrbitDistance(limit);
}

float labPartFade(const io::LabPart& part, const view::Bounds& bounds,
                  const view::Vec3& eye, const view::Vec3& target,
                  const RigLayout& layout) noexcept {
  if (part.group != "room" && part.group != "clutter" && part.group != "bench") return 1;
  return view::wallFade(bounds, eye, target,
                        kWallFadePadMetres * kCellsPerMetre * layout.xform.labScale);
}

view::OrbitCamera toBoardCamera(const view::OrbitCamera& station,
                                const view::RigXform& x) noexcept {
  auto c = station;
  c.target = x.toBoard(station.target);
  c.distance *= x.labScale;
  c.nearZ *= x.labScale;
  c.farZ *= x.labScale;
  return c;
}

void placeInstances(std::vector<Instance>& list, const view::RigXform& x) noexcept {
  for (auto& i : list) {
    const auto c = x.toBoard({i.center[0], i.center[1], i.center[2]});
    i.center[0] = c.x;
    i.center[1] = c.y;
    i.center[2] = c.z;
    for (float& s : i.scale) s *= x.labScale;
  }
}

}  // namespace cb::render
