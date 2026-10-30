// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <toml++/toml.hpp>

#include "pieces/moveset.hpp"

namespace cb {

/// The shared library of moveset files: `<name>.moveset`, plain text, one expression each
/// (the language's own comments say what it is). Names resolve through the directories set
/// here, in order; with none set, the places a development tree and an install keep
/// `movesets/`. A name is only ever a bare identifier, so it cannot reach outside them.
void setMovesetDirs(std::vector<std::filesystem::path> dirs);
[[nodiscard]] std::vector<std::filesystem::path> movesetDirs();

/// The text of a library moveset, or nothing.
[[nodiscard]] std::optional<std::string> libraryMoveset(std::string_view name);

/// The context a variant's moveset text is read against: its axes, its orientation axis,
/// and a lookup that tries the variant's own `[movesets]` table before the library.
[[nodiscard]] MovesetContext movesetContextFor(const toml::table& root);

}  // namespace cb
