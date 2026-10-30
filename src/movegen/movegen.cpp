// SPDX-License-Identifier: GPL-3.0-or-later
#include "movegen/movegen.hpp"

#include <algorithm>

#include "diag/counters.hpp"

namespace cb {
namespace {

enum class Occupant { Empty, Own, Enemy };

Occupant classify(const Position& p, CellId c, Color side) {
  const Piece t = p.at(c);
  if (t.empty()) return Occupant::Empty;
  return t.colorOf() == side ? Occupant::Own : Occupant::Enemy;
}

}  // namespace

MoveGen::MoveGen(const VariantSpec& v) : v_(&v) {
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    for (const MoveAtom& a : v.pieces[i].atoms) {
      if (a.mode == MoveMode::Hop) hasHoppers_ = true;
    }
  }
  // Only a board with glued or reflecting faces can send a ray back through a
  // cell it has already crossed, so an ordinary box pays nothing for this.
  rayCap_ = v.geom.isBox() ? v.dims.cellCount() : v.dims.cellCount() * 2 * kMaxDims;
}

void MoveGen::emit(const Position& p, Move m, PieceTypeId type, Color orient,
                   bool reorient, const Coord& toCoord, MoveList& out) const {
  if (reorient) {
    m.flags = static_cast<std::uint8_t>(m.flags |
                                        static_cast<std::uint8_t>(MoveFlag::Reorient));
    orient = opponent(orient);
  }
  const PieceTypeDef& def = v_->pieces[type];
  const Region& promo = v_->promotion[static_cast<std::size_t>(orient)];
  if (!def.promotesTo.empty() && promo.contains(toCoord)) {
    // One move per promotion choice. A variant that declares no promotion region
    // (a torus has no last rank) simply never reaches this branch.
    for (PieceTypeId to : def.promotesTo) {
      Move pm = m;
      pm.flags = static_cast<std::uint8_t>(
          pm.flags | static_cast<std::uint8_t>(MoveFlag::Promotion));
      pm.promoteTo = to;
      out.push(pm);
    }
    return;
  }
  (void)p;
  out.push(m);
}

