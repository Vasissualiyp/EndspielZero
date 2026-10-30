// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/game.hpp"

#include <algorithm>
#include <utility>

namespace cb {

std::string_view toString(GameResult r) noexcept {
  switch (r) {
    case GameResult::InProgress:
      return "in progress";
    case GameResult::WhiteWins:
      return "White wins";
    case GameResult::BlackWins:
      return "Black wins";
    case GameResult::Draw:
      return "draw";
  }
  return "?";
}

std::string_view toString(EndReason r) noexcept {
  switch (r) {
    case EndReason::None:
      return "";
    case EndReason::Checkmate:
      return "checkmate";
    case EndReason::Stalemate:
      return "stalemate";
    case EndReason::DrawClock:
      return "the draw clock";
    case EndReason::Repetition:
      return "threefold repetition";
    case EndReason::NoLegalMoves:
      return "no legal moves";
    case EndReason::RuleDeclared:
      return "a variant rule";
    case EndReason::Resignation:
      return "resignation";
    case EndReason::Agreement:
      return "agreement";
  }
  return "?";
}

namespace {

rules::RuleSet ruleSetOf(const VariantSpec& v) {
  if (!v.ruleSet) return {};
  return *static_cast<const rules::RuleSet*>(v.ruleSet.get());
}

}  // namespace

Game::Game(const VariantSpec& v)
    : v_(&v),
      pos_(Position::startPosition(v)),
      startPos_(pos_),
      gen_(v),
      engine_(ruleSetOf(v)) {
  initTemporal();
  hashes_.push_back(pos_.hash());
  // Whether this variant has royal pieces at all is a property of the variant, not of
  // the position: a king blown off the board must make the move illegal rather than
  // quietly turn the game into one without kings.
  startHasRoyal_ = pos_.findRoyal(Color::White) != kInvalidCell ||
                   pos_.findRoyal(Color::Black) != kInvalidCell;
}

void Game::initTemporal() {
  const DimSpec& d = v_->dims;
  bool turnFound = false;
  bool lineFound = false;
  std::uint8_t spatial[2]{0, 1};
  int spatialCount = 0;
  for (std::uint8_t a = 0; a < d.dims(); ++a) {
    if (d.kind(a) == AxisKind::Temporal && !turnFound) {
      turnAxis_ = a;
      turnFound = true;
    } else if (d.kind(a) == AxisKind::Multiverse && !lineFound) {
      lineAxis_ = a;
      lineFound = true;
    } else if (d.kind(a) == AxisKind::Spatial && spatialCount < 2) {
      spatial[spatialCount++] = a;
    }
  }
  if (!turnFound && !lineFound) return;
  temporal_ = true;
  fileAxis_ = spatial[0];
  rankAxis_ = spatial[1];
  temporal::TemporalPolicy policy;
  policy.originLine = static_cast<temporal::LineId>(d.extent(lineAxis_) / 2);
  policy.whiteSign = v_->temporalWhiteSign;
  policy.blackSign = v_->temporalBlackSign;
  policy.branchAdvance = static_cast<temporal::Turn>(v_->temporalBranchAdvance);
  multi_.emplace(policy, static_cast<temporal::Turn>(d.extent(turnAxis_) - 1),
                 static_cast<temporal::LineId>(d.extent(lineAxis_) - 1));
  temporalTurn_ = pos_.sideToMove();
}

temporal::BoardKey Game::keyOf(CellId c) const {
  const Coord co = v_->dims.toCoord(c);
  return temporal::BoardKey{co.c[turnAxis_], co.c[lineAxis_]};
}

CellId Game::cellOn(const temporal::BoardKey& b, std::int16_t file,
                    std::int16_t rank) const {
  Coord co(v_->dims.dims());
  for (std::uint8_t a = 0; a < v_->dims.dims(); ++a) co.c[a] = 0;
  co.c[fileAxis_] = file;
  co.c[rankAxis_] = rank;
  co.c[turnAxis_] = b.turn;
  co.c[lineAxis_] = b.line;
  return v_->dims.toCell(co);
}

std::vector<temporal::BoardKey> Game::actionable() const {
  std::vector<temporal::BoardKey> out;
  if (!multi_) return out;
  for (const temporal::BoardKey& b : multi_->present()) {
    if (multi_->playable(b) && multi_->sideToMove(b) == temporalTurn_) out.push_back(b);
  }
  return out;
}

void Game::copyBoard(const temporal::BoardKey& from, const temporal::BoardKey& to) {
  const auto files = static_cast<std::int16_t>(v_->dims.extent(fileAxis_));
  const auto ranks = static_cast<std::int16_t>(v_->dims.extent(rankAxis_));
  for (std::int16_t f = 0; f < files; ++f) {
    for (std::int16_t r = 0; r < ranks; ++r) {
      pos_.setCell(cellOn(to, f, r), pos_.at(cellOn(from, f, r)), nullptr);
    }
  }
}

void Game::applyTemporal(const Move& m) {
  const temporal::BoardKey src = keyOf(m.from);
  const temporal::BoardKey dst = keyOf(m.to);
  const Piece mover = pos_.at(m.from);
  const auto created = multi_->apply(src, dst);
  if (!created.has_value() || created->empty()) return;

  // The source's timeline advances without the piece.
  const temporal::BoardKey sf = (*created)[0];
  copyBoard(src, sf);
  const Coord from = v_->dims.toCoord(m.from);
  pos_.setCell(cellOn(sf, from.c[fileAxis_], from.c[rankAxis_]), Piece{}, nullptr);

  const auto arrive = [&](const temporal::BoardKey& b) {
    const Coord to = v_->dims.toCoord(m.to);
    if (m.captureCell != kInvalidCell) {
      const Coord cap = v_->dims.toCoord(m.captureCell);
      pos_.setCell(cellOn(b, cap.c[fileAxis_], cap.c[rankAxis_]), Piece{}, nullptr);
    }
    const Piece arrived =
        m.promoteTo != kNoPiece ? Piece{m.promoteTo, mover.color, 0} : mover;
    pos_.setCell(cellOn(b, to.c[fileAxis_], to.c[rankAxis_]), arrived, nullptr);
  };

  if (created->size() == 1) {
    arrive(sf);  // the move stayed on the source board's own future
  } else {
    const temporal::BoardKey db = (*created)[1];
    copyBoard(dst, db);  // the destination also advances, carrying the arrival
    arrive(db);
  }
}

void Game::settleTemporalTurn() {
  for (int guard = 0; guard < 16; ++guard) {
    if (!actionable().empty()) break;
    temporalTurn_ = opponent(temporalTurn_);
  }
  pos_.setSideToMove(temporalTurn_);
}

bool Game::boardVisible(CellId c) const {
  if (!temporal_ || !multi_) return true;
  const Coord co = v_->dims.toCoord(c);
  return multi_->exists(temporal::BoardKey{co.c[turnAxis_], co.c[lineAxis_]});
}

bool Game::cellInPresent(CellId c) const {
  if (!temporal_ || !multi_) return false;
  const temporal::BoardKey k = keyOf(c);
  for (const temporal::BoardKey& b : multi_->present()) {
    if (b.turn == k.turn && b.line == k.line) return true;
  }
  return false;
}

bool Game::hasCaptureFrom(CellId c) const {
  // kInvalidCell means "from anywhere", which is how a forced-capture rule asks whether
  // the side to move has any capture at all.
  MoveList list(v_->moveUpperBound());
  gen_.generatePseudoLegal(pos_, list);
  for (const Move& m : list) {
    if (m.captureCell == kInvalidCell) continue;
    if (c == kInvalidCell || m.from == c) return true;
  }
  return false;
}

rules::RuleEnv Game::makeEnv(const Move* m, Color mover) const {
  rules::RuleEnv env;
  env.pos = const_cast<Position*>(&pos_);
  env.move = m;
  env.mover = mover;
  env.hasCaptureFrom = [this](CellId c) { return hasCaptureFrom(c); };
  env.isAttacked = [this](CellId c, Color by) { return gen_.isAttacked(pos_, c, by); };
  return env;
}

void Game::invalidate() {
  legalCacheValid_ = false;
}

const std::vector<Move>& Game::legalMoves() const {
  if (legalCacheValid_) return legalCache_;
  legalCacheValid_ = true;
  Position& p = const_cast<Position&>(pos_);

  if (temporal_) {
    // Only pieces on a present board that is the mover's to answer may move. The move
    // generator itself is unchanged: time travel is just another direction.
    p.setSideToMove(temporalTurn_);
    MoveList legal(v_->moveUpperBound());
    gen_.generateLegal(p, legal);
    const std::vector<temporal::BoardKey> act = actionable();
    legalCache_.clear();
    for (const Move& m : legal) {
      const temporal::BoardKey src = keyOf(m.from);
      bool onActionable = false;
      for (const temporal::BoardKey& b : act) {
        if (b == src) {
          onActionable = true;
          break;
        }
      }
      if (!onActionable) continue;
      // The piece may stay on its board, step into that board's own future, or travel to
      // any board that already exists - including one where it is the opponent's turn,
      // which is how a knight off the start square branches a timeline.
      const temporal::BoardKey dst = keyOf(m.to);
      const bool sameBoard = dst.turn == src.turn && dst.line == src.line;
      const bool ownFuture = dst.turn == src.turn + 1 && dst.line == src.line;
      if (!sameBoard && !ownFuture && !multi_->exists(dst)) continue;
      legalCache_.push_back(m);
    }
    return legalCache_;
  }

  if (engine_.empty()) {
    // No rules: royal safety is the only condition, and movegen decides it directly.
    MoveList list(v_->moveUpperBound());
    gen_.generateLegal(p, list);
    legalCache_.assign(list.begin(), list.end());
    return legalCache_;
  }

  // With rules in play, legality has to account for a move's *consequences*. In atomic
  // chess a capture that blows up your own king is illegal, and no amount of looking at
  // the board before the explosion will tell you that. So each candidate is played, its
  // effects are run, and only then is the royal piece examined.
  const Color mover = pos_.sideToMove();
  MoveList pseudo(v_->moveUpperBound());
  gen_.generatePseudoLegal(p, pseudo);

  legalCache_.clear();
  legalCache_.reserve(pseudo.size());
  for (const Move& m : pseudo) {
    rules::RuleEnv filterEnv = makeEnv(&m, mover);
    if (engine_.run(rules::Trigger::OnMoveFilter, filterEnv, nullptr).forbidden) continue;

    Undo u;
    p.make(m, u);
    rules::RuleEnv env = makeEnv(&m, mover);
    if (m.captureCell != kInvalidCell)
      (void)engine_.run(rules::Trigger::OnCapture, env, &u);
    (void)engine_.run(rules::Trigger::OnMoveEnd, env, &u);

    const CellId royal = p.findRoyal(mover);
    const bool hadRoyal = startHasRoyal_;
    // A variant with royals requires yours to survive and to be safe; a variant without
    // them (checkers and friends) has no such condition.
    const bool ok = !hadRoyal || (royal != kInvalidCell &&
                                  !gen_.isAttacked(p, royal, opponent(mover)));

    p.unmake(m, u);
    if (ok) legalCache_.push_back(m);
  }
  return legalCache_;
}

bool Game::isLegal(const Move& m) const {
  const auto& moves = legalMoves();
  return std::find(moves.begin(), moves.end(), m) != moves.end();
}

Result<void> Game::play(const Move& m) {
  if (temporal_) {
    if (!isLegal(m)) {
      return fail(ErrorCode::ValidationError, "that move is not legal in this position");
    }
    temporalHistory_.push_back(TemporalSnapshot{pos_, *multi_, temporalTurn_});
    applyTemporal(m);
    settleTemporalTurn();
    played_.push_back(m);
    repeated_.push_back(false);
    hashes_.push_back(pos_.hash());
    invalidate();
    return {};
  }
  if (!isLegal(m)) {
    return fail(ErrorCode::ValidationError, "that move is not legal in this position");
  }
  const Color mover = pos_.sideToMove();
  Undo u;
  pos_.make(m, u);

  bool repeat = false;
  if (!engine_.empty()) {
    rules::RuleEnv env = makeEnv(&m, mover);
    rules::RuleOutcome outcome;
    if (m.captureCell != kInvalidCell) {
      outcome = engine_.run(rules::Trigger::OnCapture, env, &u);
    }
    const rules::RuleOutcome after = engine_.run(rules::Trigger::OnMoveEnd, env, &u);
    repeat = outcome.repeatTurn || after.repeatTurn;
    if (outcome.gameOver || after.gameOver) {
      ruleEnded_ = true;
      ruleOutcome_ = outcome.gameOver ? outcome.outcome : after.outcome;
      ruleOutcomeMover_ = mover;
    }
    if (repeat) {
      // Hand the turn back. Done through setSideToMove so the hash stays consistent,
      // and reversed the same way in undo().
      pos_.setSideToMove(mover);
    }
    rules::RuleEnv turnEnv = makeEnv(&m, mover);
    (void)engine_.run(rules::Trigger::OnTurnEnd, turnEnv, &u);
  }

  history_.push_back(std::move(u));
  played_.push_back(m);
  repeated_.push_back(repeat);
  hashes_.push_back(pos_.hash());
  invalidate();
  return {};
}

bool Game::undo() {
  if (temporal_) {
    if (temporalHistory_.empty()) return false;
    TemporalSnapshot snap = std::move(temporalHistory_.back());
    temporalHistory_.pop_back();
    pos_ = std::move(snap.pos);
    multi_ = std::move(snap.multi);
    temporalTurn_ = snap.turn;
    played_.pop_back();
    repeated_.pop_back();
    hashes_.pop_back();
    invalidate();
    return true;
  }
  if (history_.empty()) return false;
  // Reverse the turn-repeat flip first, so unmake's own side flip lands where it
  // expects to and the hash comes back exactly.
  if (repeated_.back()) pos_.setSideToMove(opponent(pos_.sideToMove()));
  pos_.unmake(played_.back(), history_.back());
  history_.pop_back();
  played_.pop_back();
  repeated_.pop_back();
  hashes_.pop_back();
  ruleEnded_ = false;
  invalidate();
  return true;
}

void Game::resign(Color who) {
  if (result() != GameResult::InProgress) return;
  resigned_ = true;
  resignedBy_ = who;
}

void Game::agreeDraw() {
  if (result() != GameResult::InProgress) return;
  agreed_ = true;
}

void Game::reset() {
  resigned_ = false;
  agreed_ = false;
  pos_ = startPos_;
  if (temporal_) {
    initTemporal();
    temporalHistory_.clear();
  }
  history_.clear();
  played_.clear();
  repeated_.clear();
  ruleEnded_ = false;
  hashes_.clear();
  hashes_.push_back(pos_.hash());
  invalidate();
}

void Game::setStartPosition(Position p) {
  startPos_ = std::move(p);
  reset();
}

bool Game::inCheck() const {
  return gen_.inCheck(pos_, pos_.sideToMove());
}

int Game::repetitionCount() const {
  const std::uint64_t current = pos_.hash();
  return static_cast<int>(std::count(hashes_.begin(), hashes_.end(), current));
}

EndReason Game::endReason() const {
  if (resigned_) return EndReason::Resignation;
  if (agreed_) return EndReason::Agreement;
  if (ruleEnded_) return EndReason::RuleDeclared;
  if (!legalMoves().empty()) {
    if (v_->halfmoveDrawLimit > 0 && pos_.halfmoveClock() >= v_->halfmoveDrawLimit) {
      return EndReason::DrawClock;
    }
    if (repetitionCount() >= 3) return EndReason::Repetition;
    return EndReason::None;
  }
  // No legal moves. What that means is the variant's business: a game with no royal
  // piece has no checkmate, and stalemate is a draw, a loss or a win by declaration.
  if (pos_.findRoyal(pos_.sideToMove()) == kInvalidCell) return EndReason::NoLegalMoves;
  return inCheck() ? EndReason::Checkmate : EndReason::Stalemate;
}

GameResult Game::result() const {
  const Color side = pos_.sideToMove();
  const Color other = opponent(side);
  if (resigned_) {
    return resignedBy_ == Color::White ? GameResult::BlackWins : GameResult::WhiteWins;
  }
  if (agreed_) return GameResult::Draw;
  if (ruleEnded_) {
    // A rule's outcome is stated relative to whoever made the move that triggered it.
    switch (ruleOutcome_) {
      case rules::Outcome::MoverWins:
        return ruleOutcomeMover_ == Color::White ? GameResult::WhiteWins
                                                 : GameResult::BlackWins;
      case rules::Outcome::MoverLoses:
        return ruleOutcomeMover_ == Color::White ? GameResult::BlackWins
                                                 : GameResult::WhiteWins;
      case rules::Outcome::Draw:
        return GameResult::Draw;
    }
  }
  switch (endReason()) {
    case EndReason::None:
      return GameResult::InProgress;
    case EndReason::Checkmate:
      return other == Color::White ? GameResult::WhiteWins : GameResult::BlackWins;
    case EndReason::Stalemate:
      switch (v_->stalemate) {
        case StalematePolicy::Draw:
          return GameResult::Draw;
        case StalematePolicy::Loss:
          return other == Color::White ? GameResult::WhiteWins : GameResult::BlackWins;
        case StalematePolicy::Win:
          return side == Color::White ? GameResult::WhiteWins : GameResult::BlackWins;
      }
      return GameResult::Draw;
    case EndReason::NoLegalMoves:
      // Being unable to move loses, which is what checkers and similar variants want.
      return other == Color::White ? GameResult::WhiteWins : GameResult::BlackWins;
    case EndReason::DrawClock:
    case EndReason::Repetition:
    case EndReason::RuleDeclared:
    case EndReason::Resignation:
    case EndReason::Agreement:
      return GameResult::Draw;
  }
  return GameResult::InProgress;
}

std::string Game::statusLine() const {
  std::string s = pos_.sideToMove() == Color::White ? "White" : "Black";
  s += " to move";
  const GameResult r = result();
  if (r != GameResult::InProgress) {
    s = std::string(toString(r));
    const EndReason why = endReason();
    if (why != EndReason::None) {
      s += " by ";
      s += toString(why);
    }
  } else if (inCheck()) {
    s += " (in check)";
  }
  s += "  -  ply " + std::to_string(plyCount());
  s += ", " + std::to_string(legalMoves().size()) + " legal move(s)";
  return s;
}

}  // namespace cb
