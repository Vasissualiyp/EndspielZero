// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/neural_heartbeat.hpp"
#include <algorithm>
#include <cmath>
#include "app/session.hpp"

namespace cb::app {
void NeuralHeartbeat::observe(const Session* session, double seconds,
                              std::optional<float> playerSeconds) {
  if (!std::isfinite(seconds)) return;
  const double dt = last_ ? std::max(0.0, seconds - *last_) : 0.0;
  // Integrate the previous frame's input up to this event. Rate changes never move phase.
  for (double elapsed = 0; elapsed < dt;) {
    const double step = std::min(1.0 / 60.0, dt - elapsed);
    const double at = *last_ + elapsed + step * .5;
    const auto past = [at](std::optional<double> start) {
      return start ? static_cast<float>(std::exp(-std::max(0.0, at - *start) / 3.0))
                   : 0.f;
    };
    const float demand = !eligible_ ? 0.f
                         : ended_   ? 50.f * past(end_)
                                    : baseline_ + 10.f * past(capture_);
    phase_.advance(demand, step);
    elapsed += step;
  }
  last_ = seconds;
  tension_ = 0;
  eligible_ = session != nullptr;
  if (!session) return;
  if (session->id() != session_ || session->presentationEpoch() != epoch_) {
    session_ = session->id();
    epoch_ = session->presentationEpoch();
    captureSeen_ = 0;
    capture_.reset();
    end_.reset();
  }
  if (session->captureSerial() != captureSeen_) {
    captureSeen_ = session->captureSerial();
    capture_ = session->capturePly() ? std::optional<double>(seconds) : std::nullopt;
  }
  if (!session->capturePly() || *session->capturePly() != session->game().plyCount())
    capture_.reset();
  const bool ended = session->game().result() != GameResult::InProgress;
  if (ended && !end_) end_ = seconds;
  if (!ended) end_.reset();
  const auto decay = [seconds](std::optional<double> start) {
    return start ? static_cast<float>(std::exp(-std::max(0.0, seconds - *start) / 3.0))
                 : 0.f;
  };
  ended_ = ended;
  if (ended) {
    tension_ = 50.f * decay(end_);
  } else {
    const bool playerTurn =
        !session->engineSide() ||
        session->game().position().sideToMove() != *session->engineSide();
    baseline_ = (playerTurn && session->game().inCheck() ? 20.f : 0.f) +
                (playerSeconds && *playerSeconds < 30.f ? 25.f : 0.f);
    tension_ = baseline_ + 10.f * decay(capture_);
  }
}
}  // namespace cb::app
