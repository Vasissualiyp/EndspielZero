// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/panel_response.hpp"

#include <algorithm>
#include <cmath>

#include "app/session.hpp"
#include "render/panel_alive.hpp"

namespace cb::render {

void PanelResponse::clear() {
  entries_.clear();
  lastSeconds_.reset();
  key_.reset();
}

bool PanelResponse::prepare(const app::Session* session, const view::Theme& theme,
                            const BoardOptions& options) {
  if (!session || theme.materials.cells != view::Material::Led) {
    clear();
    return false;
  }
  const auto& cfg = session->viewConfig();
  const ResetKey next{
      session,
      session->presentationEpoch(),
      theme.materials.cells,
      {theme.boardDark.array(), theme.boardLight.array(), theme.ember.array(),
       theme.moss.array(), theme.blood.array(), theme.peek.array()},
      options.flat,
      options.surface,
      options.swapLayerColors,
      options.showLegalMoves,
      options.showLastMove,
      cfg.gridVertical,
      cfg.screenAxes,
      cfg.gridAxes};
  if (!key_ || *key_ != next) {
    clear();
    key_ = next;
  }
  return true;
}

void PanelResponse::apply(InstanceSet& set, std::span<const PanelTarget> targets,
                          double seconds) {
  if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
  if (lastSeconds_ && seconds < *lastSeconds_) entries_.clear();
  lastSeconds_ = seconds;
  ++generation_;
  for (const auto& target : targets) {
    auto [it, inserted] = entries_.try_emplace(target.cell);
    auto& entry = it->second;
    if (entry.seen == generation_) continue;
    entry.seen = generation_;
    if (!inserted) {
      const float delta = std::max({std::abs(entry.target.r - target.color.r),
                                    std::abs(entry.target.g - target.color.g),
                                    std::abs(entry.target.b - target.color.b)});
      if (delta > 1.0f / 255.0f) {
        entry.from = entry.target;
        entry.changedAt = seconds;
        entry.active = true;
      }
    }
    entry.target = target.color;
    const double age = seconds - entry.changedAt;
    entry.gain = 0;
    if (entry.active && age >= 0 && age < .24) {
      const double t = age / .24;
      entry.gain = static_cast<float>(.018 * (1 - t * t * (3 - 2 * t)));
    }
  }
  std::erase_if(entries_,
                [&](const auto& item) { return item.second.seen != generation_; });
  for (const auto& target : targets) {
    const Entry& entry = entries_.at(target.cell);
    const float gain = entry.gain;
    // Seconds since the change while any emitter may still be moving; negative once
    // settled, so a capture long after the change (or at first exposure) shows the final
    // colour only.
    const double age = seconds - entry.changedAt;
    const bool moving =
        entry.active && age >= 0 && age < static_cast<double>(kTransitionMaxSeconds);
    const auto write = [&](float (&signal)[4], float (&prev)[4]) {
      signal[1] = signal[0] > 0 ? gain : 0;
      if (signal[0] > 0 && moving) {
        prev[0] = entry.from.r;
        prev[1] = entry.from.g;
        prev[2] = entry.from.b;
        prev[3] = static_cast<float>(age);
      } else {
        prev[0] = prev[1] = prev[2] = 0;
        prev[3] = -1;
      }
    };
    if (target.destination == PanelDestination::Instance) {
      auto& inst = set.instances.at(target.first);
      write(inst.panelSignal, inst.panelPrev);
    } else {
      for (std::size_t i = target.first;
           i < static_cast<std::size_t>(target.first) + target.count; ++i) {
        auto& v = set.surfaceVertices.at(i);
        write(v.panelSignal, v.panelPrev);
      }
    }
  }
}
}  // namespace cb::render
