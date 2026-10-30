// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/fen.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <vector>

#include "io/fen_order.hpp"
#include "io/notation.hpp"

namespace cb {
namespace {

/// Whether a piece of this kind stands where the variant's start position put one. That is
/// FEN-N's default for "has it moved": a pawn on its home square is unmoved without anyone
/// saying so, which is exactly what a standard FEN assumes.
bool startsHere(const VariantSpec& v, CellId cell, PieceTypeId type, std::uint8_t color) {
  for (const StartPiece& sp : v.start) {
    if (v.dims.toCell(sp.at) == cell && sp.type == type &&
        static_cast<std::uint8_t>(sp.color) == color) {
      return true;
    }
  }
  return false;
}

/// The partner that holds a compound move's *right*: walking the mover's line, the first
/// piece of the side that is of a partner kind (and unmoved, when required), whatever stands
/// between - a queen on d1 does not take away the right to castle, only the move for now.
/// FEN's castling field is exactly this, per letter.
CellId compoundPartner(const Position& p, const CompoundMove& cm, Color side, CellId from) {
  const VariantSpec& v = p.variant();
  const Piece piece = p.at(from);
  if (piece.empty() || piece.type != cm.mover || piece.colorOf() != side) return kInvalidCell;
  if (cm.moverUnmoved && piece.hasMoved()) return kInvalidCell;
  const Direction dir = v.dirTable[cm.unitDir[static_cast<std::size_t>(side)]];
  Walker w = v.geom.start(from, dir);
  std::uint32_t k = 0;
  while (k++ < v.dims.cellCount() && v.geom.step(w)) {
    if (w.cell == from) return kInvalidCell;
    const Piece other = p.at(w.cell);
    if (other.empty() || other.colorOf() != side) continue;
    if (std::find(cm.partners.begin(), cm.partners.end(), other.type) == cm.partners.end()) continue;
    if (k <= cm.steps) continue;
    if (cm.partnerUnmoved && other.hasMoved()) continue;
    return w.cell;
  }
  return kInvalidCell;
}

/// Does some piece of `side` hold this compound move's right?
CellId compoundRight(const Position& p, const CompoundMove& cm, Color side) {
  for (CellId c = 0; c < p.cellCount(); ++c) {
    if (const CellId partner = compoundPartner(p, cm, side, c); partner != kInvalidCell) return partner;
  }
  return kInvalidCell;
}

}  // namespace

std::string toFen(const Position& p) {
  const VariantSpec& v = p.variant();
  const DimSpec& d = v.dims;

  std::string board;
  int run = 0;
  const auto flush = [&] {
    if (run > 0) {
      board += std::to_string(run);
      run = 0;
    }
  };
  forEachInFenOrder(
      d,
      [&](CellId c) {
        const Piece piece = p.at(c);
        if (piece.empty()) {
          ++run;
          return;
        }
        flush();
        const char sym = v.pieces[piece.type].symbol;
        board += piece.colorOf() == Color::White ? static_cast<char>(std::toupper(sym))
                                                 : static_cast<char>(std::tolower(sym));
        if (piece.flipped())
          board += '\'';  // forward reversed by a seam (Piece::flipped)
      },
      [&](std::uint8_t axis) {
        if (axis >= d.dims()) return;  // the final rollover writes no separator
        flush();
        board += fenSeparatorFor(axis);
      });
  flush();

  std::string castling;
  for (const CastleTemplate& ct : v.castles) {
    if ((p.castleRights() & (1u << ct.rightsBit)) != 0) castling += ct.name;
  }
  // The compound moves' letters, White's then Black's, in declaration order: KQkq.
  for (const Color side : {Color::White, Color::Black}) {
    for (const CompoundMove& cm : v.compounds) {
      if (cm.fenLetter == 0 || compoundRight(p, cm, side) == kInvalidCell) continue;
      castling += side == Color::White ? static_cast<char>(std::toupper(cm.fenLetter))
                                       : static_cast<char>(std::tolower(cm.fenLetter));
    }
  }
  if (castling.empty()) castling = "-";

  std::ostringstream out;
  out << board << ' ' << (p.sideToMove() == Color::White ? 'w' : 'b') << ' ' << castling
      << ' ' << cellName(d, p.epTarget()) << ' ' << p.halfmoveClock() << ' '
      << p.fullmoveNumber();

  // The unmoved pieces, only when reading this FEN *without* the field would not give them
  // back: a game that has gone the usual way writes the usual FEN, so nothing existing
  // changes. The reader's own default is the yardstick, so the two cannot disagree.
  std::string unmoved;
  for (CellId c = 0; c < d.cellCount(); ++c) {
    const Piece piece = p.at(c);
    if (piece.empty() || !v.pieces[piece.type].tracksMoved || piece.hasMoved()) continue;
    if (!unmoved.empty()) unmoved += ',';
    unmoved += cellName(d, c);
  }
  bool unusual = false;
  if (const auto base = fromFen(v, out.str()); base.has_value()) {
    for (CellId c = 0; c < d.cellCount() && !unusual; ++c) {
      const Piece piece = p.at(c);
      if (piece.empty() || !v.pieces[piece.type].tracksMoved) continue;
      unusual = base->at(c).hasMoved() != piece.hasMoved();
    }
  }
  // Marks other than the en-passant target (which has its own field): `tag:cell:plies:side`,
  // "plies" being 0 for a mark that lasts forever. They need the field before them written.
  std::string marks;
  for (const Mark& m : p.marks()) {
    if (m.tag == 0) continue;
    if (!marks.empty()) marks += ',';
    marks += v.markTags[m.tag].name + ":" + cellName(d, m.cell) + ":" + std::to_string(m.ttl) +
             ":" + (m.owner == 0 ? "w" : "b");
  }
  if (unusual || !marks.empty()) out << ' ' << (unmoved.empty() ? "-" : unmoved);
  if (!marks.empty()) out << ' ' << marks;
  return out.str();
}

Result<Position> fromFen(const VariantSpec& v, std::string_view fen) {
  std::vector<std::string> fields;
  {
    const std::string fenText(fen);
    std::istringstream in{fenText};
    std::string f;
    while (in >> f) fields.push_back(f);
  }
  if (fields.size() < 2) {
    return fail(ErrorCode::ParseError,
                "a FEN needs at least a board and a side to move; got " +
                    std::to_string(fields.size()) + " field(s)");
  }

  const DimSpec& d = v.dims;
  Position p(v);

  // ---- board ---------------------------------------------------------------
  std::vector<CellId> order;
  order.reserve(d.cellCount());
  forEachInFenOrder(d, [&](CellId c) { order.push_back(c); }, [](std::uint8_t) {});

  std::size_t idx = 0;
  const std::string& board = fields[0];
  for (std::size_t i = 0; i < board.size(); ++i) {
    const char ch = board[i];
    if (ch == '/' || ch == '|') continue;  // separators carry no cells
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      std::size_t j = i;
      while (j < board.size() && std::isdigit(static_cast<unsigned char>(board[j])) != 0)
        ++j;
      int skip = 0;
      std::from_chars(board.data() + i, board.data() + j, skip);
      idx += static_cast<std::size_t>(skip);
      i = j - 1;
      continue;
    }
    const char upper = static_cast<char>(std::toupper(ch));
    const PieceTypeId type = v.findPieceBySymbol(upper);
    if (type == kNoPiece) {
      return fail(ErrorCode::ParseError, std::string("no piece in variant '") + v.name +
                                             "' has the symbol '" + ch + "'");
    }
    if (idx >= order.size()) {
      return fail(ErrorCode::ParseError,
                  "the board section describes more cells than the board has (" +
                      std::to_string(d.cellCount()) + ")");
    }
    const Color color =
        std::isupper(static_cast<unsigned char>(ch)) != 0 ? Color::White : Color::Black;
    std::uint8_t flags = 0;
    if (i + 1 < board.size() && board[i + 1] == '\'') {
      if (!v.pieces[type].reorients) {
        return fail(
            ErrorCode::ParseError,
            std::string("'") + ch +
                "'' marks a flipped piece, but this piece cannot flip on this board");
      }
      flags = kPieceFlipped;
      ++i;
    }
    p.place(order[idx], type, color, flags);
    ++idx;
  }
  if (idx != order.size()) {
    return fail(ErrorCode::ParseError,
                "the board section describes " + std::to_string(idx) +
                    " cells but the board has " + std::to_string(order.size()));
  }

