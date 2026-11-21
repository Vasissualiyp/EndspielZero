// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include "view/implant_fx.hpp"

namespace cb::app {
class Session;
/// Presentation-only event tracker. Time is supplied by the front end, as in FxTimeline.
/// Phase survives screen and session changes. An optional countdown input is ready for
/// time controls; the current game has no countdown clock.
class NeuralHeartbeat {
 public:
  void observe(const Session* session, double seconds,
               std::optional<float> playerSeconds = std::nullopt);
  [[nodiscard]] float tension() const noexcept { return tension_; }
  [[nodiscard]] const view::HeartbeatPhase& phase() const noexcept { return phase_; }

 private:
  view::HeartbeatPhase phase_;
  std::optional<double> last_, capture_, end_;
  std::uint64_t session_{0}, epoch_{0}, captureSeen_{0};
  float tension_{0}, baseline_{0};
  bool eligible_{false}, ended_{false};
};
}  // namespace cb::app
