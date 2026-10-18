// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <toml++/toml.hpp>

#include "assets/piece_model.hpp"

namespace cb {

/// How a piece's authored body and icon are spelled in a variant file: a `[piece.body]`
/// and a `[piece.icon]` table of integer permille. The editor's document writes them and
/// the loader reads them, so the two cannot drift. Internal to `io`; nothing above it
/// sees TOML.
toml::table bodyToToml(const assets::PieceModel& body);
std::optional<assets::PieceModel> bodyFromToml(const toml::table& table);
toml::table iconToToml(const assets::IconModel& icon);
std::optional<assets::IconModel> iconFromToml(const toml::table& table);

}  // namespace cb
