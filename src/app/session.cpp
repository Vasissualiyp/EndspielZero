// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

#include "audio/audio.hpp"
#include "io/fen.hpp"
#include "io/game_file.hpp"
#include "io/notation.hpp"

namespace cb::app {
namespace {

/// Shortest signed difference between two angles, so a shot offset that wraps from just
/// under pi to just over it does not spin the camera the long way round.
float wrapAngle(float a) noexcept {
  constexpr float kPi = 3.14159265358979f;
  constexpr float kTwoPi = 2.0f * kPi;
  while (a > kPi) a -= kTwoPi;
  while (a < -kPi) a += kTwoPi;
  return a;
}

/// Half-extent of a cell's pick box. Slightly over half a cell, so neighbouring boxes
/// overlap and there is no dead strip on a divider line: the nearer cell wins, which puts
/// the switch at the middle of the line. (It was 0.45, leaving a 0.1 gap where nothing
/// was picked, and the hover flickered off between two squares.)
const view::Vec3 kPickHalfExtent{0.505f, 0.505f, 0.06f};

}  // namespace

Result<Action> parseAction(const VariantSpec& v, std::string_view line) {
  std::istringstream in{std::string(line)};
  std::string verb;
  in >> verb;
  Action a;
  if (verb.empty() || verb[0] == '#') return a;  // blank or comment

  if (verb == "click") {
    std::string where;
    in >> where;
    auto cell = parseCell(v.dims, where);
    if (!cell.has_value()) return fail(cell.error().code, cell.error().message);
    a.kind = ActionKind::ClickCell;
    a.cell = *cell;
    return a;
  }
  if (verb == "hover") {
    std::string where;
    in >> where;
    a.kind = ActionKind::Hover;
    if (where == "off") return a;
    auto cell = parseCell(v.dims, where);
    if (!cell.has_value()) return fail(cell.error().code, cell.error().message);
    a.cell = *cell;
    std::string mod;
    in >> mod;
    a.ctrl = mod == "ctrl";
    return a;
  }
  if (verb == "undo") {
    a.kind = ActionKind::Undo;
    return a;
  }
  if (verb == "engine") {
    a.kind = ActionKind::EngineMove;
    return a;
  }
  if (verb == "reset") {
    a.kind = ActionKind::Reset;
    return a;
  }
  if (verb == "confirm") {
    a.kind = ActionKind::Confirm;
    return a;
  }
  if (verb == "cancel") {
    a.kind = ActionKind::Cancel;
    return a;
  }
  if (verb == "resign") {
    a.kind = ActionKind::Resign;
    return a;
  }
  if (verb == "draw") {
    a.kind = ActionKind::AgreeDraw;
    return a;
  }
  if (verb == "orbit") {
    a.kind = ActionKind::Orbit;
    in >> a.dx >> a.dy;
    return a;
  }
  if (verb == "pan") {
    a.kind = ActionKind::Pan;
    in >> a.dx >> a.dy;
    return a;
  }
  if (verb == "zoom") {
    a.kind = ActionKind::Zoom;
    in >> a.dx;
    return a;
  }
  if (verb == "axes") {
    a.kind = ActionKind::SetScreenAxes;
    in >> a.text;
    std::string mode;
    if (in >> mode && mode == "vertical") a.gridVertical = true;
    return a;
  }
  if (verb == "promote") {
    a.kind = ActionKind::SetPromotion;
    in >> a.text;
    return a;
  }
  return fail(ErrorCode::ParseError, "unknown action '" + verb + "'");
}

Result<std::unique_ptr<Session>> Session::create(VariantSpec variant) {
  if (!variant.finalized()) {
    return fail(ErrorCode::Internal, "a session needs a finalized variant");
  }
  auto s = std::unique_ptr<Session>(new Session());
  s->variant_ = std::make_unique<VariantSpec>(std::move(variant));
  s->game_ = std::make_unique<Game>(*s->variant_);
  s->chosenCfg_ = view::ViewConfig::forBoard(s->variant_->dims);
  if (auto ok = s->chosenCfg_.validate(s->variant_->dims); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message);
  }
  s->refreshView();
  s->refreshSnapshot();
  return s;
}

view::ViewConfig Session::effectiveViewConfig() const {
  view::ViewConfig cfg = chosenCfg_;
  if (!flat_) return cfg;
  // A depth axis is invisible from straight above: the layers would sit exactly on top
  // of one another and only the top board would show. Demote any third screen axis to a
  // grid axis so the boards spread across the screen instead, and never stack them.
  if (cfg.screenAxes.size() > 2) {
    SmallVec<std::uint8_t, 3> screen;
    SmallVec<std::uint8_t, kMaxDims> grid;
    for (std::size_t i = 0; i < cfg.screenAxes.size(); ++i) {
      if (i < 2) {
        screen.push(cfg.screenAxes[i]);
      } else {
        grid.push(cfg.screenAxes[i]);
      }
    }
    for (std::uint8_t a : cfg.gridAxes) grid.push(a);
    cfg.screenAxes = screen;
    cfg.gridAxes = grid;
  }
  cfg.gridVertical = false;
  return cfg;
}

bool Session::boardVisible(CellId c) const {
  return game_ == nullptr || game_->boardVisible(c);
}

