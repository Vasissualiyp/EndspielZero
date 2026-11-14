// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/lab_rig.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "app/session.hpp"

namespace cb::render {
namespace {
constexpr float kBodyBelowFace = 0.13f;  ///< cell centre to the slab body's centre
constexpr float kBodyHalf = 0.185f;      ///< half the slab's thickness, back to cell top
constexpr float kMinMastCells = 0.62f * kCellsPerMetre;
constexpr float kTubeCells = 0.6f * kCellsPerMetre;   ///< one tube's length
constexpr float kReachCells = 1.15f * kCellsPerMetre;  ///< both tubes, overlapped
constexpr float kGlowLiftCells = 0.12f * kCellsPerMetre;
constexpr float kGlowRadiusMetres = 2.5f;
constexpr std::array<float, 4> kIdentity{0, 0, 0, 1};

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
view::Vec3 partCentre(const LabScene& lab, const view::RigXform& x, std::string_view name) {
  const auto* p = lab.part(name);
  return p ? x.toBoard(lab.toWorld(p->pos)) : view::Vec3{};
}
}  // namespace

std::optional<RigLayout> rigLayout(const LabScene& lab,
                                   const std::vector<view::Placement>& placements) {
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
  out.xform = view::placeLayout(out.layoutBounds, out.volume);
  out.slabs = view::slabsOf(placements, kBezelMargin);
  out.groups = view::mastGroups(out.slabs, out.xform, out.volume);
  out.travels = static_cast<int>(out.groups.size()) <= view::kMaxTravellingGroups;
  out.lastLock = view::deployLastLock(static_cast<int>(out.groups.size()));
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

  out.bay.assign(out.slabs.size(), -1);
  out.paths.resize(out.slabs.size());
  for (std::uint32_t g = 0; g < out.groups.size(); ++g) {
    const auto& group = out.groups[g];
    const auto& top = out.slabs[group.slabs.front()];
    const int topSlot = std::min<int>(static_cast<int>(group.slabs.front()), 7);
    const auto topBay = partCentre(lab, x, bayName(lab, topSlot));
    // The lowest slab's yoke rides just under the rail; the stack keeps its spacing.
    const float bottomZ = out.slabs[group.slabs.back()].z;
    const float liftBottom = group.mastTopZ - kMinMastCells + yokeBelow;
    for (const auto s : group.slabs) {
      const auto& slab = out.slabs[s];
      const int slot = std::min<int>(static_cast<int>(s), 7);
      out.bay[s] = s < 8 ? static_cast<int>(s) : -1;
      const auto* bayPart = lab.part(bayName(lab, slot));
      auto& path = out.paths[s];
      path.bayCentre = partCentre(lab, x, bayName(lab, slot));
      path.bayQuat =
          bayPart ? quatFromYawPitchRoll(radians(bayPart->rot[0]), radians(bayPart->rot[1]),
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
      path.pickCentre = {pickX,
                         std::clamp(topBay.y, railMinY - path.mastOffset.y,
                                    railMaxY - path.mastOffset.y),
                         topBay.z - below};
      path.departure =
          view::kDeployFirstDeparture + view::kDeployStagger * static_cast<float>(g);
      path.parkCarriage = park;
    }
  }
  return out;
}

std::vector<view::SlabPose> rigPoses(const RigLayout& layout, RigPhase phase, float age,
                                     float intensity) {
  std::vector<view::SlabPose> out;
  out.reserve(layout.paths.size());
  const auto groups = static_cast<int>(layout.groups.size());
  for (std::size_t i = 0; i < layout.paths.size(); ++i) {
    const auto& path = layout.paths[i];
    if (!layout.travels || phase == RigPhase::Rest) {
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
    if (intensity < .5f) {
      out.push_back(view::slabDeploy(reversed, -1, 1));
      continue;
    }
    const int g = static_cast<int>(layout.slabGroup[i]);
    const float u = age - view::kDeployStagger * static_cast<float>(groups - 1 - g);
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
          {std::lerp(bayW, width, f) / backSize, std::lerp(bayD, depth, f) / backSize, 1});
      if (pose.quat[3] > .9999f) {
        // The yoke runs under the slab from the mast axis to the back's boss.
        const float dx = path.mastOffset.x, dy = path.mastOffset.y;
        const float len = std::hypot(dx, dy);
        const float yaw = std::atan2(-dx, dy);
        add(RigPiece::Kind::Yoke, gi, si, "rig.yoke",
            {pose.centre.x + dx * .5f, pose.centre.y + dy * .5f,
             pose.centre.z - path.yokeBelow},
            quatFromYawPitchRoll(yaw, 0, 0), {1, len / (0.03f * kCellsPerMetre), 1});
      }
    }
  }
  return pieces;
}

void carryBoard(InstanceSet& set, const RigLayout& layout,
                const std::vector<view::SlabPose>& poses) {
  if (layout.slabs.empty() || poses.size() != layout.slabs.size()) return;
  if (std::all_of(poses.begin(), poses.end(), [](const auto& p) { return p.clamped; }))
    return;
  // Per slab: its pivot (the deployed centre), its pose and its size from edge-on in the
  // bay (the parked slab's footprint) to its own once face-up.
  struct Carry {
    bool moves{false}, hidden{false};
    view::Vec3 pivot, centre, scale{1, 1, 1};
    std::array<float, 4> quat{};
  };
  std::vector<Carry> carry(layout.slabs.size());
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
  // An instance belongs to the slab under it: inside its bezel, on the highest face at
  // or below it (a plinth hangs just under its own face).
  const auto slabOf = [&](const Instance& i) -> int {
    int best = -1;
    float bestDz = 1e30f;
    for (std::size_t s = 0; s < layout.slabs.size(); ++s) {
      const auto& slab = layout.slabs[s];
      if (i.center[0] < slab.minX - .05f || i.center[0] > slab.maxX + .05f ||
          i.center[1] < slab.minY - .05f || i.center[1] > slab.maxY + .05f)
        continue;
      const float dz = i.center[2] - slab.z;
      const float rank = dz >= -view::kSlabBelowFace - .05f ? dz : 1e6f - dz;
      if (rank < bestDz) {
        bestDz = rank;
        best = static_cast<int>(s);
      }
    }
    return best;
  };
  const auto move = [&](Instance& i) {
    const int s = slabOf(i);
    if (s < 0 || !carry[static_cast<std::size_t>(s)].moves) return;
    const auto& c = carry[static_cast<std::size_t>(s)];
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
  };
  for (auto* list : {&set.instances, &set.shadows, &set.flourish})
    for (auto& i : *list) move(i);
  if (set.standbyLed) move(*set.standbyLed);
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
    if (layout.bay[i] == slot && (!layout.travels || !poses[i].inBay)) return true;
  return false;
}

std::vector<bool> slabsShown(const RigLayout& layout,
                             const std::vector<view::SlabPose>& poses) {
  std::vector<bool> out(layout.slabs.size(), true);
  for (std::size_t i = 0; i < out.size() && i < poses.size(); ++i)
    out[i] = poses[i].clamped;
  return out;
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
