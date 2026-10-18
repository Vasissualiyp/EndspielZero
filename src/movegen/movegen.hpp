// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "movegen/move_list.hpp"
#include "position/position.hpp"

namespace cb {

/// Move generation over the general lattice. Nothing here knows what a pawn is:
/// it walks atoms, and the atoms come from the variant (ARCH section 7).
class MoveGen {
 public:
  explicit MoveGen(const VariantSpec& v);

  /// One move buffer per search depth, reserved once. Without this, a recursive
  /// walk would allocate a buffer per node, which both violates the
  /// no-allocation invariant and dominates the runtime.
  class Buffers {
   public:
    void ensure(int depth, std::uint32_t capacity) {
      while (static_cast<int>(lists_.size()) <= depth) lists_.emplace_back();
      for (MoveList& l : lists_) {
        if (l.capacity() < capacity) l.reserve(capacity);
      }
    }
    MoveList& at(int depth) { return lists_[static_cast<std::size_t>(depth)]; }

   private:
    std::vector<MoveList> lists_;
  };

  /// Every move the atoms permit, before checking whether it leaves a royal piece
  /// attacked.
  void generatePseudoLegal(const Position& p, MoveList& out) const;

  /// As above, but for a nominated side regardless of whose turn it is, and with
  /// the option of skipping castling. Used by the attack scan, which must not
  /// recurse into castling's own safety check.
  void generatePseudoLegalFor(const Position& p, Color side, bool includeCastles,
                              MoveList& out) const;

  /// Pseudo-legal moves filtered by royal safety. Needs a mutable position
  /// because legality is decided by make/unmake.
  void generateLegal(Position& p, MoveList& out) const;

  /// Is `target` attacked by any piece of `by`?
  ///
  /// "Attacked" means: if a piece of the opposing colour stood on that cell, could
  /// `by` capture it? The question is deliberately independent of what actually
  /// occupies the cell, because castling safety asks it about empty cells.
  ///
  /// On a box board this walks outward from the target along the reverse of each
  /// attacking direction, so the cost is proportional to the atoms that exist
  /// rather than to the pieces on the board. On a board with glued faces it falls
  /// back to scanning `by`'s moves forward - see isAttackedByForwardScan for why
  /// the backward walk is not valid there. Both paths are differentially tested
  /// against the naive oracle (ADR-0009).
  [[nodiscard]] bool isAttacked(const Position& p, CellId target, Color by) const;

  /// Is any royal piece of `c` attacked? False for variants with no royals.
  [[nodiscard]] bool inCheck(const Position& p, Color c) const;

  /// Would this pseudo-legal move leave the mover's royal piece attacked?
  [[nodiscard]] bool leavesRoyalExposed(Position& p, const Move& m) const;

  [[nodiscard]] std::uint64_t perft(Position& p, int depth) const;
  [[nodiscard]] std::uint64_t perft(Position& p, int depth, Buffers& bufs) const;

  /// perft split by first move, which is how a node-count mismatch is bisected
  /// down to a single move (see the cb-perft-golden skill).
  struct DivideEntry {
    Move move;
    std::uint64_t nodes;
  };
  [[nodiscard]] std::vector<DivideEntry> perftDivide(Position& p, int depth) const;

 private:
  void generateForPiece(const Position& p, CellId from, MoveList& out) const;
  void generateAtom(const Position& p, CellId from, const Coord& fromCoord, Color side,
                    PieceTypeId type, const MoveAtom& atom, MoveList& out) const;
  void emit(const Position& p, Move m, PieceTypeId type, Color side, const Coord& toCoord,
            MoveList& out) const;
  void generateCastles(const Position& p, Color side, MoveList& out) const;
  /// The variant's compound moves (castling and kin): the mover's step, the partner found
  /// along it, the cells that must not be attacked.
  void generateCompounds(const Position& p, Color side, MoveList& out) const;

  /// The slow, always-correct attack test: generate everything `by` could do and
  /// look for a capture of the target.
  ///
  /// Required whenever the board has glued faces. The backward walk assumes that
  /// retracing a ray from its destination returns along the same path, and on some
  /// gluings that is false: when one step leaves the box on two axes at once, the
  /// two seam transforms are applied in a fixed axis order, and for gluings whose
  /// transforms do not commute the reverse crossing folds differently. A torus or a
  /// Klein bottle is fine - their corner folds agree either way - but the general
  /// case is not, so correctness wins here and the fast path is kept for boxes,
  /// which is every variant through M2 and the whole of standard chess.
  ///
  /// Making the fold provably order-independent (or rejecting gluings where it is
  /// not) would let the fast path run everywhere; that is M3 follow-up work, and
  /// tests/unit/geometry documents the ambiguity so it cannot be forgotten.
  [[nodiscard]] bool isAttackedByForwardScan(const Position& p, CellId target,
                                             Color by) const;

  /// A ray ends when it returns to the state it started in. The step map is a
  /// bijection on (cell, direction), so every orbit is a cycle through the start -
  /// and being a cycle is exactly what makes forward and backward walks cover the
  /// same cells.
  [[nodiscard]] static bool rayClosed(const Walker& w, CellId from,
                                      const Direction& startDir) {
    return w.cell == from && w.dir == startDir;
  }

  const VariantSpec* v_;
  /// True if any piece type hops. Hoppers cannot be detected by the reverse walk
  /// (their threat depends on a hurdle), so such a variant uses the forward scan
  /// for the whole query rather than per atom.
  bool hasHoppers_{false};
  /// Hard bound on one ray's length, used for unlimited sliders. An orbit can be
  /// longer than the board has cells - a rook on a Moebius band needs two circuits
  /// to come home - so this is generous; blockers stop almost every real ray first.
  std::uint32_t rayCap_{0};
};

}  // namespace cb
