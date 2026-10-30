// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
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
  [[nodiscard]] Result<std::vector<MoveAtom>> pieceAtoms(std::string_view piece) const;
  [[nodiscard]] Result<void> setPieceAtoms(std::string_view piece,
                                           const std::vector<MoveAtom>& atoms);

  /// A piece's authored body and icon. Setting one that equals what the document already
  /// holds is a no-op rather than an undo step, so a screen can commit every frame.
  [[nodiscard]] Result<std::optional<assets::PieceModel>> pieceModel(
      std::string_view piece) const;
  [[nodiscard]] Result<std::optional<assets::IconModel>> pieceIcon(
      std::string_view piece) const;
  Result<void> setPieceModel(std::string_view piece,
                             const std::optional<assets::PieceModel>& m);
  Result<void> setPieceIcon(std::string_view piece,
                            const std::optional<assets::IconModel>& m);

  /// Custom fields and rules (M7.2), whole lists at a time: a form edits a working copy
  /// and commits it, and an unchanged list is a no-op rather than an undo step.
  using FieldInfo = VariantDoc::FieldInfo;
  using RuleInfo = VariantDoc::RuleInfo;
  using EffectInfo = VariantDoc::EffectInfo;
  [[nodiscard]] std::vector<FieldInfo> fields(bool cellFields) const;
  Result<void> setFields(bool cellFields, const std::vector<FieldInfo>& fields);
  [[nodiscard]] std::vector<RuleInfo> rules() const;
  Result<void> setRules(const std::vector<RuleInfo>& rules);

  /// Would the document load? The editor is a front end to the loader and `finalize()`,
  /// never a second set of rules (M7.0.3): this loads the serialized text exactly as the
  /// game would and returns the first error, with its line. Cached per document state,
  /// so a screen may ask every frame.
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
  /// The last document text `validate()` checked and what it said.
  mutable std::string validatedText_;
  mutable Result<void> validated_{};
};

}  // namespace cb::app