void MoveGen::generateAtom(const Position& p, CellId from, const Coord& fromCoord,
                           Color side, Color orient, PieceTypeId type,
                           const MoveAtom& atom, MoveList& out) const {
  // Direction spans and home regions follow the piece's orientation; occupancy and
  // capture follow its colour. The two differ only for a flipped piece.
  const std::size_t ci = static_cast<std::size_t>(orient);
  const Region& gate = atom.fromRegion[ci];
  if (gate.active() && !gate.contains(fromCoord)) return;
  if (atom.unmoved && p.at(from).hasMoved()) return;

  const Geometry& geom = v_->geom;
  // Which of the piece's atoms this is, carried on the move so `make` can read the marks it
  // leaves behind.
  const auto atomIdx = static_cast<std::uint8_t>(&atom - v_->pieces[type].atoms.data());
  bool leavesPassedMark = false;
  for (const MarkSpec& m : atom.marks) leavesPassedMark = leavesPassedMark || m.where == MarkWhere::Passed;
  const std::uint32_t begin = atom.dirBegin[ci];
  const std::uint32_t end = atom.dirEnd[ci];
  const bool tracks = v_->pieces[type].reorients;
  const auto orientAxis = static_cast<std::uint8_t>(v_->orientationAxis);

  for (std::uint32_t di = begin; di < end; ++di) {
    const Direction startDir = v_->dirTable[di];
    Walker w = geom.start(from, startDir);
    if (tracks) {
      w.upAxis = orientAxis;
      w.upSign = 1;
    }
    bool hurdleFound = false;
    /// The cell occupied one step earlier, which is what a double step leaves
    /// behind as an en-passant target. Tracked rather than computed, because
    /// "one cell backwards" is not a thing on a wrapped or higher-dimensional
    /// board.
    CellId prevCell = kInvalidCell;

    const std::uint32_t steps = atom.maxK == kUnlimited ? rayCap_ : atom.maxK;
    for (std::uint32_t k = 1; k <= steps; ++k) {
      if (!geom.step(w)) break;
      if (rayClosed(w, from, startDir)) break;

      const CellId to = w.cell;
      // A directional piece whose frame came back reversed lands flipped.
      const bool flipNow = tracks && w.upReversed(orientAxis);
      // A cell some blocking mark is on: a slider stops short of it, a leaper does not land.
      if (v_->hasBlockingMarks && p.blockedAt(to)) {
        if (atom.mode == MoveMode::Leap) {
          prevCell = to;
          continue;
        }
        break;
      }
      // The origin still holds the moving piece, so a slider is blocked there and
      // a leaper passes over it - handled by the ordinary occupancy classification.
      const Occupant occ = classify(p, to, side);
      const bool longEnough = k >= atom.minK;

      if (atom.mode == MoveMode::Hop) {
        // Grasshopper semantics: clear exactly one occupied cell, then land on the
        // cell immediately beyond it. Cannon-style "capture anything beyond the
        // hurdle" is a separate mode and arrives with the rule VM (M5).
        if (!hurdleFound) {
          if (occ != Occupant::Empty) hurdleFound = true;
          continue;
        }
        if (occ == Occupant::Own) break;
        if (occ == Occupant::Empty && atom.capture != CapturePolicy::Must) {
          emit(p, Move{.from = from, .to = to, .atomIndex = atomIdx}, type, orient, flipNow,
               w.coord, out);
        } else if (occ == Occupant::Enemy && atom.capture != CapturePolicy::Cannot) {
          emit(p,
               Move{.from = from,
                    .to = to,
                    .captureCell = to,
                    .flags = static_cast<std::uint8_t>(MoveFlag::Capture),
                    .atomIndex = atomIdx},
               type, orient, flipNow, w.coord, out);
        }
        break;
      }

      if (occ == Occupant::Own) {
        if (atom.mode == MoveMode::Slide) break;
        continue;  // a leaping rider passes over everything
      }

      if (occ == Occupant::Enemy) {
        // A move that captures at an offset takes only that piece, never the one it lands on.
        if (longEnough && atom.capture != CapturePolicy::Cannot && atom.captureAt.empty()) {
          emit(p,
               Move{.from = from,
                    .to = to,
                    .captureCell = to,
                    .flags = static_cast<std::uint8_t>(MoveFlag::Capture),
                    .atomIndex = atomIdx},
               type, orient, flipNow, w.coord, out);
        }
        if (atom.mode == MoveMode::Slide) break;
        continue;
      }

      // The destination is empty.
      if (longEnough) {
        if (atom.capture != CapturePolicy::Must) {
          Move m{.from = from, .to = to, .atomIndex = atomIdx};
          if (leavesPassedMark && !atom.leavesEnPassant) m.epTarget = prevCell;
          if (atom.leavesEnPassant && prevCell != kInvalidCell) {
            m.flags = static_cast<std::uint8_t>(
                m.flags | static_cast<std::uint8_t>(MoveFlag::LeavesEnPassant));
            m.epTarget = prevCell;
            m.epVictim = to;
          }
          emit(p, m, type, orient, flipNow, w.coord, out);
        } else if (!atom.captureAt.empty()) {
          // A capture-only move onto an empty cell that takes the piece beside it: en passant
          // (the landing cell must carry the opponent's mark), a checkers jump (it need not).
          const Mark* needed = atom.needsMark >= 0
                                   ? p.markAt(to, static_cast<std::uint16_t>(atom.needsMark))
                                   : nullptr;
          if (atom.needsMark < 0 || (needed != nullptr && needed->owner != static_cast<std::uint8_t>(side))) {
            Walker vw = geom.start(to, v_->dirTable[atom.captureDir[ci]]);
            if (geom.step(vw)) {
              const Piece victim = p.at(vw.cell);
              if (!victim.empty() && victim.colorOf() != side) {
                std::uint8_t flags = static_cast<std::uint8_t>(MoveFlag::Capture);
                if (atom.needsMark >= 0) flags = static_cast<std::uint8_t>(flags | static_cast<std::uint8_t>(MoveFlag::EnPassant));
                emit(p, Move{.from = from, .to = to, .captureCell = vw.cell, .flags = flags, .atomIndex = atomIdx},
                     type, orient, flipNow, w.coord, out);
              }
            }
          }
        } else if (v_->enPassant && to == p.epTarget() && p.epVictim() != kInvalidCell) {
          // A capture-only atom landing on the en-passant target takes the piece
          // that passed through, not the piece on the destination.
          emit(p,
               Move{.from = from,
                    .to = to,
                    .captureCell = p.epVictim(),
                    .flags = MoveFlag::Capture | MoveFlag::EnPassant,
                    .atomIndex = atomIdx},
               type, orient, flipNow, w.coord, out);
        }
      }

      prevCell = to;
      CB_COUNT(MovesGenerated);
    }
  }
}

