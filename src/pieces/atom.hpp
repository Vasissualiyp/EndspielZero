// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "base/result.hpp"
#include "base/small_vec.hpp"
#include "space/coord.hpp"

namespace cb {

enum class Color : std::uint8_t { White = 0, Black = 1 };
constexpr int kNumColors = 2;

inline Color opponent(Color c) noexcept {
  return c == Color::White ? Color::Black : Color::White;
}

/// How a piece traverses the cells between origin and destination.
enum class MoveMode : std::uint8_t {
  Slide,  ///< repeat the atom step; blocked by the first occupied cell
  Leap,   ///< ignore whatever lies between (a knight)
  Hop,    ///< must clear exactly one occupied cell (a cannon, a grasshopper)
};

/// What a move may do to an occupied destination.
enum class CapturePolicy : std::uint8_t {
  May,     ///< the ordinary case
  Must,    ///< only legal as a capture (a pawn's diagonal; a checkers jump)
  Cannot,  ///< only legal onto an empty cell (a pawn's push)
};

constexpr std::uint32_t kUnlimited = 0xFFFF'FFFFu;

/// The three kinds of move (M19). They differ in what the vector means:
///
///   Extrapolated  the magnitudes are *free*: permuted over every axis and every sign. A
///                 rook is `[1]`, a knight `[1,2]`. This is the original atom, and all but
///                 the pawn are this.
///   Exact         the vector is literal and signed, positional by axis, taken once:
///                 `[1,0]` is one cell along axis 0 and nothing else.
///   Expanded      a literal vector times `k`, in both senses unless `sign` restricts it:
///                 `[1,0]` is right or left, any distance; with `Positive` it is a lance.
///
/// Extrapolated moves may also *pin* components: a pawn's capture is "pin forward +1, one
/// free magnitude 1", which in two dimensions is the two forward diagonals and in four is
/// +/-1 on any one of the three other axes with +1 forward.
enum class MoveKind : std::uint8_t { Extrapolated, Exact, Expanded };

/// Which senses an expanded move runs in.
enum class StepSign : std::uint8_t { Both, Positive, Negative };

/// A component fixed to an axis with a sign: `{axis 1, +1}` is "one step forward" on a
/// board whose second axis is rank.
struct Pin {
  std::uint8_t axis{0};
  std::int16_t value{0};

  friend bool operator==(const Pin&, const Pin&) = default;
};

/// A region of the board, expressed as a predicate on one axis. Deliberately
/// weak: "the last rank" and "the home rank" do not exist on a torus, so a
/// variant there simply leaves the region inactive (M3.4).
struct Region {
  int axis{-1};
  std::int16_t value{0};

  [[nodiscard]] bool active() const noexcept { return axis >= 0; }
  [[nodiscard]] bool contains(const Coord& p) const noexcept {
    return active() && p.c[static_cast<std::size_t>(axis)] == value;
  }
};

/// A "vector move" in the sense of the project spec: a multiset of nonzero
/// magnitudes, implicitly padded with zeros ("NULL"), expanded over every
/// assignment of those magnitudes to distinct axes and every combination of
/// signs, repeated up to `maxK` times.
///
///   rook        [1]^inf   slide
///   bishop      [1,1]^inf slide
///   knight      [1,2]^1   leap
///   king        [1]^1 + [1,1]^1
///   nightrider  [1,2]^inf slide
///
/// The expansion is dimension-agnostic, which is the whole point: a knight on a
/// 4-D board is this same atom, not new engine code.
struct MoveAtom {
  /// Which of the three kinds this is; see `MoveKind`.
  MoveKind kind{MoveKind::Extrapolated};
  /// Exact and expanded: the whole vector, one pin per nonzero component. Extrapolated:
  /// the components that are fixed rather than free (usually none).
  SmallVec<Pin, kMaxDims> pins{};
  /// For the other colour, negate every pin on the variant's orientation axis. On by
  /// default for the kinds that have a direction (the loader sets it); a rook has none.
  bool flip{false};
  /// Expanded only: run in both senses, or one.
  StepSign sign{StepSign::Both};
  /// Extrapolated: the free magnitudes, canonical: ascending, all > 0. Empty for the
  /// literal kinds.
  SmallVec<std::int16_t, kMaxDims> mags;
  /// Step counts this atom may take: k in [minK, maxK]. minK > 1 is how a
  /// "moves exactly n spaces" move (a pawn's double step) is expressed without
  /// duplicating the single-step move.
  std::uint32_t minK{1};
  std::uint32_t maxK{1};
  MoveMode mode{MoveMode::Leap};
  CapturePolicy capture{CapturePolicy::May};

