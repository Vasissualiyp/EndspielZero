// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/vm.hpp"

#include <algorithm>

namespace cb::rules {
namespace {

/// Cells within a Chebyshev radius of `centre`, found by stepping through the geometry
/// so that an explosion on a torus wraps exactly as a move would. Includes the centre.
std::vector<CellId> neighbourhood(const Position& pos, CellId centre, int radius) {
  const VariantSpec& v = pos.variant();
  const std::uint8_t n = v.dims.dims();
  std::vector<CellId> out{centre};
  if (radius <= 0) return out;

  // Enumerate every offset with components in [-radius, radius], skipping the zero
  // offset, and let the geometry resolve each one.
  std::vector<std::int16_t> offset(n, static_cast<std::int16_t>(-radius));
  for (;;) {
    bool allZero = true;
    for (std::uint8_t a = 0; a < n; ++a) {
      if (offset[a] != 0) allZero = false;
    }
    if (!allZero) {
      std::array<std::int16_t, kMaxDims> vec{};
      for (std::uint8_t a = 0; a < n; ++a) vec[a] = offset[a];
      Walker w = v.geom.start(centre, Direction::make(vec, n));
      if (v.geom.step(w)) {
        if (std::find(out.begin(), out.end(), w.cell) == out.end()) out.push_back(w.cell);
      }
    }
    std::uint8_t a = 0;
    for (;; ++a) {
      if (a >= n) return out;
      if (offset[a] < radius) {
        ++offset[a];
        break;
      }
      offset[a] = static_cast<std::int16_t>(-radius);
    }
  }
}

}  // namespace

std::int64_t RuleEngine::eval(const Expr& e, const RuleEnv& env,
                              std::size_t& steps) const {
  if (++steps > kStepBudget) return 0;
  const Position& p = *env.pos;

  const auto cellOf = [&](std::size_t i) -> CellId {
    const auto raw = eval(e.args[i], env, steps);
    if (raw < 0 || raw >= static_cast<std::int64_t>(p.cellCount())) return kInvalidCell;
    return static_cast<CellId>(raw);
  };

  switch (e.op) {
    case ExprOp::ConstInt:
      return e.imm;
    case ExprOp::MoveFrom:
      return env.move != nullptr ? env.move->from : kInvalidCell;
    case ExprOp::MoveTo:
      return env.move != nullptr ? env.move->to : kInvalidCell;
    case ExprOp::MoveCaptureCell:
      return env.move != nullptr ? env.move->captureCell : kInvalidCell;
    case ExprOp::MoverColor:
      return static_cast<std::int64_t>(env.mover);
    case ExprOp::MoverType:
      return env.move != nullptr ? p.at(env.move->to).type : kNoPiece;
    case ExprOp::IsCapture:
      return env.move != nullptr && env.move->captureCell != kInvalidCell ? 1 : 0;
    case ExprOp::CellArg:
      return env.cellArg;
    case ExprOp::SideToMove:
      return static_cast<std::int64_t>(p.sideToMove());
    case ExprOp::HalfmoveClock:
      return p.halfmoveClock();

    case ExprOp::TypeAt: {
      const CellId c = cellOf(0);
      return c == kInvalidCell ? kNoPiece : p.at(c).type;
    }
    case ExprOp::ColorAt: {
      const CellId c = cellOf(0);
      // An empty cell has no colour; -1 is distinguishable from either side.
      return c == kInvalidCell || p.at(c).empty() ? -1 : p.at(c).color;
    }
    case ExprOp::IsEmpty: {
      const CellId c = cellOf(0);
      return c == kInvalidCell || p.at(c).empty() ? 1 : 0;
    }
    case ExprOp::PieceField: {
      const CellId c = cellOf(0);
      return c == kInvalidCell ? 0 : p.pieceField(static_cast<int>(e.imm), c);
    }
    case ExprOp::CellField: {
      const CellId c = cellOf(0);
      return c == kInvalidCell ? 0 : p.cellField(static_cast<int>(e.imm), c);
    }
    case ExprOp::CoordOf: {
      const CellId c = cellOf(0);
      if (c == kInvalidCell) return -1;
      return p.variant().dims.toCoord(c).c[static_cast<std::size_t>(e.imm)];
    }
    case ExprOp::AnyCapture:
      // kInvalidCell asks "anywhere", which is what a forced-capture rule needs.
      return env.hasCaptureFrom && env.hasCaptureFrom(kInvalidCell) ? 1 : 0;

    case ExprOp::Attacked: {
      const CellId c = cellOf(0);
      if (c == kInvalidCell || !env.isAttacked) return 0;
      return env.isAttacked(c, opponent(env.mover)) ? 1 : 0;
    }
    case ExprOp::Marked: {
      const CellId c = cellOf(0);
      if (c == kInvalidCell) return 0;
      return p.markAt(c, static_cast<std::uint16_t>(e.imm)) != nullptr ? 1 : 0;
    }
    case ExprOp::HasCaptureFrom: {
      const CellId c = cellOf(0);
      if (c == kInvalidCell || !env.hasCaptureFrom) return 0;
      return env.hasCaptureFrom(c) ? 1 : 0;
    }

    case ExprOp::Not:
      return eval(e.args[0], env, steps) == 0 ? 1 : 0;
    case ExprOp::And:
      for (const Expr& a : e.args) {
        if (eval(a, env, steps) == 0) return 0;
      }
      return 1;
    case ExprOp::Or:
      for (const Expr& a : e.args) {
        if (eval(a, env, steps) != 0) return 1;
      }
      return 0;
    case ExprOp::Eq:
      return eval(e.args[0], env, steps) == eval(e.args[1], env, steps) ? 1 : 0;
    case ExprOp::Ne:
      return eval(e.args[0], env, steps) != eval(e.args[1], env, steps) ? 1 : 0;
    case ExprOp::Lt:
      return eval(e.args[0], env, steps) < eval(e.args[1], env, steps) ? 1 : 0;
    case ExprOp::Gt:
      return eval(e.args[0], env, steps) > eval(e.args[1], env, steps) ? 1 : 0;
    case ExprOp::Add:
      return eval(e.args[0], env, steps) + eval(e.args[1], env, steps);
    case ExprOp::Sub:
      return eval(e.args[0], env, steps) - eval(e.args[1], env, steps);
  }
  return 0;
}

void RuleEngine::applyEffect(const Effect& eff, RuleEnv& env, Undo* undo,
                             RuleOutcome& out) const {
  Position& p = *env.pos;
  std::size_t& steps = out.steps;

  const auto cellArg = [&](std::size_t i) -> CellId {
    if (i >= eff.args.size()) return kInvalidCell;
    const auto raw = eval(eff.args[i], env, steps);
    if (raw < 0 || raw >= static_cast<std::int64_t>(p.cellCount())) return kInvalidCell;
    return static_cast<CellId>(raw);
  };

  switch (eff.op) {
    case EffectOp::ForbidMove:
      out.forbidden = true;
      return;
    case EffectOp::RepeatTurn:
      out.repeatTurn = true;
      return;
    case EffectOp::EndGame:
      out.gameOver = true;
      out.outcome = static_cast<Outcome>(eff.imm);
      return;

    case EffectOp::Destroy: {
      const CellId c = cellArg(0);
      if (c != kInvalidCell && !p.at(c).empty()) p.setCell(c, Piece{}, undo);
      return;
    }

    case EffectOp::DestroyRegion: {
      const CellId centre = cellArg(0);
      if (centre == kInvalidCell) return;
      for (CellId c : neighbourhood(p, centre, static_cast<int>(eff.imm))) {
        if (++steps > kStepBudget) {
          out.budgetExhausted = true;
          return;
        }
        if (p.at(c).empty()) continue;
        if (!eff.filter.empty()) {
          // The filter sees each cell in turn through CellArg, which is how "everything
          // except pawns" is expressed without a special-purpose opcode.
          const CellId saved = env.cellArg;
          env.cellArg = c;
          const bool keep = eval(eff.filter[0], env, steps) == 0;
          env.cellArg = saved;
          if (keep) continue;
        }
        p.setCell(c, Piece{}, undo);
      }
      return;
    }

    case EffectOp::Transform: {
      const CellId c = cellArg(0);
      if (c == kInvalidCell || p.at(c).empty()) return;
      Piece piece = p.at(c);
      piece.type = static_cast<PieceTypeId>(eff.imm);
      p.setCell(c, piece, undo);
      return;
    }

    case EffectOp::Spawn: {
      const CellId c = cellArg(0);
      if (c == kInvalidCell) return;
      const auto color = eff.args.size() > 1
                             ? static_cast<std::uint8_t>(eval(eff.args[1], env, steps))
                             : static_cast<std::uint8_t>(env.mover);
      p.setCell(c, Piece{static_cast<PieceTypeId>(eff.imm), color, 0}, undo);
      return;
    }

    case EffectOp::SetPieceField: {
      const CellId c = cellArg(0);
      if (c == kInvalidCell || eff.args.size() < 2) return;
      p.setPieceField(static_cast<int>(eff.imm), c,
                      static_cast<std::int32_t>(eval(eff.args[1], env, steps)), undo);
      return;
    }
    case EffectOp::SetCellField: {
      const CellId c = cellArg(0);
      if (c == kInvalidCell || eff.args.size() < 2) return;
      p.setCellField(static_cast<int>(eff.imm), c,
                     static_cast<std::int32_t>(eval(eff.args[1], env, steps)), undo);
      return;
    }
  }
}

RuleOutcome RuleEngine::run(Trigger t, RuleEnv& env, Undo* undo) const {
  RuleOutcome out;
  for (const Rule& r : rules_.rules) {
    if (r.trigger != t) continue;
    if (!r.condition.empty() && eval(r.condition[0], env, out.steps) == 0) continue;
    for (const Effect& eff : r.effects) {
      applyEffect(eff, env, undo, out);
      if (out.budgetExhausted) return out;
      // A veto is final: nothing after it can make the move legal again, and running
      // further effects would mutate a position that is about to be discarded.
      if (out.forbidden && t == Trigger::OnMoveFilter) return out;
    }
  }
  if (out.steps > kStepBudget) out.budgetExhausted = true;
  return out;
}

}  // namespace cb::rules
