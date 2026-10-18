// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/result.hpp"
#include "base/zobrist.hpp"
#include "geometry/geometry.hpp"
#include "assets/piece_model.hpp"
#include "pieces/atom.hpp"
#include "space/dim_spec.hpp"

namespace cb {

/// A custom data field attached to pieces or to cells.
///
/// Columns, not per-piece maps: a variant declaring one field gets one array, and a
/// variant declaring none pays nothing at all (ARCH section 5). This is what makes
/// "explosive chess", "charged pieces" and the rest data rather than engine code.
struct FieldDecl {
  std::string name;
  std::int32_t defaultValue{0};
  std::int32_t minValue{-32768};
  std::int32_t maxValue{32767};
  /// Whether the field is part of the position's identity. An unhashed field is
  /// cosmetic: two positions differing only in it are the same position.
  bool hashed{true};
};

using PieceTypeId = std::uint16_t;
constexpr PieceTypeId kNoPiece = 0;  ///< type 0 means "empty cell"

/// A piece kind. Everything that distinguishes a knight from a nightrider lives
/// in `atoms`; nothing about movement is hardcoded anywhere else.
struct PieceTypeDef {
  std::string name;
  char symbol{'?'};   ///< upper case; lower case denotes Black in notation
  bool royal{false};  ///< losing or exposing it ends the game, per the variant
  std::vector<MoveAtom> atoms;
  std::vector<PieceTypeId> promotesTo;  ///< empty means this piece never promotes
  /// Whether moving this piece resets the draw clock. Declared rather than
  /// inferred: "it is a pawn" is not a concept the engine has.
  bool resetsDrawClock{false};

  /// Appearance, declared the same way movement is. \`shape\` names an archetype the
  /// renderer assembles from primitives; empty means "derive it from the atoms".
  /// Height is in permille of a pawn, as an integer because this layer is part of the
  /// deterministic core and may not contain floating point.
  std::string shape;
  int heightPermille{0};
  /// Computed by finalize(): some move of this piece needs `unmoved`, so the engine keeps
  /// its moved bit. A piece nothing asks about is never tracked.
  bool tracksMoved{false};
  /// An authored body and flat icon (M7.3), when the variant gives the piece them. Both
  /// are cosmetic and outside the variant's identity: changing how a bishop looks must
  /// not make a saved game unreplayable. Integer permille, like everything here.
  std::optional<assets::PieceModel> body;
  std::optional<assets::IconModel> icon;
};

/// A multi-piece move template. Castling is the familiar instance, but the shape
/// is general: an ordered list of displacements plus conditions, which works in
/// any dimension and on any topology.
struct CastleTemplate {
  std::string name;  ///< the FEN letter for standard chess ("K", "Q", "k", "q")
  Color color{Color::White};
  CellId kingFrom{kInvalidCell};
  CellId kingTo{kInvalidCell};
  CellId rookFrom{kInvalidCell};
  CellId rookTo{kInvalidCell};
  std::vector<CellId> mustBeEmpty;
  std::vector<CellId> mustBeSafe;  ///< not attacked by the opponent
  std::uint8_t rightsBit{0};
};

struct StartPiece {
  Coord at;
  PieceTypeId type{kNoPiece};
  Color color{Color::White};
};

/// What to do when the side to move has no legal move and is not in check.
enum class StalematePolicy : std::uint8_t { Draw, Loss, Win };

/// How hard a variant admits to being. It decides nothing about play - it is metadata
/// for the library, which orders its list by this and colours each entry with it.
///
/// Ordered easiest to hardest with `Other` last, so a variant whose author has not said
/// (or a Workshop package this build does not know) collects at the end rather than
/// claiming a place it has not earned.
enum class Difficulty : std::uint8_t { Easy = 0, Medium, Hard, Impossible, Other };

/// The lower-case word a variant file uses for a level. Round-trips the TOML spelling.
[[nodiscard]] std::string_view difficultyName(Difficulty d) noexcept;

/// A fully resolved variant: everything L4..L8 needs, precomputed once at load.
/// Nothing below L9 ever parses text.
class VariantSpec {
 public:
  std::string name;
  /// One line, in the variant author's own words, of what the game *is*. The picker
  /// shows it in place of a generated description, because a player choosing a game
  /// wants the author's pitch rather than an axis count.
  std::string description;
  /// Advertised difficulty, from the variant file's optional `difficulty` key. Not part
  /// of play; the library uses it to order and colour its list. `Other` when unset.
  Difficulty difficulty{Difficulty::Other};
  DimSpec dims;
  Geometry geom;