std::vector<view::TimelineLink> Session::timelineLinks() const {
  std::vector<view::TimelineLink> out;
  if (game_ == nullptr) return out;
  const temporal::Multiverse* m = game_->multiverse();
  if (m == nullptr) return out;
  for (const temporal::Timeline& t : m->timelines().timelines()) {
    if (t.parentTurn < 0) continue;  // the original timeline came from nowhere
    out.push_back(view::TimelineLink{t.parentLine, t.id, t.parentTurn});
  }
  return out;
}

void Session::refreshView() {
  viewCfg_ = effectiveViewConfig();
  placements_ = view::layout(variant_->dims, viewCfg_);
  // A temporal variant's lattice is mostly space the multiverse has not filled yet:
  // show only the boards that exist, so the game opens on one board, not a grid of
  // empty ones.
  placements_.erase(
      std::remove_if(placements_.begin(), placements_.end(),
                     [&](const view::Placement& p) { return !boardVisible(p.cell); }),
      placements_.end());
  seams_ = view::SeamMap::build(*variant_, viewCfg_, theme_);
  // Framed for the shape of the area the board is drawn into, which the interface
  // narrows with its rails - not for the whole window.
  camera_ = view::OrbitCamera::frame(view::boundsOf(placements_), boardAspect_);
  applyViewMode();
}

void Session::refreshTemporalView() {
  if (game_ == nullptr || !game_->isTemporal()) return;
  const float yaw = camera_.yaw;
  const float pitch = camera_.pitch;
  const float distance = camera_.distance;
  refreshView();
  // Center on the board(s) to answer on, so the newly created board the player must move
  // on is in view rather than off to the side. The zoom is the player's: a new board must
  // not pull the camera back, or every turn reads as a zoom-out.
  float cx = 0.0f;
  float cy = 0.0f;
  float cz = 0.0f;
  int n = 0;
  for (const view::Placement& p : placements_) {
    if (game_->cellInPresent(p.cell)) {
      cx += p.x;
      cy += p.y;
      cz += p.z;
      ++n;
    }
  }
  if (n > 0)
    camera_.target = view::Vec3{cx / static_cast<float>(n), cy / static_cast<float>(n),
                                cz / static_cast<float>(n)};
  if (!flat_) {
    camera_.yaw = yaw;
    camera_.pitch = pitch;
  }
  camera_.distance = distance;
}

void Session::setTheme(const view::Theme& t) {
  theme_ = t;
  // The seam colours come out of the palette, so they are rebuilt with it.
  seams_ = view::SeamMap::build(*variant_, viewCfg_, theme_);
}

void Session::setFlatView(bool flat) {
  if (flat == flat_) return;
  flat_ = flat;
  // The layout itself changes: flat mode moves a depth axis out of the way, so the
  // board has to be re-laid and re-framed, not merely re-aimed.
  refreshView();
}

bool Session::feedHotSeat(char key) {
  if (!hotSeatEnabled_ || game_ == nullptr) return false;
  const bool cancel = key == HotSeat::whiteCancel() || key == HotSeat::blackCancel();
  if (!cancel && !HotSeat::ownerOf(key).has_value()) return false;

  const std::optional<CellId> square =
      hotSeatKeys_.feed(key, variant_->dims, game_->position().sideToMove());
  if (square.has_value()) {
    Action a;
    a.kind = ActionKind::ClickCell;
    a.cell = *square;
    (void)apply(a);
  }
  return true;
}

void Session::applyViewMode() {
  camera_.orthographic = flat_;
  if (flat_) {
    // Straight down. The camera's own up-vector fallback handles the degenerate basis,
    // so this needs no nudge off the pole to avoid a singularity.
    camera_.pitch = 1.5707963f;
    camera_.yaw = 0.0f;
  } else {
    // Coming back from a flat view the pitch is still at the pole; pull it into the
    // solid range so the board is read from above rather than edge-on or underneath.
    camera_.pitch = std::clamp(camera_.pitch, 0.05f, 1.5207963f);
  }
}

view::OrbitCamera Session::camera() const noexcept {
  return cameraOver(placements_, view::boundsOf(placements_), viewCfg_);
}

view::OrbitCamera Session::playerCamera() const noexcept {
  view::OrbitCamera out = camera_;
  out.distance *= 1.0f + 0.18f * pullBack_;
  return out;
}

view::OrbitCamera Session::cameraOver(const std::vector<view::Placement>& placements,
                                      const view::Bounds& scene,
                                      const view::ViewConfig& cfg) const noexcept {
  view::OrbitCamera out = camera_;
  out.distance *= 1.0f + 0.18f * pullBack_;
  if (!shotInFlight()) return out;

  // The move camera is a time-varying contribution to this one accessor, so the board,
  // the picking ray and every label drawn over it agree by construction - which is why it
  // is folded in here rather than kept as a second camera elsewhere.
  //
  // The shot is applied as an **offset in its own frame**: the pose it would use with
  // following off is subtracted first, so the player's own orbit, pan and zoom survive
  // and the move camera only leads them. A player who turned the board keeps that turn.
  //
  // `placements`/`scene`/`cfg` are parameters so the geometry view can ask for the same
  // blend against its own surface placements (M17.16); the flat board passes the
  // session's own layout, so this is the one definition either way.
  view::CameraPolicy off = cameraPolicy_;
  off.follow = view::FollowMode::Off;
  const view::CameraPose settled =
      view::moveCamera(lastPath_, placements, cfg, scene, off, 1.0f);
  const view::CameraPose shot = view::moveCamera(lastPath_, placements, cfg, scene,
                                                 cameraPolicy_, anim_.progress());
  const float k = followStrength_ * view::shotEnvelope(anim_.progress());
  out.target = out.target + (shot.target - settled.target) * k;
  out.yaw = out.yaw + wrapAngle(shot.yaw - settled.yaw) * k;
  out.pitch = std::clamp(out.pitch + (shot.pitch - settled.pitch) * k, 0.10f, 1.53f);
  out.distance = std::max(1.0f, out.distance + (shot.distance - settled.distance) * k);
  return out;
}

