// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "position/position.hpp"

namespace cb::rules {

/// Everything the VM can talk about. No floating point, ever: rule evaluation has to be
/// bit-identical across machines for multiplayer and replay to mean anything
/// (ADR-0005).
enum class ExprOp : std::uint8_t {
  ConstInt,         ///< imm
  MoveFrom,         ///< the cell the mover left
  MoveTo,           ///< the cell it arrived on
  MoveCaptureCell,  ///< the cell a piece was taken from, or kInvalidCell
  MoverColor,
  MoverType,
  IsCapture,
  CellArg,     ///< the cell currently bound by an iterating effect
  TypeAt,      ///< args[0] = cell
  ColorAt,     ///< args[0] = cell
  IsEmpty,     ///< args[0] = cell
  PieceField,  ///< imm = field index, args[0] = cell
  CellField,   ///< imm = field index, args[0] = cell
  SideToMove,
  HalfmoveClock,
  CoordOf,         ///< imm = axis, args[0] = cell
  HasCaptureFrom,  ///< args[0] = cell; answered by the orchestrating layer
  Attacked,        ///< args[0] = cell; is it attacked by the side not the mover (answered by the orchestrating layer)
  Marked,          ///< imm = mark tag, args[0] = cell; does the cell carry that mark
  AnyCapture,      ///< does the side to move have any capture at all
  Not,
  And,
  Or,
  Eq,
  Ne,
  Lt,
  Gt,
  Add,
  Sub,
};

/// A condition or a value, as a small tree.
///
/// A tree rather than a stack machine because it is what the TOML naturally describes,
/// because it can be type-checked at load, and because with no loops and no jumps its
/// termination is structural rather than something a budget has to enforce.
struct Expr {
  ExprOp op{ExprOp::ConstInt};
  std::int64_t imm{0};
  std::vector<Expr> args;

  static Expr constant(std::int64_t v) { return Expr{ExprOp::ConstInt, v, {}}; }
  static Expr nullary(ExprOp o) { return Expr{o, 0, {}}; }
  static Expr unary(ExprOp o, Expr a, std::int64_t imm = 0) {
    Expr e{o, imm, {}};
    e.args.push_back(std::move(a));
    return e;
  }
  static Expr binary(ExprOp o, Expr a, Expr b) {
    Expr e{o, 0, {}};
    e.args.push_back(std::move(a));
    e.args.push_back(std::move(b));
    return e;
  }

  [[nodiscard]] std::string toString() const;
  /// Number of nodes, which is what the load-time complexity budget counts.
  [[nodiscard]] std::size_t nodeCount() const;
};

std::string_view toString(ExprOp op) noexcept;
/// How many arguments an opcode takes; -1 means "any number".
int arity(ExprOp op) noexcept;

}  // namespace cb::rules
