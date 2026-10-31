// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>
#include <span>
#include <unordered_map>

#include "render/board_renderer.hpp"

namespace cb::app {
class Session;
}

namespace cb::render {

/// Explicit presentation history, owned by an interactive board's front end.
/// Pure instance construction and independent captures do not own colour events.
class PanelResponse {
 public:
  /// Call after UI/request mutations, before building the ordinary board. Null
  /// source or a non-LED material clears history and disables target collection.
  [[nodiscard]] bool prepare(const app::Session* ordinarySession,
                             const view::Theme& theme, const BoardOptions& options);
  /// Observe each logical cell once, then write its gain to every supplied range.
  /// First exposure is settled; invisible cells are forgotten. Time is never read.
  void apply(InstanceSet& set, std::span<const PanelTarget> targets, double seconds);
  void clear();

 private:
  struct ResetKey {
    const void* source{nullptr};  // compare identity only; never dereferenced
    std::uint64_t epoch{0};
    view::Material material{view::Material::Matte};
    std::array<std::array<float, 4>, 6> palette;
    bool flat{}, surface{}, swapLayers{}, legal{}, last{}, vertical{};
    SmallVec<std::uint8_t, 3> screenAxes;
    SmallVec<std::uint8_t, kMaxDims> gridAxes;
    friend bool operator==(const ResetKey&, const ResetKey&) = default;
  };
  struct Entry {
    view::Rgba target;
    view::Rgba from;  ///< the colour the last change left (living colour change)
    double changedAt{0};
    std::uint64_t seen{0};
    float gain{0};
    bool active{false};
  };
  std::optional<ResetKey> key_;
  std::optional<double> lastSeconds_;
  std::unordered_map<CellId, Entry> entries_;
  std::uint64_t generation_{0};
};
}  // namespace cb::render