  /// Restrict the expansion to the forward half-space along the variant's
  /// orientation axis. This is how "forward" acquires a meaning in any number of
  /// dimensions, for pawns and anything else that cares.
  bool oriented{false};
  /// Available only while the piece stands in this region - per colour, because
  /// a home rank is mirrored. Inactive means "always available". This is how a
  /// pawn's double step is expressed in a way that round-trips FEN: the
  /// condition is *where the pawn is*, not a hidden "has moved" bit.
  Region fromRegion[kNumColors]{};
  /// Passing over cells must leave an en-passant target behind (a double step).
  bool leavesEnPassant{false};

  /// Span into the variant's global direction table, per colour. The two spans
  /// differ only for oriented atoms.
  std::uint32_t dirBegin[kNumColors]{};
  std::uint32_t dirEnd[kNumColors]{};

  /// Span used when searching *backwards* from a threatened cell.
  ///
  /// For an unoriented atom this is just the move span: the expansion of a
  /// magnitude multiset is closed under signed axis permutations, and every
  /// boundary transform is one, so a transported direction is always still in the
  /// set. An *oriented* atom's span is a filtered half of that set, and a seam
  /// that reverses orientation maps a forward direction to a backward one - out of
  /// the span entirely. Searching backwards must therefore start from the full
  /// unfiltered expansion and check the arrival direction on the way in, or
  /// pawn-like threats across a Moebius or Klein seam are simply missed.
  /// Per colour, because an oriented atom's directions are mirrored between the
  /// two sides: searching for a Black threat with White's directions finds nothing.
  std::uint32_t threatBegin[kNumColors]{};
  std::uint32_t threatEnd[kNumColors]{};

  [[nodiscard]] std::uint32_t dirCount(Color c) const {
    return dirEnd[static_cast<std::size_t>(c)] - dirBegin[static_cast<std::size_t>(c)];
  }

  /// Sort magnitudes and reject malformed ones, so that [2,1] and [1,2] are the
  /// same atom and comparisons are meaningful.
  static Result<MoveAtom> canonicalize(MoveAtom a);

  [[nodiscard]] std::string toString() const;

  friend bool operator==(const MoveAtom& a, const MoveAtom& b);
};

/// The directions an atom moves in for one colour, whatever its kind: the one definition
/// that movegen, the preview and the tests agree on. `orientAxis` is the variant's
/// orientation axis or -1. An atom that needs more axes than the board has expands to
/// nothing, as `expandAtom` does. An exact slide of `n` cells is the unit direction taken
/// `n` times (see `exactSteps`), so the cells between are checked.
std::vector<Direction> expandAtomFor(const MoveAtom& atom, std::uint8_t dims,
                                     int orientAxis, Color color);

/// How many steps an exact slide is: the gcd of its vector, which is what makes a
/// two-cell step need its first cell empty. 1 for anything else.
std::uint32_t exactSteps(const MoveAtom& atom) noexcept;

/// The step range the engine walks for this atom: an exact move is exactly its
/// `exactSteps`, everything else is `[minK, maxK]`.
struct StepRange {
  std::uint32_t lo;
  std::uint32_t hi;
};
StepRange stepRange(const MoveAtom& atom) noexcept;

/// Expand an atom's magnitudes into direction vectors, in a canonical
/// (lexicographic) order so that generation is deterministic across compilers.
///
/// An atom of order r > dims expands to nothing: a piece whose move needs three
/// distinct axes simply cannot move on a 2-D board. That is a deliberate,
/// tested decision rather than an error (M1.4).
std::vector<Direction> expandAtom(const SmallVec<std::int16_t, kMaxDims>& mags,
                                  std::uint8_t dims);

/// As above, but keeping only directions pointing forward along `orientAxis`
/// for White, and their negations for Black.
std::vector<Direction> expandAtomOriented(const SmallVec<std::int16_t, kMaxDims>& mags,
                                          std::uint8_t dims, std::uint8_t orientAxis,
                                          Color color);

/// The closed form for the number of directions an atom expands to in `dims`
/// dimensions: P(dims, r) / prod(multiplicity!) * 2^r, or 0 when r > dims.
/// Tests assert the enumeration against this independently computed value, which
/// is what catches the classic duplicate-magnitude double-count.
std::uint64_t expectedDirectionCount(const SmallVec<std::int16_t, kMaxDims>& mags,
                                     std::uint8_t dims);

/// The atoms of a *line* mover: `maxK` steps along a line using up to `maxAxes` axes at
/// once, one atom `[1]^r` for each r in 1..maxAxes (r > dims expands to nothing, so the
/// same list serves every board).
///
///   king   `straightLineAtoms(1)`: one or two axes - rook and bishop steps (default 2).
///   queen  `straightLineAtoms(kUnlimited, kMaxDims)`: every straight line in every
///          dimension, so on a cube also the triagonals, on a tesseract the
///          quadragonals, and so on. The unicorn is the triagonal slider *alone*.
///
/// Tests assert the queen reaches every direction of support 1..dims.
[[nodiscard]] std::vector<MoveAtom> straightLineAtoms(std::uint32_t maxK, std::size_t maxAxes = 2);

}  // namespace cb
