// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
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
  /// Cell-local panel UV; tag 0 uses the ordinary slab's coordinates, +1 is a
  /// sampled surface face, -1 is a sidewall. Independent of slide/world position.
  float panelCoord[3]{};
  /// Cell display signal: stable seed, explicit response gain, effect weight (negative
  /// when the cell carries a gameplay cue), heat (own channel, M23.9 D). Sampled LED
  /// faces supply this here; ordinary slabs use their instance instead.
  float panelSignal[4]{};
  /// Living colour change: the colour this cell is leaving (rgb) and seconds since it
  /// changed (negative = settled, no transition). Written by `PanelResponse`.
  float panelPrev[4]{0.0f, 0.0f, 0.0f, -1.0f};
};

static_assert(sizeof(MeshVertex) == 88);

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

/// A variant's own pieces can bring bodies of their own, and each takes one of these
/// extra shape slots after the archetypes. A shape index at or above `Archetype::Count`
/// is therefore a custom body; a variant with more custom pieces than slots draws the
/// rest from their archetype, which is the fallback that keeps a variant playable.
inline constexpr std::size_t kCustomShapes = 12;
inline constexpr std::size_t kShapeSlots =
    static_cast<std::size_t>(Archetype::Count) + kCustomShapes;
[[nodiscard]] constexpr Archetype customShape(std::size_t slot) noexcept {
  return static_cast<Archetype>(static_cast<std::size_t>(Archetype::Count) + slot);
}

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

/// A piece body built from an authored model: the profile swept about the axis, plus the
/// elements the symmetry places. Local units as the archetypes use them - the foot's
/// radius a third of a cell, z up, the model's permille divided by a thousand - and the
/// same `MeshVertex` the shared buffers take, with indices relative to the first vertex.
/// Deterministic: one model, one mesh.
struct ModelMesh {
  std::vector<MeshVertex> vertices;
  std::vector<std::uint16_t> indices;
  /// The highest point of the body, in local units.
  float height{0.0f};
};
ModelMesh assembleModel(const assets::PieceModel& model);

/// Relative height, with a pawn at 1.0. Encodes value, so the tallest piece on the
/// board is the one that matters most even in an army you have never seen.
float heightFor(const PieceTypeDef& piece);

/// Build every archetype into one vertex and index buffer, with a range per shape.
/// A piece body to build into the library at a custom slot, with the icon its puck and
/// token take - the authored one, or the shipped one for its archetype.
struct CustomBody {
  std::size_t slot{0};
  /// The shipped shape the piece would otherwise be, whose icon a body with none of its
  /// own keeps.
  Archetype fallback{Archetype::Tower};
  assets::PieceModel body;
  std::optional<assets::IconModel> icon;
};

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

  static MeshLibrary build(const std::vector<CustomBody>& custom = {});
};

}  // namespace cb::render
