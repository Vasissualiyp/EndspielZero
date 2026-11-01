// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/fx_timeline.hpp"

#include "app/session.hpp"
#include "view/board_boot.hpp"

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
  if (session->bootSerial() == bootSeen_) return;
  if (swallow) {
    bootSeen_ = session->bootSerial();
    bootStart_.reset();
  } else if (eligible) {
    bootSeen_ = session->bootSerial();
    bootStart_ = now;
  }
}

std::optional<float> FxTimeline::bootAge(double now) const {
  if (!bootStart_ || now < *bootStart_) return std::nullopt;
  const double age = now - *bootStart_;
  if (age >= static_cast<double>(view::kBootSeconds)) return std::nullopt;
  return static_cast<float>(age);
}

}  // namespace cb::app
