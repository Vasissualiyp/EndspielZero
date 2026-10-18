// SPDX-License-Identifier: GPL-3.0-or-later
//
// M7.0: the editor as a headless document - open, edit, undo/redo, save - driven and
// tested exactly like app::Session, with no window in sight.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "app/editor.hpp"
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

MoveAtom knightLeap() {
  MoveAtom a;
  a.mags.push(1);
  a.mags.push(2);
  a.maxK = 1;
  a.mode = MoveMode::Leap;
  auto canon = MoveAtom::canonicalize(a);
  return canon.has_value() ? *canon : a;
}

}  // namespace

TEST_CASE("the editor tracks dirt and undoes an edit", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  CHECK_FALSE(editor->dirty());
  CHECK_FALSE(editor->canUndo());
  CHECK_FALSE(editor->canRedo());

  auto rook = editor->pieceAtoms("rook");
  REQUIRE(rook.has_value());
  auto atoms = *rook;
  atoms.push_back(knightLeap());
  REQUIRE(editor->setPieceAtoms("rook", atoms).has_value());

  CHECK(editor->dirty());
  CHECK(editor->canUndo());
  const auto edited = loadVariantToml(editor->serialize(), "standard");
  const auto original =
      loadVariantToml(readFile(test::variantPath("standard")), "standard");
  REQUIRE(edited.has_value());
  REQUIRE(original.has_value());
  CHECK(edited->variantId() != original->variantId());

  // Undo returns exactly to the shipped game, not merely to something that loads.
  editor->undo();
  CHECK_FALSE(editor->dirty());
  CHECK(editor->canRedo());
  const auto back = loadVariantToml(editor->serialize(), "standard");
  REQUIRE(back.has_value());
  CHECK(back->variantId() == original->variantId());

  // Redo puts the edit back.
  editor->redo();
  const auto again = loadVariantToml(editor->serialize(), "standard");
  REQUIRE(again.has_value());
  CHECK(again->variantId() == edited->variantId());
}

TEST_CASE("a new edit after an undo drops the redo branch", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  editor->setDescription("first");
  editor->undo();
  CHECK(editor->canRedo());
  editor->setDescription("second");
  CHECK_FALSE(editor->canRedo());
  CHECK(editor->description() == "second");
}