  // ---- side to move --------------------------------------------------------
  if (fields[1] == "w") {
    p.setSideToMove(Color::White);
  } else if (fields[1] == "b") {
    p.setSideToMove(Color::Black);
  } else {
    return fail(ErrorCode::ParseError,
                "side to move must be 'w' or 'b', not '" + fields[1] + "'");
  }

  // ---- castling rights -----------------------------------------------------
  if (fields.size() > 2 && fields[2] != "-") {
    std::uint8_t mask = 0;
    for (char ch : fields[2]) {
      bool found = false;
      for (const CastleTemplate& ct : v.castles) {
        if (ct.name.size() == 1 && ct.name[0] == ch) {
          mask = static_cast<std::uint8_t>(mask | (1u << ct.rightsBit));
          found = true;
          break;
        }
      }
      for (const CompoundMove& cm : v.compounds) {
        if (cm.fenLetter != 0 && std::toupper(cm.fenLetter) == std::toupper(ch)) found = true;
      }
      if (!found) {
        return fail(ErrorCode::ParseError, std::string("castling field mentions '") + ch +
                                               "', which this variant does not declare");
      }
    }
    p.setCastleRights(mask);
  }

  // ---- en passant ----------------------------------------------------------
  if (fields.size() > 3 && fields[3] != "-") {
    const auto target = parseCell(d, fields[3]);
    if (!target.has_value()) return fail(target.error().code, target.error().message);
    // The victim is the piece that passed through the target: one step against the
    // mover's forward direction. Found through the geometry so that a wrapped
    // board gives the right answer rather than an arithmetic one.
    if (v.orientationAxis < 0) {
      return fail(
          ErrorCode::ValidationError,
          "this variant has no orientation axis, so an en-passant target is meaningless");
    }
    std::array<std::int16_t, kMaxDims> back{};
    back[static_cast<std::size_t>(v.orientationAxis)] =
        p.sideToMove() == Color::White ? -1 : 1;
    Walker w = v.geom.start(*target, Direction::make(back, d.dims()));
    if (!v.geom.step(w)) {
      return fail(ErrorCode::ValidationError,
                  "en-passant target " + fields[3] + " has no cell behind it");
    }
    p.setEpTarget(*target, w.cell);
  }

