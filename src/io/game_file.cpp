// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/game_file.hpp"

#include <sstream>

#include "io/fen.hpp"
#include "io/notation.hpp"

namespace cb {
namespace {

std::string_view trim(std::string_view s) {
  const std::size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const std::size_t last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

}  // namespace

GameFile toGameFile(const VariantSpec& variant, const Game& game) {
  GameFile file;
  file.variant = variant.name;
  file.variantId = variant.variantId();
  file.startFen = toFen(game.startPosition());
  file.moves.reserve(game.moveHistory().size());
  for (const Move& m : game.moveHistory()) file.moves.push_back(moveText(variant, m));
  return file;
}

std::string gameFileText(const GameFile& file) {
  std::ostringstream out;
  out << "# EndspielZero game v1\n";
  out << "variant " << file.variant << '\n';
  out << "variant_id " << file.variantId << '\n';
  out << "start " << file.startFen << '\n';
  for (const std::string& m : file.moves) out << "move " << m << '\n';
  return out.str();
}

std::string toGameFileText(const VariantSpec& variant, const Game& game) {
  return gameFileText(toGameFile(variant, game));
}

Result<GameFile> parseGameFile(std::string_view text) {
  GameFile file;
  bool sawVariant = false;
  std::istringstream in{std::string(text)};
  std::string line;
  while (std::getline(in, line)) {
    const std::string_view view = trim(line);
    if (view.empty() || view.front() == '#') continue;
    const std::size_t split = view.find_first_of(" \t");
    const std::string_view key =
        split == std::string_view::npos ? view : view.substr(0, split);
    const std::string_view value = split == std::string_view::npos
                                       ? std::string_view{}
                                       : trim(view.substr(split + 1));
    if (key == "variant") {
      file.variant = std::string(value);
      sawVariant = !value.empty();
    } else if (key == "variant_id") {
      std::uint64_t id = 0;
      const std::string s(value);
      if (!s.empty()) {
        try {
          id = std::stoull(s);
        } catch (const std::exception&) {
          return fail(ErrorCode::ParseError, "variant_id is not a number: '" + s + "'");
        }
      }
      file.variantId = id;
    } else if (key == "start") {
      file.startFen = std::string(value);
    } else if (key == "move") {
      if (value.empty()) {
        return fail(ErrorCode::ParseError, "a move line has no move");
      }
      file.moves.emplace_back(value);
    } else {
      return fail(ErrorCode::ParseError,
                  "unknown game-file key '" + std::string(key) + "'");
    }
  }
  if (!sawVariant) return fail(ErrorCode::ParseError, "game file names no variant");
  if (file.startFen.empty()) {
    return fail(ErrorCode::ParseError, "game file has no start position");
  }
  return file;
}

Result<void> verifyGameFileVariant(const GameFile& file, const VariantSpec& variant) {
  if (file.variantId != 0 && file.variantId != variant.variantId()) {
    return fail(ErrorCode::ValidationError,
                "game was played on a different variant of '" + file.variant +
                    "' (its rules have changed since it was saved)");
  }
  return {};
}

std::string gameFileIllegalMove(std::size_t ply, std::string_view text) {
  return "move " + std::to_string(ply) + " ('" + std::string(text) +
         "') is not legal in this position";
}

Result<void> applyGameFile(const GameFile& file, Game& game) {
  if (auto ok = verifyGameFileVariant(file, game.variant()); !ok.has_value()) return ok;
  auto start = fromFen(game.variant(), file.startFen);
  if (!start.has_value()) return fail(start.error().code, start.error().message);
  game.setStartPosition(*start);
  for (std::size_t ply = 0; ply < file.moves.size(); ++ply) {
    const std::string& text = file.moves[ply];
    const Move* found = nullptr;
    for (const Move& m : game.legalMoves()) {
      if (moveText(game.variant(), m) == text) {
        found = &m;
        break;
      }
    }
    if (found == nullptr) {
      return fail(ErrorCode::ValidationError, gameFileIllegalMove(ply + 1, text));
    }
    // Copy before playing: `play` invalidates the legal-move cache `found` points into.
    const Move chosen = *found;
    if (auto ok = game.play(chosen); !ok.has_value()) return ok;
  }
  return {};
}

}  // namespace cb
