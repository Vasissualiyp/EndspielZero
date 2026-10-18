// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/result.hpp"
#include "io/variant_doc.hpp"
#include "space/dim_spec.hpp"

namespace cb::app {

/// The authoring document with the editing around it that a screen needs: dirty
/// tracking, undo/redo, and save. It owns no window and no Vulkan, so it is driven -
/// and tested - exactly like `app::Session`: edits in, state out.
///
/// Undo is snapshot-based: every accepted edit records the document's serialized text.
/// A variant file is small, correctness is worth more than the bytes, and it means undo
/// can never half-restore a structure the way a hand-written inverse would.
class Editor {
 public:
  static Result<Editor> open(std::string_view text, std::string_view sourceName);
  static Result<Editor> openFile(const std::filesystem::path& path);

  [[nodiscard]] const std::string& sourceName() const noexcept { return sourceName_; }
  [[nodiscard]] Result<void> saveFile(const std::filesystem::path& path);

  [[nodiscard]] bool dirty() const noexcept { return cursor_ != savedCursor_; }
  /// Bumps on every accepted edit, undo and redo, so a screen can tell its copy of a
  /// piece's body or its cached validation is stale without comparing documents.
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

  [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
  [[nodiscard]] bool canRedo() const noexcept { return cursor_ + 1 < history_.size(); }
  void undo();
  void redo();

  [[nodiscard]] std::string name() const;
  void setName(std::string name);
  [[nodiscard]] std::string description() const;
  void setDescription(std::string text);

  /// Index of the variant's orientation axis, or -1. Read by the move preview.
  [[nodiscard]] int orientationAxis() const;

  /// The board designer's axes: name, extent, kind, pitch and whether the axis is glued
  /// to its opposite face. Every edit goes through the undo stack.
  using AxisInfo = VariantDoc::AxisInfo;
  [[nodiscard]] std::vector<AxisInfo> axes() const;
  Result<void> setAxisExtent(std::size_t index, int extent);
  Result<void> setAxisKind(std::size_t index, AxisKind kind);
  Result<void> setAxisPeriodic(std::size_t index, bool periodic);
  Result<void> addAxis();
  Result<void> removeAxis(std::size_t index);

  [[nodiscard]] std::vector<std::string> pieceNames() const;
  /// The named bundles of moves the move palette offers - rook, bishop, knight, queen,
  /// king, unicorn, step, forward step, forward capture and pawn - as the atoms they
  /// stand for on a board of `boardAxes` axes whose forward axis is `orientAxis` (-1
  /// for none, which leaves the forward bundles empty). One definition, so the palette
  /// and the tests that build the shipped pieces from it cannot disagree.
  [[nodiscard]] static std::vector<MoveAtom> bundleAtoms(std::string_view bundle,
                                                         std::size_t boardAxes,
                                                         int orientAxis);
  /// Union atoms into a piece: one already there (by canonical form) is not added again.
  /// Returns how many were new.
  Result<std::size_t> addAtoms(std::string_view piece, const std::vector<MoveAtom>& atoms);
  [[nodiscard]] Result<std::vector<MoveAtom>> pieceAtoms(std::string_view piece) const;
  /// The moveset text (M19) - the symbolic spelling of the piece's moves, e.g.
  /// `U r in 1..D : [1 x r]^k` - and the setter that checks it against this board.
  [[nodiscard]] Result<std::string> pieceMovesText(std::string_view piece) const;
  Result<void> setPieceMovesText(std::string_view piece, std::string_view text);
  [[nodiscard]] Result<void> setPieceAtoms(std::string_view piece,
                                           const std::vector<MoveAtom>& atoms);

  /// A piece's identity, the piece list, and authored bodies. Every change is one entry
  /// on the undo stack.
  using PieceInfo = VariantDoc::PieceInfo;
  [[nodiscard]] Result<PieceInfo> pieceInfo(std::string_view piece) const;
  Result<void> setPieceInfo(std::string_view piece, const PieceInfo& info);
  Result<void> renamePiece(std::string_view piece, std::string_view to);
  Result<void> addPiece(std::string_view name, std::string_view symbol,
                        std::string_view base);
  Result<void> removePiece(std::string_view piece);
  [[nodiscard]] Result<std::optional<assets::PieceModel>> pieceBody(
      std::string_view piece) const;
  Result<void> setPieceBody(std::string_view piece,
                            const std::optional<assets::PieceModel>& body);
  [[nodiscard]] Result<std::optional<assets::IconModel>> pieceIcon(
      std::string_view piece) const;
  Result<void> setPieceIcon(std::string_view piece,
                            const std::optional<assets::IconModel>& icon);

  /// Custom fields and the ordered rule list (M7.2). Whole-list setters: order is meaning,
  /// so the screen edits a list and writes it back, one undo entry per gesture.
  using FieldInfo = VariantDoc::FieldInfo;
  using RuleInfo = VariantDoc::RuleInfo;
  using EffectInfo = VariantDoc::EffectInfo;
  [[nodiscard]] std::vector<FieldInfo> fields(bool pieceFields) const;
  Result<void> setFields(bool pieceFields, const std::vector<FieldInfo>& list);
  [[nodiscard]] std::vector<RuleInfo> rules() const;
  Result<void> setRules(const std::vector<RuleInfo>& list);
  /// What the engine reads the rules as - the loader's own rule set described one per
  /// line - or the loader's message when the document does not load.
  [[nodiscard]] Result<std::string> describeRules() const;

  /// Does the document, as it stands, load? The editor is a front end to the loader and
  /// grows no second set of rules: this runs the real one on the serialized text and
  /// returns its message, which is what the screen shows against the control that broke
  /// it. SAVE is held back while this fails.
  [[nodiscard]] Result<void> validate() const;

  [[nodiscard]] std::string serialize() const;

 private:
  Editor(VariantDoc doc, std::string sourceName);

  /// Record the state before a mutation and, if it succeeded, the state after it, so a
  /// failed edit leaves the history untouched.
  template <class Fn>
  Result<void> edit(Fn&& fn);

  [[nodiscard]] Result<void> resetTo(const std::string& snapshot);

  VariantDoc doc_;
  std::string sourceName_;
  std::vector<std::string> history_;
  std::size_t cursor_{0};
  std::size_t savedCursor_{0};
  std::uint64_t revision_{0};
};

}  // namespace cb::app