  // ---- clocks --------------------------------------------------------------
  int halfmove = 0;
  int fullmove = 1;
  if (fields.size() > 4)
    std::from_chars(fields[4].data(), fields[4].data() + fields[4].size(), halfmove);
  if (fields.size() > 5)
    std::from_chars(fields[5].data(), fields[5].data() + fields[5].size(), fullmove);
  p.setClocks(halfmove, fullmove);

  // ---- which pieces have moved ------------------------------------------------
  // Default: a piece has moved unless it stands where the start position put one. An
  // explicit field (cells, or "-") overrides that - it is how a position says exactly.
  for (CellId c = 0; c < d.cellCount(); ++c) {
    const Piece piece = p.at(c);
    if (piece.empty() || !v.pieces[piece.type].tracksMoved) continue;
    p.setMoved(c, !startsHere(v, c, piece.type, piece.color));
  }
  if (fields.size() > 6) {
    for (CellId c = 0; c < d.cellCount(); ++c) p.setMoved(c, true);
    if (fields[6] != "-") {
      std::string cells = fields[6];
      std::size_t at = 0;
      while (at <= cells.size()) {
        const std::size_t comma = std::min(cells.find(',', at), cells.size());
        const auto cell = parseCell(d, cells.substr(at, comma - at));
        if (!cell.has_value()) return fail(cell.error().code, cell.error().message);
        p.setMoved(*cell, false);
        at = comma + 1;
      }
    }
  }

  // ---- compound moves' rights --------------------------------------------------
  // A letter the castling field leaves out is a right given up: the partner has moved, even
  // if it still stands where it began. (A letter present asks nothing more: the pieces being
  // unmoved is what holds the right.)
  if (!v.compounds.empty() && fields.size() > 2) {
    for (const Color side : {Color::White, Color::Black}) {
      for (const CompoundMove& cm : v.compounds) {
        if (cm.fenLetter == 0) continue;
        const char letter = side == Color::White ? static_cast<char>(std::toupper(cm.fenLetter))
                                                  : static_cast<char>(std::tolower(cm.fenLetter));
        if (fields[2].find(letter) != std::string::npos) continue;
        for (CellId c = 0; c < d.cellCount(); ++c) {
          if (const CellId partner = compoundPartner(p, cm, side, c); partner != kInvalidCell) {
            p.setMoved(partner, true);
          }
        }
      }
    }
  }

  // ---- marks -------------------------------------------------------------------
  if (fields.size() > 7 && fields[7] != "-") {
    std::size_t at = 0;
    const std::string& list = fields[7];
    while (at <= list.size()) {
      const std::size_t comma = std::min(list.find(',', at), list.size());
      const std::string item = list.substr(at, comma - at);
      std::vector<std::string> parts;
      std::size_t q = 0;
      while (q <= item.size()) {
        const std::size_t colon = std::min(item.find(':', q), item.size());
        parts.push_back(item.substr(q, colon - q));
        q = colon + 1;
      }
      if (parts.size() != 4) {
        return fail(ErrorCode::ParseError, "a mark is written tag:cell:plies:side, not '" + item + "'");
      }
      const int tag = v.findMarkTag(parts[0]);
      if (tag < 0) return fail(ErrorCode::ParseError, "no mark tag is called '" + parts[0] + "'");
      const auto cell = parseCell(d, parts[1]);
      if (!cell.has_value()) return fail(cell.error().code, cell.error().message);
      p.addMark(Mark{*cell, static_cast<std::uint16_t>(tag),
                     static_cast<std::uint16_t>(std::strtol(parts[2].c_str(), nullptr, 10)),
                     static_cast<std::uint8_t>(parts[3] == "b" ? 1 : 0), kInvalidCell});
      at = comma + 1;
    }
  }

  return p;
}

}  // namespace cb
