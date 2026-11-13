// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/offscreen_target.hpp"

namespace cb::render {
namespace {

Result<void> makeView(const VulkanContext& ctx, VkImage image, VkFormat format,
                      VkImageAspectFlags aspect, VkImageView& viewOut) {
  VkImageViewCreateInfo vci{};
  vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.image = image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = format;
  vci.subresourceRange = {aspect, 0, 1, 0, 1};
  if (const VkResult r = vkCreateImageView(ctx.device(), &vci, nullptr, &viewOut);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create image view: " + describe(r));
  }
  return {};
}

Result<void> makeImage(const VulkanContext& ctx, std::uint32_t w, std::uint32_t h,
                       VkFormat format, VkImageUsageFlags usage,
                       VkImageAspectFlags aspect, VkSampleCountFlagBits samples,
                       VkImage& image, VkDeviceMemory& memory, VkImageView& viewOut) {
  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = format;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = samples;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (const VkResult r = vkCreateImage(ctx.device(), &ici, nullptr, &image);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create image: " + describe(r));
  }

  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(ctx.device(), image, &req);
  const auto type =
      ctx.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!type.has_value()) return fail(type.error().code, type.error().message);

  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = *type;
  if (const VkResult r = vkAllocateMemory(ctx.device(), &mai, nullptr, &memory);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot allocate image memory: " + describe(r));
  }
  vkBindImageMemory(ctx.device(), image, memory, 0);

  return makeView(ctx, image, format, aspect, viewOut);
}

}  // namespace

Result<void> OffscreenTarget::allocate(const VulkanContext& ctx, bool allocateColor) {
  if (allocateColor) {
    // The resolved colour: one sample, read by the blur and copied out by readPixels.
    if (auto r =
            makeImage(ctx, width_, height_, colorFormat_,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, color_, colorMem_,
                      colorView_);
        !r.has_value()) {
      return r;
    }
  } else if (auto r = makeView(ctx, color_, colorFormat_, VK_IMAGE_ASPECT_COLOR_BIT,
                               colorView_);
             !r.has_value()) {
    // A wrapped swapchain image: the image is the window's, only the view is ours.
    return r;
  }
  if (msaaEnabled()) {
    // The multisampled attachment the board draws into, resolved into `color_`. Its
    // format must match the colour attachment it resolves into.
    if (auto r = makeImage(ctx, width_, height_, colorFormat_,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
                           kSampleCount, msaaColor_, msaaColorMem_, msaaColorView_);
        !r.has_value()) {
      return r;
    }
  }
  if (auto r = makeImage(ctx, width_, height_, colorFormat_,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, scratch_,
                         scratchMem_, scratchView_);
      !r.has_value()) {
    return r;
  }
  // Depth matches the colour sample count: every attachment in a render pass must.
  return makeImage(ctx, width_, height_, kDepthFormat,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                       (ctx.depthSampleable() ? static_cast<VkImageUsageFlags>(VK_IMAGE_USAGE_SAMPLED_BIT)
                              : VkImageUsageFlags{0}),
                   VK_IMAGE_ASPECT_DEPTH_BIT, kSampleCount, depth_, depthMem_, depthView_);
}

