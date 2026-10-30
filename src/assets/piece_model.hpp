// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/result.hpp"

namespace cb::assets {

/// A piece's shape, as a small vector description rather than a mesh file.
///
/// The whole point of the sandbox is that a variant is data, and a piece nobody
/// anticipated still has to have a body. So geometry is authored the way a glyph is:
/// a closed outline, a rotation, and a symmetry group. The renderer turns that into a
/// mesh exactly as it turns primitives into one today - see ADR-0015 and M7.3.
///
/// **Integer permille throughout**, and no floating point anywhere in this module. A
/// model is content: it is saved, shared, and eventually signed, so two machines must
/// agree on it byte for byte. Trigonometry belongs to the assembly step, above here.
///
/// Geometry here is cosmetic and deliberately does *not* enter `VariantId`: changing how
/// a bishop looks must not make a saved game unreplayable.

/// A point in an authoring plane, in permille of the piece's box (0..1000).
///
/// For a profile the axes are *(radius, height)*, radius outward from the axis of
/// revolution and height up from the foot. For an icon they are the 100x100 glyph box,
/// scaled to permille so both surfaces speak one unit.
struct ModelPoint {
  std::int16_t x{0};
  std::int16_t y{0};

  friend bool operator==(const ModelPoint&, const ModelPoint&) = default;
};

/// How the revolve repeats around the axis.
enum class SymmetryKind : std::uint8_t {
  Full,    ///< radially symmetric - a pawn. Carries no elements, by definition.
  KFold,   ///< `k` copies evenly spaced - the points of a crown.
  Mirror,  ///< one reflected pair - a knight's ears.
};

struct Symmetry {
  SymmetryKind kind{SymmetryKind::Full};
  /// Only meaningful for `KFold`.
  std::uint8_t k{4};

  friend bool operator==(const Symmetry&, const Symmetry&) = default;
};

/// How many copies of an element this symmetry places. `Full` places none, because a
/// radially symmetric piece cannot carry a part that is not radially symmetric.
[[nodiscard]] int copiesOf(const Symmetry& s) noexcept;

/// A closed polyline. Wound in either direction; the assembly step does not care.
struct Outline {
  std::vector<ModelPoint> points;

  friend bool operator==(const Outline&, const Outline&) = default;
};

/// A part that is not radially symmetric, placed in the piece's frame and then repeated
/// by the symmetry group. One ear, mirrored, is two ears.
struct Element {
  /// The part's shape in a vertical plane through the axis: `x` runs along the radial
  /// direction (or, when `tangent`, round the piece), `y` is height above the anchor.
  /// `x` may be negative - a knight's head reaches both sides of the axis.
  Outline outline;
  /// Where the element's origin sits, in the same permille frame as the profile.
  std::int16_t radius{0};
  std::int16_t height{0};
  /// Sideways offset of the origin, round the piece. With a `Mirror` symmetry a nonzero
  /// offset gives the reflected pair (a knight's two ears); with `KFold` every copy has
  /// it. Zero puts the part on the symmetry plane.
  std::int16_t lateral{0};
  /// How thick the part is, extruded both ways from its plane, in permille.
  std::int16_t thickness{120};
  /// The outline spans the tangent direction instead of the radial one: a crown's
  /// points are broad round the rim, a knight's head is long outward.
  bool tangent{false};

  friend bool operator==(const Element&, const Element&) = default;
};

/// The 3-D model: a side profile swept about the height axis, plus placed elements.
struct PieceModel {
  /// The closed profile in the (radius, height) plane. Its foot sits on height 0.
  Outline profile;
  /// Angular segments of the sweep. Low values are faceted on purpose.
  std::uint8_t segments{16};
  Symmetry symmetry{};
  std::vector<Element> elements;

  friend bool operator==(const PieceModel&, const PieceModel&) = default;
};

/// The 2-D model: what the flat board draws. Straight segments only, which is both the
/// visual language and exactly what an ImGui draw list fills without tessellation.
struct IconModel {
  /// Outlines drawn in the piece's own colour.
  std::vector<Outline> fills;
  /// Outlines drawn in the colour behind the piece, cutting detail out of the
  /// silhouette. The cheap style has none.
  std::vector<Outline> cuts;

  friend bool operator==(const IconModel&, const IconModel&) = default;
};

/// Is this a shape the assembly step can build?
///
/// Returns a message rather than a bool, because every one of these is something an
/// author did and needs telling about - a profile that crosses itself has no inside, and
/// rendering it badly is worse than refusing it.
[[nodiscard]] Result<void> validate(const PieceModel& m);
[[nodiscard]] Result<void> validate(const IconModel& m);

/// A first-draft icon, from the model's side silhouette.
///
/// M7.3.5 asks for the *top-down* silhouette. It is the wrong projection and the draft
/// uses the side instead: seen from above, every radially symmetric piece is a disc, so
/// a top-down draft would hand the author the same circle for a pawn and for a queen.
/// The side view is what the shipped tables draw and what a player reads.
[[nodiscard]] IconModel iconFromProfile(const PieceModel& m);

/// The profile a shipped archetype would have, so "start from the pawn" always gives
/// something editable and no piece is ever un-editable for want of an asset (M7.3.4).
/// `name` is an archetype's lower-case name; an unknown one gives the plain slab.
[[nodiscard]] PieceModel archetypeModel(std::string_view name);

}  // namespace cb::assets
