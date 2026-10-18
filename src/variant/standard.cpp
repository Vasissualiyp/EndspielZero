// SPDX-License-Identifier: GPL-3.0-or-later
#include "variant/standard.hpp"

namespace cb {
namespace {

MoveAtom atom(std::initializer_list<int> mags, std::uint32_t maxK, MoveMode mode) {
  MoveAtom a;
  for (int m : mags) a.mags.push(static_cast<std::int16_t>(m));
  a.maxK = maxK;
  a.mode = mode;
  return a;
}

}  // namespace

Result<VariantSpec> makeStandardChessLifted(int extraDims) {
  if (extraDims < 0 || 2 + extraDims > kMaxDims) {
    return fail(ErrorCode::BudgetExceeded, "lifted board would exceed kMaxDims");
  }

  VariantSpec v;
  v.name =
      extraDims == 0 ? "standard" : "standard-lifted-" + std::to_string(2 + extraDims);

  std::vector<AxisDecl> axes{{8, AxisKind::Spatial, "file"},
                             {8, AxisKind::Spatial, "rank"}};
  for (int i = 0; i < extraDims; ++i) {
    axes.push_back(AxisDecl{1, AxisKind::Spatial, "w" + std::to_string(i)});
  }
  auto dims = DimSpec::create(axes);
  if (!dims.has_value()) return fail(dims.error().code, dims.error().message);
  v.dims = *dims;

  auto geom = Geometry::create(v.dims, {});  // a plain box: eight walls, no gluing
  if (!geom.has_value()) return fail(geom.error().code, geom.error().message);
  v.geom = *geom;

  constexpr int kRankAxis = 1;
  v.orientationAxis = kRankAxis;

  const auto cell = [&](int file, int rank) {
    Coord p(v.dims.dims());
    p.c[0] = static_cast<std::int16_t>(file);
    p.c[1] = static_cast<std::int16_t>(rank);
    return v.dims.toCell(p);
  };

  // ---- piece types --------------------------------------------------------
  v.pieces.clear();
  v.pieces.emplace_back();  // index 0: the reserved empty entry

  // Pawn. Four atoms, all derived from the general algebra:
  //   push      [1]^1      forward, never captures
  //   double    [1]^2      forward, exactly two, slides so the path must be clear,
  //                        only from the home rank, and leaves an ep target
  //   capture   [1,1]^1    forward, capture only
  MoveAtom push = atom({1}, 1, MoveMode::Leap);
  push.oriented = true;
  push.capture = CapturePolicy::Cannot;

  MoveAtom dbl = atom({1}, 2, MoveMode::Slide);
  dbl.minK = 2;
  dbl.oriented = true;
  dbl.capture = CapturePolicy::Cannot;
  dbl.unmoved = true;
  dbl.marks.push_back(MarkSpec{0, MarkWhere::Passed, 1, 1});  // the "ep" tag, one ply

  MoveAtom pawnCapture = atom({1, 1}, 1, MoveMode::Leap);
  pawnCapture.oriented = true;
  pawnCapture.capture = CapturePolicy::Must;

  PieceTypeDef pawn;
  pawn.name = "pawn";
  pawn.symbol = 'P';
  // En passant: land on the cell a double step passed over, take the pawn beside it.
  MoveAtom passant = atom({1}, 1, MoveMode::Leap);
  passant.pins.push(Pin{static_cast<std::uint8_t>(kRankAxis), 1});
  passant.flip = true;
  passant.capture = CapturePolicy::Must;
  passant.captureAt.push(Pin{static_cast<std::uint8_t>(kRankAxis), -1});
  passant.needsMark = 0;

  pawn.atoms = {push, dbl, pawnCapture, passant};
  pawn.resetsDrawClock = true;
  v.pieces.push_back(pawn);

  PieceTypeDef knight;
  knight.name = "knight";
  knight.symbol = 'N';
  knight.atoms = {atom({1, 2}, 1, MoveMode::Leap)};
  v.pieces.push_back(knight);

  PieceTypeDef bishop;
  bishop.name = "bishop";
  bishop.symbol = 'B';
  bishop.atoms = {atom({1, 1}, kUnlimited, MoveMode::Slide)};
  v.pieces.push_back(bishop);

  PieceTypeDef rook;
  rook.name = "rook";
  rook.symbol = 'R';
  rook.atoms = {atom({1}, kUnlimited, MoveMode::Slide)};
  v.pieces.push_back(rook);

  // The queen moves along every straight line, whatever the dimension; the king takes
  // one step along a line of one or two axes. See `straightLineAtoms`.
  PieceTypeDef queen;
  queen.name = "queen";
  queen.symbol = 'Q';
  queen.atoms = straightLineAtoms(kUnlimited, v.dims.dims());
  v.pieces.push_back(queen);

  PieceTypeDef king;
  king.name = "king";
  king.symbol = 'K';
  king.royal = true;
  king.atoms = straightLineAtoms(1);
  v.pieces.push_back(king);

  const PieceTypeId kPawn = 1, kKnight = 2, kBishop = 3, kRook = 4, kQueen = 5, kKing = 6;
  v.pieces[kPawn].promotesTo = {kKnight, kBishop, kRook, kQueen};

  v.promotion[static_cast<std::size_t>(Color::White)] = Region{kRankAxis, 7};
  v.promotion[static_cast<std::size_t>(Color::Black)] = Region{kRankAxis, 0};
  v.enPassant = false;
  v.stalemate = StalematePolicy::Draw;
  v.halfmoveDrawLimit = 100;

  // ---- starting position --------------------------------------------------
  const PieceTypeId backRank[8] = {kRook, kKnight, kBishop, kQueen,
                                   kKing, kBishop, kKnight, kRook};
  const auto coordOf = [&](int file, int rank) {
    Coord p(v.dims.dims());
    p.c[0] = static_cast<std::int16_t>(file);
    p.c[1] = static_cast<std::int16_t>(rank);
    return p;
  };
  for (int f = 0; f < 8; ++f) {
    v.start.push_back(StartPiece{coordOf(f, 0), backRank[f], Color::White});
    v.start.push_back(StartPiece{coordOf(f, 1), kPawn, Color::White});
    v.start.push_back(StartPiece{coordOf(f, 6), kPawn, Color::Black});
    v.start.push_back(StartPiece{coordOf(f, 7), backRank[f], Color::Black});
  }
  v.startSideToMove = Color::White;

  // ---- castling -----------------------------------------------------------
  // Cell lists rather than "two squares to the right", so the same template
  // shape works in any dimension and on any topology.
  const auto makeCastle = [&](const char* fenLetter, Color c, int rank, int kingToFile,
                              int rookFromFile, int rookToFile,
                              std::initializer_list<int> empty,
                              std::initializer_list<int> safe, std::uint8_t bit) {
    CastleTemplate ct;
    ct.name = fenLetter;
    ct.color = c;
    ct.kingFrom = cell(4, rank);
    ct.kingTo = cell(kingToFile, rank);
    ct.rookFrom = cell(rookFromFile, rank);
    ct.rookTo = cell(rookToFile, rank);
    for (int f : empty) ct.mustBeEmpty.push_back(cell(f, rank));
    for (int f : safe) ct.mustBeSafe.push_back(cell(f, rank));
    ct.rightsBit = bit;
    return ct;
  };
  v.castles.push_back(makeCastle("K", Color::White, 0, 6, 7, 5, {5, 6}, {4, 5, 6}, 0));
  v.castles.push_back(makeCastle("Q", Color::White, 0, 2, 0, 3, {1, 2, 3}, {4, 3, 2}, 1));
  v.castles.push_back(makeCastle("k", Color::Black, 7, 6, 7, 5, {5, 6}, {4, 5, 6}, 2));
  v.castles.push_back(makeCastle("q", Color::Black, 7, 2, 0, 3, {1, 2, 3}, {4, 3, 2}, 3));

  auto ok = v.finalize();
  if (!ok.has_value()) return fail(ok.error().code, ok.error().message, ok.error().line);
  return v;
}

Result<VariantSpec> makeStandardChess() {
  return makeStandardChessLifted(0);
}

}  // namespace cb
