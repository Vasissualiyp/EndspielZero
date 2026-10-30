// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/piece_model.hpp"
#include "base/result.hpp"
#include "pieces/atom.hpp"
#include "space/dim_spec.hpp"

namespace cb {

/// A variant's source file, held as an editable document.
///
/// `VariantSpec` is the resolved, frozen thing the engine runs: it has thrown away the
/// author's spelling, the atom order and the rule text. The editor needs those back, so
/// it edits this instead - a copy of the source that `serialize()` writes to a file the
/// loader accepts, and which must round-trip to the same `VariantId` (M7.0.2).
///
/// Typed accessors for the editor controls arrive with the editor screens (M7.1+); this
/// owns the storage and the round trip the serializer is judged by. The TOML is kept
/// behind a pointer so this header does not leak the parser into every layer.
class VariantDoc {
 public:
  VariantDoc(VariantDoc&&) noexcept;
  VariantDoc& operator=(VariantDoc&&) noexcept;
  ~VariantDoc();

  VariantDoc(const VariantDoc&) = delete;
  VariantDoc& operator=(const VariantDoc&) = delete;

  /// Parse a `variant.toml`, failing exactly where the loader would: one TOML dialect,
  /// one error format.
  [[nodiscard]] static Result<VariantDoc> parse(std::string_view text,
                                                std::string_view sourceName);

  /// The document as TOML text, ready to be loaded back.
  [[nodiscard]] std::string serialize() const;

  /// The piece names, in declaration order. This is the `PieceTypeId` order, so the
  /// editor shows it and never silently reorders it.
  [[nodiscard]] std::vector<std::string> pieceNames() const;

  /// A piece's atoms as they are authored. Fails when no piece has that name.
  [[nodiscard]] Result<std::vector<MoveAtom>> pieceAtoms(std::string_view piece) const;

  /// Replace a piece's atoms, writing the `[[piece.move]]` list back. Fails when no
  /// piece has that name.
  [[nodiscard]] Result<void> setPieceAtoms(std::string_view piece,
                                           const std::vector<MoveAtom>& atoms);

  /// A piece's authored body and icon (`[piece.model]`, `[piece.icon]`), or nothing when
  /// it has none and the renderer assembles one from its archetype. Setting `nullopt`
  /// removes the table. Fails when no piece has that name; the setters also refuse a
  /// model `assets::validate` refuses, with its message.
  [[nodiscard]] Result<std::optional<assets::PieceModel>> pieceModel(
      std::string_view piece) const;
  [[nodiscard]] Result<std::optional<assets::IconModel>> pieceIcon(
      std::string_view piece) const;
  [[nodiscard]] Result<void> setPieceModel(std::string_view piece,
                                           const std::optional<assets::PieceModel>& m);
  [[nodiscard]] Result<void> setPieceIcon(std::string_view piece,
                                          const std::optional<assets::IconModel>& m);

  /// A custom data field as the editor shows it (M7.2.1): a `[[piece_field]]` or a
  /// `[[cell_field]]`. Validation is the loader's, through the editor's `validate()`.
  struct FieldInfo {
    std::string name;
    int defaultValue{0};
    int minValue{-32768};
    int maxValue{32767};
    bool hashed{true};
    friend bool operator==(const FieldInfo&, const FieldInfo&) = default;
  };
  [[nodiscard]] std::vector<FieldInfo> fields(bool cellFields) const;
  void setFields(bool cellFields, const std::vector<FieldInfo>& fields);

  /// A rule as the editor's form holds it (M7.2.2): its trigger word, its condition as
  /// the S-expression text the file stores, and its ordered effects, each an op word and
  /// the op's keys as text (rule_catalogue.hpp says which keys an op takes). Kept as
  /// text, so a form can hold a half-typed expression and the loader - the one parser -
  /// is what says whether it means anything.
  struct EffectInfo {
    std::string op;
    std::vector<std::pair<std::string, std::string>> args;
    friend bool operator==(const EffectInfo&, const EffectInfo&) = default;
  };
  struct RuleInfo {
    std::string name;
    std::string when{"on_move_end"};
    std::string condition;
    std::vector<EffectInfo> effects;
    friend bool operator==(const RuleInfo&, const RuleInfo&) = default;
  };
  [[nodiscard]] std::vector<RuleInfo> rules() const;
  void setRules(const std::vector<RuleInfo>& rules);

  /// The author's one-line pitch. Cosmetic: not part of `VariantId`.
  [[nodiscard]] std::string description() const;
  void setDescription(std::string text);

  /// The variant's name. Part of `VariantId`, so renaming is a real edit.
  [[nodiscard]] std::string name() const;
  void setName(std::string name);

  /// Index of the axis "forward" is measured along, or -1 when none is declared. The
  /// piece designer needs it: an oriented atom's forward half depends on it.
  [[nodiscard]] int orientationAxis() const;

  /// A board axis as the board designer edits it. `periodic` is the common glued face -
  /// a `[[geometry.identify]]` gluing this axis to its opposite - so a cylinder or torus
  /// is one toggle. Mirror/Klein (with flips and swaps) is authored in the file.
  struct AxisInfo {
    std::string name;
    int extent{0};
    AxisKind kind{AxisKind::Spatial};
    int pitch{1};
    bool periodic{false};
  };
  [[nodiscard]] std::vector<AxisInfo> axes() const;
  Result<void> setAxisExtent(std::size_t index, int extent);
  Result<void> setAxisKind(std::size_t index, AxisKind kind);
  Result<void> setAxisPeriodic(std::size_t index, bool periodic);
  /// Append a fresh spatial axis, or drop the last one. A board keeps at least one.
  Result<void> addAxis();
  Result<void> removeAxis(std::size_t index);

 private:
  VariantDoc();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace cb
