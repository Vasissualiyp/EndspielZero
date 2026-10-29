// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "base/result.hpp"

namespace cb::render {

/// Vulkan instance, device and queue, with no window and no surface.
///
/// Headless by construction, which is the decision that makes the renderer testable:
/// a test can create a context, render a frame into an image, read the pixels back and
/// assert on them, on a machine with no display at all. The windowed path adds a
/// surface and a swapchain on top of exactly this.
///
/// Validation layers are enabled whenever they are available, and every message they
/// produce is recorded. A test that renders a frame asserts the list is empty: a
/// validation error is a failing test, not a line of console noise (M4.1).
/// At namespace scope rather than nested, because a nested class's default member
/// initializers cannot be used in a default argument inside the enclosing class.
struct ContextOptions {
  bool validation{true};
  /// Prefer a real GPU, but fall back to whatever is present (llvmpipe, typically) so a
  /// machine with no GPU still exercises the entire path.
  bool preferDiscrete{true};
  std::string applicationName{"EndspielZero"};
  /// Extra instance extensions, which is how a window system asks for what it needs
  /// (SDL reports these). Empty for the headless path.
  std::vector<std::string> instanceExtensions;
  /// Require the device to support swapchains. Off for headless rendering, which is
  /// what keeps the test path independent of any display.
  bool needSwapchain{false};
};

class VulkanContext {
 public:
  static Result<VulkanContext> create(const ContextOptions& opts = {});

  VulkanContext() = default;
  ~VulkanContext();
  VulkanContext(const VulkanContext&) = delete;
  VulkanContext& operator=(const VulkanContext&) = delete;
  VulkanContext(VulkanContext&& other) noexcept { *this = std::move(other); }
  VulkanContext& operator=(VulkanContext&& other) noexcept;

  [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physical_; }
  [[nodiscard]] VkDevice device() const noexcept { return device_; }
  [[nodiscard]] VkQueue queue() const noexcept { return queue_; }
  [[nodiscard]] std::uint32_t queueFamily() const noexcept { return queueFamily_; }
  [[nodiscard]] VkCommandPool commandPool() const noexcept { return pool_; }
  [[nodiscard]] const std::string& deviceName() const noexcept { return deviceName_; }
  [[nodiscard]] bool validationEnabled() const noexcept {
    return debugMessenger_ != VK_NULL_HANDLE;
  }

  /// Everything the validation layers have said since the context was created.
  [[nodiscard]] std::vector<std::string> takeValidationMessages() const;
  [[nodiscard]] std::size_t validationErrorCount() const;

  /// Record a one-shot command buffer, submit it, and wait. Used for uploads and for
  /// the whole of an offscreen frame - simplicity beats pipelining for a turn-based
  /// game, and it keeps the test path deterministic.
  Result<void> submitAndWait(const std::function<void(VkCommandBuffer)>& record) const;

  /// Allocate device memory for a buffer or image, choosing a heap by property flags.
  ///
  /// A deliberately thin wrapper rather than a full sub-allocator: this renderer makes
  /// a handful of allocations, not thousands. It exists so that swapping in VMA later
  /// is a change in one place - see the M4 status note in docs/plan/M4-renderer.md.
  [[nodiscard]] Result<std::uint32_t> findMemoryType(std::uint32_t typeBits,
                                                     VkMemoryPropertyFlags props) const;

 private:
  VkInstance instance_{VK_NULL_HANDLE};
  VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue queue_{VK_NULL_HANDLE};
  VkCommandPool pool_{VK_NULL_HANDLE};
  std::uint32_t queueFamily_{0};
  std::string deviceName_;
};

/// Turn a VkResult into a message worth reading.
std::string describe(VkResult r);

}  // namespace cb::render
