// SPDX-License-Identifier: GPL-3.0-or-later
//
// The play board as its own shape (M17). Where every cell of a glued two-dimensional
// variant goes once the board stops being a picture of a topology and becomes it.
//
// Three things live here because they must agree: the patch of surface each square is,
// the seat a piece stands on, and the ray a click is tested against. They are all read
// off one sampling of the surface, which is the cheapest way to keep ADR-0011's invariant
// - the pick follows what was drawn - true by construction rather than by care.
#include "render/play_surface.hpp"

#include <algorithm>
#include <cmath>

#include "app/overture.hpp"

namespace cb::render {
namespace {

/// The overture world is Y-up; the board's world is Z-up and the camera orbits about Z.
/// One quarter turn about X takes one to the other. Without it a torus stands on its rim
/// like a wheel and a cylinder lies on its side under a camera that cannot get above
/// either - which is how the surface first arrived.
view::Vec3 upright(const OvVec3& p) {
  return {p.x, -p.z, p.y};
}

view::Vec3 sample(app::SurfaceKind kind, SurfacePose pose, float u, float v) {
  return upright(derivedSurfaceAt(kind, u, v, pose));
}

/// The outward normal at a lattice fraction, from the surface's own tangents.
///
/// The differences are deliberately *unclamped*: every warp in the catalogue is a smooth
/// function of the lattice coordinate well outside [0, 1] - which is also what makes the
/// slide work - so sampling past an edge is the honest tangent, while clamping halves it
/// and leaves the rim corners facing the wrong way.
view::Vec3 normalAt(app::SurfaceKind kind, SurfacePose pose, float u, float v, int nx,
                    int nz) {
  const float hu = 0.2f / static_cast<float>(nx);
  const float hv = 0.2f / static_cast<float>(nz);
  const view::Vec3 du = sample(kind, pose, u + hu, v) - sample(kind, pose, u - hu, v);
  const view::Vec3 dv = sample(kind, pose, u, v + hv) - sample(kind, pose, u, v - hv);
  // Out of the surface, not into it: `cross(dv, du)` is the outward side for every warp
  // in the catalogue, checked on the cylinder where "outward" is not a matter of taste.
  const view::Vec3 n = view::cross(dv, du);
  const float len = view::length(n);
  if (len < 1e-7f) return {0.0f, 0.0f, 1.0f};  // a pinch, or a hole closed to nothing
  return n * (1.0f / len);
}

/// Nearest positive ray parameter at which `ray` enters the capsule of radius `r` about
/// the segment `[a, b]`, or false. Analytic - no per-triangle mesh test - because the
/// shape view's piece pick only has to bound the silhouette a click may land on, not be
/// pixel-perfect (M17.27). A capsule is the union of the finite cylinder with its two
/// end spheres, and all three are convex, so the entry is the nearest of their own
/// entries; the camera always starts outside the piece.
bool rayCapsule(const view::OrbitCamera::Ray& ray, const view::Vec3& a,
                const view::Vec3& b, float r, float& tOut) {
  const view::Vec3 ba = b - a;
  const view::Vec3 oa = ray.origin - a;
  const float baba = view::dot(ba, ba);
  const float bard = view::dot(ba, ray.direction);
  const float baoa = view::dot(ba, oa);
  const float rdoa = view::dot(ray.direction, oa);
  const float oaoa = view::dot(oa, oa);
  const float A = baba - bard * bard;
  float best = 1e30f;
  bool found = false;
  // The curved wall, when the ray is not parallel to the axis. The root is inside the
  // finite segment when its axial coordinate `y` lies in `[0, baba]`.
  if (A > 1e-9f) {
    const float B = baba * rdoa - baoa * bard;
    const float C = baba * oaoa - baoa * baoa - r * r * baba;
    const float h = B * B - A * C;
    if (h >= 0.0f) {
      const float t = (-B - std::sqrt(h)) / A;
      const float y = baoa + t * bard;
      if (y > 0.0f && y < baba && t > 1e-4f) {
        best = t;
        found = true;
      }
    }
  }
  // The two end caps, as hemispheres. A ray parallel to the axis - or one that enters
  // through the flat top - is found here and nowhere else.
  for (const view::Vec3& c : {a, b}) {
    const view::Vec3 oc = ray.origin - c;
    const float bb = view::dot(ray.direction, oc);
    const float h = bb * bb - (view::dot(oc, oc) - r * r);
    if (h < 0.0f) continue;
    const float t = -bb - std::sqrt(h);
    if (t > 1e-4f && t < best) {
      best = t;
      found = true;
    }
  }
  if (!found) return false;
  tOut = best;
  return true;
}

/// A unit quaternion from an orthonormal, right-handed frame given as the images of the
/// local axes. The standard branch-on-the-largest-diagonal form: the naive trace formula
/// loses all its precision when the rotation is near a half-turn, which on a closed
/// surface is most of the far side.
std::array<float, 4> quatOf(const view::Vec3& ex, const view::Vec3& ey,
                            const view::Vec3& ez) {
  const float m00 = ex.x, m10 = ex.y, m20 = ex.z;
  const float m01 = ey.x, m11 = ey.y, m21 = ey.z;
  const float m02 = ez.x, m12 = ez.y, m22 = ez.z;
  const float trace = m00 + m11 + m22;
  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    return {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
  }
  if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    return {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
  }
  if (m11 > m22) {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    return {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
  }
  const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
  return {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
}

/// Every warp in the catalogue repeats after two laps of either lattice axis - one lap
/// for the orientable ones, two where a seam reverses a coordinate and the board has to
/// go round twice to come home. `period` is that distance in *cells* (two laps, `2 * nx`
/// or `2 * nz`); the wrap lives here, in cells, because only the board knows its own
/// extent (M17.9). Keeping the slide inside that window costs nothing and stops a long
/// drag grinding the trigonometry down to noise.
float wrapSlide(float s, float period) {
  return std::fmod(s, period);
}

/// How high a gliding piece hovers, in cells, while it travels. It reaches the genuine
/// surface point at each boundary, so the path only needs a small lift to clear the half-
/// cell chords either side of it; 0.4 of a cell reads as a hover without floating
/// (M17.19).
constexpr float kSurfaceHoverCells = 0.4f;

/// The hover's own envelope over the move: 0 on the square at either end, 1 through the
/// body, with a smooth ramp at each end so the piece does not pop.
float hoverEnvelope(float t) {
  const float r = std::clamp(std::min(t, 1.0f - t) / 0.22f, 0.0f, 1.0f);
  return r * r * (3.0f - 2.0f * r);
}

}  // namespace

float shapeEase(float local) noexcept {
  const float t = std::clamp(local, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float seamRailWeight(app::SurfaceKind kind, int axis, float formed) noexcept {
  const float f = std::clamp(formed, 0.0f, 1.0f);
  float begin = 0.0f;
  float end = 1.0f;
  switch (kind) {
    case app::SurfaceKind::Tube:
      if (axis != 0) return 1.0f;
      break;
    case app::SurfaceKind::Torus:
      if (axis == 0) {
        end = view::kTorusRollEnd;
      } else if (axis == 1) {
        begin = view::kTorusRollEnd;
      } else {
        return 1.0f;
      }
      break;
    case app::SurfaceKind::Klein:
      if (axis != 0 && axis != 1) return 1.0f;
      end = view::kKleinRingEnd;
      break;
    case app::SurfaceKind::Band:
      if (axis != 0) return 1.0f;
      begin = kRibbonStretchWindow;
      break;
    default:
      return 1.0f;
  }
  return shapeEase(stageForm(f, begin, end));
}

float plinthOpacity(app::SurfaceKind kind, float formed) noexcept {
  float firstStageEnd = 0.0f;
  switch (kind) {
    case app::SurfaceKind::Tube:
      firstStageEnd = 1.0f;
      break;
    case app::SurfaceKind::Torus:
      firstStageEnd = view::kTorusRollEnd;
      break;
    case app::SurfaceKind::Klein:
      firstStageEnd = view::kKleinRingEnd;
      break;
    case app::SurfaceKind::Band:
      firstStageEnd = kRibbonStretchWindow;
      break;
    default:
      return 0.0f;
  }
  const float f = std::clamp(formed, 0.0f, 1.0f);
  return 1.0f - shapeEase(stageForm(f, 0.0f, kPlinthFadeShare * firstStageEnd));
}

ShapeBeat shapeBeat(float elapsed, float alignSeconds, float approachSeconds,
                    float travelSeconds, float returnSeconds) noexcept {
  float t = std::max(0.0f, elapsed);
  if (alignSeconds > 0.0f) {
    if (t < alignSeconds) return {ShapeStage::Align, t / alignSeconds};
    t -= alignSeconds;
  }
  if (approachSeconds > 0.0f) {
    if (t < approachSeconds) return {ShapeStage::Approach, t / approachSeconds};
    t -= approachSeconds;
  }
  if (travelSeconds > 0.0f) {
    if (t < travelSeconds) return {ShapeStage::Travel, t / travelSeconds};
    t -= travelSeconds;
  }
  if (returnSeconds > 0.0f) {
    if (t < returnSeconds) return {ShapeStage::Return, t / returnSeconds};
    t -= returnSeconds;
  }
  return {ShapeStage::Done, 1.0f};
}

bool PlaySurface::slidesAlongRanks(const VariantSpec& v) noexcept {
  const app::SurfaceKind kind = app::overtureSignature(v).surface;
  return kind == app::SurfaceKind::Torus || kind == app::SurfaceKind::Klein;
}

SurfaceRail surfaceRail(const view::SeamMap& seams, CellId cell, int axis, int i, int j) {
  SurfaceRail out;
  if (axis != 0 && axis != 1) return out;
  // The rail runs along a wrap edge, and there are two corners that meet there: the low
  // end of the coordinate-0 row is that cell's Min face, the high end of the
  // coordinate-(extent-1) row is the next cell's Max face. The two sit at one coincident
  // physical point, but the board is built from per-cell vertex copies, so each corner
  // has to be tinted on its own cell - checking only the Min corner left the far side of
  // the join plain and the ring faded in from one side (M17.21 revision).
  const int corner = axis == 0 ? i : j;
  Side want;
  if (corner == 0) {
    want = Side::Min;
  } else if (corner == kSurfaceSubdiv) {
    want = Side::Max;
  } else {
    return out;
  }
  for (const view::SeamFace& f : seams.at(cell)) {
    if (static_cast<int>(f.screenAxis) != axis) continue;
    if (f.side != want) continue;
    if (f.kind != view::SeamKind::Glued) continue;  // a mirror leads nowhere; no hue
    out.color = f.color;
    out.weight = 1.0f;
    return out;
  }
  return out;
}

bool PlaySurface::pointAt(float file, float rank, view::Vec3& out) const {
  if (stacked_ || nx_ <= 0 || nz_ <= 0) return false;
  out = sample(kind_, surfacePose_, file / static_cast<float>(nx_) + su_,
               rank / static_cast<float>(nz_) + sv_);
  return true;
}

bool PlaySurface::nearestBoundary(const SurfaceSeat& a, const SurfaceSeat& b,
                                  view::Vec3& out) const {
  if (stacked_ || nx_ <= 0 || nz_ <= 0) return false;
  const auto shortStep = [](float d, int extent) {
    const float e = static_cast<float>(extent);
    if (d > e * 0.5f) d -= e;
    if (d < -e * 0.5f) d += e;
    return d;
  };
  // Seat centres are at (file + 0.5, rank + 0.5); the boundary is half a step on from
  // there, so the half-cell offset belongs in the base too - without it the waypoint sat
  // half a cell off the line of an orthogonal step (a corner, then the middle of the
  // edge).
  const float bf = static_cast<float>(a.file) + 0.5f +
                   shortStep(static_cast<float>(b.file - a.file), nx_) * 0.5f;
  const float br = static_cast<float>(a.rank) + 0.5f +
                   shortStep(static_cast<float>(b.rank - a.rank), nz_) * 0.5f;
  return pointAt(bf, br, out);
}

PlaySurface PlaySurface::build(const VariantSpec& v, SurfacePose pose) {
  PlaySurface s;
  // Above two dimensions the shape is authored, not a parametrised surface (M17.12).
  OvVec3 probe;
  if (v.dims.dims() >= 3 && playShapePosition(v, 0, probe)) {
    s.buildStacked(v, pose);
    return s;
  }
  if (!hasPlaySurface(v)) return s;
  // INVERT swaps which side is out, it does not move the surface (M17.7, revised). The
  // geometric eversion the library's shapes carry mirrors the whole board about the
  // origin
  // - every cell lands somewhere else - which is not what a board wants. The play surface
  // keeps the geometry and flips the normal the seat and the patch read, so the pieces
  // move to the other face while the squares stay put.
  const bool inverted = pose.evert > 0.5f;
  pose.evert = 0.0f;
  const app::SurfaceKind kind = app::overtureSignature(v).surface;
  // The ribbon is the one warp whose two axes trade against each other, and the play
  // board spends only `kRibbonStretch` of the library's stretch on it (see the header).
  // The morph eases that in *before* the ring curls (M18 revision, item 2): the first
  // `kRibbonStretchWindow` of `formed` is spent stretching with the ring angle held at
  // zero, and the remaining fraction is the curl - so the ribbon visibly elongates, then
  // loops, rather than arriving fully stretched on frame 0. The stretch is inert for
  // every other warp; those take the play value and keep the one-ramp morph.
  const float form = std::clamp(pose.formed, 0.0f, 1.0f);
  if (kind == app::SurfaceKind::Band) {
    pose.stretch = kRibbonStretch * stageForm(form, 0.0f, kRibbonStretchWindow);
    pose.formed = stageForm(form, kRibbonStretchWindow, 1.0f);
  } else {
    pose.stretch = kRibbonStretch;
  }
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  s.nx_ = nx;
  s.nz_ = nz;
  const float fnx = static_cast<float>(nx);
  const float fnz = static_cast<float>(nz);

  // The slide is in cells; the surface is parametrised over the whole board. Taken out of
  // the pose and folded into every sample, so nothing below this line knows about it.
  // Two laps is `2 * nx` cells along the files and `2 * nz` along the ranks, so a full
  // drag carries a1 all the way round and home rather than snapping after two squares.
  float su = wrapSlide(pose.slideU, 2.0f * fnx) / fnx;
  // The rank window is two laps for every warp whose rank seam only reverses a
  // coordinate (its twist closes after one or two) - including Klein with a nonzero
  // square shift, once the compensation just below cancels the shift's own
  // v-dependence. An earlier revision widened this window to `2 * nz * nx` for a
  // nonzero shift, reasoning that `kleinSurf`'s spiral (`lemniscateAngle(u +
  // collapsePhase * v)`) needed that many laps to close; it does, on its own, but the
  // compensation below makes the *rendered* pattern never depend on the slide amount in
  // the first place, so there is no longer a longer period to protect against. Reverted
  // to the plain two laps every other warp already uses.
  float sv = slidesAlongRanks(v) ? wrapSlide(pose.slideV, 2.0f * fnz) / fnz : 0.0f;
  // A rank slide moves the board along the tube - which is open, and so has nowhere to go -
  // until the bend closes it into a ring. It comes in with the bend, or the first bent
  // frame would carry the whole board away from where the open tube left it.
  // The nearest equivalent lap, not the raw two-lap window: a slide accumulated by a long
  // middle-drag (125 cells) would otherwise carry the ring several laps round while the
  // bend ramps it in (M21). Bit-identical for |slideV| <= nz / 2.
  if (kind == app::SurfaceKind::Torus) {
    sv = view::nearestLap(pose.slideV, 0.0f, fnz) / fnz *
         stageForm(form, view::kTorusRollEnd, 1.0f);
  }
  // A nonzero square shift only lines the rank seam up at `sv == 0` by itself:
  // `kleinSurf` samples `lemniscateAngle(u + collapsePhase * v)`, and the seam's true
  // partner of `(u, rank 0)` is `(1 - u - collapsePhase * (v_lo + v_hi), rank nz - 1)`
  // for whatever two absolute v values the seam's two sides land on - `v_lo = sv`,
  // `v_hi = 1 + sv`, since every sample adds the same `sv` to its own lattice `v`. The
  // naive `(1 - u, 1)` partner `SeamMap`/the ring maths otherwise assume is only exactly
  // right at `collapsePhase * (v_lo + v_hi) == 0`; everywhere else the two sides drift
  // apart by exactly that many file-widths, which is the "purple edge touches the pink
  // edge" seam misalignment while dragging a Klein bottle with a square shift set.
  // Subtracting that same amount from `su` - the one knob that moves every sample's `u`
  // together - cancels it: proven by a differential sweep across every file, every
  // shift and every slide amount, down to float noise. It also happens to make `su`'s
  // own `phase * sv` term exactly cancel the sample's own `phase * v`'s `sv` term (the
  // algebra: `(u - phase * (0.5 + sv)) + phase * (vv + sv)` has no `sv` left in it) - the
  // whole spiral pattern stops moving with the slide at all, which is why the wider
  // window above is no longer needed.
  if (kind == app::SurfaceKind::Klein && pose.collapsePhase != 0.0f) {
    su -= pose.collapsePhase * (0.5f + sv);
  }
  pose.slideU = 0.0f;
  pose.slideV = 0.0f;
  s.kind_ = kind;
  s.surfacePose_ = pose;
  s.su_ = su;
  s.sv_ = sv;
  const auto at = [&](float u, float vv) { return sample(kind, pose, u + su, vv + sv); };

  // The gapless grid first: every patch and seat is cut from it, the pick ray is tested
  // against it, and the shape's axis is its cross-section centroid. Sampling the surface
  // once, up front, is what keeps the three in step - ADR-0011's invariant.
  const int cu = nx * kSubdiv + 1;
  const int cv = nz * kSubdiv + 1;
  s.corners_.reserve(static_cast<std::size_t>(cu * cv));
  for (int i = 0; i < cu; ++i) {
    for (int j = 0; j < cv; ++j) {
      s.corners_.push_back(at(static_cast<float>(i) / static_cast<float>(nx * kSubdiv),
                              static_cast<float>(j) / static_cast<float>(nz * kSubdiv)));
    }
  }

  // The shape's axis at each rank: the centroid of the cross-section, used only to seed
  // the normal walk at file 0 (M17.18).
  std::vector<view::Vec3> axisAt(static_cast<std::size_t>(cv));
  for (int j = 0; j < cv; ++j) {
    view::Vec3 sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < cu; ++i) {
      sum = sum + s.corners_[static_cast<std::size_t>(i * cv + j)];
    }
    axisAt[static_cast<std::size_t>(j)] = sum * (1.0f / static_cast<float>(cu));
  }

  // The normal field, built by a continuity walk rather than a per-point test against the
  // centroid. A non-orientable shape has no globally consistent outward normal, so the
  // one unavoidable flip has to land *somewhere*; the walk puts it exactly at the file
  // edge the variant already glues (it never closes the loop), instead of at the Klein
  // cross-section's pinch, where the centroid test divides by ~zero and the sign comes
  // out as noise two samples apart (M17.18).
  std::vector<view::Vec3> normals(static_cast<std::size_t>(cu * cv));
  for (int j = 0; j < cv; ++j) {
    const float fv = static_cast<float>(j) / static_cast<float>(nz * kSubdiv);
    for (int i = 0; i < cu; ++i) {
      const float fu = static_cast<float>(i) / static_cast<float>(nx * kSubdiv);
      view::Vec3 n = normalAt(kind, pose, fu + su, fv + sv, nx, nz);
      if (i == 0) {
        const view::Vec3 out = s.corners_[static_cast<std::size_t>(i * cv + j)] -
                               axisAt[static_cast<std::size_t>(j)];
        if (view::length(out) > 1e-5f && view::dot(n, out) < 0.0f) n = n * -1.0f;
      } else if (view::dot(n, normals[static_cast<std::size_t>((i - 1) * cv + j)]) <
                 0.0f) {
        n = n * -1.0f;
      }
      normals[static_cast<std::size_t>(i * cv + j)] = n;
    }
  }
  // Bilinear lookup into the already-continuous field, so two patch corners a fraction of
  // a cell apart cannot come back opposite.
  const auto nrm = [&](float u, float vv) {
    const float gi = std::clamp(u, 0.0f, 1.0f) * static_cast<float>(cu - 1);
    const float gj = std::clamp(vv, 0.0f, 1.0f) * static_cast<float>(cv - 1);
    const int i0 = static_cast<int>(std::floor(gi));
    const int j0 = static_cast<int>(std::floor(gj));
    const int i1 = std::min(i0 + 1, cu - 1);
    const int j1 = std::min(j0 + 1, cv - 1);
    const float a = gi - static_cast<float>(i0);
    const float b = gj - static_cast<float>(j0);
    view::Vec3 n =
        normals[static_cast<std::size_t>(i0 * cv + j0)] * ((1.0f - a) * (1.0f - b)) +
        normals[static_cast<std::size_t>(i1 * cv + j0)] * (a * (1.0f - b)) +
        normals[static_cast<std::size_t>(i0 * cv + j1)] * ((1.0f - a) * b) +
        normals[static_cast<std::size_t>(i1 * cv + j1)] * (a * b);
    n = view::length(n) > 1e-6f ? view::normalize(n) : view::Vec3{0.0f, 0.0f, 1.0f};
    return inverted ? n * -1.0f : n;
  };

  // The per-cell file tangent (`ex`), also by a continuity walk: seed file 0, then keep
  // each next file's tangent agreeing with the last. The piece's own frame reads it, so a
  // mid-board flip would tip the pieces too.
  std::vector<view::Vec3> exField(static_cast<std::size_t>(nx * nz));
  for (int r = 0; r < nz; ++r) {
    for (int f = 0; f < nx; ++f) {
      const float u = (static_cast<float>(f) + 0.5f) / fnx;
      const float vv = (static_cast<float>(r) + 0.5f) / fnz;
      const view::Vec3 n = nrm(u, vv);
      const view::Vec3 along = at(u + 0.5f / fnx, vv) - at(u - 0.5f / fnx, vv);
      const view::Vec3 flat = along - n * view::dot(along, n);
      view::Vec3 ex = view::length(flat) > 1e-6f ? view::normalize(flat)
                                                 : view::Vec3{1.0f, 0.0f, 0.0f};
      if (f > 0 &&
          view::dot(ex, exField[static_cast<std::size_t>((f - 1) * nz + r)]) < 0.0f) {
        ex = ex * -1.0f;
      }
      exField[static_cast<std::size_t>(f * nz + r)] = ex;
    }
  }

  s.seats_.reserve(static_cast<std::size_t>(nx * nz));
  s.patches_.reserve(static_cast<std::size_t>(nx * nz));
  constexpr int kSide = kSubdiv + 1;
  for (int f = 0; f < nx; ++f) {
    for (int r = 0; r < nz; ++r) {
      const CellId cell = v.dims.toCell(Coord::of({f, r}));
      const float u = (static_cast<float>(f) + 0.5f) / fnx;
      const float vv = (static_cast<float>(r) + 0.5f) / fnz;

      // The square, as a patch of the surface. Its corners run across the cell's own
      // lattice footprint, inset so the line between squares is a real gap in the shape
      // rather than a stripe painted on it.
      SurfacePatch patch;
      patch.cell = cell;
      for (int i = 0; i < kSide; ++i) {
        for (int j = 0; j < kSide; ++j) {
          const float ou = kCoverage * (static_cast<float>(i) / kSubdiv - 0.5f) / fnx;
          const float ov = kCoverage * (static_cast<float>(j) / kSubdiv - 0.5f) / fnz;
          const std::size_t k = static_cast<std::size_t>(i * kSide + j);
          patch.pos[k] = at(u + ou, vv + ov);
          patch.normal[k] = nrm(u + ou, vv + ov);
        }
      }
      s.patches_.push_back(patch);

      SurfaceSeat seat;
      seat.cell = cell;
      seat.file = f;
      seat.rank = r;
      seat.centre = at(u, vv);
      seat.normal = nrm(u, vv);
      // A piece is sized to the square it stands on, so the step is the distance to the
      // neighbouring seats rather than anything about the flat board.
      seat.stepU = 0.5f * (view::length(at(u + 1.0f / fnx, vv) - seat.centre) +
                           view::length(seat.centre - at(u - 1.0f / fnx, vv)));
      seat.stepV = 0.5f * (view::length(at(u, vv + 1.0f / fnz) - seat.centre) +
                           view::length(seat.centre - at(u, vv - 1.0f / fnz)));
      const view::Vec3 ex = exField[static_cast<std::size_t>(f * nz + r)];
      const view::Vec3 ey = view::cross(seat.normal, ex);
      if (view::length(ey) > 1e-6f) {
        seat.quat = quatOf(ex, view::normalize(ey), seat.normal);
      }
      s.seats_.push_back(seat);
    }
  }

  view::Bounds b;
  bool first = true;
  for (const view::Vec3& p : s.corners_) {
    if (first) {
      b = {p.x, p.y, p.z, p.x, p.y, p.z};
      first = false;
      continue;
    }
    b.minX = std::min(b.minX, p.x);
    b.minY = std::min(b.minY, p.y);
    b.minZ = std::min(b.minZ, p.z);
    b.maxX = std::max(b.maxX, p.x);
    b.maxY = std::max(b.maxY, p.y);
    b.maxZ = std::max(b.maxZ, p.z);
  }
  // Room for what stands on it: a piece is about a cell tall, and a camera framed on the
  // bare surface cuts the crowns off the pieces facing it.
  const float pad = 0.9f;
  b.minX -= pad;
  b.minY -= pad;
  b.minZ -= pad;
  b.maxX += pad;
  b.maxY += pad;
  b.maxZ += pad;
  s.bounds_ = b;
  return s;
}

float morphFollowBlend(float dt) noexcept {
  return std::clamp(dt * kMorphFollowRate, 0.0f, 1.0f);
}

bool easeMorphFraming(app::Session& session, const BoardOptions& options,
                      SurfacePose pose, float k) {
  if (!options.surface) return false;
  const PlaySurface surf = PlaySurface::build(session.variant(), pose);
  if (surf.empty()) return false;
  // `bounds()` already pads for the pieces, so headroom stays 0 (M17.11).
  session.easeFrameOn(surf.bounds(), 0.0f, k);
  return true;
}

void frameGeometryCamera(app::Session& session, const BoardOptions& options,
                         SurfacePose pose) {
  if (options.surface) {
    const PlaySurface surf = PlaySurface::build(session.variant(), pose);
    if (!surf.empty()) {
      // `bounds()` already pads for the pieces, so headroom would drop the shape down the
      // window (M17.11).
      session.frameOn(surf.bounds(), 0.0f);
      return;
    }
  }
  session.frameOn(view::boundsOf(session.placements()));
}

namespace {
const SurfaceSeat* seatOf(const PlaySurface& s, CellId c) {
  for (const SurfaceSeat& st : s.seats()) {
    if (st.cell == c) return &st;
  }
  return nullptr;
}
float fitOf(const SurfaceSeat& s) {
  return std::min(std::sqrt(s.stepU * s.stepV), 1.0f);
}
}  // namespace

SurfaceMoveSample surfaceMoveSample(const view::MovePath& path, const PlaySurface& surf,
                                    float t) {
  SurfaceMoveSample out;
  const SurfaceSeat* a = seatOf(surf, path.from);
  const SurfaceSeat* b = seatOf(surf, path.to);
  if (a == nullptr || b == nullptr) return out;
  t = std::clamp(t, 0.0f, 1.0f);
  constexpr float kPi = 3.14159265358979f;

  if (path.leap || path.steps.empty()) {
    // Arc directly from the start seat to the end, lifted outward along the blended
    // normal - the same `sin(pi t) * 0.65` the flat board's leap uses, so a knight clears
    // the shape rather than cutting through it.
    const view::Vec3 chord = a->centre + (b->centre - a->centre) * t;
    view::Vec3 n = a->normal + (b->normal - a->normal) * t;
    n = view::length(n) > 1e-6f ? view::normalize(n) : a->normal;
    out.ground = chord;
    out.position = chord + n * (std::sin(t * kPi) * 0.65f);
    out.normal = n;
    out.quat = t < 0.5f ? a->quat : b->quat;
    out.fit = fitOf(*a) + (fitOf(*b) - fitOf(*a)) * t;
    return out;
  }

  // Glide: walk the seats the engine's route passes through, in order. A portal or bounce
  // step is not a special case here - on the surface there is no gap to open a doorway
  // across, so the walk simply continues through the point sequence with no cut.
  std::vector<const SurfaceSeat*> seats{a};
  for (const view::PathStep& s : path.steps) {
    const SurfaceSeat* st = seatOf(surf, s.to);
    if (st == nullptr) return out;
    seats.push_back(st);
  }
  std::vector<view::Vec3> centres;
  centres.reserve(seats.size());
  for (const SurfaceSeat* s : seats) centres.push_back(s->centre);

  // The travel polyline reaches the *boundary* between two squares, not a straight chord
  // from centre to centre: a waypoint on the shared edge (orthogonal move) or shared
  // corner (diagonal move) is inserted between each pair. It is sampled from the surface
  // at the *lattice* boundary coordinate - the genuine point on the surface - not the
  // chord midpoint of the two seats, which on a fast-curving board (a torus's inner ring)
  // sits well inside the surface (that was the clipping this replaces). A D >= 3 stacked
  // shape has no parametrisation, so it falls back to the midpoint.
  std::vector<view::Vec3> points;
  points.reserve(seats.size() * 2 - 1);
  points.push_back(centres.front());
  for (std::size_t i = 1; i < centres.size(); ++i) {
    const SurfaceSeat& prev = *seats[i - 1];
    const SurfaceSeat& cur = *seats[i];
    view::Vec3 boundary{};
    if (!surf.nearestBoundary(prev, cur, boundary)) {
      boundary = (prev.centre + cur.centre) * 0.5f;  // stacked: no surface to sample
    }
    points.push_back(boundary);
    points.push_back(cur.centre);
  }
  out.position = view::pointAlong(points, t);
  out.ground = out.position;  // on the surface, before the hover below lifts it

  // Orientation snaps to the nearer route *cell* as the piece passes it - the boundary
  // waypoint is not a cell; the scale blends, so a piece does not visibly resize at a
  // cell boundary.
  std::vector<float> cum(centres.size(), 0.0f);
  float total = 0.0f;
  for (std::size_t i = 1; i < centres.size(); ++i) {
    total += view::length(centres[i] - centres[i - 1]);
    cum[i] = total;
  }
  std::size_t i = 1;
  const float want = t * total;
  while (i + 1 < cum.size() && cum[i] < want) ++i;
  const float seg = std::max(1e-5f, cum[i] - cum[i - 1]);
  const float local = std::clamp((want - cum[i - 1]) / seg, 0.0f, 1.0f);
  const SurfaceSeat& sa = *seats[i - 1];
  const SurfaceSeat& sb = *seats[i];
  view::Vec3 n = sa.normal + (sb.normal - sa.normal) * local;
  out.normal = view::length(n) > 1e-6f ? view::normalize(n) : sa.normal;
  out.quat = local < 0.5f ? sa.quat : sb.quat;
  out.fit = fitOf(sa) + (fitOf(sb) - fitOf(sa)) * local;

  // Hover the moving piece off the surface, so its base does not scrape through the
  // squares where the path bends over them - most on a fast-curving shape's inner side.
  // Zero at each end (the piece sets off and lands sitting on its square), one cell
  // through the body of the move.
  const float cellSize =
      0.5f * (std::min(sa.stepU, sa.stepV) + std::min(sb.stepU, sb.stepV));
  out.position =
      out.position + out.normal * (kSurfaceHoverCells * cellSize * hoverEnvelope(t));
  return out;
}

view::Vec3 chaseEyeDirection(const view::Vec3& normal, const view::Vec3& travel,
                             float lift) {
  // The same a/b/c construction `surfaceChaseCamera` draws with, and the one the antclip
  // search scores against (M17.20): `a` the direction of motion, `b` the piece's upright
  // (its surface normal) squared to `a`, and `c = -a cos(theta) + b sin(theta)` the
  // back-and-up vector, i.e. the direction from the piece to the eye.
  view::Vec3 a = travel;
  if (view::length(a) < 1e-5f) a = view::Vec3{0.0f, -1.0f, 0.0f};  // any direction
  a = view::normalize(a);
  view::Vec3 b = normal - a * view::dot(normal, a);
  if (view::length(b) < 1e-5f) b = view::Vec3{0.0f, 0.0f, 1.0f};  // degenerate fallback
  b = view::normalize(b);
  const float cosT = 1.0f / std::sqrt(1.0f + lift * lift);
  const float sinT = lift * cosT;
  return view::normalize(a * -cosT + b * sinT);
}

namespace {
/// The direction (not normalised) the chase camera travels at progress `t` along `path`
/// on `surf`: the windowed finite difference of `surfaceMoveSample`, or the chord for a
/// leap. Pure in `t`, and the one place the camera's own travel comes from.
view::Vec3 chaseTravel(const view::MovePath& path, const PlaySurface& surf, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const SurfaceMoveSample here = surfaceMoveSample(path, surf, t);
  view::Vec3 travel;
  if (path.leap) {
    // A leap's sampled position reverses at its apex, so its own difference turns the
    // camera round mid-air; a leap has one honest direction - the chord it crosses.
    const SurfaceSeat* a = seatOf(surf, path.from);
    const SurfaceSeat* b = seatOf(surf, path.to);
    travel = (a != nullptr && b != nullptr) ? b->centre - a->centre : here.normal;
  } else {
    // A window in `t`, not a single step: the vector between the samples either side is
    // continuous through a cell corner, where `sample(t + eps) - sample(t)` swivels. The
    // window shrinks smoothly at the ends because the samples clamp there, so the camera
    // still settles rather than snapping.
    constexpr float kWindow = 0.10f;
    const SurfaceMoveSample behind =
        surfaceMoveSample(path, surf, std::max(0.0f, t - kWindow));
    const SurfaceMoveSample ahead =
        surfaceMoveSample(path, surf, std::min(1.0f, t + kWindow));
    travel = ahead.position - behind.position;
  }
  if (view::length(travel) < 1e-5f)
    travel = here.normal;  // any tangent; the piece stands
  return travel;
}
}  // namespace

float clearEyeDistance(const PlaySurface& surf, const view::Vec3& target,
                       const view::Vec3& direction, float maxDistance,
                       float minDistance) {
  constexpr float kEps = 0.02f;
  view::Vec3 dir = direction;
  if (view::length(dir) < 1e-6f) dir = view::Vec3{0.0f, 0.0f, 1.0f};
  dir = view::normalize(dir);
  const float far = std::max(minDistance, maxDistance);
  if (!surf.blocked(target + dir * far, target, kEps)) return far;
  // The nominal distance is blocked, so bisect for the largest still-unblocked one. Eight
  // steps resolve a cell's worth of distance, and `lo` stays a known-clear distance.
  float lo = minDistance;
  float hi = far;
  if (surf.blocked(target + dir * lo, target, kEps)) return lo;
  for (int i = 0; i < 8; ++i) {
    const float mid = 0.5f * (lo + hi);
    if (surf.blocked(target + dir * mid, target, kEps)) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  return lo;
}

view::OrbitCamera surfaceChaseCamera(const SurfaceMoveSample& piece,
                                     const view::Vec3& travel, float distance,
                                     bool upright, float lift) {
  // The frame the piece stands in, built explicitly so the camera's orientation is a fact
  // about the piece and not a side effect of a world-up hint:
  //
  //   a = the direction of motion (the route's travel),
  //   b = the piece's upright (its surface normal), squared to `a`,
  //   n = a x b, across the piece and along the screen's horizontal,
  //   c = -a rotated about `n` by the elevation: the back-and-up vector, i.e. the
  //       direction from the piece to the eye. `lift = tan(elevation)`, so its sine and
  //       cosine are the elevation's.
  //
  // The camera looks along `-c`, its right is `n`, and its up is `c x n` - the piece's
  // upright projected perpendicular to the view. That is the negative of `n x c`
  // (`(a x b) x c`), which points *down* the piece; using that order would hang the piece
  // upside down. The travel is already smoothed by the caller, so a cell corner does not
  // swivel `a`.
  view::Vec3 a = travel;
  if (view::length(a) < 1e-5f) a = view::Vec3{0.0f, -1.0f, 0.0f};  // any direction
  a = view::normalize(a);
  view::Vec3 b = piece.normal - a * view::dot(piece.normal, a);
  if (view::length(b) < 1e-5f) b = view::Vec3{0.0f, 0.0f, 1.0f};  // degenerate fallback
  b = view::normalize(b);
  const view::Vec3 n = view::normalize(view::cross(a, b));
  const view::Vec3 c = chaseEyeDirection(piece.normal, travel, lift);

  view::OrbitCamera cam;
  cam.target = piece.position;
  cam.distance = std::max(0.5f, distance);
  cam.pitch = std::asin(std::clamp(c.z, -0.99f, 0.99f));
  cam.yaw = std::atan2(c.x, -c.y);
  if (!upright) return cam;  // the tilt mode: no roll, so the piece rides the shape
  // Roll so the piece's own up is screen up. The target up is `c x n`; the basis is
  // rebuilt from the camera that was actually constructed - after the pitch clamp, and
  // with the camera's own up-hint - so it is exactly the basis `cameraBasis` will apply,
  // and the roll cannot be a degree off wherever the camera is steep or near a pole (the
  // loose test did not see either).
  const view::Vec3 eyeDir = view::normalize(cam.eye() - cam.target);  // target -> eye
  const view::Vec3 viewDir = eyeDir * -1.0f;                          // eye -> target
  const view::Vec3 r = view::normalize(view::cross(viewDir, cam.upHint()));
  const view::Vec3 up0 = view::cross(r, viewDir);
  view::Vec3 wantUp = view::cross(c, n);
  if (view::length(wantUp) > 1e-5f) {
    wantUp = view::normalize(wantUp);
    // Negated: `atan2` gives the turn from the unrolled up to `wantUp`, but
    // `cameraBasis` applies `roll` as `up' = up cos - right sin` (the opposite sense),
    // so the raw angle rolls the piece *away* from upright by twice its lean. See the
    // M17.16 upright test.
    cam.roll =
        -std::atan2(view::dot(view::cross(up0, wantUp), viewDir), view::dot(up0, wantUp));
  }
  return cam;
}

view::OrbitCamera surfaceFollowCamera(const view::MovePath& path, const PlaySurface& surf,
                                      float t, float distance, bool upright, float lift) {
  t = std::clamp(t, 0.0f, 1.0f);
  const SurfaceMoveSample here = surfaceMoveSample(path, surf, t);
  const view::Vec3 travel = chaseTravel(path, surf, t);
  // The eye the camera would actually sit at must not be behind the shape: clamp the
  // requested distance to the largest one along the shared eye direction that is clear of
  // `surf` (M17.20). `surfaceChaseCamera` itself stays mesh-free and unit-tested.
  const view::Vec3 dir = chaseEyeDirection(here.normal, travel, lift);
  const float clamped = clearEyeDistance(surf, here.position, dir, distance);
  return surfaceChaseCamera(here, travel, clamped, upright, lift);
}

bool followClips(const view::MovePath& path, const PlaySurface& surf, float t,
                 float lookahead, float distance, float lift) {
  if (surf.empty()) return false;
  const float eps = 0.02f;
  const float t2 = std::min(1.0f, std::max(0.0f, t + lookahead));
  // The camera travels from where it is now to where the follow puts it next; if that
  // segment, or the line from there to the piece, crosses a tile, the board has to turn.
  const view::OrbitCamera now = surfaceFollowCamera(path, surf, t, distance, false, lift);
  const view::OrbitCamera next =
      surfaceFollowCamera(path, surf, t2, distance, false, lift);
  const view::Vec3 piece = surfaceMoveSample(path, surf, t2).position;
  return surf.blocked(now.eye(), next.eye(), eps) || surf.blocked(next.eye(), piece, eps);
}

namespace {

/// The outward normal of one seat at a candidate slide, without building the whole
/// surface. The align search scores ~100 candidate slides; building every patch, seat and
/// picker for each was the entire cost of a followed move (M17.19 measured ~5 fps). For
/// an orientable shape this normal is *exactly* the field `PlaySurface::build` walks -
/// the walk only ever fixes a non-orientable sign - so the search's ranking is unchanged.
/// `basePose` carries the board's own stretch and no slide; the slide is folded in as
/// `su`/`sv`, exactly as `build` does before it samples.
view::Vec3 seatNormalProbe(app::SurfaceKind kind, const SurfacePose& basePose, int nx,
                           int nz, float su, float sv, int f, int r) {
  const float u = (static_cast<float>(f) + 0.5f) / static_cast<float>(nx) + su;
  const float vv = (static_cast<float>(r) + 0.5f) / static_cast<float>(nz) + sv;
  return normalAt(kind, basePose, u, vv, nx, nz);
}

/// The one seat of `s` on `target`, or null.
const SurfaceSeat* seatFor(const PlaySurface& s, CellId target) {
  for (const SurfaceSeat& t : s.seats()) {
    if (t.cell == target) return &t;
  }
  return nullptr;
}

/// One candidate's score: how far the eye can sit along that candidate's own chase
/// direction before the shape blocks it, and how squarely the target faces that eye.
/// `achieved` is the primary key, `facing` the tie-break (M17.20).
struct SlideScore {
  float achieved{-1.0f};
  float facing{-1e9f};
};

/// True when `a` beats `b`: the least-clamped candidate wins, facing breaking a tie.
[[nodiscard]] inline bool betterSlideScore(const SlideScore& a, const SlideScore& b) {
  constexpr float kTie = 1e-4f;
  return a.achieved > b.achieved + kTie ||
         (std::abs(a.achieved - b.achieved) <= kTie && a.facing > b.facing);
}

/// Sweep slide offsets for the one whose score is best. `probeToEye` is a cheap ordering
/// of the grid from a candidate's raw normal; `score` is the real per-candidate score, so
/// a caller whose camera depends on the built pose (the chase) can score the very camera
/// it would draw. Only a closed ring has an inner/outer side to turn, so an open tube or
/// ribbon has nothing to search.
///
/// On an orientable ring the candidates are first ranked by a single-cell probe (whose
/// normal is the walked field, since the walk only fixes a sign) and the full surface is
/// built in facing order only until one is fully clear at the nominal distance - the
/// first such candidate is the argmax, at a couple of builds instead of `stepsU *
/// stepsV`. If none is clear, the whole grid is scored so the least-clamped rotation
/// wins. A non-orientable shape keeps the exact per-candidate build, because a lone raw
/// normal is not the walked field it draws with.
template <typename Probe, typename ScoreFn>
SlideOffset searchSlide(const VariantSpec& v, CellId target, float eyeDistance,
                        Probe probeToEye, ScoreFn score,
                        const SlideOffset* hint = nullptr) {
  if (!PlaySurface::slidesAlongRanks(v)) return {};
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  const float periodU = 2.0f * static_cast<float>(nx);
  const float periodV = 2.0f * static_cast<float>(nz);
  // A coarse grid on both axes. Twelve steps each is a cell and a half at the board's
  // usual eight, and the result is eased into, so the exact grid point does not show.
  //
  // On a non-orientable board the U axis is not searched: measured on the Klein bottle's
  // rank seam its tile-corner gap grew to about ten times an ordinary gap, while the V
  // axis stayed near one, so only V is offered there.
  const bool orientable = v.geom.isOrientable();
  constexpr int kSteps = 12;
  constexpr float kTie = 1e-4f;
  // Hysteresis (M17.20): the previous frame's offset wins outright while it is still
  // basically clear, so a piece's cell-to-cell motion does not re-target a different
  // rotation purely because the global argmax ticked. Without this the search is a
  // stateless argmax and the shape jitters.
  if (hint != nullptr) {
    SurfacePose hp;
    hp.slideU = hint->u;
    hp.slideV = hint->v;
    const SlideScore got = score(PlaySurface::build(v, hp), *hint);
    if (got.achieved >= eyeDistance * 0.9f) return *hint;
  }

  if (orientable) {
    const app::SurfaceKind kind = app::overtureSignature(v).surface;
    SurfacePose base;
    // What the board fixes, so the probe samples the drawn surface (the morph is settled
    // whenever a move is followed).
    base.stretch = kRibbonStretch;
    const Coord cell = v.dims.toCoord(target);
    const int f = cell[0];
    const int r = cell[1];
    struct Cand {
      float facing;
      float u;
      float v;
    };
    std::vector<Cand> cands;
    cands.reserve(static_cast<std::size_t>(kSteps * kSteps));
    for (int iu = 0; iu < kSteps; ++iu) {
      for (int iv = 0; iv < kSteps; ++iv) {
        const float u = periodU * static_cast<float>(iu) / static_cast<float>(kSteps);
        const float vv = periodV * static_cast<float>(iv) / static_cast<float>(kSteps);
        const float su = wrapSlide(u, periodU) / static_cast<float>(nx);
        const float sv = wrapSlide(vv, periodV) / static_cast<float>(nz);
        const view::Vec3 n = seatNormalProbe(kind, base, nx, nz, su, sv, f, r);
        view::Vec3 toEye = probeToEye(n);
        toEye =
            view::length(toEye) > 1e-6f ? view::normalize(toEye) : view::Vec3{0, 0, 1};
        cands.push_back({view::dot(n, toEye), u, vv});
      }
    }
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.facing > b.facing; });
    SlideOffset best{};
    SlideScore bestScore;
    bool have = false;
    for (const Cand& c : cands) {
      SurfacePose pose;
      pose.slideU = c.u;
      pose.slideV = c.v;
      const PlaySurface s = PlaySurface::build(v, pose);
      const SlideScore got = score(s, {c.u, c.v});
      if (!have || betterSlideScore(got, bestScore)) {
        bestScore = got;
        best = {c.u, c.v};
        have = true;
      }
      // Fully clear, reached in facing order: no later candidate can face better.
      if (got.achieved >= eyeDistance - kTie) break;
    }
    return best;
  }

  SlideOffset best{};
  SlideScore bestScore;
  bool have = false;
  for (int iu = 0; iu < 1; ++iu) {
    for (int iv = 0; iv < kSteps; ++iv) {
      SurfacePose pose;
      pose.slideU = periodU * static_cast<float>(iu) / static_cast<float>(kSteps);
      pose.slideV = periodV * static_cast<float>(iv) / static_cast<float>(kSteps);
      const PlaySurface s = PlaySurface::build(v, pose);
      const SlideScore got = score(s, {pose.slideU, pose.slideV});
      if (!have || betterSlideScore(got, bestScore)) {
        bestScore = got;
        best = {pose.slideU, pose.slideV};
        have = true;
      }
    }
  }
  return best;
}

}  // namespace

SlideOffset alignSlideU(const VariantSpec& v, CellId target, const view::Vec3& travel,
                        float eyeDistance, float lift) {
  view::Vec3 fwd = travel;
  if (view::length(fwd) < 1e-5f) fwd = view::Vec3{0.0f, -1.0f, 0.0f};
  fwd = view::normalize(fwd);
  // Where the chase camera would sit for each candidate: the same shared eye direction
  // the camera itself draws with (M17.20), so the search can never answer a different
  // question.
  const auto toEye = [&](const view::Vec3& normal) {
    return chaseEyeDirection(normal, fwd, lift);
  };
  const auto score = [&](const PlaySurface& s, const SlideOffset&) {
    SlideScore out;
    const SurfaceSeat* seat = seatFor(s, target);
    if (seat == nullptr) return out;
    view::Vec3 dir = chaseEyeDirection(seat->normal, fwd, lift);
    dir = view::length(dir) > 1e-6f ? view::normalize(dir) : view::Vec3{0, 0, 1};
    out.achieved = clearEyeDistance(s, seat->centre, dir, eyeDistance);
    out.facing = view::dot(seat->normal, dir);
    return out;
  };
  return searchSlide(v, target, eyeDistance, toEye, score);
}

SlideOffset alignSlideU(const VariantSpec& v, CellId target, const view::MovePath& path,
                        float t, float eyeDistance, float lift, const SlideOffset* hint) {
  // The chase camera's own travel depends on the pose the board is turned to, so the
  // search must score each candidate against *that candidate's* travel - not one vector
  // read off the pose it started from. And the camera draws the moving *sample*, not the
  // target's seat centre, so the score is taken at the sample too. Otherwise a rotation
  // the search calls clear can still put the camera looking through the tube (M17.20).
  // The cheap probe only orders the grid (from a representative travel); every
  // candidate's real score is rebuilt.
  const PlaySurface unposed = PlaySurface::build(v);
  const view::Vec3 probeTravel = chaseTravel(path, unposed, t);
  const auto probeToEye = [&](const view::Vec3& normal) {
    return chaseEyeDirection(normal, probeTravel, lift);
  };
  const auto score = [&](const PlaySurface& s, const SlideOffset&) {
    SlideScore out;
    const SurfaceMoveSample sample = surfaceMoveSample(path, s, t);
    view::Vec3 dir = chaseEyeDirection(sample.normal, chaseTravel(path, s, t), lift);
    dir = view::length(dir) > 1e-6f ? view::normalize(dir) : view::Vec3{0, 0, 1};
    out.achieved = clearEyeDistance(s, sample.position, dir, eyeDistance);
    out.facing = view::dot(sample.normal, dir);
    return out;
  };
  return searchSlide(v, target, eyeDistance, probeToEye, score, hint);
}

SlideOffset alignSlideToFace(const VariantSpec& v, CellId target,
                             const view::Vec3& toCamera, float eyeDistance) {
  view::Vec3 toCam = toCamera;
  if (view::length(toCam) < 1e-5f) toCam = view::Vec3{0.0f, 0.0f, 1.0f};
  toCam = view::normalize(toCam);
  // The camera angle is fixed (the turntable), so the eye direction is the same for
  // every candidate; only the seat moves under it.
  return searchSlide(
      v, target, eyeDistance, [&](const view::Vec3&) { return toCam; },
      [&](const PlaySurface& s, const SlideOffset&) {
        SlideScore out;
        const SurfaceSeat* seat = seatFor(s, target);
        if (seat == nullptr) return out;
        out.achieved = clearEyeDistance(s, seat->centre, toCam, eyeDistance);
        out.facing = view::dot(seat->normal, toCam);
        return out;
      });
}

void PlaySurface::buildStacked(const VariantSpec& v, SurfacePose pose) {
  stacked_ = true;
  const DimSpec& d = v.dims;
  const int nx = static_cast<int>(d.extent(0));
  const int nz = static_cast<int>(d.extent(1));
  nx_ = nx;
  nz_ = nz;
  // INVERT swaps which side is out, exactly as on the parametrised 2-D surfaces: the
  // tiles do not move, the outward normal is reversed and the piece's frame follows it,
  // so the pieces stand on the other face (M17.22, matching M17.7 revised).
  const bool inverted = pose.evert > 0.5f;
  pose.evert = 0.0f;
  // The slide, in cells, wrapped into the same two-lap window the 2-D path uses before it
  // reaches the shape function. `hyper4` has no periodic axis and ignores it (M17.22).
  pose.slideU = wrapSlide(pose.slideU, 2.0f * static_cast<float>(nx));
  pose.slideV = wrapSlide(pose.slideV, 2.0f * static_cast<float>(nz));
  // A neighbour's position, wrapping a periodic axis and clamping a bounded one. The
  // difference matters: on `torus3d` the file and rank axes are periodic, so file 0's
  // "previous" is file 3, not file 0 again - clamping returns the cell's own position, so
  // every boundary cell's frame tilts on a half-length, wrongly-based tangent, which is
  // the fish-scales scatter; on `hyper4` all four axes are bounded, so clamping is right
  // (M17.13).
  const bool periodicU = v.geom.boundaryKind(0, Side::Max) == BoundaryKind::Periodic;
  const bool periodicV = v.geom.boundaryKind(1, Side::Max) == BoundaryKind::Periodic;
  const auto positionAt = [&](const Coord& base, int axis, int offset, view::Vec3& out) {
    const std::size_t a = static_cast<std::size_t>(axis);
    const int extent = axis == 0 ? nx : nz;
    const bool periodic = axis == 0 ? periodicU : periodicV;
    int idx = base.c[a] + offset;
    idx = periodic ? ((idx % extent) + extent) % extent : std::clamp(idx, 0, extent - 1);
    Coord c = base;
    c.c[a] = static_cast<std::int16_t>(idx);
    OvVec3 p;
    if (!playShapePosition(v, d.toCell(c), p, pose)) return false;
    out = {p.x, -p.z, p.y};  // into the board's Z-up world, as the 2-D shapes are
    return true;
  };
  // One axis's tangent, cell size and direction at `base`. A two-sided difference spans
  // two cells; at a bounded boundary only one side is real, so the one-sided difference
  // is a whole cell - not half of a fictitious two (M17.13).
  const auto axisFrame = [&](const Coord& base, int axis, const view::Vec3& centre,
                             view::Vec3& tangent, float& cellSize, view::Vec3& dir) {
    const int extent = axis == 0 ? nx : nz;
    const bool periodic = axis == 0 ? periodicU : periodicV;
    const int idx = base.c[static_cast<std::size_t>(axis)];
    view::Vec3 plus{}, minus{};
    bool twoSided = true;
    if (periodic || (idx > 0 && idx < extent - 1)) {
      if (!positionAt(base, axis, +1, plus) || !positionAt(base, axis, -1, minus)) {
        return false;
      }
      tangent = plus - minus;
    } else if (idx == 0) {
      if (!positionAt(base, axis, +1, plus)) return false;
      tangent = plus - centre;
      twoSided = false;
    } else {
      if (!positionAt(base, axis, -1, minus)) return false;
      tangent = centre - minus;
      twoSided = false;
    }
    const float len = view::length(tangent);
    if (len < 1e-6f) return false;
    cellSize = (twoSided ? 0.5f : 1.0f) * len;
    dir = tangent * (1.0f / len);
    return true;
  };

  view::Bounds b;
  bool first = true;
  const auto eat = [&](const view::Vec3& p) {
    if (first) {
      b = {p.x, p.y, p.z, p.x, p.y, p.z};
      first = false;
      return;
    }
    b.minX = std::min(b.minX, p.x);
    b.minY = std::min(b.minY, p.y);
    b.minZ = std::min(b.minZ, p.z);
    b.maxX = std::max(b.maxX, p.x);
    b.maxY = std::max(b.maxY, p.y);
    b.maxZ = std::max(b.maxZ, p.z);
  };

  constexpr int kSide = kSubdiv + 1;
  // The finite difference a patch corner's normal is read off, in *cells*: the same
  // 0.2-cell step the 2-D path's `normalAt` uses. Deliberately unclamped - both authored
  // embeddings are smooth functions of their coordinates well outside the board, so
  // sampling past a bounded edge is the honest tangent, while clamping would halve it and
  // tilt the rim (M17.12 part A).
  constexpr float kNormalStep = 0.2f;
  const int total = static_cast<int>(d.cellCount());
  for (int id = 0; id < total; ++id) {
    const CellId cell = static_cast<CellId>(id);
    OvVec3 raw;
    if (!playShapePosition(v, cell, raw, pose)) continue;
    const Coord c = d.toCoord(cell);
    const view::Vec3 centre{raw.x, -raw.z, raw.y};
    view::Vec3 tangentU{}, tangentV{}, ex{}, vDir{};
    float cellU = 0.0f;
    float cellV = 0.0f;
    if (!axisFrame(c, 0, centre, tangentU, cellU, ex)) continue;
    if (!axisFrame(c, 1, centre, tangentV, cellV, vDir)) continue;
    view::Vec3 normal = view::cross(tangentV, tangentU);
    normal = view::length(normal) > 1e-6f ? view::normalize(normal)
                                          : view::Vec3{0.0f, 0.0f, 1.0f};
    // The inverse swaps the outward side; `ey` is derived from the flipped normal, so the
    // piece's frame stays right-handed and its +Z is the new normal.
    if (inverted) normal = normal * -1.0f;
    const view::Vec3 ey = view::cross(normal, ex);

    SurfaceSeat seat;
    seat.cell = cell;
    seat.file = c[0];
    seat.rank = c[1];
    seat.centre = centre;
    seat.normal = normal;
    seat.stepU = cellU;
    seat.stepV = cellV;
    seat.quat = quatOf(ex, ey, normal);
    seats_.push_back(seat);

    // The tile is a patch of the continuous surface, sampled on the same sub-grid the 2-D
    // path cuts its patches from: its corners are points *on* the shell/tesseract rather
    // than a tangent plane extrapolated from the centre, so a cell reads as part of the
    // curved manifold and two tiles either side of a cell edge come from the same curve
    // (M17.12 part A). Sampling the shared `playShapeAt` rather than `shellTube`/the
    // tesseract here is what keeps the patch and the seat's centre one derivation.
    //
    // `baseAxis` is the cell centre in fractional lattice coordinates: `+0.5` puts a cell
    // centre on the half-integer the shape functions expect, and `du`/`dv` span the
    // cell's own footprint in cells, inset by `kCoverage` so the line between squares is
    // a real gap in the shape rather than a stripe painted on it.
    std::array<float, kMaxDims> baseAxis{};
    for (int a = 0; a < d.dims(); ++a) {
      baseAxis[static_cast<std::size_t>(a)] =
          static_cast<float>(c.c[static_cast<std::size_t>(a)]) + 0.5f;
    }
    const auto at = [&](float du, float dv) {
      std::array<float, kMaxDims> a = baseAxis;
      a[0] += du;
      a[1] += dv;
      OvVec3 p;
      if (!playShapeAt(v, a, p, pose)) return centre;
      return view::Vec3{p.x, -p.z, p.y};
    };
    SurfacePatch patch;
    patch.cell = cell;
    for (int i = 0; i < kSide; ++i) {
      for (int j = 0; j < kSide; ++j) {
        const float du = (static_cast<float>(i) / kSubdiv - 0.5f) * kCoverage;
        const float dv = (static_cast<float>(j) / kSubdiv - 0.5f) * kCoverage;
        const std::size_t k = static_cast<std::size_t>(i * kSide + j);
        patch.pos[k] = at(du, dv);
        // The corner's own normal, from the surface's finite-difference tangents,
        // rank-tangent by file-tangent in the same order as the seat's. Oriented to agree
        // with the seat's normal so the patch shades on the side the piece stands; the
        // order matches everywhere except across a fold, where the flip is the honest
        // sign of the other sheet.
        const view::Vec3 dU = at(du + kNormalStep, dv) - at(du - kNormalStep, dv);
        const view::Vec3 dV = at(du, dv + kNormalStep) - at(du, dv - kNormalStep);
        view::Vec3 n = view::cross(dV, dU);
        if (view::dot(n, normal) < 0.0f) n = n * -1.0f;
        patch.normal[k] = view::length(n) > 1e-6f ? view::normalize(n) : normal;
      }
    }
    patches_.push_back(patch);
    eat(centre);
  }

  const float pad = 0.9f;
  b.minX -= pad;
  b.minY -= pad;
  b.minZ -= pad;
  b.maxX += pad;
  b.maxY += pad;
  b.maxZ += pad;
  bounds_ = b;
}

CellId PlaySurface::pick(const view::OrbitCamera& camera, float width, float height,
                         float px, float py, float ghost,
                         const view::PositionView* pieces, float pieceHeightScale,
                         bool pieceCylinders) const {
  const view::OrbitCamera::Ray ray = camera.pickRay(px, py, width, height);
  // Moeller-Trumbore, nearest hit wins: a cell round the back of the shape is behind the
  // one in front of it, and the one in front is the answer.
  const auto hit = [&](const view::Vec3& a, const view::Vec3& b, const view::Vec3& c,
                       float& t) {
    const view::Vec3 e1 = b - a;
    const view::Vec3 e2 = c - a;
    const view::Vec3 pv = view::cross(ray.direction, e2);
    const float det = view::dot(e1, pv);
    if (std::abs(det) < 1e-9f) return false;
    const float inv = 1.0f / det;
    const view::Vec3 tv = ray.origin - a;
    const float uu = view::dot(tv, pv) * inv;
    if (uu < 0.0f || uu > 1.0f) return false;
    const view::Vec3 qv = view::cross(tv, e1);
    const float vv = view::dot(ray.direction, qv) * inv;
    if (vv < 0.0f || uu + vv > 1.0f) return false;
    t = view::dot(e2, qv) * inv;
    return t > 1e-4f;
  };

  // The volume an occupied seat's piece stands in, along the seat's normal: from the
  // surface up to the piece's rendered top, radius matched to its footprint - the same
  // `0.8 * fit` the render path scales a body by, and the same mesh foot radius that
  // body is built from, so the two cannot drift (M17.27). A piece is opaque; the caller
  // folds this hit into the tile walk as an opaque candidate.
  const auto pieceHitT = [&](const SurfaceSeat& seat, float& tOut) -> bool {
    if (pieces == nullptr || seat.cell >= pieces->cellCount()) return false;
    const Piece occupant = pieces->at(seat.cell);
    if (occupant.empty()) return false;
    const PieceTypeDef& def =
        pieces->variant().pieces[static_cast<std::size_t>(occupant.type)];
    const float hgt = 1.0f + (heightFor(def) - 1.0f) * pieceHeightScale;
    const float fit = std::min(std::sqrt(seat.stepU * seat.stepV), 1.0f);
    const float radius = 0.8f * fit * kPieceFootRadius;
    view::Vec3 base = seat.centre;
    view::Vec3 top = seat.centre;
    if (pieceCylinders) {
      // The icon puck is centred on the surface and protrudes both ways (M17.25).
      const float halfThick = 0.8f * fit * hgt * kCylinderDiscHalfThick;
      base = seat.centre - seat.normal * halfThick;
      top = seat.centre + seat.normal * halfThick;
    } else {
      const float lift = PlaySurface::kThickness * 0.5f;
      base = seat.centre + seat.normal * lift;
      top = base + seat.normal * (0.8f * fit * hgt);
    }
    return rayCapsule(ray, base, top, radius, tOut);
  };

  // A D >= 3 shape is a set of curved tiles rather than one gapless sheet, so each
  // patch's own sub-quads are tested, exactly as the 2-D path tests `corners_` (M17.12
  // part A). A ray can cross several sheets, so every intersection is collected rather
  // than stopping at the first.
  if (stacked_) {
    struct Hit {
      float t;
      CellId cell;
      bool opaque;  ///< a piece, not a tile
    };
    std::vector<Hit> hits;
    constexpr int kSide = kSubdiv + 1;
    for (const SurfacePatch& p : patches_) {
      for (int i = 0; i + 1 < kSide; ++i) {
        for (int j = 0; j + 1 < kSide; ++j) {
          const view::Vec3& a = p.pos[static_cast<std::size_t>(i * kSide + j)];
          const view::Vec3& b = p.pos[static_cast<std::size_t>((i + 1) * kSide + j)];
          const view::Vec3& c = p.pos[static_cast<std::size_t>((i + 1) * kSide + j + 1)];
          const view::Vec3& d = p.pos[static_cast<std::size_t>(i * kSide + j + 1)];
          float t = 0.0f;
          if (hit(a, b, c, t) || hit(a, c, d, t)) hits.push_back({t, p.cell, false});
        }
      }
    }
    // Each occupied seat also stands a piece at its centre, along its normal; a click on
    // the visible body has to resolve to the cell, not slip past the bare tile.
    for (const SurfaceSeat& seat : seats_) {
      float t = 0.0f;
      if (pieceHitT(seat, t)) hits.push_back({t, seat.cell, true});
    }
    if (hits.empty()) return kInvalidCell;
    std::sort(hits.begin(), hits.end(),
              [](const Hit& a, const Hit& b) { return a.t < b.t; });
    const float alpha = std::clamp(ghost, 0.0f, 1.0f);
    // Fully opaque: the nearest candidate is the answer - a piece and the tile it stands
    // on are the same cell, so this is unchanged. Ghosted: the sheets in front are
    // see-through, so the tile the eye settles on is the first at which the accumulated
    // opacity toward the eye reaches the visibility threshold - a click through a
    // ghosted outer shell reaches the one inside it. A piece is opaque, so it wins as
    // soon as the ray reaches it *unless* the ghosted tiles in front have already
    // accumulated enough opacity to hide it - the click bypasses through the squares to
    // the piece (M17.22, M17.27).
    if (alpha >= kGhostPickVisibility) return hits.front().cell;
    float transmitted = 1.0f;
    for (const Hit& h : hits) {
      if (h.opaque) return h.cell;
      transmitted *= (1.0f - alpha);
      if (1.0f - transmitted >= kGhostPickVisibility) return h.cell;
    }
    return hits.front().cell;
  }

  if (corners_.empty()) return kInvalidCell;
  const int cv = nz_ * kSubdiv + 1;
  const auto corner = [&](int i, int j) -> const view::Vec3& {
    return corners_[static_cast<std::size_t>(i * cv + j)];
  };

  CellId best = kInvalidCell;
  float bestT = 1e30f;
  for (int i = 0; i + 1 < nx_ * kSubdiv + 1; ++i) {
    for (int j = 0; j + 1 < cv; ++j) {
      const view::Vec3& a = corner(i, j);
      const view::Vec3& b = corner(i + 1, j);
      const view::Vec3& c = corner(i + 1, j + 1);
      const view::Vec3& d = corner(i, j + 1);
      float t = 0.0f;
      if ((hit(a, b, c, t) || hit(a, c, d, t)) && t < bestT) {
        bestT = t;
        best = seats_[static_cast<std::size_t>((i / kSubdiv) * nz_ + (j / kSubdiv))].cell;
      }
    }
  }
  // The same piece volumes, for the parametrised single-sheet surfaces: a piece on a
  // steeply turned part of the tube stands clear of its tile, so a click on its body has
  // to resolve to its cell too (M17.27). A single sheet has no ghosting, so nearest wins.
  for (const SurfaceSeat& seat : seats_) {
    float t = 0.0f;
    if (pieceHitT(seat, t) && t < bestT) {
      bestT = t;
      best = seat.cell;
    }
  }
  return best;
}

bool PlaySurface::blocked(const view::Vec3& eye, const view::Vec3& target,
                          float eps) const {
  const view::Vec3 delta = target - eye;
  const float len = view::length(delta);
  if (len < 1e-5f) return false;
  const view::Vec3 dir = delta * (1.0f / len);
  const auto hit = [&](const view::Vec3& a, const view::Vec3& b, const view::Vec3& c,
                       float& t) {
    const view::Vec3 e1 = b - a;
    const view::Vec3 e2 = c - a;
    const view::Vec3 pv = view::cross(dir, e2);
    const float det = view::dot(e1, pv);
    if (std::abs(det) < 1e-9f) return false;
    const float inv = 1.0f / det;
    const view::Vec3 tv = eye - a;
    const float uu = view::dot(tv, pv) * inv;
    if (uu < 0.0f || uu > 1.0f) return false;
    const view::Vec3 qv = view::cross(tv, e1);
    const float vv = view::dot(dir, qv) * inv;
    if (vv < 0.0f || uu + vv > 1.0f) return false;
    t = view::dot(e2, qv) * inv;
    return true;
  };
  const float limit = len - eps;
  const auto tri = [&](const view::Vec3& a, const view::Vec3& b, const view::Vec3& c,
                       const view::Vec3& d) {
    float t = 0.0f;
    return (hit(a, b, c, t) || hit(a, c, d, t)) && t > 1e-4f && t < limit;
  };
  if (stacked_) {
    constexpr int kSide = kSubdiv + 1;
    for (const SurfacePatch& p : patches_) {
      for (int i = 0; i + 1 < kSide; ++i) {
        for (int j = 0; j + 1 < kSide; ++j) {
          const view::Vec3& a = p.pos[static_cast<std::size_t>(i * kSide + j)];
          const view::Vec3& b = p.pos[static_cast<std::size_t>((i + 1) * kSide + j)];
          const view::Vec3& c = p.pos[static_cast<std::size_t>((i + 1) * kSide + j + 1)];
          const view::Vec3& d = p.pos[static_cast<std::size_t>(i * kSide + j + 1)];
          if (tri(a, b, c, d)) return true;
        }
      }
    }
    return false;
  }
  if (corners_.empty()) return false;
  const int cv = nz_ * kSubdiv + 1;
  const auto corner = [&](int i, int j) -> const view::Vec3& {
    return corners_[static_cast<std::size_t>(i * cv + j)];
  };
  for (int i = 0; i + 1 < nx_ * kSubdiv + 1; ++i) {
    for (int j = 0; j + 1 < cv; ++j) {
      if (tri(corner(i, j), corner(i + 1, j), corner(i + 1, j + 1), corner(i, j + 1))) {
        return true;
      }
    }
  }
  return false;
}

std::vector<view::Vec3> hypercubeScaffoldCorners(const VariantSpec& v, SurfacePose pose) {
  std::vector<view::Vec3> corners;
  if (v.name != "hyper4" || v.dims.dims() != 4) return corners;
  corners.reserve(16);
  for (int i = 0; i < 16; ++i) {
    std::array<float, kMaxDims> axis{};
    for (int d = 0; d < 4; ++d) {
      // The extent has to match what the renderer actually projects, axis by axis. Only
      // file (`d = 0`) and rank (`d = 1`) are subdivided by `buildStacked`'s patch
      // sampling, so only there is the lattice's literal edge (`{0, 4}`, i.e. `{-2, +2}`
      // in the projection's own frame) a point the real geometry reaches. Level
      // (`d = 2`) and aeon (`d = 3`) are pinned at every cell's own centre, so they never
      // leave `{0.5, 3.5}` (`{-1.5, +1.5}`) on screen; pushing them to `{0, 4}` here
      // would be a combination the renderer never produces, which the perspective divide
      // turns into corners far outside the board - the "spikes past the stack" defect
      // (M17.12 part D revision). Keep each axis at the extent the real geometry reaches.
      const bool subdivided = d < 2;
      const float lo = subdivided ? 0.0f : 0.5f;
      const float hi = subdivided ? 4.0f : 3.5f;
      axis[static_cast<std::size_t>(d)] = ((i >> d) & 1) ? hi : lo;
    }
    OvVec3 p;
    if (!playShapeAt(v, axis, p, pose)) return {};
    corners.push_back(view::Vec3{p.x, -p.z, p.y});  // into the board's Z-up world
  }
  return corners;
}

std::vector<ScaffoldEdge> hypercubeScaffoldEdges(const VariantSpec& v, SurfacePose pose) {
  std::vector<ScaffoldEdge> edges;
  const std::vector<view::Vec3> corners = hypercubeScaffoldCorners(v, pose);
  if (corners.size() != 16) return edges;
  edges.reserve(32);
  for (int i = 0; i < 16; ++i) {
    for (int d = 0; d < 4; ++d) {
      // The 4-cube's XOR adjacency, each pair emitted once: the same topology the menu
      // tesseract in `deco.cpp` draws.
      const int j = i ^ (1 << d);
      if (j < i) continue;
      const view::Vec3& a = corners[static_cast<std::size_t>(i)];
      const view::Vec3& b = corners[static_cast<std::size_t>(j)];
      const view::Vec3 delta = b - a;
      const float len = view::length(delta);
      if (len < 1e-6f) continue;
      const view::Vec3 ex = delta * (1.0f / len);
      // Any two unit vectors perpendicular to the edge complete the frame; a bar is
      // symmetric about its own length, so the roll does not matter. Pick a seed that is
      // not parallel to `ex`, then orthogonalise it.
      const view::Vec3 seed = std::abs(ex.x) < 0.9f ? view::Vec3{1.0f, 0.0f, 0.0f}
                                                    : view::Vec3{0.0f, 1.0f, 0.0f};
      const view::Vec3 ey = view::normalize(seed - ex * view::dot(seed, ex));
      const view::Vec3 ez = view::cross(ex, ey);
      ScaffoldEdge edge;
      edge.a = a;
      edge.b = b;
      edge.quat = quatOf(ex, ey, ez);
      edges.push_back(edge);
    }
  }
  return edges;
}

}  // namespace cb::render