Result<OffscreenTarget> OffscreenTarget::create(const VulkanContext& ctx,
                                                std::uint32_t width, std::uint32_t height,
                                                VkFormat colorFormat) {
  if (width == 0 || height == 0) {
    return fail(ErrorCode::ValidationError, "an offscreen target needs a nonzero size");
  }
  OffscreenTarget t;
  t.ctx_ = &ctx;
  t.width_ = width;
  t.height_ = height;
  t.colorFormat_ = colorFormat;
  if (auto r = t.allocate(ctx, /*allocateColor=*/true); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  return t;
}

Result<OffscreenTarget> OffscreenTarget::wrapColor(const VulkanContext& ctx,
                                                   VkImage color, VkFormat colorFormat,
                                                   std::uint32_t width,
                                                   std::uint32_t height) {
  if (width == 0 || height == 0 || color == VK_NULL_HANDLE) {
    return fail(ErrorCode::ValidationError, "a wrapped target needs a sized image");
  }
  OffscreenTarget t;
  t.ctx_ = &ctx;
  t.width_ = width;
  t.height_ = height;
  t.colorFormat_ = colorFormat;
  t.color_ = color;
  t.ownsColor_ = false;
  if (auto r = t.allocate(ctx, /*allocateColor=*/false); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  return t;
}

OffscreenTarget& OffscreenTarget::operator=(OffscreenTarget&& o) noexcept {
  if (this == &o) return *this;
  std::swap(ctx_, o.ctx_);
  std::swap(width_, o.width_);
  std::swap(height_, o.height_);
  std::swap(colorFormat_, o.colorFormat_);
  std::swap(ownsColor_, o.ownsColor_);
  std::swap(color_, o.color_);
  std::swap(colorMem_, o.colorMem_);
  std::swap(colorView_, o.colorView_);
  std::swap(msaaColor_, o.msaaColor_);
  std::swap(msaaColorMem_, o.msaaColorMem_);
  std::swap(msaaColorView_, o.msaaColorView_);
  std::swap(scratch_, o.scratch_);
  std::swap(scratchMem_, o.scratchMem_);
  std::swap(scratchView_, o.scratchView_);
  std::swap(depth_, o.depth_);
  std::swap(depthMem_, o.depthMem_);
  std::swap(depthView_, o.depthView_);
  return *this;
}

OffscreenTarget::~OffscreenTarget() {
  if (ctx_ == nullptr || ctx_->device() == VK_NULL_HANDLE) return;
  const VkDevice d = ctx_->device();
  if (colorView_ != VK_NULL_HANDLE) vkDestroyImageView(d, colorView_, nullptr);
  if (ownsColor_) {
    if (color_ != VK_NULL_HANDLE) vkDestroyImage(d, color_, nullptr);
    if (colorMem_ != VK_NULL_HANDLE) vkFreeMemory(d, colorMem_, nullptr);
  }
  if (msaaColorView_ != VK_NULL_HANDLE) vkDestroyImageView(d, msaaColorView_, nullptr);
  if (msaaColor_ != VK_NULL_HANDLE) vkDestroyImage(d, msaaColor_, nullptr);
  if (msaaColorMem_ != VK_NULL_HANDLE) vkFreeMemory(d, msaaColorMem_, nullptr);
  if (scratchView_ != VK_NULL_HANDLE) vkDestroyImageView(d, scratchView_, nullptr);
  if (scratch_ != VK_NULL_HANDLE) vkDestroyImage(d, scratch_, nullptr);
  if (scratchMem_ != VK_NULL_HANDLE) vkFreeMemory(d, scratchMem_, nullptr);
  if (depthView_ != VK_NULL_HANDLE) vkDestroyImageView(d, depthView_, nullptr);
  if (depth_ != VK_NULL_HANDLE) vkDestroyImage(d, depth_, nullptr);
  if (depthMem_ != VK_NULL_HANDLE) vkFreeMemory(d, depthMem_, nullptr);
}

Result<std::vector<std::uint8_t>> OffscreenTarget::readPixels() const {
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(width_) * height_ * 4;

  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = bytes;
  bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer staging = VK_NULL_HANDLE;
  if (const VkResult r = vkCreateBuffer(ctx_->device(), &bci, nullptr, &staging);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create a readback buffer: " + describe(r));
  }

  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(ctx_->device(), staging, &req);
  const auto type =
      ctx_->findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!type.has_value()) {
    vkDestroyBuffer(ctx_->device(), staging, nullptr);
    return fail(type.error().code, type.error().message);
  }
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = *type;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  vkAllocateMemory(ctx_->device(), &mai, nullptr, &mem);
  vkBindBufferMemory(ctx_->device(), staging, mem, 0);

  // The colour image is left in TRANSFER_SRC_OPTIMAL by the renderer, so this only has
  // to copy; keeping that contract in one place avoids a redundant barrier per frame.
  const auto submitted = ctx_->submitAndWait([&](VkCommandBuffer cmd) {
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(cmd, color_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1,
                           &copy);
  });

  std::vector<std::uint8_t> pixels;
  if (submitted.has_value()) {
    void* mapped = nullptr;
    vkMapMemory(ctx_->device(), mem, 0, bytes, 0, &mapped);
    pixels.assign(static_cast<std::uint8_t*>(mapped),
                  static_cast<std::uint8_t*>(mapped) + bytes);
    vkUnmapMemory(ctx_->device(), mem);
  }
  vkDestroyBuffer(ctx_->device(), staging, nullptr);
  vkFreeMemory(ctx_->device(), mem, nullptr);
  if (!submitted.has_value())
    return fail(submitted.error().code, submitted.error().message);
  return pixels;
}

}  // namespace cb::render