void MoveGen::generateForPiece(const Position& p, CellId from, MoveList& out) const {
  const Piece piece = p.at(from);
  const Color side = piece.colorOf();
  // Decoded once per piece and then carried through every ray, so the inner loop
  // never divides (ARCH section 4.2).
  const Coord fromCoord = v_->dims.toCoord(from);
  for (const MoveAtom& atom : v_->pieces[piece.type].atoms) {
    generateAtom(p, from, fromCoord, side, piece.orientation(), piece.type, atom, out);
  }
}

void MoveGen::generateCastles(const Position& p, Color side, MoveList& out) const {
  for (std::size_t i = 0; i < v_->castles.size(); ++i) {
    const CastleTemplate& ct = v_->castles[i];
    if (ct.color != side) continue;
    if ((p.castleRights() & (1u << ct.rightsBit)) == 0) continue;

    const Piece king = p.at(ct.kingFrom);
    const Piece rook = p.at(ct.rookFrom);
    if (king.empty() || rook.empty()) continue;
    if (king.colorOf() != side || rook.colorOf() != side) continue;

    bool ok = true;
    for (CellId c : ct.mustBeEmpty) {
      if (!p.isEmpty(c)) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;
    for (CellId c : ct.mustBeSafe) {
      if (isAttacked(p, c, opponent(side))) {
        ok = false;
        break;
      }
    }
    if (!ok) continue;

    out.push(Move{.from = ct.kingFrom,
                  .to = ct.kingTo,
                  .flags = static_cast<std::uint8_t>(MoveFlag::Castle),
                  .castleIndex = static_cast<std::uint8_t>(i),
                  .partnerFrom = ct.rookFrom,
                  .partnerTo = ct.rookTo});
  }
}

void MoveGen::generateCompounds(const Position& p, Color side, MoveList& out) const {
  const Geometry& geom = v_->geom;
  const std::size_t ci = static_cast<std::size_t>(side);
  for (std::size_t i = 0; i < v_->compounds.size(); ++i) {
    const CompoundMove& cm = v_->compounds[i];
    const Direction dir = v_->dirTable[cm.unitDir[ci]];
    p.occupancy(side).forEach([&](std::size_t idx) {
      const auto from = static_cast<CellId>(idx);
      const Piece piece = p.at(from);
      if (piece.type != cm.mover) return;
      if (cm.moverUnmoved && piece.hasMoved()) return;

      // Walk the mover's line, collecting the cells, until the first piece. That piece is
      // the partner: found, never named, so no coordinate is written into the rule.
      // The line the mover walks, on the stack: generation allocates nothing. A line longer
      // than this holds no compound move (castling is a few cells).
      constexpr std::size_t kLine = 128;
      CellId cells[kLine];
      std::size_t nCells = 0;
      Walker w = geom.start(from, dir);
      CellId partnerCell = kInvalidCell;
      for (std::uint32_t k = 1; k <= rayCap_; ++k) {
        if (!geom.step(w)) break;
        if (rayClosed(w, from, dir)) break;
        if (nCells == kLine) break;
        cells[nCells++] = w.cell;
        if (!p.isEmpty(w.cell)) {
          partnerCell = w.cell;
          break;
        }
      }
      // The partner must lie beyond where the mover lands, so every cell it crosses and the
      // one it lands on is empty.
      if (partnerCell == kInvalidCell || nCells <= cm.steps) return;
      const Piece partner = p.at(partnerCell);
      if (partner.colorOf() != side) return;
      if (std::find(cm.partners.begin(), cm.partners.end(), partner.type) == cm.partners.end()) return;
      if (cm.partnerUnmoved && partner.hasMoved()) return;

      const CellId dest = cells[cm.steps - 1];
      const CellId crossed = cm.steps >= 2 ? cells[cm.steps - 2] : from;
      for (const CompoundMove::SafeCell sc : cm.safe) {
        switch (sc) {
          case CompoundMove::SafeCell::From:
            if (isAttacked(p, from, opponent(side))) return;
            break;
          case CompoundMove::SafeCell::Path:
            for (std::uint32_t k = 0; k + 1 < cm.steps; ++k) {
              if (isAttacked(p, cells[k], opponent(side))) return;
            }
            break;
          case CompoundMove::SafeCell::To:
            if (isAttacked(p, dest, opponent(side))) return;
            break;
        }
      }
      out.push(Move{.from = from,
                    .to = dest,
                    .flags = static_cast<std::uint8_t>(MoveFlag::Castle),
                    .castleIndex = static_cast<std::uint8_t>(kCompoundBit | i),
                    .partnerFrom = partnerCell,
                    .partnerTo = crossed});
    });
  }
}

void MoveGen::generatePseudoLegal(const Position& p, MoveList& out) const {
  generatePseudoLegalFor(p, p.sideToMove(), /*includeCastles=*/true, out);
}

void MoveGen::generatePseudoLegalFor(const Position& p, Color side, bool includeCastles,
                                     MoveList& out) const {
  p.occupancy(side).forEach(
      [&](std::size_t cell) { generateForPiece(p, static_cast<CellId>(cell), out); });
  if (includeCastles) {
    generateCastles(p, side, out);
    generateCompounds(p, side, out);
  }
  // On a box board a move can only be discovered once, so this costs nothing
  // there and is skipped entirely.
  if (!v_->geom.isBox()) out.sortAndUnique();
}

Position MoveGen::probePosition(const Position& p, CellId target, Color by) const {
  // A hypothetical enemy piece on the target, so that an empty cell can still be
  // "attacked" - castling safety depends on exactly that reading.
  Position probePos = p;
  const Piece standing = probePos.at(target);
  if (standing.empty() || standing.colorOf() == by) {
    probePos.removeAt(target);
    probePos.place(target, static_cast<PieceTypeId>(v_->pieces.size() - 1), opponent(by));
  }
  return probePos;
}

bool MoveGen::isAttackedByForwardScan(const Position& p, CellId target, Color by) const {
  const Position probePos = probePosition(p, target, by);
  MoveList probe(v_->moveUpperBound());
  generatePseudoLegalFor(probePos, by, /*includeCastles=*/false, probe);
  for (const Move& m : probe) {
    if (m.captureCell == target) return true;
  }
  return false;
}

void MoveGen::movesFrom(const Position& p, CellId from, MoveList& out) const {
  if (p.at(from).empty()) return;
  generateForPiece(p, from, out);
}

std::vector<std::vector<CellId>> MoveGen::tracePaths(const Position& probe, CellId from,
                                                     const MoveAtom& atom,
                                                     CellId target) const {
  std::vector<std::vector<CellId>> result;
  const Piece piece = probe.at(from);
  const std::size_t ci = static_cast<std::size_t>(piece.orientation());
  const Coord fromCoord = v_->dims.toCoord(from);
  const Region& gate = atom.fromRegion[ci];
  if (gate.active() && !gate.contains(fromCoord)) return result;
  if (atom.unmoved && piece.hasMoved()) return result;

  const Geometry& geom = v_->geom;
  for (std::uint32_t di = atom.dirBegin[ci]; di < atom.dirEnd[ci]; ++di) {
    const Direction startDir = v_->dirTable[di];
    Walker w = geom.start(from, startDir);
    std::vector<CellId> cells;
    bool hurdle = false;
    const std::uint32_t steps = atom.maxK == kUnlimited ? rayCap_ : atom.maxK;
    for (std::uint32_t k = 1; k <= steps; ++k) {
      if (!geom.step(w)) break;
      if (rayClosed(w, from, startDir)) break;
      const CellId c = w.cell;
      const bool blocked = v_->hasBlockingMarks && probe.blockedAt(c);
      if (c == target && !blocked) {
        const bool ok = atom.mode == MoveMode::Hop ? hurdle : k >= atom.minK;
        if (ok) {
          result.push_back(cells);
          break;
        }
        if (atom.mode != MoveMode::Leap) break;
      }
      if (blocked) {
        if (atom.mode == MoveMode::Leap) {
          cells.push_back(c);
          continue;
        }
        break;
      }
      const bool occupied = !probe.at(c).empty();
      if (atom.mode == MoveMode::Hop) {
        if (hurdle) break;  // the landing cell must be the very next one
        cells.push_back(c);
        if (occupied) hurdle = true;
        continue;
      }
      if (atom.mode == MoveMode::Slide) {
        if (occupied) break;
        cells.push_back(c);
      } else {
        cells.push_back(c);  // a leaping rider passes over everything
      }
    }
  }
  return result;
}

std::vector<MoveGen::Attacker> MoveGen::attackers(const Position& p, CellId target,
                                                  Color by) const {
  const Position probe = probePosition(p, target, by);
  MoveList list(v_->moveUpperBound());
  generatePseudoLegalFor(probe, by, /*includeCastles=*/false, list);

  std::vector<CellId> froms;
  std::vector<CellId> landsOnTarget;
  for (const Move& m : list) {
    if (m.captureCell != target) continue;
    froms.push_back(m.from);
    if (m.to == target) landsOnTarget.push_back(m.from);
  }
  std::sort(froms.begin(), froms.end());
  froms.erase(std::unique(froms.begin(), froms.end()), froms.end());

  std::vector<Attacker> out;
  for (const CellId from : froms) {
    // An offset capture (checkers jump, en passant) never lands on the target.
    if (std::find(landsOnTarget.begin(), landsOnTarget.end(), from) == landsOnTarget.end()) {
      out.push_back(Attacker{from, {}});
      continue;
    }
    std::vector<std::vector<CellId>> routes;
    for (const MoveAtom& atom : v_->pieces[probe.at(from).type].atoms) {
      if (atom.capture == CapturePolicy::Cannot || !atom.captureAt.empty()) continue;
      for (auto& r : tracePaths(probe, from, atom, target)) {
        if (std::find(routes.begin(), routes.end(), r) == routes.end()) {
          routes.push_back(std::move(r));
        }
      }
    }
    if (routes.empty()) out.push_back(Attacker{from, {}});
    for (auto& r : routes) out.push_back(Attacker{from, std::move(r)});
  }
  return out;
}

bool MoveGen::isAttacked(const Position& p, CellId target, Color by) const {
  // A hopper's threat depends on a hurdle between it and the target, and a glued
  // board can make the backward walk a non-inverse of the forward one; either way
  // the scan is the only correct answer.
  if (hasHoppers_ || !v_->geom.isBox()) return isAttackedByForwardScan(p, target, by);

  // No piece is ever flipped here: only a board with an orientation-reversing face can
  // flip one (PieceTypeDef::reorients), and such a board is never a box.
  const Geometry& geom = v_->geom;

  for (std::size_t ti = 1; ti < v_->pieces.size(); ++ti) {
    const PieceTypeDef& def = v_->pieces[ti];
    for (const MoveAtom& atom : def.atoms) {
      if (atom.capture == CapturePolicy::Cannot) continue;  // cannot threaten anything
      const std::size_t ci = static_cast<std::size_t>(by);
      // A move that captures at an offset does not threaten the cell it lands on but the
      // one beside it: to threaten `target` it must land on the cell that offset away,
      // which must be empty (and carry the opponent's mark, for en passant). The backward
      // search then starts from there.
      CellId anchor = target;
      if (!atom.captureAt.empty()) {
        Walker lw = geom.start(target, v_->dirTable[atom.captureDir[ci]].negated());
        if (!geom.step(lw)) continue;
        anchor = lw.cell;
        if (!p.isEmpty(anchor)) continue;
        if (atom.needsMark >= 0) {
          const Mark* mk = p.markAt(anchor, static_cast<std::uint16_t>(atom.needsMark));
          if (mk == nullptr || mk->owner == static_cast<std::uint8_t>(by)) continue;
        }
      }
      // Walk the threat span, not the move span: on a seam that reverses
      // orientation a forward direction is carried out of the move span, so
      // restricting the search to it would miss the threat (MoveAtom::threatBegin).
      for (std::uint32_t di = atom.threatBegin[ci]; di < atom.threatEnd[ci]; ++di) {
        // Walk away from the target along the reverse of the attacking direction;
        // a piece found there attacks the target along it.
        const Direction startDir = v_->dirTable[di].negated();
        Walker w = geom.start(anchor, startDir);
        const std::uint32_t steps = atom.maxK == kUnlimited ? rayCap_ : atom.maxK;
        for (std::uint32_t k = 1; k <= steps; ++k) {
          if (!geom.step(w)) break;
          if (rayClosed(w, anchor, startDir)) break;
          const Piece cand = p.at(w.cell);
          if (cand.empty()) continue;
          bool matches = cand.type == static_cast<PieceTypeId>(ti) &&
                         cand.colorOf() == by && k >= atom.minK;
          if (matches && atom.oriented) {
            // The direction we arrived with, reversed, is the direction the
            // candidate would actually travel. For an oriented atom that must be
            // one of its permitted directions - which after a seam crossing is not
            // implied by having started from one.
            const Direction forward = w.dir.negated();
            bool permitted = false;
            for (std::uint32_t si = atom.dirBegin[ci]; si < atom.dirEnd[ci]; ++si) {
              if (v_->dirTable[si] == forward) {
                permitted = true;
                break;
              }
            }
            matches = permitted;
          }
          if (matches) {
            const Region& gate = atom.fromRegion[ci];
            if ((!gate.active() || gate.contains(w.coord)) &&
                !(atom.unmoved && cand.hasMoved())) {
              CB_COUNT(LegalityChecks);
              return true;
            }
          }
          if (atom.mode == MoveMode::Slide) break;  // blocked
        }
      }
    }
  }
  return false;
}

bool MoveGen::inCheck(const Position& p, Color c) const {
  const CellId royal = p.findRoyal(c);
  if (royal == kInvalidCell) return false;  // a variant with no royal piece
  return isAttacked(p, royal, opponent(c));
}

bool MoveGen::leavesRoyalExposed(Position& p, const Move& m) const {
  const Color side = p.sideToMove();
  Undo u;
  p.make(m, u);
  const bool exposed = inCheck(p, side);
  p.unmake(m, u);
  return exposed;
}

void MoveGen::generateLegal(Position& p, MoveList& out) const {
  MoveList pseudo(v_->moveUpperBound());
  generatePseudoLegal(p, pseudo);
  for (const Move& m : pseudo) {
    if (!leavesRoyalExposed(p, m)) out.push(m);
  }
}

std::uint64_t MoveGen::perft(Position& p, int depth) const {
  Buffers bufs;
  bufs.ensure(depth, v_->moveUpperBound());
  return perft(p, depth, bufs);
}

std::uint64_t MoveGen::perft(Position& p, int depth, Buffers& bufs) const {
  if (depth <= 0) return 1;
  MoveList& moves = bufs.at(depth);
  moves.clear();
  generatePseudoLegal(p, moves);

  // Iterating by index rather than by pointer: a deeper call reuses a different
  // buffer, so this one is stable, but indexing keeps that obvious.
  std::uint64_t nodes = 0;
  const Color side = p.sideToMove();
  const std::size_t n = moves.size();
  for (std::size_t i = 0; i < n; ++i) {
    const Move m = moves[i];
    Undo u;
    p.make(m, u);
    if (!inCheck(p, side)) {
      nodes += depth == 1 ? 1 : perft(p, depth - 1, bufs);
    }
    p.unmake(m, u);
  }
  return nodes;
}

std::vector<MoveGen::DivideEntry> MoveGen::perftDivide(Position& p, int depth) const {
  std::vector<DivideEntry> out;
  if (depth <= 0) return out;
  MoveList moves(v_->moveUpperBound());
  generatePseudoLegal(p, moves);
  const Color side = p.sideToMove();
  Buffers bufs;
  bufs.ensure(depth, v_->moveUpperBound());
  for (const Move& m : moves) {
    Undo u;
    p.make(m, u);
    if (!inCheck(p, side)) {
      out.push_back(DivideEntry{m, depth == 1 ? 1 : perft(p, depth - 1, bufs)});
    }
    p.unmake(m, u);
  }
  return out;
}

}  // namespace cb