bool Session::advanceAnimation(float dt) {
  anim_.advance(dt);
  if (!anim_.active()) pathValid_ = false;
  return anim_.active();
}

void Session::setCameraMode(std::string_view mode) noexcept {
  if (mode == "piece") {
    cameraPolicy_.follow = view::FollowMode::Piece;
  } else if (mode == "route") {
    cameraPolicy_.follow = view::FollowMode::Route;
  } else {
    cameraPolicy_.follow = view::FollowMode::Off;  // the default, and the pre-M11 look
  }
}

bool Session::shotInFlight() const noexcept {
  return pathValid_ && anim_.active() && cameraPolicy_.follow != view::FollowMode::Off &&
         followStrength_ > 0.0f;
}

void Session::setBoardAspect(float aspect) {
  if (aspect <= 0.0f || std::abs(aspect - boardAspect_) < 0.01f) return;
  boardAspect_ = aspect;
  const view::Vec3 keptTarget = camera_.target;
  const float keptDistance = camera_.distance;
  const float keptYaw = camera_.yaw;
  const float keptPitch = camera_.pitch;
  camera_ = view::OrbitCamera::frame(view::boundsOf(placements_), boardAspect_);
  // A resize re-frames, but whatever the player had turned, panned or zoomed to is
  // theirs to keep. A temporal board has just been centered on the present board, so it
  // keeps that target even on the first framing.
  if (framedOnce_ || (game_ != nullptr && game_->isTemporal())) {
    camera_.target = keptTarget;
    camera_.distance = keptDistance;
    camera_.yaw = keptYaw;
    camera_.pitch = keptPitch;
  }
  framedOnce_ = true;
  applyViewMode();
}

void Session::frameOn(const view::Bounds& b, float headroom) {
  const float yaw = camera_.yaw;
  const float pitch = camera_.pitch;
  camera_ = view::OrbitCamera::frame(b, boardAspect_, headroom);
  camera_.yaw = yaw;
  camera_.pitch = pitch;
  framedOnce_ = true;
  applyViewMode();
}

void Session::easeFrameOn(const view::Bounds& b, float headroom, float k) {
  const view::OrbitCamera want = view::OrbitCamera::frame(b, boardAspect_, headroom);
  const float t = std::clamp(k, 0.0f, 1.0f);
  // Only the frame moves toward the box; the angle, roll and field of view stay the
  // player's, exactly as `frameOn` promises.
  camera_.target = camera_.target + (want.target - camera_.target) * t;
  camera_.distance = camera_.distance + (want.distance - camera_.distance) * t;
  framedOnce_ = true;
  applyViewMode();
}

const Move* Session::findMove(CellId from, CellId to) const {
  const Move* best = nullptr;
  for (const Move& m : game_->legalMoves()) {
    if (m.from != from || m.to != to) continue;
    if (best == nullptr) {
      best = &m;
      continue;
    }
    // Several moves can share from/to: the promotion choices. Prefer the piece the user
    // asked for, and otherwise the last one declared, which is the strongest by
    // convention in every variant that ships here.
    if (!promotionPreference_.empty() && m.promoteTo != kNoPiece &&
        variant_->pieces[m.promoteTo].name == promotionPreference_) {
      best = &m;
    } else if (promotionPreference_.empty() && m.promoteTo > best->promoteTo) {
      best = &m;
    }
  }
  return best;
}

std::vector<PieceTypeId> Session::promotionChoicesFor(CellId from, CellId to) const {
  std::vector<PieceTypeId> out;
  for (const Move& m : game_->legalMoves()) {
    if (m.from != from || m.to != to || m.promoteTo == kNoPiece) continue;
    if (std::find(out.begin(), out.end(), m.promoteTo) == out.end())
      out.push_back(m.promoteTo);
  }
  return out;
}

