// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/result.hpp"
#include "pieces/atom.hpp"

namespace cb {

/// The moveset language (M19 section 9): a small, typed notation for sets of moves, so a
/// piece's whole movement is one line and a file of them can be shared.
///
///   rook       = [1]^k                       -- k is 1..inf
///   knight     = [1,2]^1
///   queen      = U r in 1..D : [1 x r]^k     -- a union over a bounded integer; D = board dimension
///   lance      = expanded [rank:+1]^k positive
///   pawnPush   = exact [rank:+1] flip cannot
///   pawnTake   = [rank:+1, 1]^1 flip must    -- a pinned component and a free one
///   hybrid     = rook U bishop U (nightrider \ knight)
///
/// A moveset *denotes* `MoveAtom`s and nothing else: parsing evaluates the text against a
/// board (its dimension and axis names), and `printMoveset` is its inverse. It is a data
/// language in the sense of ADR-0005 - declarative, no recursion, no loops except the
/// bounded union, structural termination, and no I/O beyond the `lookup` the caller
/// supplies for names defined elsewhere - so a downloaded moveset is as safe to load as a
/// variant.
struct MovesetContext {
  /// The board the text is read against: its axes, in order, and the orientation axis
  /// (-1 for none). `D` in the text is `dims`.
  std::uint8_t dims{2};
  std::vector<std::string> axisNames;
  int orientAxis{-1};
  /// A name the text does not define itself - the variant's `[movesets]` table, or the
  /// shared library of moveset files. Returns that name's text, or nothing.
  std::function<std::optional<std::string>(std::string_view)> lookup;
};

/// Evaluate moveset text to the moves it denotes, in order of first appearance and without
/// duplicates. The text is either one expression or a sequence of `name = expression`
/// lines, whose value is its last line. Errors carry the line and name the column.
[[nodiscard]] Result<std::vector<MoveAtom>> evalMoveset(std::string_view text,
                                                        const MovesetContext& ctx);

/// The text a set of moves reads as: each move spelled out, joined by `U`. Everything that
/// is the default is left out, so what is printed is what an author would have typed, and
/// `evalMoveset(printMoveset(a)) == a` for every set the language can express.
/// `axisNames` names the axes pinned components refer to.
[[nodiscard]] std::string printMoveset(const std::vector<MoveAtom>& atoms,
                                       const std::vector<std::string>& axisNames,
                                       int orientAxis);

}  // namespace cb
