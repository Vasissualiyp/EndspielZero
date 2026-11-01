// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN
#include <array>
#include <cmath>
#include <utility>

#include "render/post_process.hpp"
#include "render/post_params.hpp"

using namespace cb;
using namespace cb::render;

namespace {
void transition(VkCommandBuffer cmd, VkImage image, VkImageLayout before,
                VkImageLayout after, VkPipelineStageFlags2 src, VkAccessFlags2 access,
                VkPipelineStageFlags2 dst, VkAccessFlags2 nextAccess) {
  VkImageMemoryBarrier2 b{}; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  b.oldLayout = before;
  b.newLayout = after;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  b.srcStageMask = src;
  b.srcAccessMask = access;
  b.dstStageMask = dst;
  b.dstAccessMask = nextAccess;
  VkDependencyInfo dep{}; dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &dep);
}

std::vector<std::uint8_t> flatFrame(const VulkanContext& ctx, PostProcess& post,
                                  const OffscreenTarget& target, view::Theme theme,
                                  float strength, double time, bool bright = false,
                                  std::uint32_t slot = 0) {
  REQUIRE(post.prepare(target.width(), target.height(), slot, strength > 0).has_value());
  REQUIRE(ctx.submitAndWait([&](VkCommandBuffer cmd) {
    transition(cmd, post.sceneImage(slot), VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo att{};
    att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    att.imageView = post.sceneView(slot);
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.clearValue.color = {{.2f, .3f, .4f, 1}};
    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea = {{0, 0}, {target.width(), target.height()}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &att;
    vkCmdBeginRendering(cmd, &ri);
    if (bright) {
      VkClearAttachment clear{};
      clear.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      clear.clearValue.color = {{4, 2, 1, 1}};
      VkClearRect rect{{{static_cast<std::int32_t>(target.width() / 2 - 8),
                         static_cast<std::int32_t>(target.height() / 2 - 8)}, {16, 16}}, 0, 1};
      vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
    }
    vkCmdEndRendering(cmd);
    post.record(cmd, target, theme, strength, 1, time, slot);
    transition(cmd, target.colorImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
               VK_ACCESS_2_TRANSFER_READ_BIT);
  }).has_value());
  auto pixels = target.readPixels();
  REQUIRE(pixels.has_value());
  REQUIRE(ctx.takeValidationMessages().empty());
  return std::move(*pixels);
}
}

TEST_CASE("HDR composite preserves identity-range colours with bounded dither",
          "[render][gpu][post]") {
  auto ctx = VulkanContext::create();
  if (!ctx.has_value()) SKIP("no Vulkan device");
  if (!PostProcess::supported(*ctx)) SKIP("no filtered blendable HDR MSAA target");
  auto post = PostProcess::create(*ctx, OffscreenTarget::kColorFormat);
  auto target = OffscreenTarget::create(*ctx, 128, 96);
  REQUIRE(post.has_value());
  REQUIRE(target.has_value());
  auto theme = view::Theme::console();
  theme.grainAmount = 0;
  const auto image = flatFrame(*ctx, *post, *target, theme, 0, .75);
  for (std::uint32_t y = 0; y < target->height(); ++y)
    for (std::uint32_t x = 0; x < target->width(); ++x) {
      const float vig = postVignette((static_cast<float>(x) + .5f) / 128,
                                     (static_cast<float>(y) + .5f) / 96);
      for (std::uint32_t c = 0; c < 3; ++c) {
        const float expected = (.2f + .1f * static_cast<float>(c)) * vig * 255;
        const auto i = (y * target->width() + x) * 4 + c;
        CHECK(std::abs(static_cast<float>(image[i]) - expected) <= 2);
      }
    }
  CHECK(post->stats(0).bloomPasses == 0);
  const auto allocations = post->stats(0).allocations;
  CHECK(flatFrame(*ctx, *post, *target, theme, 0, .75) == image);
  CHECK(post->stats(0).allocations == allocations);
}

TEST_CASE("bloom spreads HDR light beyond its source and skips at zero strength",
          "[render][gpu][post]") {
  auto ctx = VulkanContext::create();
  if (!ctx.has_value()) SKIP("no Vulkan device");
  if (!PostProcess::supported(*ctx)) SKIP("no filtered blendable HDR MSAA target");
  auto post = PostProcess::create(*ctx, OffscreenTarget::kColorFormat);
  auto target = OffscreenTarget::create(*ctx, 256, 192);
  REQUIRE(post.has_value());
  REQUIRE(target.has_value());
  auto theme = view::Theme::manifold();
  theme.grainAmount = 0;
  const auto off = flatFrame(*ctx, *post, *target, theme, 0, .75, true);
  const auto on = flatFrame(*ctx, *post, *target, theme, .06f, .75, true);
  double ringOff = 0, ringOn = 0;
  for (int y = 80; y < 112; ++y)
    for (int x = 112; x < 144; ++x) {
      if (x >= 120 && x < 136 && y >= 88 && y < 104) continue;
      const auto i = static_cast<std::size_t>((y * 256 + x) * 4);
      ringOff += off[i] + off[i+1] + off[i+2];
      ringOn += on[i] + on[i+1] + on[i+2];
    }
  CHECK(ringOn > ringOff + 50);
  CHECK(post->stats(0).bloomPasses == 2 * bloomLevelCount(256, 192) - 1);
  const auto allocations = post->stats(0).allocations;
  CHECK(flatFrame(*ctx, *post, *target, theme, 0, .75, true) == off);
  CHECK(post->stats(0).bloomPasses == 0);
  CHECK(post->stats(0).allocations == allocations);
  CHECK(flatFrame(*ctx, *post, *target, theme, .06f, .75, true) == on);
  CHECK(post->stats(0).allocations == allocations);
}

TEST_CASE("post resources are per slot and noise is pure in stepped presentation time",
          "[render][gpu][post]") {
  auto ctx = VulkanContext::create();
  if (!ctx.has_value()) SKIP("no Vulkan device");
  if (!PostProcess::supported(*ctx)) SKIP("no filtered blendable HDR MSAA target");
  auto post = PostProcess::create(*ctx, OffscreenTarget::kColorFormat);
  auto a = OffscreenTarget::create(*ctx, 128, 96);
  auto b = OffscreenTarget::create(*ctx, 96, 64);
  REQUIRE(post.has_value());
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  const auto theme = view::Theme::manifold();
  const auto first = flatFrame(*ctx, *post, *a, theme, 0, .75);
  const auto original = post->sceneImage(0);
  CHECK(flatFrame(*ctx, *post, *a, theme, 0, .76) == first);
  CHECK(flatFrame(*ctx, *post, *a, theme, 0, .8) != first);
  (void)flatFrame(*ctx, *post, *b, theme, .06f, .75, false, 1);
  CHECK(post->sceneImage(0) == original);
  CHECK(post->sceneImage(1) != original);
  CHECK(flatFrame(*ctx, *post, *a, theme, 0, .75) == first);
  const auto generation = post->stats(0).allocations;
  (void)flatFrame(*ctx, *post, *b, theme, 0, .75);
  CHECK(post->stats(0).allocations > generation);
  // Move ownership after use; no duplicated images, descriptors or destruction.
  auto moved = std::move(*post);
  (void)flatFrame(*ctx, moved, *b, theme, 0, .75);
}
#endif