Result<void> Session::playChecked(const Move& m) {
  const Move chosen = m;  // play() invalidates the legal-move cache the move points into
  // The route is traced against the position the piece set off from, before the move is
  // applied: occupancy decides which way round a glued board the piece actually went,
  // and after play() the blocker it avoided and the mover itself are both gone.
  const Piece mover = game_->position().at(chosen.from);
  view::MovePath path;
  const bool animating = animSeconds_ > 0.0f && !mover.empty();
  if (animating) {
    // The piece's orientation, not its colour: a pawn a seam has turned round walks the
    // other colour's forward directions (Piece::flipped), and so does its route.
    path = view::tracePath(*variant_, game_->position(), mover.type, mover.orientation(),
                           chosen);
  }
  if (auto ok = game_->play(chosen); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message);
  }
  // One sound for the move that landed - a capture is a sharper, lower version of it -
  // and a second, distinct one the moment the new side to move is in check (M18.5). The
  // whole call is a no-op when no sink is installed, which is every capture and every
  // test: playing a sound must never require a device.
  audio::play(chosen.isCapture() ? audio::Sound::Capture : audio::Sound::Move);
  if (game_->inCheck()) audio::play(audio::Sound::Check);
  if (animating) {
    anim_.start(viewCfg_, placements_, seams_, theme_, path, animSeconds_);
    // The camera reads the same route the animation draws, so the two cannot disagree
    // about which way round a glued board the piece went.
    lastPath_ = path;
    pathValid_ = true;
  } else {
    anim_.clear();
    pathValid_ = false;
  }
  message_ = moveText(*variant_, chosen);
  selected_ = kInvalidCell;
  pending_ = PendingPromotion{};
  refreshSnapshot();
  refreshTemporalView();
  return {};
}

Result<void> Session::choosePromotion(PieceTypeId piece) {
  if (!pending_.active) return fail(ErrorCode::Internal, "no promotion is pending");
  for (const Move& m : game_->legalMoves()) {
    if (m.from == pending_.from && m.to == pending_.to && m.promoteTo == piece) {
      return playChecked(m);
    }
  }
  return fail(ErrorCode::ValidationError, "that is not one of the promotion choices");
}

void Session::cancelPromotion() {
  pending_ = PendingPromotion{};
  selected_ = kInvalidCell;
  message_ = "promotion cancelled";
  refreshSnapshot();
}

Result<void> Session::confirmMove() {
  if (!pendingMove_.active) {
    return fail(ErrorCode::Internal, "no move is waiting for confirmation");
  }
  const CellId from = pendingMove_.from;
  const CellId to = pendingMove_.to;
  pendingMove_ = PendingMove{};
  const Move* m = findMove(from, to);
  if (m == nullptr) {
    message_ = "that move is no longer legal";
    selected_ = kInvalidCell;
    refreshSnapshot();
    return fail(ErrorCode::ValidationError, "that move is no longer legal");
  }
  return playChecked(*m);
}

void Session::cancelMove() {
  if (!pendingMove_.active) return;
  pendingMove_ = PendingMove{};
  message_ = "cancelled";
  refreshSnapshot();
}

void Session::refreshSnapshot() {
  // A piece that arrives on the square under the pointer (the player's own move, the
  // computer's, an undo) is not hover-tinted until the pointer leaves the square and
  // comes back: the tint would otherwise sit on the piece that just landed.
  Piece before{};
  if (hoverCell_ < snapshot_.cellCount()) before = snapshot_.at(hoverCell_);
  snapshot_ = view::PositionView::capture(game_->position());
  if (hoverCell_ < snapshot_.cellCount()) {
    const Piece after = snapshot_.at(hoverCell_);
    if (!after.empty() &&
        (after.type != before.type || after.colorOf() != before.colorOf())) {
      suppressedHover_ = hoverCell_;
    }
  }
  snapshot_.setSelected(selected_);

  std::vector<CellId> targets;
  if (selected_ != kInvalidCell) {
    for (const Move& m : game_->legalMoves()) {
      if (m.from == selected_) targets.push_back(m.to);
    }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  }
  snapshot_.setHighlighted(std::move(targets));
  refreshHints();
}

bool Session::cancelSelection() {
  if (boardLocked()) return false;
  if (pending_.active) {
    cancelPromotion();
    return true;
  }
  if (pendingMove_.active) {
    cancelMove();
    return true;
  }
  if (selected_ == kInvalidCell) return false;
  selected_ = kInvalidCell;
  refreshSnapshot();
  return true;
}

void Session::setHintOptions(const HintOptions& o) {
  if (o.ctrlPeek == hints_.ctrlPeek && o.attackHints == hints_.attackHints) return;
  hints_ = o;
  refreshHints();
}

void Session::setHover(CellId cell, bool ctrl) {
  if (cell == hoverCell_ && ctrl == hoverCtrl_) return;
  hoverCell_ = cell;
  hoverCtrl_ = ctrl;
  if (cell != suppressedHover_) suppressedHover_ = kInvalidCell;
  refreshHints();
}

