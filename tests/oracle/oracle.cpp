// SPDX-License-Identifier: GPL-3.0-or-later
#include "oracle/oracle.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <tuple>

namespace cb::oracle {
namespace {

/// Walk k steps of `dir` from `from`, re-deriving everything each step. Returns
/// the visited cells in order, stopping where the board does.
/// One step of a ray, as the oracle sees it.
struct RayCell {
  CellId cell{kInvalidCell};
  Coord coord{};
};

/// Walk a ray, re-deriving everything each step.
///
/// Termination: a ray ends when it returns to the state it started in. The step
/// map is a bijection on (cell, direction), so every orbit is a cycle through the
/// start. Ending on a mere *cell* revisit would be wrong in a subtle way - it makes
/// reachability asymmetric, so walking the ray backwards from a destination stops
/// somewhere else, and the reverse-attack search would disagree with forward
/// generation on exactly the non-orientable boards it is hardest to reason about.
std::vector<RayCell> ray(const VariantSpec& v, CellId from, const Direction& dir,
                         std::uint32_t maxSteps) {
  std::vector<RayCell> out;
  Walker w = v.geom.start(from, dir);
  const std::uint32_t cap =
      maxSteps == kUnlimited ? v.dims.cellCount() * 2 * kMaxDims : maxSteps;
  for (std::uint32_t k = 0; k < cap; ++k) {
    if (!v.geom.step(w)) break;
    if (w.cell == from && w.dir == dir) break;  // orbit closed
    out.push_back(RayCell{w.cell, v.dims.toCoord(w.cell)});
    // Re-seed from the cell and the current direction, discarding any carried
    // state, so a bug in the incremental walk cannot hide here too.
    const Direction here = w.dir;
    w = v.geom.start(w.cell, here);
  }
  return out;
}

/// Expanded directions, memoised per (variant, atom, colour).
///
/// The oracle stays naive in *structure* - it still re-derives the expansion from
/// the atom rather than trusting the variant's direction table and spans, which is
/// the independence that matters - but it does not re-run the expansion inside
/// every ray. Without this the property suite takes hours and therefore does not
/// get run, which is a worse outcome than a slightly less naive oracle.
const std::vector<Direction>& directionsFor(const VariantSpec& v, const MoveAtom& atom,
                                            Color side) {
  struct Key {
    std::uint64_t variantId;
    std::string atom;
    int color;
    bool operator<(const Key& o) const {
      return std::tie(variantId, atom, color) < std::tie(o.variantId, o.atom, o.color);
    }
  };
  static thread_local std::map<Key, std::vector<Direction>> cache;
  const Key key{v.variantId(), atom.toString(), static_cast<int>(side)};
  const auto it = cache.find(key);
  if (it != cache.end()) return it->second;

  std::vector<Direction> dirs = expandAtomFor(atom, v.dims.dims(), v.orientationAxis, side);

  // An atom's magnitudes are units of *movement*; a direction is in lattice cells, and
  // an axis may be sampled more finely than a piece moves along it - the turn axis holds
  // two boards per turn, one for each player. Written out here rather than shared with
  // the engine's own scaling: the oracle is only worth having while it is an independent
  // implementation of the same rule.
  for (Direction& d : dirs) {
    std::array<std::int16_t, kMaxDims> v2{};
    for (std::uint8_t a = 0; a < d.n; ++a) {
      v2[a] = static_cast<std::int16_t>(d.v[a] * v.dims.pitch(a));
    }
    d = Direction::make(v2, d.n);
  }
  return cache.emplace(key, std::move(dirs)).first->second;
}

void appendPromotions(const VariantSpec& v, const Move& base, PieceTypeId type,
                      Color side, const Coord& toCoord, std::vector<Move>& out) {
  const PieceTypeDef& def = v.pieces[type];
  const Region& promo = v.promotion[static_cast<std::size_t>(side)];
  if (!def.promotesTo.empty() && promo.contains(toCoord)) {
    for (PieceTypeId to : def.promotesTo) {
      Move m = base;
      m.flags = static_cast<std::uint8_t>(m.flags |
                                          static_cast<std::uint8_t>(MoveFlag::Promotion));
      m.promoteTo = to;
      out.push_back(m);
    }
    return;
  }
  out.push_back(base);
}

}  // namespace

std::vector<Move> pseudoLegal(const Position& p) {
  const VariantSpec& v = p.variant();
  const Color side = p.sideToMove();
  std::vector<Move> out;

  for (CellId from = 0; from < v.dims.cellCount(); ++from) {
    const Piece piece = p.at(from);
    if (piece.empty() || piece.colorOf() != side) continue;
    const Coord fromCoord = v.dims.toCoord(from);

    for (const MoveAtom& atom : v.pieces[piece.type].atoms) {
      const Region& gate = atom.fromRegion[static_cast<std::size_t>(side)];
      if (gate.active() && !gate.contains(fromCoord)) continue;
      if (atom.unmoved && piece.hasMoved()) continue;
      bool leavesPassed = false;
      for (const MarkSpec& m : atom.marks) leavesPassed = leavesPassed || m.where == MarkWhere::Passed;

      for (const Direction& dir : directionsFor(v, atom, side)) {
        const auto cells = ray(v, from, dir, atom.maxK);
        bool hurdleFound = false;
        CellId prev = kInvalidCell;

        for (std::size_t i = 0; i < cells.size(); ++i) {
          const auto k = static_cast<std::uint32_t>(i + 1);
          const CellId to = cells[i].cell;
          const Coord& toCoord = cells[i].coord;
          const Piece target = p.at(to);
          const bool own = !target.empty() && target.colorOf() == side;
          const bool enemy = !target.empty() && !own;
          const bool longEnough = k >= atom.minK;
          // A cell a blocking mark is on: a slider stops, a leaper does not land.
          if (v.hasBlockingMarks && p.blockedAt(to)) {
            if (atom.mode == MoveMode::Leap) {
              prev = to;
              continue;
            }
            break;
          }

          if (atom.mode == MoveMode::Hop) {
            if (!hurdleFound) {
              if (!target.empty()) hurdleFound = true;
              continue;
            }
            if (own) break;
            if (target.empty() && atom.capture != CapturePolicy::Must) {
              appendPromotions(v, Move{.from = from, .to = to}, piece.type, side, toCoord,
                               out);
            } else if (enemy && atom.capture != CapturePolicy::Cannot) {
              appendPromotions(
                  v,
                  Move{.from = from,
                       .to = to,
                       .captureCell = to,
                       .flags = static_cast<std::uint8_t>(MoveFlag::Capture)},
                  piece.type, side, toCoord, out);
            }
            break;
          }

          if (own) {
            if (atom.mode == MoveMode::Slide) break;
            prev = to;
            continue;
          }
          if (enemy) {
            if (longEnough && atom.capture != CapturePolicy::Cannot && atom.captureAt.empty()) {
              appendPromotions(
                  v,
                  Move{.from = from,
                       .to = to,
                       .captureCell = to,
                       .flags = static_cast<std::uint8_t>(MoveFlag::Capture)},
                  piece.type, side, toCoord, out);
            }
            if (atom.mode == MoveMode::Slide) break;
            prev = to;
            continue;
          }

          if (longEnough) {
            if (atom.capture != CapturePolicy::Must) {
              Move m{.from = from, .to = to};
              if (leavesPassed && !atom.leavesEnPassant) m.epTarget = prev;
              if (atom.leavesEnPassant && prev != kInvalidCell) {
                m.flags = static_cast<std::uint8_t>(
                    m.flags | static_cast<std::uint8_t>(MoveFlag::LeavesEnPassant));
                m.epTarget = prev;
                m.epVictim = to;
              }
              appendPromotions(v, m, piece.type, side, toCoord, out);
            } else if (!atom.captureAt.empty()) {
              // Captures at an offset: land empty, take the piece beside the landing cell.
              MoveAtom off;
              off.kind = MoveKind::Exact;
              off.pins = atom.captureAt;
              off.flip = atom.flip;
              auto offDirs = expandAtomFor(off, v.dims.dims(), v.orientationAxis, side);
              const Mark* mk = atom.needsMark >= 0
                                   ? p.markAt(to, static_cast<std::uint16_t>(atom.needsMark))
                                   : nullptr;
              if (!offDirs.empty() &&
                  (atom.needsMark < 0 || (mk != nullptr && mk->owner != static_cast<std::uint8_t>(side)))) {
                std::array<std::int16_t, kMaxDims> v2{};
                for (std::uint8_t a = 0; a < offDirs[0].n; ++a) {
                  v2[a] = static_cast<std::int16_t>(offDirs[0].v[a] * v.dims.pitch(a));
                }
                const auto victimCells = ray(v, to, Direction::make(v2, offDirs[0].n), 1);
                if (!victimCells.empty()) {
                  const Piece victim = p.at(victimCells[0].cell);
                  if (!victim.empty() && victim.colorOf() != side) {
                    appendPromotions(
                        v,
                        Move{.from = from,
                             .to = to,
                             .captureCell = victimCells[0].cell,
                             .flags = static_cast<std::uint8_t>(
                                 MoveFlag::Capture | (atom.needsMark >= 0 ? MoveFlag::EnPassant : MoveFlag::None))},
                        piece.type, side, toCoord, out);
                  }
                }
              }
            } else if (v.enPassant && to == p.epTarget() &&
                       p.epVictim() != kInvalidCell) {
              appendPromotions(v,
                               Move{.from = from,
                                    .to = to,
                                    .captureCell = p.epVictim(),
                                    .flags = MoveFlag::Capture | MoveFlag::EnPassant},
                               piece.type, side, toCoord, out);
            }
          }
          prev = to;
        }
      }
    }
  }

  // Deduplicate: on a glued board the same move can be discovered from two
  // directions of one atom, or by a ray crossing a cell twice.
  if (!v.geom.isBox()) {
    sortMoves(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
  }

  // Castling, checked condition by condition with no shortcuts.
  for (std::size_t i = 0; i < v.castles.size(); ++i) {
    const CastleTemplate& ct = v.castles[i];
    if (ct.color != side) continue;
    if ((p.castleRights() & (1u << ct.rightsBit)) == 0) continue;
    if (p.at(ct.kingFrom).empty() || p.at(ct.rookFrom).empty()) continue;
    if (p.at(ct.kingFrom).colorOf() != side || p.at(ct.rookFrom).colorOf() != side)
      continue;
    bool ok = true;
    for (CellId c : ct.mustBeEmpty) ok = ok && p.at(c).empty();
    for (CellId c : ct.mustBeSafe) ok = ok && !isAttacked(p, c, opponent(side));
    if (!ok) continue;
    out.push_back(Move{.from = ct.kingFrom,
                       .to = ct.kingTo,
                       .flags = static_cast<std::uint8_t>(MoveFlag::Castle),
                       .castleIndex = static_cast<std::uint8_t>(i)});
  }
  // Compound moves (castling and kin), done the long way: for every piece that could be the
  // mover, walk its line cell by cell, find the partner, and test each condition by name.
  for (std::size_t i = 0; i < v.compounds.size(); ++i) {
    const CompoundMove& cm = v.compounds[i];
    auto unit = expandAtomFor(cm.step, v.dims.dims(), v.orientationAxis, side);
    if (unit.empty()) continue;
    std::array<std::int16_t, kMaxDims> scaled{};
    for (std::uint8_t a = 0; a < unit[0].n; ++a) {
      scaled[a] = static_cast<std::int16_t>(unit[0].v[a] * v.dims.pitch(a));
    }
    const Direction dir = Direction::make(scaled, unit[0].n);
    const std::uint32_t steps = exactSteps(cm.step);
    for (CellId from = 0; from < v.dims.cellCount(); ++from) {
      const Piece mover = p.at(from);
      if (mover.empty() || mover.colorOf() != side || mover.type != cm.mover) continue;
      if (cm.moverUnmoved && mover.hasMoved()) continue;
      const auto cells = ray(v, from, dir, v.dims.cellCount());
      CellId partner = kInvalidCell;
      std::size_t at = 0;
      for (; at < cells.size(); ++at) {
        if (!p.at(cells[at].cell).empty()) {
          partner = cells[at].cell;
          break;
        }
      }
      if (partner == kInvalidCell || at < steps) continue;  // partner beyond the landing cell
      const Piece q = p.at(partner);
      if (q.colorOf() != side) continue;
      if (std::find(cm.partners.begin(), cm.partners.end(), q.type) == cm.partners.end()) continue;
      if (cm.partnerUnmoved && q.hasMoved()) continue;
      const CellId dest = cells[steps - 1].cell;
      const CellId crossed = steps >= 2 ? cells[steps - 2].cell : from;
      bool ok = true;
      for (const CompoundMove::SafeCell sc : cm.safe) {
        if (sc == CompoundMove::SafeCell::From) ok = ok && !isAttacked(p, from, opponent(side));
        if (sc == CompoundMove::SafeCell::To) ok = ok && !isAttacked(p, dest, opponent(side));
        if (sc == CompoundMove::SafeCell::Path) {
          for (std::uint32_t k = 0; k + 1 < steps; ++k) {
            ok = ok && !isAttacked(p, cells[k].cell, opponent(side));
          }
        }
      }
      if (!ok) continue;
      out.push_back(Move{.from = from,
                         .to = dest,
                         .flags = static_cast<std::uint8_t>(MoveFlag::Castle),
                         .castleIndex = static_cast<std::uint8_t>(kCompoundBit | i),
                         .partnerFrom = partner,
                         .partnerTo = crossed});
    }
  }
  return out;
}

bool isAttacked(const Position& p, CellId target, Color by) {
  // "Attacked" means: if a piece of the opposing colour stood here, could `by`
  // capture it? That question is independent of what actually occupies the cell -
  // castling safety asks it about empty cells - so a hypothetical enemy is placed
  // there before scanning.
  Position tmp = p;
  const VariantSpec& vs = p.variant();
  const Piece standing = tmp.at(target);
  if (standing.empty() || standing.colorOf() == by) {
    if (!standing.empty()) tmp.removeAt(target);
    tmp.place(target, static_cast<PieceTypeId>(vs.pieces.size() - 1), opponent(by));
  }
  tmp.setSideToMove(by);
  // Castling can never capture, so recursion through pseudoLegal's castle safety
  // check is avoided by scanning atoms directly rather than calling pseudoLegal.
  const VariantSpec& v = p.variant();
  for (CellId from = 0; from < v.dims.cellCount(); ++from) {
    const Piece piece = tmp.at(from);
    if (piece.empty() || piece.colorOf() != by) continue;
    const Coord fromCoord = v.dims.toCoord(from);
    for (const MoveAtom& atom : v.pieces[piece.type].atoms) {
      if (atom.capture == CapturePolicy::Cannot) continue;
      if (!atom.captureAt.empty()) continue;  // takes the piece beside where it lands
      if (atom.unmoved && piece.hasMoved()) continue;
      const Region& gate = atom.fromRegion[static_cast<std::size_t>(by)];
      if (gate.active() && !gate.contains(fromCoord)) continue;
      for (const Direction& dir : directionsFor(v, atom, by)) {
        const auto cells = ray(v, from, dir, atom.maxK);
        bool hurdleFound = false;
        for (std::size_t i = 0; i < cells.size(); ++i) {
          const auto k = static_cast<std::uint32_t>(i + 1);
          const CellId to = cells[i].cell;
          const Piece occupant = tmp.at(to);
          if (atom.mode == MoveMode::Hop) {
            if (!hurdleFound) {
              if (!occupant.empty()) hurdleFound = true;
              continue;
            }
            if (to == target && !occupant.empty() && occupant.colorOf() != by)
              return true;
            break;
          }
          if (!occupant.empty()) {
            if (occupant.colorOf() != by && to == target && k >= atom.minK) return true;
            if (atom.mode == MoveMode::Slide) break;
          }
        }
      }
    }
  }
  return false;
}

std::vector<Move> legal(Position& p) {
  const Color side = p.sideToMove();
  std::vector<Move> out;
  for (const Move& m : pseudoLegal(p)) {
    Undo u;
    p.make(m, u);
    bool exposed = false;
    const CellId royal = p.findRoyal(side);
    if (royal != kInvalidCell) exposed = isAttacked(p, royal, opponent(side));
    p.unmake(m, u);
    if (!exposed) out.push_back(m);
  }
  return out;
}

std::uint64_t perft(Position& p, int depth) {
  if (depth <= 0) return 1;
  std::uint64_t nodes = 0;
  for (const Move& m : legal(p)) {
    Undo u;
    p.make(m, u);
    nodes += depth == 1 ? 1 : perft(p, depth - 1);
    p.unmake(m, u);
  }
  return nodes;
}

void sortMoves(std::vector<Move>& moves) {
  std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
    return std::tie(a.from, a.to, a.captureCell, a.promoteTo, a.flags, a.castleIndex) <
           std::tie(b.from, b.to, b.captureCell, b.promoteTo, b.flags, b.castleIndex);
  });
}

}  // namespace cb::oracle
