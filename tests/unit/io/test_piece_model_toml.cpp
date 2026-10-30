// SPDX-License-Identifier: GPL-3.0-or-later
//
// M7.3.4: a piece's authored body and icon live in the variant file, round-trip through
// the editor's document, and stay out of VariantId.
#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include "app/editor.hpp"
#include "io/variant_doc.hpp"
#include "io/variant_toml.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

assets::IconModel triangle() {
  assets::IconModel icon;
  icon.fills.push_back(assets::Outline{{{500, 100}, {850, 900}, {150, 900}}});
  icon.cuts.push_back(assets::Outline{{{450, 600}, {550, 600}, {500, 500}}});
  return icon;
}

}  // namespace

TEST_CASE("a model written by the document loads back into the spec", "[unit][io]") {
  const std::string text = readFile(test::variantPath("standard"));
  auto doc = VariantDoc::parse(text, "standard");
  REQUIRE(doc.has_value());
  const assets::PieceModel crown = assets::archetypeModel("crown");  // k-fold + element
  REQUIRE(doc->setPieceModel("queen", crown).has_value());
  REQUIRE(doc->setPieceIcon("queen", triangle()).has_value());

  auto back = doc->pieceModel("queen");
  REQUIRE(back.has_value());
  REQUIRE(back->has_value());
  CHECK(**back == crown);

  const auto loaded = loadVariantToml(doc->serialize(), "standard");
  REQUIRE(loaded.has_value());
  const PieceTypeDef& queen = loaded->pieces[loaded->findPiece("queen")];
  REQUIRE(queen.model.has_value());
  CHECK(*queen.model == crown);
  REQUIRE(queen.icon.has_value());
  CHECK(*queen.icon == triangle());
  CHECK_FALSE(loaded->pieces[loaded->findPiece("rook")].model.has_value());

  // Cosmetic: how a queen looks is not what she is.
  const auto original = loadVariantToml(text, "standard");
  REQUIRE(original.has_value());
  CHECK(loaded->variantId() == original->variantId());

  // And clearing it removes the table.
  REQUIRE(doc->setPieceModel("queen", std::nullopt).has_value());
  CHECK(doc->serialize().find("[piece.model]") == std::string::npos);
}

TEST_CASE("a broken model in a variant file is refused with its line", "[unit][io]") {
  std::string text = readFile(test::variantPath("standard"));
  const std::string anchor = "name = \"rook\"\n";
  const auto at = text.find(anchor);
  REQUIRE(at != std::string::npos);
  // A bow-tie: the profile crosses itself, so it has no inside to sweep.
  text.insert(at + anchor.size(),
              "model = { profile = [[0, 0], [300, 300], [300, 0], [0, 300]] }\n");
  const auto loaded = loadVariantToml(text, "bad");
  REQUIRE_FALSE(loaded.has_value());
  CHECK(loaded.error().message.find("rook") != std::string::npos);
  CHECK(loaded.error().message.find("crosses itself") != std::string::npos);
}

TEST_CASE("the editor commits a model as one undo step, and refuses a no-op",
          "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  const assets::PieceModel tower = assets::archetypeModel("tower");
  REQUIRE(editor->setPieceModel("rook", tower).has_value());
  CHECK(editor->dirty());
  REQUIRE(editor->canUndo());
  // Setting the same model again records nothing.
  REQUIRE(editor->setPieceModel("rook", tower).has_value());
  editor->undo();
  CHECK_FALSE(editor->canUndo());
  auto m = editor->pieceModel("rook");
  REQUIRE(m.has_value());
  CHECK_FALSE(m->has_value());
  editor->redo();
  m = editor->pieceModel("rook");
  REQUIRE(m.has_value());
  REQUIRE(m->has_value());
  CHECK(**m == tower);
}

TEST_CASE("save is refused while the document would not load", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  CHECK(editor->validate().has_value());
  // A piece with no moves is something finalize() refuses.
  REQUIRE(editor->setPieceAtoms("rook", {}).has_value());
  const Result<void> v = editor->validate();
  REQUIRE_FALSE(v.has_value());
  CHECK(v.error().message.find("rook") != std::string::npos);

  const auto path = std::filesystem::temp_directory_path() / "cb_refused_save.toml";
  std::filesystem::remove(path);
  CHECK_FALSE(editor->saveFile(path).has_value());
  CHECK_FALSE(std::filesystem::exists(path));
  CHECK(editor->dirty());

  editor->undo();
  CHECK(editor->validate().has_value());
}
