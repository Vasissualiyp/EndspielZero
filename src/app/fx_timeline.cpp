// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/fx_timeline.hpp"

#include "app/session.hpp"
#include "view/board_boot.hpp"
#include "view/check_ripple.hpp"

namespace cb::app {

void FxTimeline::observe(const Session* session, double now, bool eligible, bool swallow) {
  if (session == nullptr) {
    *this = FxTimeline{};
    return;
  }
  if (session->id() != sessionId_) {
    *this = FxTimeline{};
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
      checkSeen_ = session->checkSerial();
      endSeen_ = session->endSerial();
      powerUpSeen_ = session->powerUpSerial();
    }
  }
  if (!swallow && !eligible) return;
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
    if (!swallow && endStart_ && endFx_) {
      powerUpStart_ = now;
      powerUpFx_ = endFx_;
    }
    endStart_.reset();
    endFx_.reset();
  }
}

std::optional<float> FxTimeline::bootAge(double now) const {
  if (!bootStart_ || now < *bootStart_) return std::nullopt;
  const double age = now - *bootStart_;
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

bool FxTimeline::animating(double now) const {
  return booting(now) || ripple(now) || end(now) || powerUp(now);
}

}  // namespace cb::app
