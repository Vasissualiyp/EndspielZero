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

TEST_CASE("the shipped pieces are built from the palette's bundles", "[unit][app]") {
  // M7.1.5: an authored standard. Start every piece empty of moves (one placeholder
  // step), drop in the bundle that names it, and the atoms must be the shipped ones.
  const VariantSpec shipped = test::loadVariant("standard");
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  const std::size_t axes = editor->axes().size();
  const int orient = editor->orientationAxis();
  REQUIRE(orient >= 0);

  for (const char* piece : {"pawn", "knight", "bishop", "rook", "queen", "king"}) {
    INFO(piece);
    REQUIRE(editor->setPieceAtoms(piece, {}).has_value());
    const std::string bundle = piece;
    const auto added = editor->addAtoms(piece, app::Editor::bundleAtoms(bundle, axes, orient));
    REQUIRE(added.has_value());
    CHECK(*added > 0);
    const auto got = editor->pieceAtoms(piece);
    REQUIRE(got.has_value());
    const PieceTypeDef& want = shipped.pieces[shipped.findPiece(piece)];
    REQUIRE(got->size() == want.atoms.size());
    for (std::size_t i = 0; i < got->size(); ++i) {
      CHECK((*got)[i].toString() == want.atoms[i].toString());
    }
    // Dropping the same bundle again is a no-op.
    const auto again = editor->addAtoms(piece, app::Editor::bundleAtoms(bundle, axes, orient));
    REQUIRE(again.has_value());
    CHECK(*again == 0);
  }
  CHECK(editor->validate().has_value());
}

TEST_CASE("rook plus bishop is the queen on a plane and not above it", "[unit][app]") {
  // The teaching claim of the palette, pinned: the union of rook and bishop is a queen
  // in two dimensions, and in three it lacks the cube diagonals the queen has.
  const auto direction = [](const std::vector<MoveAtom>& atoms, std::uint8_t dims) {
    std::size_t n = 0;
    for (const MoveAtom& a : atoms) n += expandAtom(a.mags, dims).size();
    return n;
  };
  std::vector<MoveAtom> rookPlusBishop = app::Editor::bundleAtoms("rook", 2, -1);
  for (const MoveAtom& a : app::Editor::bundleAtoms("bishop", 2, -1)) rookPlusBishop.push_back(a);
  CHECK(direction(rookPlusBishop, 2) == direction(app::Editor::bundleAtoms("queen", 2, -1), 2));
  CHECK(direction(rookPlusBishop, 3) + 8 == direction(app::Editor::bundleAtoms("queen", 3, -1), 3));
}

TEST_CASE("fields and rules are edited as the loader reads them", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("charged")), "charged");
  REQUIRE(editor.has_value());

  const auto fields = editor->fields(true);
  REQUIRE(fields.size() == 1);
  CHECK(fields[0].name == "charge");
  CHECK(fields[0].maxValue == 8);
  CHECK(editor->fields(false).empty());

  const auto rules = editor->rules();
  REQUIRE(rules.size() == 2);
  CHECK(rules[0].when == "on_move_end");
  REQUIRE(rules[0].effects.size() == 1);
  CHECK(rules[0].effects[0].op == "set_piece_field");
  CHECK(rules[1].condition.find("charge") != std::string::npos);

  // Writing the list back unchanged is a no-op to the loader: same game.
  const auto before = loadVariantToml(editor->serialize(), "charged");
  REQUIRE(before.has_value());
  REQUIRE(editor->setRules(rules).has_value());
  REQUIRE(editor->setFields(true, fields).has_value());
  const auto after = loadVariantToml(editor->serialize(), "charged");
  REQUIRE(after.has_value());
  CHECK(after->variantId() == before->variantId());

  // Order is meaning: swapping the two rules changes the game.
  auto swapped = rules;
  std::swap(swapped[0], swapped[1]);
  REQUIRE(editor->setRules(swapped).has_value());
  const auto reordered = loadVariantToml(editor->serialize(), "charged");
  REQUIRE(reordered.has_value());
  CHECK(reordered->variantId() != before->variantId());
  editor->undo();

  // A rule that names a field that does not exist is the loader's own complaint.
  auto broken = rules;
  broken[0].effects[0].args = {{"at", "move.to"}, {"field", "nonesuch"}, {"value", "(add 1 1)"}};
  REQUIRE(editor->setRules(broken).has_value());
  const auto bad = editor->validate();
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error().message.find("nonesuch") != std::string::npos);
  editor->undo();

  const auto described = editor->describeRules();
  REQUIRE(described.has_value());
  CHECK(described->find("moving charges a piece") != std::string::npos);
}