void Session::refreshHints() {
  snapshot_.setPeekMoves({});
  snapshot_.setHint({}, {}, {}, {});
  snapshot_.setHover(kInvalidCell);
  const CellId cell = hoverCell_;
  if (cell >= variant_->dims.cellCount()) return;
  if (shotInFlight() || watching_ || cell == suppressedHover_) return;
  // The hovered square is always marked, whatever the options say.
  snapshot_.setHover(cell);
  // The attack, defence and peek hints are Ctrl-only.
  if (!hoverCtrl_ || game_->isTemporal() || game_->result() != GameResult::InProgress ||
      pending_.active || pendingMove_.active) {
    return;
  }
  const Position& pos = game_->position();
  const MoveGen& gen = game_->moveGen();
  const auto distinct = [](std::vector<CellId> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
  };
  const auto fold = [&](const std::vector<MoveGen::Attacker>& as,
                        std::vector<CellId>& from, std::vector<CellId>& path) {
    for (const auto& a : as) {
      from.push_back(a.from);
      path.insert(path.end(), a.path.begin(), a.path.end());
    }
    from = distinct(std::move(from));
    path = distinct(std::move(path));
  };

  const auto highlighted = snapshot_.highlighted();
  const bool isTarget =
      std::find(highlighted.begin(), highlighted.end(), cell) != highlighted.end();
  std::vector<CellId> af, ap, df, dp;
  if (hints_.attackHints && isTarget && selected_ != kInvalidCell) {
    // A legal destination: judged on the position after the move.
    const Move* m = findMove(selected_, cell);
    if (m == nullptr) return;
    const Color mover = pos.sideToMove();
    Game after = *game_;
    if (!after.play(*m).has_value()) return;
    fold(after.moveGen().attackers(after.position(), cell, opponent(mover)), af, ap);
    fold(after.moveGen().attackers(after.position(), cell, mover), df, dp);
  } else if (!pos.at(cell).empty()) {
    // Ctrl over a piece: its moves (either side's), and who attacks and who defends it.
    if (hints_.ctrlPeek) {
      std::vector<CellId> to;
      if (pos.at(cell).colorOf() == pos.sideToMove()) {
        for (const Move& m : game_->legalMoves()) {
          if (m.from == cell) to.push_back(m.to);
        }
      } else {
        MoveList l(variant_->moveUpperBound());
        gen.movesFrom(pos, cell, l);
        for (const Move& m : l) to.push_back(m.to);
      }
      snapshot_.setPeekMoves(distinct(std::move(to)));
    }
    if (!hints_.attackHints) return;
    // The piece's own square: the other side's attackers, and its own side's defenders
    // (the probe lifts the piece itself off its square, so it never defends itself).
    const Color own = pos.at(cell).colorOf();
    fold(gen.attackers(pos, cell, opponent(own)), af, ap);
    fold(gen.attackers(pos, cell, own), df, dp);
  } else if (hints_.attackHints) {
    // An empty square: who attacks it, and who could recapture there.
    fold(gen.attackers(pos, cell, opponent(pos.sideToMove())), af, ap);
    fold(gen.attackers(pos, cell, pos.sideToMove()), df, dp);
  } else {
    return;
  }
  snapshot_.setHint(std::move(af), std::move(ap), std::move(df), std::move(dp));
}

