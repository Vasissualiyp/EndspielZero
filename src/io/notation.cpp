// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/notation.hpp"

#include <cctype>
#include <charconv>

#include <vector>
#include "movegen/movegen.hpp"

namespace cb {
namespace {

bool algebraic(const DimSpec& d) {
  return d.dims() == 2 && d.extent(0) <= 26 && d.extent(1) <= 9;
}

}  // namespace

std::string cellName(const DimSpec& d, CellId c) {
  if (c == kInvalidCell) return "-";
  const Coord p = d.toCoord(c);
  if (algebraic(d)) {
    std::string s;
    s += static_cast<char>('a' + p.c[0]);
    s += static_cast<char>('1' + p.c[1]);
    return s;
  }
  return p.toString();
}

Result<CellId> parseCell(const DimSpec& d, std::string_view text) {
  if (text.empty()) return fail(ErrorCode::ParseError, "empty cell name");

  if (text.front() == '(') {
    if (text.back() != ')') {
      return fail(ErrorCode::ParseError,
                  "cell '" + std::string(text) + "' is missing its ')'");
    }
    Coord p(d.dims());
    std::size_t axis = 0;
    std::size_t i = 1;
    while (i < text.size() - 1) {
      std::size_t j = text.find(',', i);
      if (j == std::string_view::npos || j > text.size() - 1) j = text.size() - 1;
      int value = 0;
      const auto* first = text.data() + i;
      const auto* last = text.data() + j;
      const auto res = std::from_chars(first, last, value);
      if (res.ec != std::errc{} || res.ptr != last) {
        return fail(ErrorCode::ParseError,
                    "cell '" + std::string(text) + "' has a non-numeric coordinate");
      }
      if (axis >= d.dims()) {
        return fail(ErrorCode::ParseError,
                    "cell '" + std::string(text) +
                        "' has more coordinates than the board has axes");
      }
      p.c[axis++] = static_cast<std::int16_t>(value);
      i = j + 1;
    }
    if (axis != d.dims()) {
      return fail(ErrorCode::ParseError, "cell '" + std::string(text) + "' needs " +
                                             std::to_string(d.dims()) + " coordinates");
    }
    if (!d.inRange(p)) {
      return fail(ErrorCode::OutOfRange, "cell " + p.toString() + " is off the board");
    }
    return d.toCell(p);
  }

  if (!algebraic(d)) {
    return fail(
        ErrorCode::ParseError,
        "this board needs the '(x,y,...)' cell form, not '" + std::string(text) + "'");
  }
  if (text.size() != 2 || text[0] < 'a' || text[1] < '1') {
    return fail(ErrorCode::ParseError, "'" + std::string(text) + "' is not a cell name");
  }
  Coord p(2);
  p.c[0] = static_cast<std::int16_t>(text[0] - 'a');
  p.c[1] = static_cast<std::int16_t>(text[1] - '1');
  if (!d.inRange(p)) {
    return fail(ErrorCode::OutOfRange,
                "cell '" + std::string(text) + "' is off the board");
  }
  return d.toCell(p);
}

std::string moveText(const VariantSpec& v, const Move& m) {
  std::string s = cellName(v.dims, m.from);
  const bool alg = v.dims.dims() == 2 && v.dims.extent(0) <= 26 && v.dims.extent(1) <= 9;
  if (!alg) s += '-';
  s += cellName(v.dims, m.to);
  if (has(m.flags, MoveFlag::Promotion) && m.promoteTo != kNoPiece) {
    s += static_cast<char>(std::tolower(v.pieces[m.promoteTo].symbol));
  }
  return s;
}

std::string sanText(const VariantSpec& v, const Position& pos, const Move& m,
                    const MoveList& legal) {
  const DimSpec& d = v.dims;
  const bool algebraic = d.dims() == 2 && d.extent(0) <= 26 && d.extent(1) <= 9;

  if (m.isCastle() && (m.castleIndex & kCompoundBit) != 0) {
    // A compound move: castling toward the higher files is O-O, toward the lower O-O-O, and
    // anything else (another axis, another piece) is its own name.
    const CompoundMove& cm = v.compounds[static_cast<std::size_t>(m.castleIndex & 0x7Fu)];
    const Coord from = d.toCoord(m.from);
    const Coord to = d.toCoord(m.to);
    if (d.dims() >= 1 && from.c[1 % d.dims()] == to.c[1 % d.dims()] && d.dims() >= 2) {
      return to.c[0] > from.c[0] ? "O-O" : "O-O-O";
    }
    return cm.name;
  }
  if (m.isCastle() && !v.castles.empty()) {
    const CastleTemplate& ct = v.castles[m.castleIndex];
    const bool kingSide = d.toCoord(ct.kingTo).c[0] > d.toCoord(ct.kingFrom).c[0];
    return kingSide ? "O-O" : "O-O-O";
  }
  if (!algebraic) return moveText(v, m);

  const Piece piece = pos.at(m.from);
  const char symbol = static_cast<char>(std::toupper(v.pieces[piece.type].symbol));
  const bool pawn = symbol == 'P';
  const bool capture = m.isCapture();

  std::string out;
  if (pawn) {
    if (capture) out += static_cast<char>('a' + d.toCoord(m.from).c[0]);
  } else {
    out += symbol;
    bool other = false;
    bool sameFile = false;
    bool sameRank = false;
    const Coord from = d.toCoord(m.from);
    for (const Move& alt : legal) {
      if (alt.to != m.to || alt.from == m.from) continue;
      if (pos.at(alt.from).type != piece.type) continue;
      other = true;
      const Coord a = d.toCoord(alt.from);
      if (a.c[0] == from.c[0]) sameFile = true;
      if (a.c[1] == from.c[1]) sameRank = true;
    }
    if (other) {
      if (!sameFile)
        out += static_cast<char>('a' + from.c[0]);
      else if (!sameRank)
        out += static_cast<char>('1' + from.c[1]);
      else
        out += cellName(d, m.from);
    }
  }
  if (capture) out += 'x';
  out += cellName(d, m.to);
  if (m.promoteTo != kNoPiece) {
    out += '=';
    out += static_cast<char>(std::toupper(v.pieces[m.promoteTo].symbol));
  }

  // The check suffix, on a board where "the position after this move" means something.
  bool temporal = false;
  for (std::uint8_t a = 0; a < d.dims(); ++a) {
    if (d.kind(a) != AxisKind::Spatial) temporal = true;
  }
  if (!temporal) {
    Position after = pos;
    Undo u;
    after.make(m, u);
    MoveGen gen(v);
    if (gen.inCheck(after, opponent(pos.sideToMove()))) {
      MoveList replies(v.moveUpperBound());
      gen.generateLegal(after, replies);
      out += replies.empty() ? '#' : '+';
    }
  }
  return out;
}

}  // namespace cb
