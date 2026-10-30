// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/geometry.hpp"

namespace cb {
namespace {

/// Build the coordinate map for leaving `axis` on `side`.
///
/// Periodic: the crossing coordinate is shifted by the extent, landing on the
/// opposite face. Mirror: the crossing coordinate is reflected about the face and
/// the crossing direction component flips, which falls out of sign[axis] = -1.
/// Any declared flip or swap is composed on top, and that is where Moebius and
/// Klein seams come from.
Transform buildFaceTransform(const DimSpec& dims, const IdentDecl& d, Side side) {
  Transform t = Transform::identity(dims.dims());
  const std::uint8_t a = d.axis;
  const auto extent = static_cast<std::int16_t>(dims.extent(a));

  if (d.kind == BoundaryKind::Periodic) {
    t.off[a] = static_cast<std::int16_t>(side == Side::Max ? -extent : extent);
  } else {  // Mirror
    t.sign[a] = -1;
    // Reflect about the outer face: x = extent  ->  extent-1;  x = -1 -> 0.
    t.off[a] = static_cast<std::int16_t>(side == Side::Max ? (2 * extent - 1) : -1);
  }

  for (std::uint8_t f : d.flipAxes) {
    if (f == a && d.kind == BoundaryKind::Mirror) continue;  // already reflected
    Transform flip = Transform::identity(dims.dims());
    flip.sign[f] = -1;
    flip.off[f] = static_cast<std::int16_t>(dims.extent(f) - 1);
    t = flip.compose(t);
  }
  if (d.swapA >= 0 && d.swapB >= 0) {
    Transform sw = Transform::identity(dims.dims());
    sw.src[static_cast<std::size_t>(d.swapA)] = static_cast<std::uint8_t>(d.swapB);
    sw.src[static_cast<std::size_t>(d.swapB)] = static_cast<std::uint8_t>(d.swapA);
    t = sw.compose(t);
  }
  return t;
}

}  // namespace

Result<Geometry> Geometry::create(const DimSpec& dims,
                                  std::span<const IdentDecl> idents) {
  Geometry g;
  g.dims_ = dims;
  g.idents_.assign(idents.begin(), idents.end());

  for (const IdentDecl& d : idents) {
    if (d.axis >= dims.dims()) {
      return fail(ErrorCode::ValidationError,
                  "identification names axis " + std::to_string(d.axis) +
                      " but the board has " + std::to_string(dims.dims()) + " axes");
    }
    if (d.kind == BoundaryKind::Open) {
      return fail(ErrorCode::ValidationError,
                  "an identification cannot declare kind 'open'; omit it instead");
    }
    for (std::uint8_t f : d.flipAxes) {
      if (f >= dims.dims()) {
        return fail(
            ErrorCode::ValidationError,
            "identification flips axis " + std::to_string(f) + ", which does not exist");
      }
    }
    if ((d.swapA >= 0) != (d.swapB >= 0)) {
      return fail(ErrorCode::ValidationError, "an axis swap needs both axes");
    }
    if (d.swapA >= 0 && (d.swapA >= dims.dims() || d.swapB >= dims.dims() ||
                         dims.extent(static_cast<std::size_t>(d.swapA)) !=
                             dims.extent(static_cast<std::size_t>(d.swapB)))) {
      return fail(ErrorCode::ValidationError,
                  "swapped axes must exist and have equal extents");
    }

    // A Periodic declaration glues both faces of its axis; a Mirror declares
    // only the named one. Contradictory declarations are a validation error
    // rather than a last-writer-wins surprise.
    const bool both = d.kind == BoundaryKind::Periodic;
    for (int s = 0; s < 2; ++s) {
      const Side side = s == 0 ? Side::Min : Side::Max;
      if (!both && side != d.side) continue;
      auto& slot = g.faceSet_[d.axis][static_cast<std::size_t>(s)];
      if (slot) {
        return fail(ErrorCode::ValidationError,
                    "axis " + std::to_string(d.axis) + " face " +
                        (side == Side::Min ? "min" : "max") + " is identified twice");
      }
      slot = true;
      g.faceXf_[d.axis][static_cast<std::size_t>(s)] = buildFaceTransform(dims, d, side);
      g.faceKind_[d.axis][static_cast<std::size_t>(s)] = d.kind;
      g.hasIdents_ = true;
      if (g.faceXf_[d.axis][static_cast<std::size_t>(s)].reversesOrientation()) {
        g.reversingFace_ = true;
        // A gluing through an orientation-reversing map makes the surface itself
        // non-orientable; a reflecting wall does not glue anything, so it turns rays
        // around without changing the topology.
        if (d.kind == BoundaryKind::Periodic) g.orientable_ = false;
      }
    }
  }
  return g;
}

const Transform* Geometry::faceTransform(std::uint8_t axis, Side side) const {
  const auto s = static_cast<std::size_t>(side == Side::Min ? 0 : 1);
  if (axis >= dims_.dims() || !faceSet_[axis][s]) return nullptr;
  return &faceXf_[axis][s];
}

BoundaryKind Geometry::boundaryKind(std::uint8_t axis, Side side) const {
  const auto s = static_cast<std::size_t>(side == Side::Min ? 0 : 1);
  if (axis >= dims_.dims() || !faceSet_[axis][s]) return BoundaryKind::Open;
  return faceKind_[axis][s];
}

bool Geometry::stepBoundary(Walker& w) const {
  CB_COUNT(BoundarySteps);
  if (!hasIdents_) {
    CB_COUNT(RaysTerminated);
    return false;
  }

  // Take the step in the covering space, then fold back through identifications
  // until every coordinate is in range. A diagonal step through a corner crosses
  // two seams, hence the loop; the cap bounds it for any well-formed geometry.
  Coord q = w.coord;
  Direction d = w.dir;
  std::uint8_t upAxis = w.upAxis;
  std::int8_t upSign = w.upSign;
  for (std::uint8_t k = 0; k < d.nsup; ++k) {
    const std::uint8_t a = d.sup[k];
    q.c[a] = static_cast<std::int16_t>(q.c[a] + d.v[a]);
  }

  for (int guard = 0; guard < kMaxSeamCrossingsPerStep; ++guard) {
    int offAxis = -1;
    Side offSide = Side::Max;
    for (std::uint8_t a = 0; a < dims_.dims(); ++a) {
      if (q.c[a] >= dims_.extent(a)) {
        offAxis = a;
        offSide = Side::Max;
        break;
      }
      if (q.c[a] < 0) {
        offAxis = a;
        offSide = Side::Min;
        break;
      }
    }
    if (offAxis < 0) {  // fully folded back into the board
      w.coord = q;
      w.cell = dims_.toCell(q);
      w.dir = d;
      w.delta = dims_.delta(d);
      w.upAxis = upAxis;
      w.upSign = upSign;
      return true;
    }

    const Transform* xf = faceTransform(static_cast<std::uint8_t>(offAxis), offSide);
    if (xf == nullptr) {  // an open face: the ray ends here
      CB_COUNT(RaysTerminated);
      return false;
    }
    q = xf->applyToCoord(q);
    d = xf->applyToDir(d);
    if (upSign != 0) {
      // out[i] = sign[i] * in[src[i]], so the tracked unit vector lands on the axis
      // whose source it is.
      for (std::uint8_t i = 0; i < xf->n; ++i) {
        if (xf->src[i] == upAxis) {
          upAxis = i;
          upSign = static_cast<std::int8_t>(upSign * xf->sign[i]);
          break;
        }
      }
    }
  }

  // Unreachable for a validated geometry: a step of bounded length cannot cross
  // more faces than there are faces. Treat as ray termination rather than UB.
  return false;
}

}  // namespace cb