Result<void> Session::apply(const Action& a) {
  // While the engine plays a game for a watcher (M12.8), the board is not the player's:
  // the actions that would move, select, undo or end the game are refused outright. The
  // view controls - orbit, pan, zoom, axes, promotion preference - still pass through, so
  // a watcher can look around the board while it plays.
  if (watching_) {
    switch (a.kind) {
      case ActionKind::ClickCell:
      case ActionKind::Undo:
      case ActionKind::Reset:
      case ActionKind::Resign:
      case ActionKind::AgreeDraw:
      case ActionKind::Confirm:
      case ActionKind::Cancel:
      case ActionKind::EngineMove:
        return {};
      default:
        break;
    }
  }
  // The computer's turn: its pieces are not the player's to move. Undo is still the
  // player's - it takes back to the player's own last turn (below).
  if (engineToMove() &&
      (a.kind == ActionKind::ClickCell || a.kind == ActionKind::Confirm)) {
    return {};
  }
  switch (a.kind) {
    case ActionKind::None:
      return {};

    case ActionKind::Hover:
      setHover(a.cell, a.ctrl);
      return {};

    case ActionKind::ClickCell: {
      if (a.cell >= variant_->dims.cellCount()) {
        return fail(ErrorCode::OutOfRange, "clicked cell is off the board");
      }
      // While a promotion is pending the board is frozen: the player has already
      // committed to the move and owes only the choice of piece.
      if (pending_.active || pendingMove_.active) return {};
      const Piece piece = game_->position().at(a.cell);
      const bool ownPiece =
          !piece.empty() && piece.colorOf() == game_->position().sideToMove();

      if (selected_ != kInvalidCell) {
        if (const Move* m = findMove(selected_, a.cell); m != nullptr) {
          auto choices = promotionChoicesFor(selected_, a.cell);
          if (choices.size() > 1 && promotionPreference_.empty()) {
            // More than one thing the pawn could become and no standing preference:
            // ask, rather than picking for the player.
            pending_ = PendingPromotion{true, selected_, a.cell, std::move(choices)};
            message_ = "choose a piece";
            refreshSnapshot();
            return {};
          }
          if (confirmMoves_) {
            pendingMove_ = PendingMove{true, selected_, a.cell};
            message_ = "confirm the move?";
            refreshSnapshot();
            return {};
          }
          return playChecked(*m);
        }
        if (a.cell == selected_) {
          selected_ = kInvalidCell;
          message_ = "deselected";
          refreshSnapshot();
          return {};
        }
      }

      if (ownPiece) {
        selected_ = a.cell;
        message_ = "selected " + cellName(variant_->dims, a.cell);
      } else {
        selected_ = kInvalidCell;
        // Clicking an empty cell or an enemy piece with nothing selected is an ordinary
        // part of using a board, not an error.
        message_ = piece.empty() ? "" : "that piece is not yours to move";
      }
      refreshSnapshot();
      return {};
    }

    case ActionKind::Resign:
      game_->resign(game_->position().sideToMove());
      selected_ = kInvalidCell;
      message_ = "resigned";
      refreshSnapshot();
      return {};

    case ActionKind::AgreeDraw:
      game_->agreeDraw();
      selected_ = kInvalidCell;
      message_ = "a draw was agreed";
      refreshSnapshot();
      return {};

    case ActionKind::Confirm:
      return confirmMove();

    case ActionKind::Cancel:
      cancelMove();
      return {};

    case ActionKind::EngineMove: {
      // Synchronous, for scripts and captures: the same search the opponent runs, for
      // whichever side is to move.
      stopEngine();
      if (game_->result() != GameResult::InProgress) return {};
      if (!engineEval_) engineEval_ = std::make_unique<ai::Evaluator>(*variant_);
      const ai::SearchResult r = ai::chooseMove(*game_, *engineEval_, engineOptions_);
      if (!r.found) return {};
      return playChecked(r.best);
    }

    case ActionKind::Undo:
      pending_ = PendingPromotion{};
      pendingMove_ = PendingMove{};
      stopEngine();
      if (!game_->undo()) {
        message_ = "nothing to undo";
        return {};
      }
      // Against the computer, undo goes back to the player's own turn: taking back only
      // the computer's reply would just have it play the same reply again.
      while (engineSide_ && game_->position().sideToMove() == *engineSide_ &&
             game_->undo()) {
      }
      selected_ = kInvalidCell;
      message_ = "undone";
      refreshSnapshot();
      refreshTemporalView();
      return {};

    case ActionKind::Reset:
      stopEngine();
      game_->reset();
      ++presentationEpoch_;
      pending_ = PendingPromotion{};
      pendingMove_ = PendingMove{};
      selected_ = kInvalidCell;
      message_ = "reset";
      refreshSnapshot();
      refreshTemporalView();
      return {};

    case ActionKind::Orbit:
      camera_.yaw += a.dx;
      // A flat view is always straight down. A solid one may tilt, but never so far as
      // to look up at the board's underside: elevation stays strictly between the
      // horizon and the pole, so the top face is always the one being read.
      if (flat_) {
        camera_.pitch = 1.5707963f;
      } else {
        camera_.pitch = std::clamp(camera_.pitch + a.dy, 0.05f, 1.5207963f);
      }
      return {};

    case ActionKind::Pan: {
      // Slide the look-at point in the plane the camera is facing, so the board moves
      // with the mouse and the viewing angle does not change.
      const view::Vec3 forward = view::normalize(camera_.target - camera_.eye());
      const view::Vec3 right = view::normalize(view::cross(forward, camera_.upHint()));
      const view::Vec3 up = view::cross(right, forward);
      camera_.target = camera_.target + right * a.dx + up * a.dy;
      return {};
    }

    case ActionKind::Zoom:
      if (a.dx > 0)
        camera_.distance = std::clamp(camera_.distance * a.dx, 1.0f, 10000.0f);
      return {};

    case ActionKind::SetScreenAxes: {
      view::ViewConfig cfg;
      if (a.text == "default") {
        // The board's own view: every axis it can draw, with the rest as a grid.
        cfg = view::ViewConfig::forBoard(variant_->dims);
      } else {
        std::stringstream ss(a.text);
        std::string name;
        std::vector<bool> used(variant_->dims.dims(), false);
        while (std::getline(ss, name, ',')) {
          const int idx = variant_->dims.axisIndex(name);
          if (idx < 0) {
            return fail(ErrorCode::ValidationError, "no axis is called '" + name + "'");
          }
          cfg.screenAxes.push(static_cast<std::uint8_t>(idx));
          used[static_cast<std::size_t>(idx)] = true;
        }
        for (std::uint8_t ax = 0; ax < variant_->dims.dims(); ++ax) {
          if (!used[ax]) cfg.gridAxes.push(ax);
        }
        cfg.gridVertical = a.gridVertical;
      }
      if (auto ok = cfg.validate(variant_->dims); !ok.has_value()) {
        return fail(ok.error().code, ok.error().message);
      }
      chosenCfg_ = cfg;
      refreshView();
      message_ = a.text == "default" ? "view: default" : "view: " + a.text;
      return {};
    }

    case ActionKind::SetPromotion:
      if (variant_->findPiece(a.text) == kNoPiece) {
        return fail(ErrorCode::ValidationError, "no piece is called '" + a.text + "'");
      }
      promotionPreference_ = a.text;
      message_ = "promoting to " + a.text;
      return {};
  }
  return {};
}

Result<void> Session::loadFen(std::string_view fen) {
  auto p = fromFen(*variant_, fen);
  if (!p.has_value()) return fail(p.error().code, p.error().message);
  game_ = std::make_unique<Game>(*variant_);
  game_->position() = std::move(*p);
  ++presentationEpoch_;
  selected_ = kInvalidCell;
  // A position entry replaces the game, so no move can still be in flight: a stale
  // animation would otherwise let the move camera keep shooting the old route.
  anim_.clear();
  pathValid_ = false;
  message_ = "position set";
  refreshSnapshot();
  return {};
}

