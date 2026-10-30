// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "base/result.hpp"
#include "diag/counters.hpp"
#include "geometry/transform.hpp"
#include "space/dim_spec.hpp"

namespace cb {

enum class Side : std::uint8_t { Min, Max };

/// How a declared boundary face behaves.
enum class BoundaryKind : std::uint8_t {
  Open,      ///< the default: a ray leaving here terminates
  Periodic,  ///< glued to the opposite face (cylinder / torus)
  Mirror,    ///< reflecting wall; the ray bounces and the direction flips
};

/// One authored boundary identification.
///
///   axis      the axis whose face is being identified
///   side      which face (Min or Max); Periodic declares both at once
///   kind      Periodic or Mirror
///   flipAxes  axes whose coordinate is reversed on crossing (x -> extent-1-x).
///             A flip on the *crossing* axis would be a mirror; a flip on
///             another axis is what makes a seam a Moebius/Klein seam.
///   swap      optional pair of axes exchanged on crossing.
struct IdentDecl {
  std::uint8_t axis{0};
  Side side{Side::Max};
  BoundaryKind kind{BoundaryKind::Periodic};
  std::vector<std::uint8_t> flipAxes;
  int swapA{-1};
  int swapB{-1};
};

/// A ray in flight: the flat cell, its decoded coordinate, the current
/// direction, and the cached flat delta for that direction. Carrying the
/// coordinate is what keeps the interior step free of any decode or division
/// (ARCH section 4.2).
struct Walker {
  CellId cell{kInvalidCell};
  Coord coord{};
  Direction dir{};
  std::int32_t delta{0};
  /// Optional frame tracking for a directional piece: where the unit vector along
  /// `upAxis` (initially the variant's orientation axis) has been carried by the seams
  /// crossed so far, and with what sign. upSign == 0 means "not tracked", which is
  /// the default and costs the boundary step one comparison.
  std::uint8_t upAxis{0};
  std::int8_t upSign{0};

  /// Has the tracked axis come back reversed? A seam that swaps it onto another axis
  /// leaves no answer, and that is read as "not reversed".
  [[nodiscard]] bool upReversed(std::uint8_t orientAxis) const noexcept {
    return upSign < 0 && upAxis == orientAxis;
  }
};

/// Boundary topology. With no identifications this is an ordinary box and the
/// interior fast path is all that ever runs.
class Geometry {
 public:
  static Result<Geometry> create(const DimSpec& dims, std::span<const IdentDecl> idents);

  [[nodiscard]] const DimSpec& dims() const noexcept { return dims_; }
  [[nodiscard]] bool isBox() const noexcept { return !hasIdents_; }
  [[nodiscard]] std::size_t identCount() const noexcept { return idents_.size(); }

  /// Is the quotient surface orientable?
  ///
  /// Only *gluings* can make it otherwise: a Moebius or Klein seam identifies two
  /// faces through an orientation-reversing map, and the resulting surface has no
  /// consistent handedness - which is why "forward" stops being definable and a
  /// bishop stops being colour-bound (M3.4).
  ///
  /// A reflecting wall is deliberately not counted. It glues nothing, so the board
  /// remains an ordinary box of distinct cells; it merely turns rays around. Ask
  /// hasOrientationReversingFace() if that is what you care about.
  [[nodiscard]] bool isOrientable() const noexcept { return orientable_; }

  /// Does any boundary face - gluing or mirror - reverse handedness? Relevant to
  /// anything reasoning about a ray's chirality rather than about the surface.
  [[nodiscard]] bool hasOrientationReversingFace() const noexcept {
    return reversingFace_;
  }

  [[nodiscard]] Walker start(CellId cell, const Direction& d) const {
    Walker w;
    w.cell = cell;
    w.coord = dims_.toCoord(cell);
    w.dir = d;
    w.delta = dims_.delta(d);
    return w;
  }

  /// Advance one step of `w.dir`. Returns false when the ray leaves the board.
  ///
  /// The interior case - overwhelmingly the common one - is a range check on the
  /// one to three axes in the direction's support plus a single integer add.
  bool step(Walker& w) const {
    const Direction& d = w.dir;
    for (std::uint8_t k = 0; k < d.nsup; ++k) {
      const std::uint8_t a = d.sup[k];
      const int nv = w.coord.c[a] + d.v[a];
      if (nv < 0 || nv >= dims_.extent(a)) return stepBoundary(w);
    }
    for (std::uint8_t k = 0; k < d.nsup; ++k) {
      const std::uint8_t a = d.sup[k];
      w.coord.c[a] = static_cast<std::int16_t>(w.coord.c[a] + d.v[a]);
    }
    w.cell = static_cast<CellId>(static_cast<std::int64_t>(w.cell) + w.delta);
    CB_COUNT(InteriorSteps);
    return true;
  }

  /// Analytic transport across one or more seams. Public so that the
  /// table-vs-analytic differential test can reach it directly (M3.2).
  bool stepBoundary(Walker& w) const;

  /// Diagnostics for the renderer and for tests: the transform applied when
  /// leaving `axis` on `side`, if that face is identified.
  [[nodiscard]] const Transform* faceTransform(std::uint8_t axis, Side side) const;

  /// What leaving `axis` on `side` does: a gluing returns the ray somewhere else, a
  /// reflecting wall turns it around, an open face ends it. The move animation needs
  /// this to tell a portal from a bounce - the two are the same boundary step and only
  /// the declaration says which it is.
  [[nodiscard]] BoundaryKind boundaryKind(std::uint8_t axis, Side side) const;

 private:
  static constexpr int kMaxSeamCrossingsPerStep = 2 * kMaxDims;

  DimSpec dims_;
  std::vector<IdentDecl> idents_;
  /// faceXf_[axis][side] - the coordinate map applied on leaving that face.
  std::array<std::array<Transform, 2>, kMaxDims> faceXf_{};
  std::array<std::array<bool, 2>, kMaxDims> faceSet_{};
  std::array<std::array<BoundaryKind, 2>, kMaxDims> faceKind_{};
  bool hasIdents_{false};
  bool orientable_{true};
  bool reversingFace_{false};
};

}  // namespace cb