  /// Index 0 is the reserved empty entry, so a PieceTypeId indexes directly.
  std::vector<PieceTypeDef> pieces;
  std::vector<CastleTemplate> castles;
  std::vector<StartPiece> start;
  Color startSideToMove{Color::White};

  /// The axis along which "forward" is defined, and therefore the axis pawn-like
  /// pieces and promotion regions refer to.
  int orientationAxis{-1};
  Region promotion[kNumColors]{};
  bool enPassant{false};
  StalematePolicy stalemate{StalematePolicy::Draw};
  /// 0 disables the rule; standard chess uses 100 half-moves.
  int halfmoveDrawLimit{0};

  /// Custom fields. Piece fields travel with the piece; cell fields stay put.
  std::vector<FieldDecl> pieceFields;
  std::vector<FieldDecl> cellFields;

  /// Temporal policy, as data (M6). Which way each player's timelines grow, and how far
  /// a new timeline advances past the board it branches from. Defaults are the reference
  /// game's: White down (-1), Black up (+1), one turn on.
  int temporalWhiteSign{-1};
  int temporalBlackSign{1};
  int temporalBranchAdvance{1};

  /// The variant's rules, as data. Held by opaque pointer so that the position layer
  /// does not need to know the rule VM exists.
  std::shared_ptr<const void> ruleSet;

  /// Digest of the rule set, supplied by whoever built it. Rules are part of a
  /// variant's identity - two games with the same pieces and different rules are not
  /// the same game - but this layer cannot see the rule types, so the owner hashes them.
  std::function<std::uint64_t()> ruleSetDigest;

  [[nodiscard]] int findPieceField(std::string_view n) const;
  [[nodiscard]] int findCellField(std::string_view n) const;

  /// Global direction table. Atom spans index into this.
  std::vector<Direction> dirTable;

  /// Hash keys, derived from the variant id so that a position's hash is
  /// reproducible across runs and hosts (ARCH section 11). Filled by finalize().
  Zobrist zob;

  /// Resolve atoms into the direction table and validate the whole spec. Call
  /// exactly once, after the declarative fields are filled in.
  [[nodiscard]] Result<void> finalize();

  [[nodiscard]] bool finalized() const noexcept { return finalized_; }
  [[nodiscard]] std::uint64_t variantId() const noexcept { return variantId_; }
  [[nodiscard]] std::uint32_t pieceCodes() const noexcept {
    return static_cast<std::uint32_t>(pieces.size()) * kNumColors;
  }
  [[nodiscard]] PieceTypeId findPiece(std::string_view name) const;
  [[nodiscard]] PieceTypeId findPieceBySymbol(char upper) const;

  /// Upper bound on moves generated from any one position, used to size move
  /// buffers once so that movegen itself never allocates.
  [[nodiscard]] std::uint32_t moveUpperBound() const noexcept { return moveBound_; }

  /// Total directions across all pieces and colours; the budget check subject.
  [[nodiscard]] std::size_t directionCount() const noexcept { return dirTable.size(); }

 private:
  bool finalized_{false};
  std::uint64_t variantId_{0};
  std::uint32_t moveBound_{0};
};

/// Budget on the size of the global direction table. [1,2,3] alone is 2688
/// directions at 8 dimensions, so this has to be a real check with a real
/// message rather than an assert (M2.3).
constexpr std::size_t kMaxDirections = 1 << 16;

}  // namespace cb
