// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every test reads the moveset library from the source tree, whatever directory it runs in.
#include <filesystem>

#include "io/moveset_library.hpp"

namespace {

const bool kMovesetsPointedAtTheSourceTree = [] {
  cb::setMovesetDirs({std::filesystem::path{CB_SOURCE_DIR} / "movesets"});
  return true;
}();

}  // namespace
