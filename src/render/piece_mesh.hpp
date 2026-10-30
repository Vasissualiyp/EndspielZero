// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "assets/piece_model.hpp"
#include "variant/variant.hpp"

namespace cb::render {

struct MeshVertex {
  float pos[3];
  float normal[3];
  /// Position within the piece's own bounding box, 0 at the base and 1 at the top.
  /// The shader uses it to shade a piece from its foot upward, which is what stops a
  /// tall piece reading as a flat slab.
  float height;
  /// Multiplied into the instance's colour. White for every authored shape, where the
  /// instance is what carries the colour and one mesh is one thing. The geometry view's
  /// board is the exception it exists for: there the whole board is a single mesh with
  /// sixty-four colours in it, because the squares are patches of one surface rather
  /// than sixty-four copies of a slab.
  float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
};

/// A range inside the shared vertex and index buffers.
struct MeshRange {
  std::uint32_t firstIndex{0};
  std::uint32_t indexCount{0};
  std::int32_t vertexOffset{0};
};

/// How the shape view's icon-faced cylinder pieces are built (M17.25).
///
/// A `PieceIcon`'s outlines are in a 100x100, y-down box; the puck recentres them on the
/// icon's own centroid and scales by this much into the mesh's local units. The icon's
/// widest base (about 50 units) then lands near a sculptural piece's foot (about 0.34),
/// so the same `fit` scale sizes the two the same way. The extruded half-thickness is in
/// the same local units. Public so the property test can state the exact map rather than
/// a loose "up to scale".
inline constexpr float kCylinderIconScale = 0.0135f;
inline constexpr float kCylinderHalfThick = 0.11f;

/// The token disc the icon decal sits on (M17.26). A true circular puck - round caps and
/// a round wall - sized so its radius is the old icon puck's own footprint, so the icon
/// shrinks into it rather than the token growing around it. Its half-thickness is a hair
/// less than the icon's so the decal's caps sit just proud of the disc instead of
/// z-fighting it at the same plane.
inline constexpr float kCylinderDiscHalfThick = 0.10f;

/// The flat view draws its glyph at 0.82 of the token circle's radius; the icon decal is
/// `kCylinderIconScale` shrunk by exactly that, so the shape view's disc-plus-figure is
/// the flat view's circle-plus-figure carried into a solid.
inline constexpr float kCylinderDecalRatio = 0.82f;
inline constexpr float kCylinderDecalScale = kCylinderIconScale * kCylinderDecalRatio;

/// The radius of the shared foot every sculptural piece stands on, in the mesh's own
/// local units - the widest part of the piece, and so the radius that bounds its
/// silhouette. `addFoot` builds to it, and the shape view's pick uses it to size the
/// volume a click may land on (M17.27). One number read in both places, so retuning the
/// pieces cannot leave the picker behind.
inline constexpr float kPieceFootRadius = 0.34f;

/// The shapes a piece can be given.
///
/// Pieces cannot ship as model files: a variant may declare a piece nobody anticipated,
/// and a Workshop author certainly will. So each piece is assembled at load from
/// primitives - a base, a shaft, a crown - against an archetype the variant names. A
/// user-defined piece gets a body without anyone authoring a mesh.
enum class Archetype : std::uint8_t {
  Cell,      ///< the board cell itself, a flat slab
  Dome,      ///< pawn
  Tower,     ///< rook
  Wedge,     ///< knight
  Spire,     ///< bishop
  Crown,     ///< queen
  Monolith,  ///< king, or anything royal
  Horn,      ///< unicorn and other pieces that only exist above two dimensions
  Portal,    ///< the opening a wrapped move passes through: a cube, so it can stand
             ///< on whichever axis the seam it belongs to actually faces
  Arrow,     ///< the flat triangular head on a timeline's rail: a direction, in the board
  Fillet,    ///< a quarter disc: the rounded corner where a connector bends into a rail
  Disc,      ///< a soft radial decal: the contact shadow under a piece (M18.2). Its
             ///< falloff is baked into its vertices, so no other shape is one
  Count
};

/// Resolve a name from a variant file. An unknown name is not an error - it falls back
/// to Tower, because a variant must never be unplayable for want of a model.
Archetype archetypeFromName(std::string_view name);
std::string_view archetypeName(Archetype a);

/// Pick a shape for a piece that did not declare one.
///
/// Royal pieces get the monolith. Otherwise the choice follows the piece's *movement*:
/// how many axes its atoms need and how far they reach. That is already the engine's
/// best evidence about what a piece is, so a variant that says nothing still gets an
/// army that reads at a glance rather than seven identical blocks.
Archetype archetypeFor(const PieceTypeDef& piece);

/// Relative height, with a pawn at 1.0. Encodes value, so the tallest piece on the
/// board is the one that matters most even in an army you have never seen.
float heightFor(const PieceTypeDef& piece);

/// How many pieces of one variant can carry an authored body (M7.3.4). A shape slot is
/// either an `Archetype` (below `Archetype::Count`) or `Archetype::Count + j` for the
/// j-th authored model; a piece past the cap falls back to its archetype, exactly as a
/// piece with no model does.
inline constexpr std::size_t kAuthoredSlots = 32;
inline constexpr std::size_t kShapeSlots =
    static_cast<std::size_t>(Archetype::Count) + kAuthoredSlots;

/// The shape slot each piece type of `v` draws with, indexed by `PieceTypeId` (entry 0,
/// the empty piece, is Tower and unused). Pure: the renderer's instance building and its
/// mesh library both derive from it, so they cannot disagree about which slot is whose.
std::vector<std::size_t> shapeSlots(const VariantSpec& v);

/// A fingerprint of every authored model and icon in `v`, in slot order - what decides
/// whether the GPU mesh library has to be rebuilt. Zero for a variant with none.
std::uint64_t authoredKey(const VariantSpec& v);

/// Build every archetype into one vertex and index buffer, with a range per shape.
struct MeshLibrary {
  std::vector<MeshVertex> vertices;
  std::vector<std::uint16_t> indices;
  MeshRange ranges[kShapeSlots];
  /// The shape view's alternate piece mesh (M17.25): each archetype's **Primitive** icon
  /// extruded into a two-sided puck, the same outline on both faces. Empty for the
  /// archetypes that are not pieces - Cell, Portal, Arrow, Fillet and Disc. Lives in the
  /// same vertex and index buffers as `ranges`, so the draw loop binds one buffer either
  /// way and only the range changes.
  MeshRange cylinderRanges[kShapeSlots];
  /// The shape view's token discs (M17.26): a plain white circular puck under each icon
  /// decal, one per archetype, in the same buffers. The disc's colour is the *instance's*
  /// - read live off `theme_.pieceToken` every build - never baked here, exactly like the
  /// icon mesh. Empty for the archetypes that are not pieces.
  MeshRange discRanges[kShapeSlots];

  /// Every archetype, plus - when `v` is given - each of its authored bodies in its own
  /// slot (see shapeSlots). An authored slot's puck and disc are its authored icon's when
  /// it has one, otherwise its archetype's.
  static MeshLibrary build(const VariantSpec* v = nullptr);

  /// Sweep an authored model into the buffers and return its range: the profile revolved
  /// about the height axis in `segments` steps, and each element extruded and placed by
  /// the symmetry group. Permille map to local units at 1/1000, so a profile foot of 340
  /// is the archetypes' shared foot radius.
  MeshRange appendModel(const assets::PieceModel& model);
};

}  // namespace cb::render
