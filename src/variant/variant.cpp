// SPDX-License-Identifier: GPL-3.0-or-later
#include "variant/variant.hpp"

#include <algorithm>

#include "base/rng.hpp"

namespace cb {
namespace {

/// A stable hash over the canonical content of the spec. Multiplayer and replays
/// compare this, never a file name (ARCH section 11).
class IdHasher {
 public:
  void add(std::uint64_t v) { h_ ^= v + 0x9E3779B97F4A7C15ULL + (h_ << 6) + (h_ >> 2); }
  void add(std::string_view s) {
    for (char c : s) add(static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
  }
  [[nodiscard]] std::uint64_t value() const noexcept { return h_; }

 private:
  std::uint64_t h_{0xCBB0'0000'0000'0001ULL};
};

}  // namespace

std::string_view difficultyName(Difficulty d) noexcept {
  switch (d) {
    case Difficulty::Easy:
      return "easy";
    case Difficulty::Medium:
      return "medium";
    case Difficulty::Hard:
      return "hard";
    case Difficulty::Impossible:
      return "impossible";
    case Difficulty::Other:
      return "other";
  }
  return "other";
}

PieceTypeId VariantSpec::findPiece(std::string_view n) const {
  for (std::size_t i = 1; i < pieces.size(); ++i) {
    if (pieces[i].name == n) return static_cast<PieceTypeId>(i);
  }
  return kNoPiece;
}

int VariantSpec::findPieceField(std::string_view n) const {
  for (std::size_t i = 0; i < pieceFields.size(); ++i) {
    if (pieceFields[i].name == n) return static_cast<int>(i);
  }
  return -1;
}

int VariantSpec::findCellField(std::string_view n) const {
  for (std::size_t i = 0; i < cellFields.size(); ++i) {
    if (cellFields[i].name == n) return static_cast<int>(i);
  }
  return -1;
}

PieceTypeId VariantSpec::findPieceBySymbol(char upper) const {
  for (std::size_t i = 1; i < pieces.size(); ++i) {
    if (pieces[i].symbol == upper) return static_cast<PieceTypeId>(i);
  }
  return kNoPiece;
}

namespace {

/// Turn a direction expressed in *units of movement* into one in lattice cells.
///
/// The expansion is pure combinatorics over magnitudes and axes, and it stays that way:
/// the pitch is applied once here, on the way into the direction table, so every layer
/// below this one goes on seeing directions in the only units it cares about. A board
/// whose axes all have pitch 1 - which is every variant but the temporal ones - gets
/// back exactly what it put in.
Direction scaleByPitch(const DimSpec& dims, const Direction& d) {
  bool scaled = false;
  std::array<std::int16_t, kMaxDims> v{};
  for (std::uint8_t a = 0; a < d.n; ++a) {
    const std::int16_t p = dims.pitch(a);
    v[a] = static_cast<std::int16_t>(d.v[a] * p);
    if (p != 1 && d.v[a] != 0) scaled = true;
  }
  return scaled ? Direction::make(v, d.n) : d;
}

}  // namespace

Result<void> VariantSpec::finalize() {
  if (finalized_) return fail(ErrorCode::Internal, "variant finalized twice");
  if (pieces.empty() || !pieces[0].name.empty()) {
    return fail(ErrorCode::Internal, "pieces[0] must be the reserved empty entry");
  }
  if (dims.cellCount() == 0) {
    return fail(ErrorCode::ValidationError, "variant has no board");
  }

  const std::uint8_t nd = dims.dims();
  if (orientationAxis >= nd) {
    return fail(ErrorCode::ValidationError,
                "orientation axis " + std::to_string(orientationAxis) +
                    " does not exist on a " + std::to_string(nd) + "-axis board");
  }

  // ---- expand atoms into the global direction table ----------------------
  // Each atom owns a contiguous span, so the table carries a few duplicate
  // vectors across atoms. That is deliberate: a contiguous span lets movegen walk
  // an atom's directions with no indirection, which matters far more in the inner
  // loop than the handful of bytes duplication costs.
  std::uint64_t worstCaseMoves = 0;
  for (std::size_t pi = 1; pi < pieces.size(); ++pi) {
    PieceTypeDef& p = pieces[pi];
    if (p.name.empty()) {
      return fail(ErrorCode::ValidationError,
                  "piece " + std::to_string(pi) + " has no name");
    }
    if (p.atoms.empty()) {
      return fail(ErrorCode::ValidationError, "piece '" + p.name + "' has no move atoms");
    }
    for (MoveAtom& atom : p.atoms) {
      const auto canon = MoveAtom::canonicalize(atom);
      if (!canon.has_value()) {
        return fail(canon.error().code,
                    "piece '" + p.name + "': " + canon.error().message);
      }
      atom = *canon;

      if (atom.oriented && orientationAxis < 0) {
        return fail(ErrorCode::ValidationError,
                    "piece '" + p.name +
                        "' has a forward-only atom but the variant declares no "
                        "orientation axis");
      }
      if (atom.flip && orientationAxis < 0) {
        return fail(ErrorCode::ValidationError,
                    "piece '" + p.name +
                        "' has a move that flips for the other side but the variant "
                        "declares no orientation axis");
      }
      for (const Pin& pin : atom.pins) {
        if (pin.axis >= nd) {
          return fail(ErrorCode::ValidationError,
                      "piece '" + p.name + "' pins axis " + std::to_string(pin.axis) +
                          ", which a " + std::to_string(nd) + "-axis board does not have");
        }
      }
      // An exact slide of n cells is the unit step taken n times, so the cells it passes
      // over are checked; the atom's own step range says so.
      if (atom.kind == MoveKind::Exact) {
        const StepRange r = stepRange(atom);
        atom.minK = r.lo;
        atom.maxK = r.hi;
      }
      // A move whose direction is its own (oriented, pinned, exact, expanded) differs by
      // colour; a plain one is symmetric and both colours share a span.
      const bool sided = atom.oriented || atom.flip || atom.kind != MoveKind::Extrapolated ||
                         !atom.pins.empty();

      for (int ci = 0; ci < kNumColors; ++ci) {
        const auto color = static_cast<Color>(ci);
        std::vector<Direction> dirs;
        if (!sided && ci == 1) {
          // Unoriented atoms are symmetric, so both colours share one span.
          atom.dirBegin[1] = atom.dirBegin[0];
          atom.dirEnd[1] = atom.dirEnd[0];
          continue;
        }
        dirs = expandAtomFor(atom, nd, orientationAxis, color);

        const auto begin = static_cast<std::uint32_t>(dirTable.size());
        for (const Direction& d : dirs) dirTable.push_back(scaleByPitch(dims, d));
        atom.dirBegin[ci] = begin;
        atom.dirEnd[ci] = static_cast<std::uint32_t>(dirTable.size());

        if (dirTable.size() > kMaxDirections) {
          return fail(ErrorCode::BudgetExceeded,
                      "piece '" + p.name + "' atom " + atom.toString() +
                          " pushes the board's "
                          "direction table past " +
                          std::to_string(kMaxDirections) + " entries on a " +
                          std::to_string(nd) +
                          "-axis board; reduce the atom's order or the dimension count");
        }
      }

      // The backward-search spans (see MoveAtom::threatBegin). Only an oriented
      // atom on a glued board needs its own; elsewhere each colour's move span
      // already is its threat span.
      if (atom.oriented && !geom.isBox()) {
        const auto begin = static_cast<std::uint32_t>(dirTable.size());
        for (const Direction& d : expandAtom(atom.mags, nd)) {
          dirTable.push_back(scaleByPitch(dims, d));
        }
        const auto end = static_cast<std::uint32_t>(dirTable.size());
        for (int ci = 0; ci < kNumColors; ++ci) {
          atom.threatBegin[ci] = begin;
          atom.threatEnd[ci] = end;
        }
      } else {
        for (int ci = 0; ci < kNumColors; ++ci) {
          atom.threatBegin[ci] = atom.dirBegin[ci];
          atom.threatEnd[ci] = atom.dirEnd[ci];
        }
      }

      const std::uint64_t reach = atom.maxK == kUnlimited ? dims.cellCount() : atom.maxK;
      worstCaseMoves += static_cast<std::uint64_t>(std::max(
                            atom.dirCount(Color::White), atom.dirCount(Color::Black))) *
                        reach;
    }

    for (PieceTypeId to : p.promotesTo) {
      if (to == kNoPiece || to >= pieces.size()) {
        return fail(ErrorCode::ValidationError,
                    "piece '" + p.name + "' promotes to an unknown piece type");
      }
    }
  }

  // ---- validate the starting position ------------------------------------
  // Canonical order first: the identity of a variant must not depend on the order
  // an author happened to list its pieces in.
  std::sort(start.begin(), start.end(), [&](const StartPiece& a, const StartPiece& b) {
    return dims.toCell(a.at) < dims.toCell(b.at);
  });

  std::vector<bool> occupied(dims.cellCount(), false);
  int royals[kNumColors]{0, 0};
  for (const StartPiece& sp : start) {
    if (sp.type == kNoPiece || sp.type >= pieces.size()) {
      return fail(
          ErrorCode::ValidationError,
          "starting position places an unknown piece type at " + sp.at.toString());
    }
    if (!dims.inRange(sp.at)) {
      return fail(
          ErrorCode::ValidationError,
          "starting position places a piece outside the board at " + sp.at.toString());
    }
    const CellId c = dims.toCell(sp.at);
    if (occupied[c]) {
      return fail(ErrorCode::ValidationError,
                  "starting position stacks two pieces on " + sp.at.toString());
    }
    occupied[c] = true;
    if (pieces[sp.type].royal) ++royals[static_cast<std::size_t>(sp.color)];
  }

  // Zero-royal and multi-royal variants are legal (checkers has none); this only
  // rejects the asymmetric accident of one side having a king and the other not.
  if ((royals[0] == 0) != (royals[1] == 0)) {
    return fail(
        ErrorCode::ValidationError,
        "one side has a royal piece and the other does not; declare both or neither");
  }

  // ---- validate castling templates ---------------------------------------
  std::vector<std::uint8_t> usedBits;
  for (const CastleTemplate& ct : castles) {
    for (CellId c : {ct.kingFrom, ct.kingTo, ct.rookFrom, ct.rookTo}) {
      if (c >= dims.cellCount()) {
        return fail(ErrorCode::ValidationError,
                    "castling template '" + ct.name + "' names a cell outside the board");
      }
    }
    if (ct.rightsBit >= 8) {
      return fail(ErrorCode::ValidationError,
                  "castling template '" + ct.name + "' needs a rights bit below 8");
    }
    if (std::find(usedBits.begin(), usedBits.end(), ct.rightsBit) != usedBits.end()) {
      return fail(ErrorCode::ValidationError, "two castling templates share rights bit " +
                                                  std::to_string(ct.rightsBit));
    }
    usedBits.push_back(ct.rightsBit);
  }

  if (enPassant && orientationAxis < 0) {
    return fail(ErrorCode::ValidationError,
                "en passant needs an orientation axis to know what 'passing' means");
  }

  // ---- move buffer bound --------------------------------------------------
  // Generous: (pieces on board) x (worst-case moves for the most mobile type).
  std::uint64_t bound = worstCaseMoves * static_cast<std::uint64_t>(start.size() + 64);
  bound = std::min<std::uint64_t>(bound, 1u << 22);
  moveBound_ = static_cast<std::uint32_t>(std::max<std::uint64_t>(bound, 256));

  // ---- field validation ---------------------------------------------------
  for (const auto* group : {&pieceFields, &cellFields}) {
    for (const FieldDecl& f : *group) {
      if (f.name.empty()) {
        return fail(ErrorCode::ValidationError, "a custom field needs a name");
      }
      if (f.minValue > f.maxValue) {
        return fail(ErrorCode::ValidationError,
                    "field '" + f.name + "' has min above max");
      }
      if (f.defaultValue < f.minValue || f.defaultValue > f.maxValue) {
        return fail(ErrorCode::ValidationError,
                    "field '" + f.name + "' has a default outside its own range");
      }
    }
  }

  // ---- identity ----------------------------------------------------------
  IdHasher h;
  h.add(name);
  h.add(dims.dims());
  for (std::uint8_t a = 0; a < nd; ++a) {
    h.add(static_cast<std::uint64_t>(dims.extent(a)));
    h.add(static_cast<std::uint64_t>(dims.kind(a)));
    h.add(dims.name(a));
  }
  h.add(geom.identCount());
  for (std::uint8_t a = 0; a < nd; ++a) {
    for (const Side s : {Side::Min, Side::Max}) {
      const Transform* t = geom.faceTransform(a, s);
      h.add(t == nullptr ? 0u : 1u);
      if (t != nullptr) h.add(t->toString());
    }
  }
  for (std::size_t pi = 1; pi < pieces.size(); ++pi) {
    h.add(pieces[pi].name);
    h.add(static_cast<std::uint64_t>(pieces[pi].symbol));
    h.add(pieces[pi].royal ? 1u : 0u);
    h.add(pieces[pi].resetsDrawClock ? 1u : 0u);
    for (PieceTypeId to : pieces[pi].promotesTo) h.add(to);
    for (const MoveAtom& a : pieces[pi].atoms) h.add(a.toString());
  }
  for (const StartPiece& sp : start) {
    h.add(sp.at.toString());
    h.add(sp.type);
    h.add(static_cast<std::uint64_t>(sp.color));
  }
  for (const CastleTemplate& ct : castles) {
    h.add(ct.name);
    h.add(static_cast<std::uint64_t>(ct.color));
    h.add(ct.kingFrom);
    h.add(ct.kingTo);
    h.add(ct.rookFrom);
    h.add(ct.rookTo);
    for (CellId c : ct.mustBeEmpty) h.add(c);
    for (CellId c : ct.mustBeSafe) h.add(c);
    h.add(ct.rightsBit);
  }
  for (int ci = 0; ci < kNumColors; ++ci) {
    h.add(static_cast<std::uint64_t>(promotion[ci].axis));
    h.add(static_cast<std::uint64_t>(promotion[ci].value));
    for (std::size_t pi = 1; pi < pieces.size(); ++pi) {
      for (const MoveAtom& a : pieces[pi].atoms) {
        h.add(static_cast<std::uint64_t>(a.fromRegion[ci].axis));
        h.add(static_cast<std::uint64_t>(a.fromRegion[ci].value));
      }
    }
  }
  h.add(static_cast<std::uint64_t>(orientationAxis));
  h.add(enPassant ? 1u : 0u);
  h.add(static_cast<std::uint64_t>(stalemate));
  h.add(static_cast<std::uint64_t>(halfmoveDrawLimit));
  h.add(static_cast<std::uint64_t>(temporalWhiteSign));
  h.add(static_cast<std::uint64_t>(temporalBlackSign));
  h.add(static_cast<std::uint64_t>(temporalBranchAdvance));
  for (const auto* group : {&pieceFields, &cellFields}) {
    for (const FieldDecl& f : *group) {
      h.add(f.name);
      h.add(static_cast<std::uint64_t>(f.defaultValue));
      h.add(static_cast<std::uint64_t>(f.minValue));
      h.add(static_cast<std::uint64_t>(f.maxValue));
      h.add(f.hashed ? 1u : 0u);
    }
  }
  if (ruleSetDigest) h.add(ruleSetDigest());
  variantId_ = h.value();

  // Keys depend on the variant id, so two different variants never share a key
  // schedule and a hash always identifies (position, variant) together.
  zob.init(variantId_, dims.cellCount(), pieceCodes(),
           static_cast<std::uint32_t>(pieceFields.size() + cellFields.size() + 64));

  finalized_ = true;
  return {};
}

}  // namespace cb