Result<void> Session::saveGame(const std::filesystem::path& path) const {
  const std::string text = toGameFileText(*variant_, *game_);
  std::ofstream out(path, std::ios::trunc);
  if (!out) return fail(ErrorCode::Internal, "cannot write '" + path.string() + "'");
  out << text;
  if (!out) return fail(ErrorCode::Internal, "writing '" + path.string() + "' failed");
  return {};
}

Result<void> Session::loadGame(const GameFile& file) {
  if (!file.variant.empty() && file.variant != variant_->name) {
    return fail(ErrorCode::ValidationError, "game file is for variant '" + file.variant +
                                                "', not '" + variant_->name + "'");
  }
  // applyGameFile can replace the start and play several moves before failing.
  ++presentationEpoch_;
  if (auto ok = applyGameFile(file, *game_); !ok.has_value()) return ok;
  selected_ = kInvalidCell;
  pending_ = PendingPromotion{};
  message_ = "game loaded";
  refreshSnapshot();
  refreshTemporalView();
  return {};
}

Result<void> Session::playMoveText(std::string_view text) {
  for (const Move& m : game_->legalMoves()) {
    if (moveText(*variant_, m) == text) return playChecked(m);
  }
  return fail(ErrorCode::ValidationError,
              "'" + std::string(text) + "' is not legal in this position");
}

Result<void> Session::startWatching(GameFile file, float dwellSeconds,
                                    float previewSeconds) {
  stopWatching();  // replace any watch already running
  watchFile_ = std::make_unique<GameFile>(std::move(file));
  watchRunner_ = std::make_unique<GameRunner>(*watchFile_, *this);
  if (auto ok = watchRunner_->reset(); !ok.has_value()) {
    stopWatching();
    return ok;
  }
  watchDwell_ = std::max(0.0f, dwellSeconds);
  watchPreview_ = std::max(0.0f, previewSeconds);
  watchClock_ = 0.0f;
  previewing_ = false;
  selected_ = kInvalidCell;
  pending_ = PendingPromotion{};
  pendingMove_ = PendingMove{};
  watching_ = true;
  if (watchRunner_->done()) {  // a file with no moves is nothing to watch
    message_ = "nothing to watch";
    stopWatching();
    return {};
  }
  if (auto ok = watchRunner_->step(); !ok.has_value()) {
    const ErrorCode code = ok.error().code;
    const std::string text = ok.error().format();
    stopWatching();
    message_ = text;
    return fail(code, ok.error().message);
  }
  message_ = "watching " + watchFile_->variant + "  -  move 1 of " +
             std::to_string(watchRunner_->count());
  return {};
}

void Session::stopWatching() noexcept {
  watching_ = false;
  watchClock_ = 0.0f;
  watchDwell_ = 0.0f;
  watchPreview_ = 0.0f;
  previewing_ = false;
  watchRunner_.reset();
  watchFile_.reset();
}

bool Session::advanceWatching(float dt) {
  if (!watching_ || watchRunner_ == nullptr) return false;
  // Still drawing the move: on a flat board that is the move animation, and on a shape
  // the follow choreography keeps it active until the board has morphed home, so this one
  // test covers both. Hold the dwell clock while anything is moving.
  if (anim_.active()) {
    watchClock_ = 0.0f;
    return true;
  }
  watchClock_ += dt;
  if (watchClock_ < watchDwell_) return true;
  if (watchPreview_ > 0.0f && !watchRunner_->done()) {
    if (!previewing_) {
      // Show the move before it happens: select the piece that is about to go, so the
      // board lights where it can go, for `watchPreview_` seconds.
      const std::string next = watchRunner_->nextMoveText();
      for (const Move& m : game_->legalMoves()) {
        if (moveText(*variant_, m) == next) {
          selected_ = m.from;
          refreshSnapshot();
          previewing_ = true;
          previewClock_ = 0.0f;
          return true;
        }
      }
    } else {
      previewClock_ += dt;
      if (previewClock_ < watchPreview_) return true;
      previewing_ = false;
    }
  }
  watchClock_ = 0.0f;
  if (watchRunner_->done()) {
    // The last move has settled and been held. Leave the mode; the caller returns to the
    // menu. The board is deliberately not left sitting on the final position.
    stopWatching();
    return false;
  }
  if (auto ok = watchRunner_->step(); !ok.has_value()) {
    message_ = ok.error().format();
    stopWatching();
    return false;
  }
  message_ = "watching " + watchFile_->variant + "  -  move " +
             std::to_string(watchRunner_->index()) + " of " +
             std::to_string(watchRunner_->count());
  return true;
}

std::size_t Session::watchIndex() const noexcept {
  return watchRunner_ != nullptr ? watchRunner_->index() : 0;
}

std::size_t Session::watchCount() const noexcept {
  return watchRunner_ != nullptr ? watchRunner_->count() : 0;
}

bool Session::watchStep() {
  if (!watching_ || watchRunner_ == nullptr || watchRunner_->done()) return false;
  previewing_ = false;
  if (auto ok = watchRunner_->step(); !ok.has_value()) {
    message_ = ok.error().format();
    return false;
  }
  watchClock_ = 0.0f;
  return true;
}

