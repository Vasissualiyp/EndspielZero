// SPDX-License-Identifier: GPL-3.0-or-later
//
// The slab skin's hardware (M24.5b). The surface maths is the existing morph, unchanged;
// everything here is dressing driven by it: ribs read the posed surface, latches read the
// morph's own stage table, and the Klein band is where the sheet meets itself.
#include "render/slab_skin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

#include "app/overture.hpp"
#include "view/morph.hpp"

namespace cb::render {
namespace {

float smooth(float edge0, float edge1, float x) {
  const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

const SurfaceSeat& nearestSeat(const PlaySurface& s, const view::Vec3& p) {
  const SurfaceSeat* best = &s.seats().front();
  float bestD = std::numeric_limits<float>::max();
  for (const SurfaceSeat& seat : s.seats()) {
    const float d = view::length(p - seat.centre);
    if (d < bestD) {
      bestD = d;
      best = &seat;
    }
  }
  return *best;
}

/// The seat of the lattice cell containing (file, rank), clamped to the board.
const SurfaceSeat& latticeSeat(const PlaySurface& s, float file, float rank) {
  const int f = std::clamp(static_cast<int>(std::floor(file)), 0, s.files() - 1);
  const int r = std::clamp(static_cast<int>(std::floor(rank)), 0, s.ranks() - 1);
  return s.seats()[static_cast<std::size_t>(f * s.ranks() + r)];
}

/// The posed surface's normal at a lattice point, from its own tangents, turned to agree
/// with the seat it lies in (the seats carry the continuity walk and the invert).
view::Vec3 normalAt(const PlaySurface& s, float file, float rank) {
  constexpr float h = 0.05f;
  view::Vec3 a{}, b{}, c{}, d{};
  (void)s.pointAt(file + h, rank, a);
  (void)s.pointAt(file - h, rank, b);
  (void)s.pointAt(file, rank + h, c);
  (void)s.pointAt(file, rank - h, d);
  view::Vec3 n = view::cross(c - d, a - b);
  const SurfaceSeat& seat = latticeSeat(s, file, rank);
  if (view::length(n) < 1e-7f) return seat.normal;
  n = view::normalize(n);
  return view::dot(n, seat.normal) < 0.0f ? n * -1.0f : n;
}

/// Lattice distance between two cells, with the variant's wraps: a seam that reverses
/// the other coordinate is crossed with that coordinate mirrored (Chebyshev).
int latticeDistance(const PlaySurface& s, int f1, int r1, int f2, int r2) {
  const int nx = s.files();
  const int nz = s.ranks();
  const auto along = [](int a, int b, int n, bool wrap) {
    const int d = std::abs(a - b);
    return wrap ? std::min(d, n - d) : d;
  };
  const bool plainU = s.wraps(0) && !s.flipsAcross(0);
  const bool plainV = s.wraps(1) && !s.flipsAcross(1);
  int best = std::max(along(f1, f2, nx, plainU), along(r1, r2, nz, plainV));
  if (s.wraps(0) && s.flipsAcross(0))
    best = std::min(best,
                    std::max(nx - std::abs(f1 - f2), along(r1, nz - 1 - r2, nz, plainV)));
  if (s.wraps(1) && s.flipsAcross(1))
    best = std::min(best,
                    std::max(along(f1, nx - 1 - f2, nx, plainU), nz - std::abs(r1 - r2)));
  return best;
}

}  // namespace

std::vector<Rib> slabRibs(const PlaySurface& shaped, int count, const view::Vec3& yoke,
                          float formed, const std::vector<CellId>* skip,
                          const view::Vec3* eye) {
  std::vector<Rib> ribs;
  if (shaped.stacked() || shaped.empty() || count <= 0) return ribs;
  // Each rib grows out from its middle over the first stretch of the morph.
  const float grow = smooth(0.0f, 0.35f, formed);
  if (!(formed > 0.0f) || grow <= 0.0f) return ribs;
  // A rib point is kept only where it is clearly behind the sheet: on a Klein bottle the
  // figure-eight's lobes are thinner than the offset, so a point can come out through the
  // far wall of its lobe, and there the rib is left out rather than drawn in front.
  const auto skipped = [&](const view::Vec3& p) {
    const SurfaceSeat& seat = nearestSeat(shaped, p);
    if (view::dot(p - seat.centre, seat.normal) > -0.5f * kRibBehindCells) return true;
    if (eye != nullptr && !shaped.blocked(*eye, p, kRibEyeEps)) return true;
    if (skip == nullptr || skip->empty()) return false;
    return std::binary_search(skip->begin(), skip->end(), seat.cell);
  };
  for (int axis = 0; axis < 2; ++axis) {
    if (!shaped.wraps(axis)) continue;
    const int length = axis == 0 ? shaped.files() : shaped.ranks();
    const int across = axis == 0 ? shaped.ranks() : shaped.files();
    const float n = static_cast<float>(length);
    const float mid = 0.5f * n;
    const float half = grow * mid;
    // One point per cell, at the cell-centre line: a point between two seats sits off
    // their tangent planes by the surface's sagitta, which on a stretched cell (a torus's
    // outer equator, the Moebius loop) is more than the rib's 0.05-cell tolerance. A
    // grown rib is the closed loop; one still growing ends exactly at its tips. Across a
    // seam that reverses the other coordinate (the Moebius files) the sheet's sides swap,
    // so that rib stops at the seam rather than closing onto the playing side.
    std::vector<float> ts;
    if (grow >= 1.0f) {
      for (float t = 0.5f; t < n; t += 1.0f) ts.push_back(t);
      if (!shaped.flipsAcross(axis)) ts.push_back(n + 0.5f);
    } else {
      ts.push_back(mid - half);
      for (float t = 0.5f; t < n; t += 1.0f)
        if (std::abs(t - mid) < half) ts.push_back(t);
      ts.push_back(mid + half);
    }
    for (int k = 0; k < count; ++k) {
      // Evenly spaced at (k + 0.5) * size / count, snapped onto the nearest cell-centre
      // line: on 8 ranks two of the six even lines fall on a cell boundary, where the
      // roll's curvature puts the rib up to 0.25 cells off its offset from every seat.
      const float even = (static_cast<float>(k) + 0.5f) * static_cast<float>(across) /
                         static_cast<float>(count);
      const float o =
          std::clamp(std::floor(even), 0.0f, static_cast<float>(across - 1)) + 0.5f;
      Rib rib;
      for (const float t : ts) {
        const float file = axis == 0 ? t : o;
        const float rank = axis == 0 ? o : t;
        view::Vec3 p{};
        if (!shaped.pointAt(file, rank, p)) continue;
        p = p - normalAt(shaped, file, rank) * kRibBehindCells;
        // The chord to the previous point must stay behind the sheet too.
        if (skipped(p) ||
            (!rib.points.empty() && skipped((p + rib.points.back()) * 0.5f))) {
          if (rib.points.size() >= 2) ribs.push_back(std::move(rib));
          rib = {};
          continue;
        }
        rib.points.push_back(p);
      }
      if (rib.points.size() >= 2) ribs.push_back(std::move(rib));
    }
  }
  // Two spokes: from the yoke to the nearest point of each of the two nearest ribs.
  std::vector<std::pair<float, std::size_t>> near;
  std::vector<view::Vec3> nearest(ribs.size());
  for (std::size_t r = 0; r < ribs.size(); ++r) {
    float best = std::numeric_limits<float>::max();
    for (const view::Vec3& p : ribs[r].points) {
      const float d = view::length(p - yoke);
      if (d < best) {
        best = d;
        nearest[r] = p;
      }
    }
    near.emplace_back(best, r);
  }
  std::sort(near.begin(), near.end());
  const std::size_t spokes = std::min<std::size_t>(2, near.size());
  for (std::size_t i = 0; i < spokes; ++i)
    ribs.push_back(Rib{{yoke, nearest[near[i].second]}});
  return ribs;
}

std::vector<Rib> outsideHoops(const PlaySurface& shaped, int count, float formed) {
  std::vector<Rib> hoops;
  if (shaped.stacked() || shaped.empty() || count <= 0 ||
      shaped.kind() != app::SurfaceKind::Torus)
    return hoops;
  const float grow = smooth(view::kTorusRollEnd, view::kTorusRollEnd + 0.2f, formed);
  if (grow <= 0.0f) return hoops;
  const float n = static_cast<float>(shaped.files());
  const float mid = 0.5f * n, half = grow * mid;
  // Two points per cell round the tube; a hoop still growing ends exactly at its tips.
  std::vector<float> ts;
  if (grow >= 1.0f) {
    for (float t = 0.0f; t <= n + 1e-4f; t += 0.5f) ts.push_back(t);
  } else {
    ts.push_back(mid - half);
    for (float t = 0.0f; t <= n; t += 0.5f)
      if (std::abs(t - mid) < half) ts.push_back(t);
    ts.push_back(mid + half);
  }
  const int ranks = shaped.ranks();
  for (int k = 0; k < count; ++k) {
    // Evenly spaced, on the nearest boundary between two rows of tiles.
    const float rank = std::round((static_cast<float>(k) + 0.5f) *
                                  static_cast<float>(ranks) / static_cast<float>(count));
    // Hardware belongs to the physical outside, independently of which side carries
    // the pieces. Once the tube has rolled shut, its section centre gives that side
    // even when the play normals are inverted or the tiles have slid around the tube.
    view::Vec3 centre{};
    constexpr int kSectionSamples = 32;
    for (int i = 0; i < kSectionSamples; ++i) {
      view::Vec3 p{};
      (void)shaped.pointAt(n * static_cast<float>(i) / kSectionSamples, rank, p);
      centre = centre + p * (1.0f / kSectionSamples);
    }
    Rib hoop;
    for (const float t : ts) {
      view::Vec3 p{};
      if (!shaped.pointAt(t, rank, p)) continue;
      auto normal = normalAt(shaped, std::min(t, n - 1e-3f), rank);
      if (view::dot(normal, p - centre) < 0.0f) normal = normal * -1.0f;
      hoop.points.push_back(p + normal * kHoopLiftCells);
    }
    if (grow >= 1.0f && hoop.points.size() >= 2) hoop.points.back() = hoop.points.front();
    if (hoop.points.size() >= 2) hoops.push_back(std::move(hoop));
  }
  return hoops;
}

std::array<float, 2> wrapCloseAt(app::SurfaceKind kind) {
  constexpr float none = std::numeric_limits<float>::quiet_NaN();
  // The seam rails' own stage table (`seamRailWeight`): an axis closes where its rail
  // has fully coloured in.
  switch (kind) {
    case app::SurfaceKind::Tube:
      return {1.0f, none};
    case app::SurfaceKind::Torus:
      return {view::kTorusRollEnd, 1.0f};
    case app::SurfaceKind::Klein:
      return {view::kKleinRingEnd, view::kKleinRingEnd};
    case app::SurfaceKind::Band:
      return {1.0f, none};
    default:
      return {none, none};
  }
}

std::array<float, 2> wrapCloseAt(const VariantSpec& v) {
  if (!hasPlaySurface(v) || v.dims.dims() != 2) {
    constexpr float none = std::numeric_limits<float>::quiet_NaN();
    return {none, none};
  }
  return wrapCloseAt(app::overtureSignature(v).surface);
}

bool latchCrossed(float before, float after, float closeAt) noexcept {
  if (std::isnan(closeAt)) return false;
  return before < closeAt && after >= closeAt;
}

float latchBoost(float age, float intensity) noexcept {
  if (intensity < 0.5f || age < 0.0f) return 1.0f;
  if (age < 0.06f) return 2.5f;
  if (age < 0.14f) return 1.6f;
  if (age < 0.25f) return 1.2f;
  return 1.0f;
}

std::vector<CellId> passThroughOf(const std::vector<SurfacePatch>& patches,
                                  const PlaySurface& lattice, float gap) {
  std::vector<CellId> out;
  const int nz = lattice.ranks();
  if (patches.empty() || nz <= 0 || !(gap > 0.0f)) return out;
  // A uniform spatial hash, cell size `gap`: each corner meets only its 27 buckets.
  struct Point {
    view::Vec3 p;
    int patch;
  };
  std::vector<Point> points;
  points.reserve(patches.size() * kSurfaceCorners);
  for (std::size_t i = 0; i < patches.size(); ++i)
    for (const view::Vec3& p : patches[i].pos) points.push_back({p, static_cast<int>(i)});
  const auto bucket = [&](const view::Vec3& p, int dx, int dy, int dz) {
    const auto q = [&](float x, int d) {
      return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::floor(x / gap)) +
                                        d + (1 << 20)) &
             0x1FFFFFu;
    };
    return (q(p.x, dx) << 42) | (q(p.y, dy) << 21) | q(p.z, dz);
  };
  std::unordered_map<std::uint64_t, std::vector<std::size_t>> grid;
  for (std::size_t i = 0; i < points.size(); ++i)
    grid[bucket(points[i].p, 0, 0, 0)].push_back(i);
  std::vector<bool> marked(patches.size(), false);
  for (std::size_t i = 0; i < points.size(); ++i) {
    const Point& a = points[i];
    if (marked[static_cast<std::size_t>(a.patch)]) continue;
    const int fa = a.patch / nz, ra = a.patch % nz;
    bool hit = false;
    for (int dx = -1; dx <= 1 && !hit; ++dx)
      for (int dy = -1; dy <= 1 && !hit; ++dy)
        for (int dz = -1; dz <= 1 && !hit; ++dz) {
          const auto it = grid.find(bucket(a.p, dx, dy, dz));
          if (it == grid.end()) continue;
          for (const std::size_t j : it->second) {
            const Point& b = points[j];
            if (b.patch == a.patch || view::length(b.p - a.p) >= gap) continue;
            if (latticeDistance(lattice, fa, ra, b.patch / nz, b.patch % nz) > 2) {
              hit = true;
              break;
            }
          }
        }
    if (hit) marked[static_cast<std::size_t>(a.patch)] = true;
  }
  for (std::size_t i = 0; i < patches.size(); ++i)
    if (marked[i]) out.push_back(patches[i].cell);
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<CellId> passThroughCells(const PlaySurface& shaped, float gap) {
  if (shaped.stacked() || shaped.empty()) return {};
  return passThroughOf(shaped.patches(), shaped, gap);
}

Instance cylinderBetween(const view::Vec3& a, const view::Vec3& b, float diameter) {
  Instance bar{};
  const view::Vec3 d = b - a;
  const float len = view::length(d);
  if (len < 1e-5f) return bar;
  const view::Vec3 dir = d * (1.0f / len);
  // The cylinder's axis is its local +Z: turn it onto the segment.
  const view::Vec3 axis = view::cross(view::Vec3{0.0f, 0.0f, 1.0f}, dir);
  const float w = 1.0f + dir.z;
  std::array<float, 4> q{axis.x, axis.y, axis.z, w};
  if (w < 1e-6f) q = {1.0f, 0.0f, 0.0f, 0.0f};
  const float qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  for (float& c : q) c /= qn;
  const view::Vec3 mid = (a + b) * 0.5f;
  bar.center[0] = mid.x;
  bar.center[1] = mid.y;
  bar.center[2] = mid.z;
  bar.scale[0] = diameter;
  bar.scale[1] = diameter;
  bar.scale[2] = len + diameter * 0.5f;
  std::copy(q.begin(), q.end(), bar.quat);
  return bar;
}

std::size_t insertLabCylinders(InstanceSet& set, const std::vector<Instance>& bars) {
  if (bars.empty()) return 0;
  auto& batch = set.labBatches[static_cast<std::size_t>(LabShape::Cylinder)];
  const std::uint32_t at = batch.first + batch.count;
  const auto n = static_cast<std::uint32_t>(bars.size());
  set.lab.insert(set.lab.begin() + at, bars.begin(), bars.end());
  bool after = false;
  for (auto& b : set.labBatches) {
    if (after) b.first += n;
    if (&b == &batch) after = true;
  }
  for (auto& b : set.labFileBatches)
    if (b.first >= at) b.first += n;
  batch.count += n;
  return bars.size();
}

std::size_t appendRibs(InstanceSet& set, const std::vector<Rib>& ribs, float diameter) {
  std::vector<Instance> bars;
  const view::Rgba metal = view::Rgba::hex(0x2A2E35);
  for (const Rib& rib : ribs) {
    for (std::size_t i = 1; i < rib.points.size(); ++i) {
      if (view::length(rib.points[i] - rib.points[i - 1]) < 1e-5f) continue;
      Instance bar = cylinderBetween(rib.points[i - 1], rib.points[i], diameter);
      bar.color[0] = metal.r;
      bar.color[1] = metal.g;
      bar.color[2] = metal.b;
      bar.color[3] = 1.0f;
      bar.material = shaderMaterial(view::Material::Metal, Cut::Plank, 0.0f);
      bars.push_back(bar);
    }
  }
  return insertLabCylinders(set, bars);
}

}  // namespace cb::render
