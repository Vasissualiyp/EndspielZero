// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/variant_toml.hpp"

#include "io/atom_toml.hpp"
#include "io/model_toml.hpp"
#include "io/moveset_library.hpp"

#include <fstream>
#include <map>
#include <sstream>

#include <toml++/toml.hpp>

#include "rules/vm.hpp"

#include "io/fen_order.hpp"
#include "io/notation.hpp"
#include "io/rule_parse.hpp"

namespace cb {
namespace {
const toml::array kNoMoves{};
}  // namespace
namespace {

int lineOf(const toml::node& n) {
  const auto& r = n.source();
  return static_cast<int>(r.begin.line);
}

Result<MoveMode> parseMode(std::string_view s, int line) {
  if (s == "slide") return MoveMode::Slide;
  if (s == "leap") return MoveMode::Leap;
  if (s == "hop") return MoveMode::Hop;
  return fail(ErrorCode::ValidationError,
              "move mode must be 'slide', 'leap' or 'hop', not '" + std::string(s) + "'",
              line);
}

Result<CapturePolicy> parseCapture(std::string_view s, int line) {
  if (s == "may") return CapturePolicy::May;
  if (s == "must") return CapturePolicy::Must;
  if (s == "cannot") return CapturePolicy::Cannot;
  return fail(
      ErrorCode::ValidationError,
      "capture policy must be 'may', 'must' or 'cannot', not '" + std::string(s) + "'",
      line);
}

Result<Color> parseColor(std::string_view s, int line) {
  if (s == "white") return Color::White;
  if (s == "black") return Color::Black;
  return fail(ErrorCode::ValidationError,
              "colour must be 'white' or 'black', not '" + std::string(s) + "'", line);
}

Result<AxisKind> parseAxisKind(std::string_view s, int line) {
  if (s == "spatial") return AxisKind::Spatial;
  if (s == "temporal") return AxisKind::Temporal;
  if (s == "multiverse") return AxisKind::Multiverse;
  return fail(ErrorCode::ValidationError,
              "axis kind must be 'spatial', 'temporal' or 'multiverse', not '" +
                  std::string(s) + "'",
              line);
}

Result<Difficulty> parseDifficulty(std::string_view s, int line) {
  if (s == "easy") return Difficulty::Easy;
  if (s == "medium") return Difficulty::Medium;
  if (s == "hard") return Difficulty::Hard;
  if (s == "impossible") return Difficulty::Impossible;
  if (s == "other") return Difficulty::Other;
  return fail(ErrorCode::ValidationError,
              "difficulty must be 'easy', 'medium', 'hard', 'impossible' or 'other', "
              "not '" +
                  std::string(s) + "'",
              line);
}

}  // namespace

Result<std::vector<StartPiece>> parseBoardSection(const DimSpec& dims,
                                                  const std::vector<PieceTypeDef>& pieces,
                                                  std::string_view board) {
  // One shared definition of FEN-N ordering (io/fen_order.hpp), so the loader and
  // the serializer cannot disagree about what the board string means.
  std::vector<Coord> order;
  order.reserve(dims.cellCount());
  forEachInFenOrder(
      dims, [&](CellId c) { order.push_back(dims.toCoord(c)); }, [](std::uint8_t) {});

  std::vector<StartPiece> out;
  std::size_t idx = 0;
  for (std::size_t i = 0; i < board.size(); ++i) {
    const char ch = board[i];
    if (ch == '/' || ch == '|') continue;
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      std::size_t j = i;
      int skip = 0;
      while (j < board.size() &&
             std::isdigit(static_cast<unsigned char>(board[j])) != 0) {
        skip = skip * 10 + (board[j] - '0');
        ++j;
      }
      idx += static_cast<std::size_t>(skip);
      i = j - 1;
      continue;
    }
    const char upper = static_cast<char>(std::toupper(ch));
    PieceTypeId type = kNoPiece;
    for (std::size_t pi = 1; pi < pieces.size(); ++pi) {
      if (pieces[pi].symbol == upper) {
        type = static_cast<PieceTypeId>(pi);
        break;
      }
    }
    if (type == kNoPiece) {
      return fail(ErrorCode::ValidationError,
                  std::string("the starting board uses symbol '") + ch +
                      "', which no declared piece has");
    }
    if (idx >= order.size()) {
      return fail(ErrorCode::ValidationError,
                  "the starting board describes more cells than the board has");
    }
    const Color color =
        std::isupper(static_cast<unsigned char>(ch)) != 0 ? Color::White : Color::Black;
    out.push_back(StartPiece{order[idx], type, color});
    ++idx;
  }
  if (idx != order.size()) {
    return fail(ErrorCode::ValidationError,
                "the starting board describes " + std::to_string(idx) +
                    " cells but the board has " + std::to_string(order.size()));
  }
  return out;
}

