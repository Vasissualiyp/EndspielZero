// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include <toml++/toml.hpp>

#include "pieces/atom.hpp"

namespace cb {

/// How the three kinds of move (M19) are spelled in a `[[piece.move]]` table, shared by the
/// loader and the editor's document so they cannot drift:
///
///   type  = "extrapolated" (default) | "exact" | "expanded"
///   vector = [1, 2]        extrapolated: free magnitudes
///          = [1, 0]        exact / expanded: the literal signed vector, one entry per axis
///   pin   = ["rank:+1"]    extrapolated: components fixed to a named axis
///   flip  = true | false   negate the orientation-axis components for the other side
///   sign  = "both" | "positive" | "negative"   (expanded)
///   marks = [{ tag = "destroyed", where = "around", radius = 1, duration = 3 }]
///           what the move leaves behind: `where` is passed | landed | around, `duration` is
///           a number of plies (omitted: forever)
///   captures_at = ["rank:-1"]   a capture-only move that takes the piece at this offset
///                               from where it lands (en passant, a checkers jump)
///   needs_mark = "ep"           ...only when the landing cell carries the opponent's mark
///
/// Reads the kind-specific keys into `atom`, failing with a message in `error`. Magnitudes
/// of an extrapolated move go to `atom.mags`; the rest go to `kind`, `pins`, `flip`, `sign`.
/// `orientAxis` is the variant's orientation axis or -1 (it decides the default of `flip`).
bool readAtomKind(const toml::table& move, const std::vector<std::string>& axisNames,
                  int orientAxis, const std::vector<std::string>& markTags, MoveAtom& atom,
                  std::string& error);

/// Write the kind-specific keys of `atom` into `move` (the `vector` key included).
void writeAtomKind(const MoveAtom& atom, const std::vector<std::string>& axisNames,
                   const std::vector<std::string>& markTags, toml::table& move);

}  // namespace cb
