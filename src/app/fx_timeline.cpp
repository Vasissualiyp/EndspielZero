// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/fx_timeline.hpp"

#include <algorithm>

#include "app/session.hpp"
#include "view/board_boot.hpp"
#include "view/check_ripple.hpp"
#include "view/hud_fx.hpp"

namespace cb::app {

void FxTimeline::observe(const Session* session, double now, bool eligible,
                         bool swallow) {
  if (session == nullptr) {
    *this = FxTimeline{};
    return;
  }
  if (session->id() != sessionId_) {
    const double gate = bootGate_;
    *this = FxTimeline{};
    bootGate_ = gate;
    sessionId_ = session->id();
  }
  if (session->bootSerial() != bootSeen_) {
    if (swallow) {
      bootSeen_ = session->bootSerial();
      bootStart_.reset();
    } else if (eligible) {
      bootSeen_ = session->bootSerial();
      bootStart_ = now;
      // A boot replaces whatever the board was doing: a reset forgets the finished game.
      rippleStart_.reset();
      endStart_.reset();
      endFx_.reset();
      powerUpStart_.reset();
      powerUpFx_.reset();
      captureSeen_ = session->captureSerial();
      captureStart_.reset();
      turnSeen_ = session->turnSerial();
      moveSeen_ = session->moveSerial();
      turnStart_.reset();
      moveStart_.reset();
      checkSeen_ = session->checkSerial();
      endSeen_ = session->endSerial();
      powerUpSeen_ = session->powerUpSerial();
    }
  }
  if (!swallow && !eligible) return;
  if (!session->capturePly()) captureStart_.reset();
  if (session->captureSerial() != captureSeen_) {
    captureSeen_ = session->captureSerial();
    captureStart_ =
        !swallow && session->capturePly() ? std::optional<double>(now) : std::nullopt;
  }
  if (session->turnSerial() != turnSeen_) {
    turnSeen_ = session->turnSerial();
    turnStart_ = swallow ? std::nullopt : std::optional<double>(now);
  }
  if (session->moveSerial() != moveSeen_) {
    moveSeen_ = session->moveSerial();
    moveStart_ = swallow ? std::nullopt : std::optional<double>(now);
  }
  if (swallow || !session->thinkingShown()) {
    thinkingStart_.reset();
  } else if (!thinkingStart_) {
    thinkingStart_ = now;
  }
  if (session->checkSerial() != checkSeen_) {
    checkSeen_ = session->checkSerial();
    if (!swallow && session->checkEvent()) {
      rippleStart_ = now;
      rippleOrigin_ = *session->checkEvent();
    }
  }
  if (session->endSerial() != endSeen_) {
    endSeen_ = session->endSerial();
    endStart_.reset();
    endFx_.reset();
    powerUpStart_.reset();
    powerUpFx_.reset();
    if (!swallow && session->endEvent()) {
      endStart_ = now;
      endFx_ = session->endEvent();
    }
  }
  if (session->powerUpSerial() != powerUpSeen_) {
    powerUpSeen_ = session->powerUpSerial();
    // An undo inside the delay never saw the board go down: nothing powers back up.
    if (!swallow && endStart_ && endFx_ &&
        now - *endStart_ >= static_cast<double>(view::kEndDelaySeconds)) {
      powerUpStart_ = now;
      powerUpFx_ = endFx_;
    }
    endStart_.reset();
    endFx_.reset();
  }
}

std::optional<float> FxTimeline::captureAge(double now) const {
  if (!captureStart_ || now < *captureStart_ || now - *captureStart_ >= .35)
    return std::nullopt;
  return static_cast<float>(now - *captureStart_);
}

std::optional<float> FxTimeline::turnAge(double now) const {
  if (!turnStart_ || now < *turnStart_ ||
      now - *turnStart_ >= static_cast<double>(view::kTurnFlipSeconds))
    return std::nullopt;
  return static_cast<float>(now - *turnStart_);
}

std::optional<float> FxTimeline::moveAge(double now) const {
  if (!moveStart_ || now < *moveStart_ ||
      now - *moveStart_ >= static_cast<double>(view::kRowRevealSeconds))
    return std::nullopt;
  return static_cast<float>(now - *moveStart_);
}

std::optional<float> FxTimeline::thinkingAge(double now) const {
  if (!thinkingStart_ || now < *thinkingStart_) return std::nullopt;
  return static_cast<float>(now - *thinkingStart_);
}

std::optional<float> FxTimeline::bootAge(double now) const {
  if (!bootStart_) return std::nullopt;
  const double start = std::max(*bootStart_, bootGate_);
  if (now < start) return now < *bootStart_ ? std::nullopt : std::optional<float>(0.0f);
  const double age = now - start;
  if (age >= static_cast<double>(view::kBootSeconds)) return std::nullopt;
  return static_cast<float>(age);
}

std::optional<FxTimeline::Ripple> FxTimeline::ripple(double now) const {
  if (!rippleStart_ || now < *rippleStart_) return std::nullopt;
  const double age = now - *rippleStart_;
  if (age >= static_cast<double>(view::kRippleSeconds)) return std::nullopt;
  return Ripple{static_cast<float>(age), rippleOrigin_};
}

std::optional<FxTimeline::End> FxTimeline::end(double now) const {
  if (!endStart_ || !endFx_ || now < *endStart_) return std::nullopt;
  return End{static_cast<float>(now - *endStart_), *endFx_};
}

std::optional<FxTimeline::End> FxTimeline::powerUp(double now) const {
  if (!powerUpStart_ || !powerUpFx_ || now < *powerUpStart_) return std::nullopt;
  const double age = now - *powerUpStart_;
  if (age >= static_cast<double>(view::kPowerUpSeconds)) return std::nullopt;
  return End{static_cast<float>(age), *powerUpFx_};
}

std::optional<FxTimeline::LatchFx> FxTimeline::latchAge(double now) const {
  if (!latchStart_) return std::nullopt;
  const double age = now - *latchStart_;
  if (age < 0.0 || age >= static_cast<double>(kLatchSeconds)) return std::nullopt;
  return LatchFx{latchAxis_, static_cast<float>(age)};
}

bool FxTimeline::animating(double now) const {
  return captureAge(now) || booting(now) || ripple(now) || end(now) || powerUp(now) ||
         turnAge(now) || moveAge(now) || thinkingAge(now) || latchAge(now);
}

}  // namespace cb::app
