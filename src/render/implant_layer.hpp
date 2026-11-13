// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <memory>

#include "render/offscreen_target.hpp"

namespace cb::render {
struct ImplantParams {
  float time{0};
  float intensity{1};
  float globalSync{1};
  float heartbeat{1};
  float dip{0};
  float pixelScale{1};
  struct Panel {
    float rect[4];
    float form, dissolve, afterimage, seed;
  };
  std::array<Panel, 16> panels{};
  std::uint32_t panelCount{0};
  std::array<std::array<float, 4>, 4> toolRects{};
  std::uint32_t toolCount{0};
  float warm[4]{};
  float body[4]{};
};

/// One transparent UI attachment and uniform buffer per in-flight frame slot.
/// The caller waits the slot fence before prepare/reuse, as for PostProcess.
class ImplantLayer {
 public:
  static Result<ImplantLayer> create(const VulkanContext& ctx, VkFormat targetFormat);
  ImplantLayer();
  ~ImplantLayer();
  ImplantLayer(ImplantLayer&&) noexcept;
  ImplantLayer& operator=(ImplantLayer&&) noexcept;
  ImplantLayer(const ImplantLayer&) = delete;
  ImplantLayer& operator=(const ImplantLayer&) = delete;
  Result<void> prepare(std::uint32_t width, std::uint32_t height, std::uint32_t slot);
  void beginLayer(VkCommandBuffer cmd, std::uint32_t slot);
  void endLayer(VkCommandBuffer cmd, std::uint32_t slot);
  void composite(VkCommandBuffer cmd, const OffscreenTarget& target,
                 const ImplantParams& params, std::uint32_t slot);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace cb::render
