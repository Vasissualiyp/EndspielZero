// SPDX-License-Identifier: GPL-3.0-or-later
#include "position/position.hpp"

#include <cassert>

namespace cb {

Position::Position(const VariantSpec& v)
    : v_(&v), cells_(v.dims.cellCount()), occAll_(v.dims.cellCount()) {
  assert(v.finalized() && "a variant must be finalized before a position uses it");
  for (auto& o : occ_) o.reset(v.dims.cellCount());
  pieceFields_.resize(v.pieceFields.size());
  for (std::size_t i = 0; i < pieceFields_.size(); ++i) {
    pieceFields_[i].assign(v.dims.cellCount(), v.pieceFields[i].defaultValue);
  }
  cellFields_.resize(v.cellFields.size());
  for (std::size_t i = 0; i < cellFields_.size(); ++i) {
    cellFields_[i].assign(v.dims.cellCount(), v.cellFields[i].defaultValue);
  }
  side_ = v.startSideToMove;
  // Room for the marks a move or two leaves, so playing never allocates for them.
  marks_.reserve(16);
  markScratch_.reserve(16);
  hash_ = computeHash();
}

Position Position::startPosition(const VariantSpec& v) {
  Position p(v);
  for (const StartPiece& sp : v.start) {
    p.place(v.dims.toCell(sp.at), sp.type, sp.color);
  }
  // Every declared castling right begins available; a loader may narrow it.
  for (const CastleTemplate& ct : v.castles) {
    p.castleRights_ = static_cast<std::uint8_t>(p.castleRights_ | (1u << ct.rightsBit));
  }
  p.side_ = v.startSideToMove;
  p.hash_ = p.computeHash();
  return p;
}

void Position::setPieceField(int index, CellId c, std::int32_t value, Undo* u) {
  auto& col = pieceFields_[static_cast<std::size_t>(index)];
  const std::int32_t old = col[c];
  if (old == value) return;
  if (u != nullptr) {
    u->fieldChanges.push_back(
        FieldChange{false, static_cast<std::uint16_t>(index), c, old});
  }
  const FieldDecl& decl = v_->pieceFields[static_cast<std::size_t>(index)];
  if (decl.hashed) {
    // Only non-default values contribute, so a board with every field at its default
    // hashes exactly like a board with no fields at all.
    if (old != decl.defaultValue)
      hash_ ^= v_->zob.field(static_cast<std::uint32_t>(index), c, old);
    if (value != decl.defaultValue) {
      hash_ ^= v_->zob.field(static_cast<std::uint32_t>(index), c, value);
    }
  }
  col[c] = value;
}

void Position::setCellField(int index, CellId c, std::int32_t value, Undo* u) {
  auto& col = cellFields_[static_cast<std::size_t>(index)];
  const std::int32_t old = col[c];
  if (old == value) return;
  if (u != nullptr) {
    u->fieldChanges.push_back(
        FieldChange{true, static_cast<std::uint16_t>(index), c, old});
  }
  const FieldDecl& decl = v_->cellFields[static_cast<std::size_t>(index)];
  if (decl.hashed) {
    const auto slot = static_cast<std::uint32_t>(v_->pieceFields.size() +
                                                 static_cast<std::size_t>(index));
    if (old != decl.defaultValue) hash_ ^= v_->zob.field(slot, c, old);
    if (value != decl.defaultValue) hash_ ^= v_->zob.field(slot, c, value);
  }
  col[c] = value;
}

void Position::setCell(CellId c, Piece p, Undo* u) {
  if (u != nullptr) u->cellRestores.emplace_back(c, cells_[c]);
  if (!cells_[c].empty()) removePiece(c);
  if (!p.empty()) addPiece(c, p);
}

void Position::clear() {
  for (Piece& c : cells_) c = Piece{};
  for (auto& o : occ_) o.clear();
  occAll_.clear();
  marks_.clear();
  rebuildBlocked();
  castleRights_ = 0;
  halfmove_ = 0;
  fullmove_ = 1;
  hash_ = computeHash();
}

void Position::addPiece(CellId c, Piece p) {
  cells_[c] = p;
  occ_[p.color].set(c);
  occAll_.set(c);
  hash_ ^= v_->zob.piece(c, pieceCode(p.type, p.colorOf()));
  if (p.hasMoved()) hash_ ^= v_->zob.moved(c);
  if (p.flipped()) hash_ ^= v_->zob.flipped(c);
}

void Position::removePiece(CellId c) {
  const Piece p = cells_[c];
  hash_ ^= v_->zob.piece(c, pieceCode(p.type, p.colorOf()));
  if (p.hasMoved()) hash_ ^= v_->zob.moved(c);
  if (p.flipped()) hash_ ^= v_->zob.flipped(c);
  cells_[c] = Piece{};
  occ_[p.color].clearBit(c);
  occAll_.clearBit(c);
}

void Position::place(CellId c, PieceTypeId type, Color color, std::uint8_t flags) {
  if (!cells_[c].empty()) removePiece(c);
  addPiece(c, Piece{type, static_cast<std::uint8_t>(color), flags});
}

void Position::removeAt(CellId c) {
  if (!cells_[c].empty()) removePiece(c);
}

void Position::setSideToMove(Color c) {
  if (c != side_) {
    hash_ ^= v_->zob.side();
    side_ = c;
  }
}

CellId Position::epTarget() const noexcept {
  for (const Mark& m : marks_) {
    if (m.tag == 0) return m.cell;
  }
  return kInvalidCell;
}

CellId Position::epVictim() const noexcept {
  for (const Mark& m : marks_) {
    if (m.tag == 0) return m.victim;
  }
  return kInvalidCell;
}

const Mark* Position::markAt(CellId c, std::uint16_t tag) const noexcept {
  for (const Mark& m : marks_) {
    if (m.cell == c && m.tag == tag) return &m;
  }
  return nullptr;
}

void Position::rebuildBlocked() {
  anyBlocked_ = false;
  if (!v_->hasBlockingMarks) return;
  if (blocked_.size() != cellCount()) blocked_.reset(cellCount());
  blocked_.clear();
  for (const Mark& m : marks_) {
    if (m.tag < v_->markTags.size() && v_->markTags[m.tag].blocks) {
      blocked_.set(m.cell);
      anyBlocked_ = true;
    }
  }
}

void Position::hashMarks(const std::vector<Mark>& marks) {
  for (const Mark& m : marks) hash_ ^= v_->zob.mark(m.cell, m.tag, m.ttl);
}

void Position::addMark(const Mark& m) {
  marks_.push_back(m);
  hash_ ^= v_->zob.mark(m.cell, m.tag, m.ttl);
  rebuildBlocked();
}

void Position::setEpTarget(CellId target, CellId victim) {
  // The tag-0 mark is the en-passant target: replace whatever there was. Set up from FEN
  // or a test, so the side to move is the one who may use it and the opponent made it.
  for (std::size_t i = marks_.size(); i-- > 0;) {
    if (marks_[i].tag == 0) {
      hash_ ^= v_->zob.mark(marks_[i].cell, 0, marks_[i].ttl);
      marks_.erase(marks_.begin() + static_cast<std::ptrdiff_t>(i));
    }
  }
  if (target != kInvalidCell) {
    addMark(Mark{target, 0, 1, static_cast<std::uint8_t>(opponent(side_)), victim});
  }
}

void Position::setCastleRights(std::uint8_t mask) {
  hash_ ^= v_->zob.castleRights(castleRights_);
  castleRights_ = mask;
  hash_ ^= v_->zob.castleRights(castleRights_);
}

void Position::setMoved(CellId c, bool moved) {
  const Piece p = cells_[c];
  if (p.empty() || !v_->pieces[p.type].tracksMoved) return;
  if (p.hasMoved() == moved) return;
  removePiece(c);
  addPiece(c, Piece{p.type, p.color,
                    moved ? static_cast<std::uint8_t>(p.flags | kPieceMoved)
                          : static_cast<std::uint8_t>(p.flags & ~kPieceMoved)});
}

void Position::setClocks(std::int32_t halfmove, std::int32_t fullmove) {
  halfmove_ = halfmove;
  fullmove_ = fullmove;
}

std::uint64_t Position::computeHash() const {
  std::uint64_t h = 0;
  for (std::size_t f = 0; f < pieceFields_.size(); ++f) {
    if (!v_->pieceFields[f].hashed) continue;
    for (CellId c = 0; c < cells_.size(); ++c) {
      if (pieceFields_[f][c] != v_->pieceFields[f].defaultValue) {
        h ^= v_->zob.field(static_cast<std::uint32_t>(f), c, pieceFields_[f][c]);
      }
    }
  }
  for (std::size_t f = 0; f < cellFields_.size(); ++f) {
    if (!v_->cellFields[f].hashed) continue;
    const auto slot = static_cast<std::uint32_t>(pieceFields_.size() + f);
    for (CellId c = 0; c < cells_.size(); ++c) {
      if (cellFields_[f][c] != v_->cellFields[f].defaultValue) {
        h ^= v_->zob.field(slot, c, cellFields_[f][c]);
      }
    }
  }
  for (CellId c = 0; c < cells_.size(); ++c) {
    const Piece p = cells_[c];
    if (p.empty()) continue;
    h ^= v_->zob.piece(c, pieceCode(p.type, p.colorOf()));
    if (p.hasMoved()) h ^= v_->zob.moved(c);
    if (p.flipped()) h ^= v_->zob.flipped(c);
  }
  if (side_ == Color::Black) h ^= v_->zob.side();
  for (const Mark& m : marks_) h ^= v_->zob.mark(m.cell, m.tag, m.ttl);
  h ^= v_->zob.castleRights(castleRights_);
  return h;
}

void Position::make(const Move& m, Undo& u) {
  u.captured = Piece{};
  u.marksInlineCount = 0;
  u.marksSpilled = false;
  u.castleRightsBefore = castleRights_;
  u.halfmoveBefore = halfmove_;
  u.hashBefore = hash_;

  const Piece mover = cells_[m.from];
  assert(!mover.empty() && "moving from an empty cell");

  bool resetClock = false;
  if (m.captureCell != kInvalidCell) {
    u.captured = cells_[m.captureCell];
    assert(!u.captured.empty() && "capturing an empty cell");
    removePiece(m.captureCell);
    resetClock = true;
  }

  // A piece's fields travel with it, so they move from the origin to the destination and
  // the origin returns to its default. Recorded so unmake puts them back.
  for (std::size_t f = 0; f < pieceFields_.size(); ++f) {
    const std::int32_t carried = pieceFields_[f][m.from];
    if (carried != v_->pieceFields[f].defaultValue ||
        pieceFields_[f][m.to] != v_->pieceFields[f].defaultValue) {
      setPieceField(static_cast<int>(f), m.to, carried, &u);
      setPieceField(static_cast<int>(f), m.from, v_->pieceFields[f].defaultValue, &u);
    }
  }

  removePiece(m.from);
  const PieceTypeId landing =
      has(m.flags, MoveFlag::Promotion) ? m.promoteTo : mover.type;
  // The mover has now moved, if anything about its type cares.
  u.moverFlags = mover.flags;
  std::uint8_t landedFlags =
      v_->pieces[landing].tracksMoved
          ? static_cast<std::uint8_t>(mover.flags | kPieceMoved)
          : static_cast<std::uint8_t>(mover.flags & ~kPieceMoved);
  if (has(m.flags, MoveFlag::Reorient)) landedFlags ^= kPieceFlipped;
  // Only a piece that reorients carries the bit: a pawn promoting into a queen sheds
  // it, so two otherwise identical queens never hash differently.
  if (!v_->pieces[landing].reorients) {
    landedFlags = static_cast<std::uint8_t>(landedFlags & ~kPieceFlipped);
  }
  addPiece(m.to, Piece{landing, mover.color, landedFlags});

  // A two-piece move (castling, any compound move): the partner is carried too, and has moved.
  if (m.partnerFrom != kInvalidCell) {
    const Piece partner = cells_[m.partnerFrom];
    u.partnerFlags = partner.flags;
    removePiece(m.partnerFrom);
    addPiece(m.partnerTo, Piece{partner.type, partner.color,
                                v_->pieces[partner.type].tracksMoved
                                    ? static_cast<std::uint8_t>(partner.flags | kPieceMoved)
                                    : partner.flags});
  }

  if (v_->pieces[mover.type].resetsDrawClock) resetClock = true;

  // Castling rights are lost when the king or the rook leaves its post, or when
  // the rook is captured on it. Expressed against the templates, so a variant
  // with different castling loses rights correctly with no extra code.
  std::uint8_t rights = castleRights_;
  for (const CastleTemplate& ct : v_->castles) {
    const bool touched = m.from == ct.kingFrom || m.from == ct.rookFrom ||
                         m.to == ct.rookFrom || m.captureCell == ct.rookFrom;
    if (touched) rights = static_cast<std::uint8_t>(rights & ~(1u << ct.rightsBit));
  }
  if (rights != castleRights_) setCastleRights(rights);

  // Marks: the ones already on the board age by a ply (and the last of them goes), then this
  // move's own are left. En passant is the tag-0 mark; the move that made it is the mover's.
  const bool leavesMarks =
      has(m.flags, MoveFlag::LeavesEnPassant) ||
      (m.atomIndex != 0xFF && !v_->pieces[mover.type].atoms[m.atomIndex].marks.empty());
  if (!marks_.empty() || leavesMarks) {
    u.saveMarks(marks_);
    hashMarks(marks_);
    std::vector<Mark>& kept = markScratch_;
    kept.clear();
    for (Mark mk : marks_) {
      if (mk.ttl != 0) {
        --mk.ttl;
        if (mk.ttl == 0) continue;
      }
      kept.push_back(mk);
    }
    if (has(m.flags, MoveFlag::LeavesEnPassant) && m.epTarget != kInvalidCell) {
      kept.push_back(Mark{m.epTarget, 0, 1, static_cast<std::uint8_t>(side_), m.epVictim});
    }
    if (m.atomIndex != 0xFF) {
      for (const MarkSpec& spec : v_->pieces[mover.type].atoms[m.atomIndex].marks) {
        const auto leave = [&](CellId cell) {
          if (cell == kInvalidCell) return;
          kept.push_back(Mark{cell, spec.tag, static_cast<std::uint16_t>(spec.duration),
                              static_cast<std::uint8_t>(side_), kInvalidCell});
        };
        switch (spec.where) {
          case MarkWhere::Passed:
            leave(m.epTarget);
            break;
          case MarkWhere::Landed:
            leave(m.to);
            break;
          case MarkWhere::Around: {
            const Coord centre = v_->dims.toCoord(m.to);
            for (CellId c = 0; c < v_->dims.cellCount(); ++c) {
              if (c == m.to) continue;
              const Coord at = v_->dims.toCoord(c);
              bool near = true;
              for (std::uint8_t a = 0; a < v_->dims.dims() && near; ++a) {
                const int dd = at.c[a] - centre.c[a];
                near = (dd < 0 ? -dd : dd) <= static_cast<int>(spec.radius);
              }
              if (near) leave(c);
            }
            break;
          }
        }
      }
    }
    marks_.swap(kept);
    hashMarks(marks_);
    rebuildBlocked();
  }

  halfmove_ = resetClock ? 0 : halfmove_ + 1;
  if (side_ == Color::Black) ++fullmove_;
  setSideToMove(opponent(side_));
}

void Position::unmake(const Move& m, const Undo& u) {
  // Rule effects and field changes are undone first, in reverse, so the board is back
  // to what the plain move left before the move itself is reversed.
  for (std::size_t i = u.cellRestores.size(); i-- > 0;) {
    const auto& [cell, piece] = u.cellRestores[i];
    if (!cells_[cell].empty()) removePiece(cell);
    if (!piece.empty()) addPiece(cell, piece);
  }
  for (std::size_t i = u.fieldChanges.size(); i-- > 0;) {
    const FieldChange& fc = u.fieldChanges[i];
    if (fc.cellField) {
      cellFields_[fc.index][fc.cell] = fc.oldValue;
    } else {
      pieceFields_[fc.index][fc.cell] = fc.oldValue;
    }
  }

  setSideToMove(opponent(side_));
  if (side_ == Color::Black) --fullmove_;

  if (m.partnerFrom != kInvalidCell) {
    const Piece partner = cells_[m.partnerTo];
    removePiece(m.partnerTo);
    addPiece(m.partnerFrom, Piece{partner.type, partner.color, u.partnerFlags});
  }

  const Piece landed = cells_[m.to];
  removePiece(m.to);
  const PieceTypeId original = has(m.flags, MoveFlag::Promotion) ? [&] {
    // The mover was whatever promotes into m.promoteTo; in every variant
    // so far that is unique per colour, and the move records enough.
    for (std::size_t i = 1; i < v_->pieces.size(); ++i) {
      for (PieceTypeId to : v_->pieces[i].promotesTo) {
        if (to == m.promoteTo) return static_cast<PieceTypeId>(i);
      }
    }
    return landed.type;
  }()
                                                                 : landed.type;
  addPiece(m.from, Piece{original, landed.color, u.moverFlags});

  if (m.captureCell != kInvalidCell) addPiece(m.captureCell, u.captured);

  // Restoring these directly (rather than re-deriving) is what makes unmake
  // exact: the hash is restored, not recomputed, so a mismatch is detectable.
  if (u.hadMarks() || !marks_.empty()) {
    u.loadMarks(marks_);
    rebuildBlocked();
  }
  castleRights_ = u.castleRightsBefore;
  halfmove_ = u.halfmoveBefore;
  hash_ = u.hashBefore;
}

CellId Position::findRoyal(Color c) const {
  CellId found = kInvalidCell;
  occ_[static_cast<std::size_t>(c)].forEach([&](std::size_t i) {
    if (found == kInvalidCell && v_->pieces[cells_[i].type].royal) {
      found = static_cast<CellId>(i);
    }
  });
  return found;
}

bool Position::validate(std::string* why) const {
  const auto bad = [&](std::string msg) {
    if (why != nullptr) *why = std::move(msg);
    return false;
  };
  BitWords all(cellCount());
  BitWords per[kNumColors]{BitWords(cellCount()), BitWords(cellCount())};
  for (CellId c = 0; c < cells_.size(); ++c) {
    const Piece p = cells_[c];
    if (p.empty()) continue;
    if (p.type >= v_->pieces.size())
      return bad("cell " + std::to_string(c) + " holds an unknown piece type");
    if (p.color >= kNumColors)
      return bad("cell " + std::to_string(c) + " holds an unknown colour");
    all.set(c);
    per[p.color].set(c);
  }
  if (!(all == occAll_)) return bad("occupancy disagrees with the cell array");
  for (int i = 0; i < kNumColors; ++i) {
    if (!(per[i] == occ_[i]))
      return bad("colour occupancy disagrees with the cell array");
  }
  if (hash_ != computeHash())
    return bad("incremental hash disagrees with a fresh computation");
  return true;
}

}  // namespace cb
