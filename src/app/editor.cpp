// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/editor.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "io/variant_toml.hpp"

namespace cb::app {

Editor::Editor(VariantDoc doc, std::string sourceName)
    : doc_(std::move(doc)), sourceName_(std::move(sourceName)) {
  history_.push_back(doc_.serialize());
}

Result<Editor> Editor::open(std::string_view text, std::string_view sourceName) {
  auto doc = VariantDoc::parse(text, sourceName);
  if (!doc.has_value())
    return fail(doc.error().code, doc.error().message, doc.error().line);
  return Editor(std::move(*doc), std::string(sourceName));
}

Result<Editor> Editor::openFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return fail(ErrorCode::ParseError,
                "cannot open variant file '" + path.string() + "'");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return open(ss.str(), path.string());
}

Result<void> Editor::saveFile(const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return fail(ErrorCode::Internal, "cannot write '" + path.string() + "'");
  }
  const std::string text = doc_.serialize();
  out << text;
  if (!out) {
    return fail(ErrorCode::Internal, "write to '" + path.string() + "' failed");
  }
  savedCursor_ = cursor_;
  return {};
}

Result<void> Editor::resetTo(const std::string& snapshot) {
  auto doc = VariantDoc::parse(snapshot, sourceName_);
  if (!doc.has_value()) {
    return fail(ErrorCode::Internal, "the edit history holds an unparseable document");
  }
  doc_ = std::move(*doc);
  return {};
}

void Editor::undo() {
  if (!canUndo()) return;
  --cursor_;
  ++revision_;
  (void)resetTo(history_[cursor_]);
}

void Editor::redo() {
  if (!canRedo()) return;
  ++cursor_;
  ++revision_;
  (void)resetTo(history_[cursor_]);
}

template <class Fn>
Result<void> Editor::edit(Fn&& fn) {
  if (auto ok = fn(doc_); !ok.has_value()) return ok;
  // Drop any redo branch, then record the new state.
  history_.resize(cursor_ + 1);
  history_.push_back(doc_.serialize());
  cursor_ = history_.size() - 1;
  ++revision_;
  return {};
}

std::string Editor::name() const {
  return doc_.name();
}

void Editor::setName(std::string name) {
  (void)edit([&](VariantDoc& d) -> Result<void> {
    d.setName(std::move(name));
    return {};
  });
}

std::string Editor::description() const {
  return doc_.description();
}

int Editor::orientationAxis() const {
  return doc_.orientationAxis();
}

std::vector<Editor::AxisInfo> Editor::axes() const {
  return doc_.axes();
}

Result<void> Editor::setAxisExtent(std::size_t index, int extent) {
  return edit(
      [&](VariantDoc& d) -> Result<void> { return d.setAxisExtent(index, extent); });
}

Result<void> Editor::setAxisKind(std::size_t index, AxisKind kind) {
  return edit([&](VariantDoc& d) -> Result<void> { return d.setAxisKind(index, kind); });
}

Result<void> Editor::setAxisPeriodic(std::size_t index, bool periodic) {
  return edit(
      [&](VariantDoc& d) -> Result<void> { return d.setAxisPeriodic(index, periodic); });
}

Result<void> Editor::addAxis() {
  return edit([&](VariantDoc& d) -> Result<void> { return d.addAxis(); });
}

Result<void> Editor::removeAxis(std::size_t index) {
  return edit([&](VariantDoc& d) -> Result<void> { return d.removeAxis(index); });
}

void Editor::setDescription(std::string text) {
  (void)edit([&](VariantDoc& d) -> Result<void> {
    d.setDescription(std::move(text));
    return {};
  });
}

std::vector<std::string> Editor::pieceNames() const {
  return doc_.pieceNames();
}

Result<std::vector<MoveAtom>> Editor::pieceAtoms(std::string_view piece) const {
  return doc_.pieceAtoms(piece);
}

Result<void> Editor::setPieceAtoms(std::string_view piece,
                                   const std::vector<MoveAtom>& atoms) {
  return edit(
      [&](VariantDoc& d) -> Result<void> { return d.setPieceAtoms(piece, atoms); });
}

Result<Editor::PieceInfo> Editor::pieceInfo(std::string_view piece) const {
  return doc_.pieceInfo(piece);
}

Result<void> Editor::setPieceInfo(std::string_view piece, const PieceInfo& info) {
  return edit([&](VariantDoc& d) -> Result<void> { return d.setPieceInfo(piece, info); });
}

Result<void> Editor::renamePiece(std::string_view piece, std::string_view to) {
  return edit([&](VariantDoc& d) -> Result<void> { return d.renamePiece(piece, to); });
}

Result<void> Editor::addPiece(std::string_view name, std::string_view symbol,
                              std::string_view base) {
  return edit(
      [&](VariantDoc& d) -> Result<void> { return d.addPiece(name, symbol, base); });
}

Result<void> Editor::removePiece(std::string_view piece) {
  return edit([&](VariantDoc& d) -> Result<void> { return d.removePiece(piece); });
}

Result<std::optional<assets::PieceModel>> Editor::pieceBody(std::string_view piece) const {
  return doc_.pieceBody(piece);
}

Result<void> Editor::setPieceBody(std::string_view piece,
                                  const std::optional<assets::PieceModel>& body) {
  return edit([&](VariantDoc& d) -> Result<void> { return d.setPieceBody(piece, body); });
}

Result<std::optional<assets::IconModel>> Editor::pieceIcon(std::string_view piece) const {
  return doc_.pieceIcon(piece);
}

Result<void> Editor::setPieceIcon(std::string_view piece,
                                  const std::optional<assets::IconModel>& icon) {
  return edit([&](VariantDoc& d) -> Result<void> { return d.setPieceIcon(piece, icon); });
}

Result<void> Editor::validate() const {
  const auto loaded = loadVariantToml(doc_.serialize(), sourceName_);
  if (loaded.has_value()) return {};
  return fail(loaded.error().code, loaded.error().message, loaded.error().line);
}

std::string Editor::serialize() const {
  return doc_.serialize();
}

}  // namespace cb::app