TEST_CASE("the editor saves to a file and reopens it", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  editor->setDescription("saved from the editor");
  CHECK(editor->dirty());

  const auto path =
      std::filesystem::temp_directory_path() / "endspielzero-editor-roundtrip.toml";
  REQUIRE(editor->saveFile(path).has_value());
  CHECK_FALSE(editor->dirty());

  auto reopened = app::Editor::openFile(path);
  REQUIRE(reopened.has_value());
  CHECK(reopened->name() == "standard");
  CHECK(reopened->description() == "saved from the editor");
  CHECK_FALSE(reopened->dirty());
  CHECK_FALSE(reopened->canUndo());

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_CASE("a piece's identity is edited and survives the loader", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  auto info = editor->pieceInfo("queen");
  REQUIRE(info.has_value());
  CHECK(info->symbol == "Q");
  CHECK_FALSE(info->royal);

  info->heightPermille = 1200;
  info->shape = "crown";
  REQUIRE(editor->setPieceInfo("queen", *info).has_value());
  const auto loaded = loadVariantToml(editor->serialize(), "standard");
  REQUIRE(loaded.has_value());
  const PieceTypeId q = loaded->findPiece("queen");
  CHECK(loaded->pieces[q].heightPermille == 1200);
  CHECK(loaded->pieces[q].shape == "crown");

  // A cosmetic edit must not change what game this is.
  const auto before = loadVariantToml(readFile(test::variantPath("standard")), "standard");
  REQUIRE(before.has_value());
  CHECK(before->variantId() == loaded->variantId());
}

TEST_CASE("pieces are added from a base, renamed, and removed", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  const std::size_t count = editor->pieceNames().size();

  REQUIRE(editor->addPiece("chancellor", "C", "rook").has_value());
  CHECK(editor->pieceNames().size() == count + 1);
  CHECK(editor->pieceNames().back() == "chancellor");
  const auto rookAtoms = editor->pieceAtoms("rook");
  const auto copyAtoms = editor->pieceAtoms("chancellor");
  REQUIRE(rookAtoms.has_value());
  REQUIRE(copyAtoms.has_value());
  CHECK(*rookAtoms == *copyAtoms);
  CHECK(editor->validate().has_value());

  // Two pieces never share a name, and a symbol is one character.
  CHECK_FALSE(editor->addPiece("rook", "X", "").has_value());
  CHECK_FALSE(editor->addPiece("thing", "XY", "").has_value());

  // Renaming follows the promotions that name the piece.
  REQUIRE(editor->renamePiece("queen", "empress").has_value());
  const auto pawn = editor->pieceInfo("pawn");
  REQUIRE(pawn.has_value());
  CHECK(std::find(pawn->promotesTo.begin(), pawn->promotesTo.end(), "empress") !=
        pawn->promotesTo.end());
  CHECK(editor->validate().has_value());

  // Removing a piece the starting position uses is the loader's complaint, not ours.
  REQUIRE(editor->removePiece("knight").has_value());
  CHECK_FALSE(editor->validate().has_value());
  editor->undo();
  CHECK(editor->validate().has_value());

  REQUIRE(editor->removePiece("chancellor").has_value());
  CHECK(editor->pieceNames().size() == count);
}

TEST_CASE("an authored body and icon are stored in the file and change no game",
          "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  const auto before = loadVariantToml(editor->serialize(), "standard");
  REQUIRE(before.has_value());

  CHECK_FALSE(editor->pieceBody("knight")->has_value());
  assets::PieceModel body = assets::archetypeModel("wedge");
  body.elements[0].lateral = 90;
  body.elements[0].tangent = true;
  REQUIRE(editor->setPieceBody("knight", body).has_value());
  assets::IconModel icon = assets::iconFromProfile(body);
  REQUIRE(editor->setPieceIcon("knight", icon).has_value());

  const auto back = editor->pieceBody("knight");
  REQUIRE(back.has_value());
  REQUIRE(back->has_value());
  CHECK(**back == body);
  const auto iconBack = editor->pieceIcon("knight");
  REQUIRE(iconBack.has_value());
  REQUIRE(iconBack->has_value());
  CHECK(**iconBack == icon);

  // Through a save and a reload, and the loader still accepts it and says it is the same
  // game: geometry is cosmetic and outside the variant's identity.
  auto again = app::Editor::open(editor->serialize(), "standard");
  REQUIRE(again.has_value());
  CHECK(**again.value().pieceBody("knight") == body);
  const auto after = loadVariantToml(editor->serialize(), "standard");
  REQUIRE(after.has_value());
  CHECK(after->variantId() == before->variantId());
  // And the loaded game carries the body, ready for the renderer.
  const PieceTypeId knight = after->findPiece("knight");
  REQUIRE(after->pieces[knight].body.has_value());
  CHECK(*after->pieces[knight].body == body);
  REQUIRE(after->pieces[knight].icon.has_value());
  CHECK(*after->pieces[knight].icon == icon);
  CHECK_FALSE(after->pieces[after->findPiece("rook")].body.has_value());

  REQUIRE(editor->setPieceBody("knight", std::nullopt).has_value());
  CHECK_FALSE(editor->pieceBody("knight")->has_value());
}

TEST_CASE("a body that cannot be built is dropped by the loader, not fatal",
          "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  // A profile that crosses itself has no inside; the loader must not refuse the variant
  // for a cosmetic.
  assets::PieceModel bad = assets::archetypeModel("dome");
  bad.profile.points = {{0, 0}, {400, 0}, {0, 400}, {400, 400}};
  REQUIRE(editor->setPieceBody("pawn", bad).has_value());
  const auto loaded = loadVariantToml(editor->serialize(), "standard");
  REQUIRE(loaded.has_value());
  CHECK_FALSE(loaded->pieces[loaded->findPiece("pawn")].body.has_value());
}