Result<void> Session::watchSeek(std::size_t index) {
  if (!watching_ || watchRunner_ == nullptr) {
    return fail(ErrorCode::OutOfRange, "no watched game to seek in");
  }
  if (index > watchRunner_->count()) {
    return fail(ErrorCode::OutOfRange, "seek past the last move");
  }
  if (auto ok = watchRunner_->reset(); !ok.has_value()) return ok;
  previewing_ = false;
  selected_ = kInvalidCell;
  for (std::size_t i = 0; i < index; ++i) {
    if (auto ok = watchRunner_->step(); !ok.has_value()) return ok;
  }
  (void)advanceAnimation(1.0e6f);  // settle: a seek is a jump, not a move to watch
  watchClock_ = 0.0f;
  return {};
}

CellId Session::pickPixel(float px, float py, float width, float height) const {
  if (boardLocked()) return kInvalidCell;
  const auto ray = camera().pickRay(px, py, width, height);
  const int hit = view::pickBox(ray, placements_, kPickHalfExtent);
  return hit < 0 ? kInvalidCell : placements_[static_cast<std::size_t>(hit)].cell;
}

CellId Session::clickPixel(float px, float py, float width, float height) {
  // While a shot owns the view, or while the engine is playing a game for a watcher, the
  // board is locked: the pose may be moving, and in either case the position is not the
  // player's to touch. One gate, `boardLocked`, so the two cases cannot drift apart.
  if (boardLocked()) return kInvalidCell;
  // The effective camera, not the raw member: the pause pull-back moves the board, and
  // the ray has to move with it or a click lands beside the cell under the cursor.
  const auto ray = camera().pickRay(px, py, width, height);
  const int hit = view::pickBox(ray, placements_, kPickHalfExtent);
  if (hit < 0) {
    selected_ = kInvalidCell;
    refreshSnapshot();
    return kInvalidCell;
  }
  const CellId cell = placements_[static_cast<std::size_t>(hit)].cell;
  Action a;
  a.kind = ActionKind::ClickCell;
  a.cell = cell;
  (void)apply(a);
  return cell;
}

Result<void> Session::applyScript(std::string_view script) {
  std::istringstream in{std::string(script)};
  std::string line;
  int lineNumber = 0;
  while (std::getline(in, line)) {
    ++lineNumber;
    auto action = parseAction(*variant_, line);
    if (!action.has_value()) {
      return fail(action.error().code, action.error().message, lineNumber);
    }
    if (auto ok = apply(*action); !ok.has_value()) {
      return fail(ok.error().code, ok.error().message, lineNumber);
    }
  }
  return {};
}

std::string Session::statusLine() const {
  std::string s = game_->statusLine();
  if (!message_.empty()) s += "  |  " + message_;
  return s;
}

// ---- the computer opponent (M10.2-M10.3) ------------------------------------------

Session::~Session() {
  stopEngine();
}

void Session::setEngineSide(std::optional<Color> side) {
  if (side == engineSide_) return;
  stopEngine();
  engineSide_ = side;
  engineClock_ = 0.0f;
}

bool Session::engineToMove() const noexcept {
  return engineSide_.has_value() && !watching_ && game_ != nullptr &&
         game_->result() == GameResult::InProgress &&
         game_->position().sideToMove() == *engineSide_;
}

void Session::stopEngine() {
  if (!engineJob_.valid()) return;
  if (engineStop_) engineStop_->store(true);
  engineJob_.wait();
  engineJob_ = {};
}

void Session::startEngine() {
  if (!engineEval_) engineEval_ = std::make_unique<ai::Evaluator>(*variant_);
  engineStop_ = std::make_shared<std::atomic<bool>>(false);
  engineAskedHash_ = game_->position().hash();
  engineAskedPly_ = game_->plyCount();
  ai::SearchOptions opts = engineOptions_;
  opts.stop = engineStop_.get();
  // The worker owns a copy of the game and of the stop flag; the evaluator and the
  // variant are the session's and outlive it, because stopEngine() waits for it.
  engineJob_ = std::async(std::launch::async, [game = *game_, eval = engineEval_.get(),
                                               opts, stop = engineStop_]() {
    (void)stop;  // held so the flag outlives an abandoned search
    return ai::chooseMove(game, *eval, opts);
  });
}

void Session::takeEngineAnswer(const ai::SearchResult& r) {
  // The question was about a position that is no longer on the board: drop the answer.
  if (!r.found || game_->position().hash() != engineAskedHash_ ||
      game_->plyCount() != engineAskedPly_ || !engineToMove()) {
    return;
  }
  (void)playChecked(r.best);
}

bool Session::advanceEngine(float dt) {
  if (engineJob_.valid()) {
    if (engineJob_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return true;
    const ai::SearchResult r = engineJob_.get();
    engineJob_ = {};
    takeEngineAnswer(r);
    engineClock_ = 0.0f;
    return true;
  }
  if (!engineToMove()) {
    engineClock_ = 0.0f;
    return false;
  }
  // Let the player's move land and be seen before the reply starts moving.
  if (anim_.active()) return true;
  engineClock_ += dt;
  if (engineClock_ < 0.25f) return true;
  startEngine();
  return true;
}

void Session::finishEngineMove() {
  if (!engineJob_.valid()) {
    if (!engineToMove()) return;
    startEngine();
  }
  const ai::SearchResult r = engineJob_.get();
  engineJob_ = {};
  takeEngineAnswer(r);
}

}  // namespace cb::app
