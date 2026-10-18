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
///
/// Reads the kind-specific keys into `atom`, failing with a message in `error`. Magnitudes
/// of an extrapolated move go to `atom.mags`; the rest go to `kind`, `pins`, `flip`, `sign`.
/// `orientAxis` is the variant's orientation axis or -1 (it decides the default of `flip`).
bool readAtomKind(const toml::table& move, const std::vector<std::string>& axisNames,
                  int orientAxis, MoveAtom& atom, std::string& error);

/// Write the kind-specific keys of `atom` into `move` (the `vector` key included).
void writeAtomKind(const MoveAtom& atom, const std::vector<std::string>& axisNames,
                   toml::table& move);

}  // namespace cb
