// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/expr.hpp"

namespace cb::rules {

std::string_view toString(ExprOp op) noexcept {
  switch (op) {
    case ExprOp::ConstInt:
      return "const";
    case ExprOp::MoveFrom:
      return "move.from";
    case ExprOp::MoveTo:
      return "move.to";
    case ExprOp::MoveCaptureCell:
      return "move.capture_cell";
    case ExprOp::MoverColor:
      return "mover.color";
    case ExprOp::MoverType:
      return "mover.type";
    case ExprOp::IsCapture:
      return "move.is_capture";
    case ExprOp::CellArg:
      return "cell";
    case ExprOp::TypeAt:
      return "type_at";
    case ExprOp::ColorAt:
      return "color_at";
    case ExprOp::IsEmpty:
      return "is_empty";
    case ExprOp::PieceField:
      return "piece_field";
    case ExprOp::CellField:
      return "cell_field";
    case ExprOp::SideToMove:
      return "side_to_move";
    case ExprOp::HalfmoveClock:
      return "halfmove_clock";
    case ExprOp::CoordOf:
      return "coord";
    case ExprOp::HasCaptureFrom:
      return "has_capture_from";
    case ExprOp::Attacked:
      return "attacked";
    case ExprOp::Marked:
      return "marked";
    case ExprOp::AnyCapture:
      return "any_capture";
    case ExprOp::Not:
      return "not";
    case ExprOp::And:
      return "and";
    case ExprOp::Or:
      return "or";
    case ExprOp::Eq:
      return "eq";
    case ExprOp::Ne:
      return "ne";
    case ExprOp::Lt:
      return "lt";
    case ExprOp::Gt:
      return "gt";
    case ExprOp::Add:
      return "add";
    case ExprOp::Sub:
      return "sub";
  }
  return "?";
}

int arity(ExprOp op) noexcept {
  switch (op) {
    case ExprOp::ConstInt:
    case ExprOp::MoveFrom:
    case ExprOp::MoveTo:
    case ExprOp::MoveCaptureCell:
    case ExprOp::MoverColor:
    case ExprOp::MoverType:
    case ExprOp::IsCapture:
    case ExprOp::CellArg:
    case ExprOp::SideToMove:
    case ExprOp::HalfmoveClock:
    case ExprOp::AnyCapture:
      return 0;
    case ExprOp::TypeAt:
    case ExprOp::ColorAt:
    case ExprOp::IsEmpty:
    case ExprOp::PieceField:
    case ExprOp::CellField:
    case ExprOp::CoordOf:
    case ExprOp::HasCaptureFrom:
    case ExprOp::Attacked:
    case ExprOp::Marked:
    case ExprOp::Not:
      return 1;
    case ExprOp::Eq:
    case ExprOp::Ne:
    case ExprOp::Lt:
    case ExprOp::Gt:
    case ExprOp::Add:
    case ExprOp::Sub:
      return 2;
    case ExprOp::And:
    case ExprOp::Or:
      return -1;
  }
  return 0;
}

std::size_t Expr::nodeCount() const {
  std::size_t n = 1;
  for (const Expr& a : args) n += a.nodeCount();
  return n;
}

std::string Expr::toString() const {
  std::string s(rules::toString(op));
  if (op == ExprOp::ConstInt) return s + "(" + std::to_string(imm) + ")";
  if (imm != 0) s += "[" + std::to_string(imm) + "]";
  if (args.empty()) return s;
  s += "(";
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (i != 0) s += ", ";
    s += args[i].toString();
  }
  return s + ")";
}

}  // namespace cb::rules