Result<VariantSpec> loadVariantToml(std::string_view text, std::string_view sourceName) {
  toml::table tbl;
  try {
    tbl = toml::parse(text, sourceName);
  } catch (const toml::parse_error& e) {
    return fail(ErrorCode::ParseError, std::string(e.description()),
                static_cast<int>(e.source().begin.line));
  }

  VariantSpec v;
  v.name = tbl["name"].value_or(std::string{});
  if (v.name.empty()) {
    return fail(ErrorCode::ValidationError, "a variant needs a 'name'");
  }
  v.description = tbl["description"].value_or(std::string{});
  // Advertising a difficulty is optional: a variant that says nothing - or one from a
  // Workshop package this build has never seen - is "other" rather than an error.
  if (const auto* node = tbl["difficulty"].as_string()) {
    const auto d = parseDifficulty(node->get(), lineOf(*node));
    if (!d.has_value()) return fail(d.error().code, d.error().message, d.error().line);
    v.difficulty = *d;
  }

  if (const auto* temporal = tbl["temporal"].as_table()) {
    v.temporalWhiteSign = (*temporal)["white_sign"].value_or(v.temporalWhiteSign);
    v.temporalBlackSign = (*temporal)["black_sign"].value_or(v.temporalBlackSign);
    v.temporalBranchAdvance =
        (*temporal)["branch_advance"].value_or(v.temporalBranchAdvance);
  }

  // ---- axes ----------------------------------------------------------------
  const auto* axesNode = tbl["axis"].as_array();
  if (axesNode == nullptr || axesNode->empty()) {
    return fail(ErrorCode::ValidationError, "a variant needs at least one [[axis]]");
  }
  std::vector<AxisDecl> axes;
  for (const auto& node : *axesNode) {
    const auto* t = node.as_table();
    if (t == nullptr)
      return fail(ErrorCode::ValidationError, "[[axis]] must be a table", lineOf(node));
    AxisDecl a;
    a.name = (*t)["name"].value_or(std::string{});
    a.extent = (*t)["extent"].value_or(0);
    const auto kind =
        parseAxisKind((*t)["kind"].value_or(std::string{"spatial"}), lineOf(node));
    if (!kind.has_value())
      return fail(kind.error().code, kind.error().message, kind.error().line);
    a.kind = *kind;
    // How many boards one unit of movement along this axis covers. Stated rather than
    // inferred from the kind: "temporal axes step two" would be the engine knowing what
    // time is, and the whole point of ADR-0007 is that it does not.
    a.pitch = (*t)["pitch"].value_or(1);
    if (a.name.empty()) {
      return fail(ErrorCode::ValidationError, "every axis needs a 'name'", lineOf(node));
    }
    axes.push_back(a);
  }
  auto dims = DimSpec::create(axes);
  if (!dims.has_value()) return fail(dims.error().code, dims.error().message);
  v.dims = *dims;

  const auto axisByName = [&](std::string_view n) { return v.dims.axisIndex(n); };

  // ---- geometry ------------------------------------------------------------
  std::vector<IdentDecl> idents;
  if (const auto* g = tbl["geometry"]["identify"].as_array()) {
    for (const auto& node : *g) {
      const auto* t = node.as_table();
      if (t == nullptr) {
        return fail(ErrorCode::ValidationError, "[[geometry.identify]] must be a table",
                    lineOf(node));
      }
      IdentDecl id;
      const std::string axisName = (*t)["axis"].value_or(std::string{});
      const int ai = axisByName(axisName);
      if (ai < 0) {
        return fail(ErrorCode::ValidationError,
                    "identification names axis '" + axisName + "', which is not declared",
                    lineOf(node));
      }
      id.axis = static_cast<std::uint8_t>(ai);

      const std::string kind = (*t)["kind"].value_or(std::string{"periodic"});
      if (kind == "periodic") {
        id.kind = BoundaryKind::Periodic;
      } else if (kind == "mirror") {
        id.kind = BoundaryKind::Mirror;
      } else {
        return fail(ErrorCode::ValidationError,
                    "boundary kind must be 'periodic' or 'mirror', not '" + kind + "'",
                    lineOf(node));
      }
      const std::string side = (*t)["side"].value_or(std::string{"max"});
      if (side == "max") {
        id.side = Side::Max;
      } else if (side == "min") {
        id.side = Side::Min;
      } else {
        return fail(ErrorCode::ValidationError, "side must be 'min' or 'max'",
                    lineOf(node));
      }
      if (const auto* flips = (*t)["flip"].as_array()) {
        for (const auto& f : *flips) {
          const std::string fn = f.value_or(std::string{});
          const int fi = axisByName(fn);
          if (fi < 0) {
            return fail(ErrorCode::ValidationError,
                        "identification flips axis '" + fn + "', which is not declared",
                        lineOf(node));
          }
          id.flipAxes.push_back(static_cast<std::uint8_t>(fi));
        }
      }
      if (const auto* swap = (*t)["swap"].as_array()) {
        if (swap->size() != 2) {
          return fail(ErrorCode::ValidationError, "'swap' needs exactly two axis names",
                      lineOf(node));
        }
        id.swapA = axisByName((*swap)[0].value_or(std::string{}));
        id.swapB = axisByName((*swap)[1].value_or(std::string{}));
        if (id.swapA < 0 || id.swapB < 0) {
          return fail(ErrorCode::ValidationError, "'swap' names an undeclared axis",
                      lineOf(node));
        }
      }
      idents.push_back(std::move(id));
    }
  }
  auto geom = Geometry::create(v.dims, idents);
  if (!geom.has_value()) return fail(geom.error().code, geom.error().message);
  v.geom = *geom;

  // ---- orientation ---------------------------------------------------------
  if (const auto oa = tbl["orientation_axis"].value<std::string>()) {
    v.orientationAxis = axisByName(*oa);
    if (v.orientationAxis < 0) {
      return fail(ErrorCode::ValidationError,
                  "orientation_axis names '" + *oa + "', which is not a declared axis");
    }
  }

  // ---- custom fields --------------------------------------------------------
  // Parsed before pieces and rules so that both can refer to them by name.
  const auto readFields = [&](const char* key,
                              std::vector<FieldDecl>& out) -> Result<void> {
    const auto* arr = tbl[key].as_array();
    if (arr == nullptr) return {};
    for (const auto& node : *arr) {
      const auto* t = node.as_table();
      if (t == nullptr) {
        return fail(ErrorCode::ValidationError,
                    std::string("[[") + key + "]] must be a table", lineOf(node));
      }
      FieldDecl f;
      f.name = (*t)["name"].value_or(std::string{});
      if (f.name.empty()) {
        return fail(ErrorCode::ValidationError,
                    std::string("every ") + key + " needs a name", lineOf(node));
      }
      f.defaultValue =
          static_cast<std::int32_t>((*t)["default"].value_or<std::int64_t>(0));
      f.minValue = static_cast<std::int32_t>((*t)["min"].value_or<std::int64_t>(-32768));
      f.maxValue = static_cast<std::int32_t>((*t)["max"].value_or<std::int64_t>(32767));
      f.hashed = (*t)["hashed"].value_or(true);
      out.push_back(std::move(f));
    }
    return {};
  };
  if (auto r = readFields("piece_field", v.pieceFields); !r.has_value()) {
    return fail(r.error().code, r.error().message, r.error().line);
  }
  if (auto r = readFields("cell_field", v.cellFields); !r.has_value()) {
    return fail(r.error().code, r.error().message, r.error().line);
  }

  // ---- mark tags -------------------------------------------------------------
  // "ep" is tag 0 always; the variant's own follow. Parsed before pieces, which name them.
  std::vector<std::string> markTagNames{"ep"};
  v.markTags.clear();
  v.markTags.push_back(VariantSpec::MarkTag{"ep", false});
  if (const auto* tags = tbl["mark"].as_array()) {
    for (const auto& node : *tags) {
      const auto* t = node.as_table();
      if (t == nullptr) {
        return fail(ErrorCode::ValidationError, "[[mark]] must be a table", lineOf(node));
      }
      const std::string name = (*t)["name"].value_or(std::string{});
      if (name.empty()) {
        return fail(ErrorCode::ValidationError, "every [[mark]] needs a name", lineOf(node));
      }
      if (name == "ep") continue;  // always declared
      markTagNames.push_back(name);
      v.markTags.push_back(VariantSpec::MarkTag{name, (*t)["blocks"].value_or(false)});
    }
  }

  // ---- pieces --------------------------------------------------------------
  v.pieces.clear();
  v.pieces.emplace_back();  // the reserved empty entry
  const auto* piecesNode = tbl["piece"].as_array();
  if (piecesNode == nullptr || piecesNode->empty()) {
    return fail(ErrorCode::ValidationError, "a variant needs at least one [[piece]]");
  }
  std::map<std::string, std::vector<std::string>> promotesTo;
  for (const auto& node : *piecesNode) {
    const auto* t = node.as_table();
    if (t == nullptr)
      return fail(ErrorCode::ValidationError, "[[piece]] must be a table", lineOf(node));
    PieceTypeDef p;
    p.name = (*t)["name"].value_or(std::string{});
    if (p.name.empty()) {
      return fail(ErrorCode::ValidationError, "every piece needs a 'name'", lineOf(node));
    }
    const std::string sym = (*t)["symbol"].value_or(std::string{});
    if (sym.size() != 1) {
      return fail(ErrorCode::ValidationError,
                  "piece '" + p.name + "' needs a single-character 'symbol'",
                  lineOf(node));
    }
    p.symbol = static_cast<char>(std::toupper(sym[0]));
    p.royal = (*t)["royal"].value_or(false);
    p.resetsDrawClock = (*t)["resets_draw_clock"].value_or(false);
    p.shape = (*t)["shape"].value_or(std::string{});
    // Height arrives as a multiplier ("1.15") and is stored in permille: the variant
    // layer is part of the deterministic core and holds no floating point.
    if (const auto h = (*t)["height"].value<double>()) {
      p.heightPermille = static_cast<int>(*h * 1000.0 + 0.5);
      if (p.heightPermille < 200 || p.heightPermille > 4000) {
        return fail(ErrorCode::ValidationError,
                    "piece '" + p.name + "': height must be between 0.2 and 4.0",
                    lineOf(node));
      }
    }
    // An authored body and icon are cosmetic: one that does not build is dropped, and the
    // piece is drawn from its archetype instead - a variant is never unplayable for want
    // of a model.
    if (const auto* bt = (*t)["body"].as_table()) {
      auto body = bodyFromToml(*bt);
      if (body.has_value() && assets::validate(*body).has_value()) p.body = std::move(body);
    }
    if (const auto* it = (*t)["icon"].as_table()) {
      auto icon = iconFromToml(*it);
      if (icon.has_value() && assets::validate(*icon).has_value()) p.icon = std::move(icon);
    }
    if (const auto* pr = (*t)["promotes_to"].as_array()) {
      for (const auto& x : *pr) promotesTo[p.name].push_back(x.value_or(std::string{}));
    }

    // A moveset written as text (the symbolic form), or by name from the library, comes
    // first; any [[piece.move]] entries follow it.
    if (const auto movesText = (*t)["moves"].value<std::string>()) {
      const MovesetContext ctx = movesetContextFor(tbl);
      auto atoms = evalMoveset(*movesText, ctx);
      if (!atoms.has_value()) {
        return fail(atoms.error().code,
                    "piece '" + p.name + "' moves: " + atoms.error().message, lineOf(node));
      }
      for (const MoveAtom& a : *atoms) p.atoms.push_back(a);
    }
    const auto* movesNode = (*t)["move"].as_array();
    if ((movesNode == nullptr || movesNode->empty()) && p.atoms.empty()) {
      return fail(ErrorCode::ValidationError,
                  "piece '" + p.name + "' needs moves = \"...\" or at least one [[piece.move]]",
                  lineOf(node));
    }
    if (movesNode == nullptr) movesNode = &kNoMoves;
    for (const auto& mnode : *movesNode) {
      const auto* mt = mnode.as_table();
      if (mt == nullptr) {
        return fail(ErrorCode::ValidationError, "[[piece.move]] must be a table",
                    lineOf(mnode));
      }
      MoveAtom atom;
      {
        std::vector<std::string> axisNames;
        for (std::size_t i = 0; i < v.dims.dims(); ++i) axisNames.push_back(v.dims.name(i));
        std::string why;
        if (!readAtomKind(*mt, axisNames, v.orientationAxis, markTagNames, atom, why)) {
          return fail(ErrorCode::ValidationError, "piece '" + p.name + "': " + why,
                      lineOf(mnode));
        }
      }

      const std::string maxText = (*mt)["max"].value_or(std::string{});
      if (maxText == "inf") {
        atom.maxK = kUnlimited;
      } else if (const auto mk = (*mt)["max"].value<std::int64_t>()) {
        atom.maxK = static_cast<std::uint32_t>(*mk);
      } else if (!maxText.empty()) {
        return fail(ErrorCode::ValidationError,
                    "piece '" + p.name + "': 'max' must be an integer or \"inf\"",
                    lineOf(mnode));
      }
      atom.minK = static_cast<std::uint32_t>((*mt)["min"].value_or<std::int64_t>(1));

      const auto mode =
          parseMode((*mt)["mode"].value_or(std::string{"leap"}), lineOf(mnode));
      if (!mode.has_value())
        return fail(mode.error().code, mode.error().message, mode.error().line);
      atom.mode = *mode;
      const auto cap =
          parseCapture((*mt)["capture"].value_or(std::string{"may"}), lineOf(mnode));
      if (!cap.has_value())
        return fail(cap.error().code, cap.error().message, cap.error().line);
      atom.capture = *cap;

      atom.oriented = (*mt)["forward"].value_or(false);
      if (atom.oriented && v.orientationAxis < 0) {
        // Caught here rather than in finalize() so the message can point at the
        // offending line in the author's file.
        return fail(ErrorCode::ValidationError,
                    "piece '" + p.name +
                        "': 'forward = true' needs the variant to declare an "
                        "orientation axis (orientation_axis = \"<axis name>\")",
                    lineOf(mnode));
      }
      atom.leavesEnPassant = (*mt)["leaves_en_passant"].value_or(false);
      atom.unmoved = (*mt)["unmoved"].value_or(false);
      if (const auto home = (*mt)["from_rank"].value<std::int64_t>()) {
        if (v.orientationAxis < 0) {
          return fail(
              ErrorCode::ValidationError,
              "piece '" + p.name +
                  "': 'from_rank' needs the variant to declare an orientation_axis",
              lineOf(mnode));
        }
        const auto axis = static_cast<std::size_t>(v.orientationAxis);
        const auto white = static_cast<std::int16_t>(*home);
        const auto black = static_cast<std::int16_t>(v.dims.extent(axis) - 1 - white);
        atom.fromRegion[static_cast<std::size_t>(Color::White)] =
            Region{v.orientationAxis, white};
        atom.fromRegion[static_cast<std::size_t>(Color::Black)] =
            Region{v.orientationAxis, black};
      }
      // Canonicalize here as well as in finalize(), so that a malformed magnitude
      // or step count is reported against the line that declared it.
      auto canon = MoveAtom::canonicalize(atom);
      if (!canon.has_value()) {
        return fail(canon.error().code,
                    "piece '" + p.name + "': " + canon.error().message, lineOf(mnode));
      }
      p.atoms.push_back(*canon);
    }
    v.pieces.push_back(std::move(p));
  }

  for (const auto& [pieceName, targets] : promotesTo) {
    const PieceTypeId from = v.findPiece(pieceName);
    for (const std::string& target : targets) {
      const PieceTypeId to = v.findPiece(target);
      if (to == kNoPiece) {
        return fail(ErrorCode::ValidationError, "piece '" + pieceName +
                                                    "' promotes to '" + target +
                                                    "', which is not declared");
      }
      v.pieces[from].promotesTo.push_back(to);
    }
  }

  // ---- promotion region ----------------------------------------------------
  if (const auto rank = tbl["promotion"]["rank"].value<std::int64_t>()) {
    if (v.orientationAxis < 0) {
      return fail(ErrorCode::ValidationError,
                  "[promotion] needs the variant to declare an orientation_axis");
    }
    const auto axis = static_cast<std::size_t>(v.orientationAxis);
    const auto white = static_cast<std::int16_t>(*rank);
    v.promotion[static_cast<std::size_t>(Color::White)] =
        Region{v.orientationAxis, white};
    v.promotion[static_cast<std::size_t>(Color::Black)] = Region{
        v.orientationAxis, static_cast<std::int16_t>(v.dims.extent(axis) - 1 - white)};
  }

  // ---- rules ---------------------------------------------------------------
  v.enPassant = tbl["en_passant"].value_or(false);
  v.halfmoveDrawLimit =
      static_cast<int>(tbl["halfmove_draw_limit"].value_or<std::int64_t>(0));
  const std::string stalemate = tbl["stalemate"].value_or(std::string{"draw"});
  if (stalemate == "draw") {
    v.stalemate = StalematePolicy::Draw;
  } else if (stalemate == "loss") {
    v.stalemate = StalematePolicy::Loss;
  } else if (stalemate == "win") {
    v.stalemate = StalematePolicy::Win;
  } else {
    return fail(ErrorCode::ValidationError,
                "stalemate must be 'draw', 'loss' or 'win', not '" + stalemate + "'");
  }
  const auto side = parseColor(tbl["side_to_move"].value_or(std::string{"white"}), 0);
  if (!side.has_value()) return fail(side.error().code, side.error().message);
  v.startSideToMove = *side;

  // ---- castling ------------------------------------------------------------
  if (const auto* cs = tbl["castle"].as_array()) {
    for (const auto& node : *cs) {
      const auto* t = node.as_table();
      if (t == nullptr)
        return fail(ErrorCode::ValidationError, "[[castle]] must be a table",
                    lineOf(node));
      CastleTemplate ct;
      ct.name = (*t)["name"].value_or(std::string{});
      const auto color =
          parseColor((*t)["color"].value_or(std::string{"white"}), lineOf(node));
      if (!color.has_value())
        return fail(color.error().code, color.error().message, color.error().line);
      ct.color = *color;
      const auto cellField = [&](const char* key) -> Result<CellId> {
        const std::string name = (*t)[key].value_or(std::string{});
        if (name.empty()) {
          return fail(ErrorCode::ValidationError,
                      "castling template '" + ct.name + "' is missing '" + key + "'",
                      lineOf(node));
        }
        auto c = parseCell(v.dims, name);
        if (!c.has_value()) return fail(c.error().code, c.error().message, lineOf(node));
        return *c;
      };
      const auto kf = cellField("king_from");
      const auto kt2 = cellField("king_to");
      const auto rf = cellField("rook_from");
      const auto rt = cellField("rook_to");
      for (const auto* r : {&kf, &kt2, &rf, &rt}) {
        if (!r->has_value())
          return fail(r->error().code, r->error().message, r->error().line);
      }
      ct.kingFrom = *kf;
      ct.kingTo = *kt2;
      ct.rookFrom = *rf;
      ct.rookTo = *rt;
      const auto cellList = [&](const char* key,
                                std::vector<CellId>& out) -> Result<void> {
        if (const auto* arr = (*t)[key].as_array()) {
          for (const auto& x : *arr) {
            auto c = parseCell(v.dims, x.value_or(std::string{}));
            if (!c.has_value())
              return fail(c.error().code, c.error().message, lineOf(node));
            out.push_back(*c);
          }
        }
        return {};
      };
      if (const auto r = cellList("empty", ct.mustBeEmpty); !r.has_value()) {
        return fail(r.error().code, r.error().message, r.error().line);
      }
      if (const auto r = cellList("safe", ct.mustBeSafe); !r.has_value()) {
        return fail(r.error().code, r.error().message, r.error().line);
      }
      ct.rightsBit =
          static_cast<std::uint8_t>((*t)["rights_bit"].value_or<std::int64_t>(0));
      v.castles.push_back(std::move(ct));
    }
  }

  // ---- compound moves ----------------------------------------------------------
  if (const auto* comps = tbl["compound"].as_array()) {
    std::vector<std::string> axisNames;
    for (std::size_t i = 0; i < v.dims.dims(); ++i) axisNames.push_back(v.dims.name(i));
    for (const auto& node : *comps) {
      const auto* t = node.as_table();
      if (t == nullptr) {
        return fail(ErrorCode::ValidationError, "[[compound]] must be a table", lineOf(node));
      }
      CompoundMove cm;
      cm.name = (*t)["name"].value_or(std::string{});
      const std::string letter = (*t)["fen"].value_or(std::string{});
      if (!letter.empty()) cm.fenLetter = letter[0];
      const std::string mover = (*t)["piece"].value_or(std::string{});
      cm.mover = v.findPiece(mover);
      if (cm.mover == kNoPiece) {
        return fail(ErrorCode::ValidationError,
                    "compound move '" + cm.name + "': no piece is called '" + mover + "'",
                    lineOf(node));
      }
      // The mover's step is an exact move, spelled like any other.
      toml::table asMove = *t;
      asMove.insert_or_assign("type", std::string("exact"));
      std::string why;
      if (!readAtomKind(asMove, axisNames, v.orientationAxis, markTagNames, cm.step, why)) {
        return fail(ErrorCode::ValidationError, "compound move '" + cm.name + "': " + why,
                    lineOf(node));
      }
      cm.moverUnmoved = (*t)["unmoved"].value_or(true);
      cm.partnerUnmoved = (*t)["partner_unmoved"].value_or(true);
      if (const auto* partners = (*t)["partner"].as_array()) {
        for (const auto& x : *partners) {
          const std::string pn = x.value_or(std::string{});
          const PieceTypeId id = v.findPiece(pn);
          if (id == kNoPiece) {
            return fail(ErrorCode::ValidationError,
                        "compound move '" + cm.name + "': no partner piece is called '" + pn + "'",
                        lineOf(node));
          }
          cm.partners.push_back(id);
        }
      }
      if (const auto* safe = (*t)["safe"].as_array()) {
        for (const auto& x : *safe) {
          const std::string sc = x.value_or(std::string{});
          if (sc == "from") {
            cm.safe.push_back(CompoundMove::SafeCell::From);
          } else if (sc == "path") {
            cm.safe.push_back(CompoundMove::SafeCell::Path);
          } else if (sc == "to") {
            cm.safe.push_back(CompoundMove::SafeCell::To);
          } else {
            return fail(ErrorCode::ValidationError,
                        "compound move '" + cm.name + "': 'safe' lists from, path and to - not '" + sc + "'",
                        lineOf(node));
          }
        }
      }
      v.compounds.push_back(std::move(cm));
    }
  }

  // ---- rules ----------------------------------------------------------------
  // Parsed after pieces and fields, because an expression resolves names against them.
  if (const auto* rulesNode = tbl["rule"].as_array()) {
    rules::RuleSet set;
    for (const auto& node : *rulesNode) {
      const auto* t = node.as_table();
      if (t == nullptr) {
        return fail(ErrorCode::ValidationError, "[[rule]] must be a table", lineOf(node));
      }
      rules::Rule rule;
      rule.name = (*t)["name"].value_or(std::string{});
      const std::string when = (*t)["when"].value_or(std::string{});
      if (when == "on_move_filter") {
        rule.trigger = rules::Trigger::OnMoveFilter;
      } else if (when == "on_capture") {
        rule.trigger = rules::Trigger::OnCapture;
      } else if (when == "on_move_end") {
        rule.trigger = rules::Trigger::OnMoveEnd;
      } else if (when == "on_turn_end") {
        rule.trigger = rules::Trigger::OnTurnEnd;
      } else if (when == "on_result_query") {
        rule.trigger = rules::Trigger::OnResultQuery;
      } else {
        return fail(ErrorCode::ValidationError,
                    "rule '" + rule.name +
                        "': 'when' must be one of on_move_filter, "
                        "on_capture, on_move_end, on_turn_end, on_result_query - not '" +
                        when + "'",
                    lineOf(node));
      }

      if (const auto cond = (*t)["if"].value<std::string>()) {
        auto e = parseRuleExpr(v, *cond);
        if (!e.has_value()) {
          return fail(e.error().code,
                      "rule '" + rule.name + "' condition: " + e.error().message,
                      lineOf(node));
        }
        rule.condition.push_back(std::move(*e));
      }

      const auto* effects = (*t)["effect"].as_array();
      if (effects == nullptr || effects->empty()) {
        return fail(ErrorCode::ValidationError,
                    "rule '" + rule.name + "' needs at least one [[rule.effect]]",
                    lineOf(node));
      }
      for (const auto& enode : *effects) {
        const auto* et = enode.as_table();
        if (et == nullptr) {
          return fail(ErrorCode::ValidationError, "[[rule.effect]] must be a table",
                      lineOf(enode));
        }
        rules::Effect eff;
        const std::string op = (*et)["op"].value_or(std::string{});
        const std::string ewhere = "rule '" + rule.name + "', effect '" + op + "'";

        const auto expr = [&](const char* key, bool required) -> Result<bool> {
          const auto source = (*et)[key].value<std::string>();
          if (!source.has_value()) {
            if (!required) return false;
            return fail(ErrorCode::ValidationError, ewhere + " needs '" + key + "'",
                        lineOf(enode));
          }
          auto e = parseRuleExpr(v, *source);
          if (!e.has_value()) {
            return fail(e.error().code, ewhere + ": " + e.error().message, lineOf(enode));
          }
          eff.args.push_back(std::move(*e));
          return true;
        };

        if (op == "destroy") {
          eff.op = rules::EffectOp::Destroy;
          if (auto r = expr("at", true); !r.has_value()) {
            return fail(r.error().code, r.error().message, r.error().line);
          }
        } else if (op == "destroy_region") {
          eff.op = rules::EffectOp::DestroyRegion;
          if (auto r = expr("at", true); !r.has_value()) {
            return fail(r.error().code, r.error().message, r.error().line);
          }
          eff.imm = (*et)["radius"].value_or<std::int64_t>(1);
          if (const auto f = (*et)["affects"].value<std::string>()) {
            auto e = parseRuleExpr(v, *f);
            if (!e.has_value()) {
              return fail(e.error().code, ewhere + " affects: " + e.error().message,
                          lineOf(enode));
            }
            eff.filter.push_back(std::move(*e));
          }
        } else if (op == "transform" || op == "spawn") {
          eff.op =
              op == "transform" ? rules::EffectOp::Transform : rules::EffectOp::Spawn;
          if (auto r = expr("at", true); !r.has_value()) {
            return fail(r.error().code, r.error().message, r.error().line);
          }
          const std::string piece = (*et)["piece"].value_or(std::string{});
          const PieceTypeId id = v.findPiece(piece);
          if (id == kNoPiece) {
            return fail(ErrorCode::ValidationError,
                        ewhere + ": no piece is called '" + piece + "'", lineOf(enode));
          }
          eff.imm = id;
          if (op == "spawn") {
            if (auto r = expr("color", false); !r.has_value()) {
              return fail(r.error().code, r.error().message, r.error().line);
            }
          }
        } else if (op == "set_piece_field" || op == "set_cell_field") {
          const bool piece = op == "set_piece_field";
          eff.op = piece ? rules::EffectOp::SetPieceField : rules::EffectOp::SetCellField;
          if (auto r = expr("at", true); !r.has_value()) {
            return fail(r.error().code, r.error().message, r.error().line);
          }
          if (auto r = expr("value", true); !r.has_value()) {
            return fail(r.error().code, r.error().message, r.error().line);
          }
          const std::string field = (*et)["field"].value_or(std::string{});
          const int index = piece ? v.findPieceField(field) : v.findCellField(field);
          if (index < 0) {
            return fail(ErrorCode::ValidationError,
                        ewhere + ": no field is called '" + field + "'", lineOf(enode));
          }
          eff.imm = index;
        } else if (op == "forbid_move") {
          eff.op = rules::EffectOp::ForbidMove;
        } else if (op == "repeat_turn") {
          eff.op = rules::EffectOp::RepeatTurn;
        } else if (op == "end_game") {
          eff.op = rules::EffectOp::EndGame;
          const std::string outcome = (*et)["outcome"].value_or(std::string{"draw"});
          if (outcome == "mover_wins") {
            eff.imm = static_cast<std::int64_t>(rules::Outcome::MoverWins);
          } else if (outcome == "mover_loses") {
            eff.imm = static_cast<std::int64_t>(rules::Outcome::MoverLoses);
          } else if (outcome == "draw") {
            eff.imm = static_cast<std::int64_t>(rules::Outcome::Draw);
          } else {
            return fail(ErrorCode::ValidationError,
                        ewhere + ": outcome must be mover_wins, mover_loses or draw",
                        lineOf(enode));
          }
        } else {
          return fail(ErrorCode::ValidationError,
                      "rule '" + rule.name + "': unknown effect '" + op + "'",
                      lineOf(enode));
        }
        rule.effects.push_back(std::move(eff));
      }
      set.rules.push_back(std::move(rule));
    }

    const rules::RuleEngine engine{set};
    if (auto ok = engine.validate(v); !ok.has_value()) {
      return fail(ok.error().code, ok.error().message);
    }
    auto shared = std::make_shared<const rules::RuleSet>(std::move(set));
    v.ruleSet = shared;
    v.ruleSetDigest = [shared] {
      // Rules change what the game is, so they change its identity. Hashing the textual
      // description keeps that stable without teaching the variant layer about rules.
      const std::string description = shared->describe();
      std::uint64_t h = 0xCBB0'0000'0000'0111ULL;
      for (char c : description)
        h = (h ^ static_cast<std::uint64_t>(static_cast<unsigned char>(c))) *
            0x100000001B3ULL;
      return h;
    };
  }

  // ---- starting position ---------------------------------------------------
  // A temporal variant may give just the opening board for the origin slice (turn 0,
  // line 0) with `[start] origin`; the rest of the lattice starts empty and boards are
  // created as the game is played. Otherwise `board` describes the whole lattice.
  const std::string origin = tbl["start"]["origin"].value_or(std::string{});
  const std::string board = tbl["start"]["board"].value_or(std::string{});
  if (origin.empty() && board.empty()) {
    return fail(ErrorCode::ValidationError,
                "a variant needs [start] board = \"...\" (or origin for a temporal one)");
  }
  if (!origin.empty()) {
    if (v.dims.dims() < 2) {
      return fail(ErrorCode::ValidationError,
                  "[start] origin needs at least two axes to hold a board");
    }
    const std::vector<AxisDecl> two{
        AxisDecl{v.dims.extent(0), AxisKind::Spatial, v.dims.name(0)},
        AxisDecl{v.dims.extent(1), AxisKind::Spatial, v.dims.name(1)},
    };
    const auto flat = DimSpec::create(two);
    if (!flat.has_value()) return fail(flat.error().code, flat.error().message);
    auto parsed = parseBoardSection(*flat, v.pieces, origin);
    if (!parsed.has_value()) return fail(parsed.error().code, parsed.error().message);
    for (const StartPiece& sp : *parsed) {
      Coord c(v.dims.dims());
      for (std::uint8_t a = 0; a < v.dims.dims(); ++a) {
        // The origin board sits in the middle of the timeline axis, so both players have
        // room to branch outward; time starts at turn 0.
        c.c[a] = v.dims.kind(a) == AxisKind::Multiverse
                     ? static_cast<std::int16_t>(v.dims.extent(a) / 2)
                     : 0;
      }
      c.c[0] = sp.at.c[0];
      c.c[1] = sp.at.c[1];
      v.start.push_back(StartPiece{c, sp.type, sp.color});
    }
  } else {
    auto placements = parseBoardSection(v.dims, v.pieces, board);
    if (!placements.has_value())
      return fail(placements.error().code, placements.error().message);
    v.start = std::move(*placements);
  }

  if (auto ok = v.finalize(); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message, ok.error().line);
  }
  return v;
}

Result<VariantSpec> loadVariantFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return fail(ErrorCode::ParseError,
                "cannot open variant file '" + path.string() + "'");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return loadVariantToml(ss.str(), path.string());
}

}  // namespace cb
