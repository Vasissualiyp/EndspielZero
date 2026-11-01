// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <span>

#include "render/offscreen_target.hpp"
#include "view/theme.hpp"

namespace cb::render {
struct PostStats {
  std::uint64_t allocations{0};
  std::uint32_t bloomPasses{0};
};

/// Frame-slot-owned HDR scene, dual-filter bloom and LDR composite (ADR-0025).
/// The caller waits the slot fence before prepare/reuse, and completes GPU work before
/// destruction, just as for BoardRenderer's instance buffers. No frame waits here.
class PostProcess {
 public:
  static constexpr VkFormat kSceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
  static constexpr std::uint32_t kFrames = 2;
  [[nodiscard]] static bool supported(const VulkanContext& ctx) noexcept;
  static Result<PostProcess> create(const VulkanContext& ctx, VkFormat outputFormat);
  PostProcess();
  [[nodiscard]] bool valid() const noexcept { return impl_ != nullptr; }
  ~PostProcess();
  PostProcess(PostProcess&&) noexcept;
  PostProcess& operator=(PostProcess&&) noexcept;
  PostProcess(const PostProcess&) = delete;
  PostProcess& operator=(const PostProcess&) = delete;

  Result<void> prepare(std::uint32_t width, std::uint32_t height, std::uint32_t slot,
                       bool bloom);
  [[nodiscard]] VkImage sceneImage(std::uint32_t slot) const;
  [[nodiscard]] VkImageView sceneView(std::uint32_t slot) const;
  [[nodiscard]] VkImage msaaImage(std::uint32_t slot) const;
  [[nodiscard]] VkImageView msaaView(std::uint32_t slot) const;
  [[nodiscard]] PostStats stats(std::uint32_t slot) const;
  /// Scene resolve enters in COLOR_ATTACHMENT_OPTIMAL. Output leaves in that layout.
  void record(VkCommandBuffer cmd, const OffscreenTarget& target, const view::Theme& theme,
              float bloomStrength, float intensity, double seconds, std::uint32_t slot);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace cb::render
