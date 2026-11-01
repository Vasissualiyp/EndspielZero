// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/post_process.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "render/post_params.hpp"
#include "shaders.hpp"

namespace cb::render {
namespace {

/// Mirrors the fragment shaders' push block (32 bytes).
struct PostPush {
  float texel[2]{};
  float strength{0};
  float grain{0};
  std::uint32_t seed{0};
  std::uint32_t first{0};
  float vignette{0};
  float pad{0};
};
static_assert(sizeof(PostPush) == 32);

constexpr std::uint32_t kSetsPerSlot = 2 + kMaxBloomLevels;

struct Gpu {
  VkImage image{VK_NULL_HANDLE};
  VkDeviceMemory memory{VK_NULL_HANDLE};
  VkImageView view{VK_NULL_HANDLE};
  std::uint32_t width{0};
  std::uint32_t height{0};
};

void destroy(VkDevice d, Gpu& g) {
  if (g.view != VK_NULL_HANDLE) vkDestroyImageView(d, g.view, nullptr);
  if (g.image != VK_NULL_HANDLE) vkDestroyImage(d, g.image, nullptr);
  if (g.memory != VK_NULL_HANDLE) vkFreeMemory(d, g.memory, nullptr);
  g = {};
}

Result<void> makeGpu(const VulkanContext& ctx, std::uint32_t w, std::uint32_t h,
                     VkImageUsageFlags usage, VkSampleCountFlagBits samples, Gpu& out) {
  out = {};
  out.width = w;
  out.height = h;
  VkImageCreateInfo ici{}; ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = PostProcess::kSceneFormat;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = samples;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (const VkResult r = vkCreateImage(ctx.device(), &ici, nullptr, &out.image);
      r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create a post-process image: " + describe(r));
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(ctx.device(), out.image, &req);
  const auto type =
      ctx.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!type.has_value()) {
    destroy(ctx.device(), out);
    return fail(type.error().code, type.error().message);
  }
  VkMemoryAllocateInfo mai{}; mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = *type;
  if (const VkResult r = vkAllocateMemory(ctx.device(), &mai, nullptr, &out.memory);
      r != VK_SUCCESS) {
    destroy(ctx.device(), out);
    return fail(ErrorCode::Internal, "cannot allocate post-process memory: " + describe(r));
  }
  if (const VkResult r = vkBindImageMemory(ctx.device(), out.image, out.memory, 0);
      r != VK_SUCCESS) {
    destroy(ctx.device(), out);
    return fail(ErrorCode::Internal, "cannot bind post-process memory: " + describe(r));
  }
  VkImageViewCreateInfo vci{}; vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.image = out.image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = PostProcess::kSceneFormat;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (const VkResult r = vkCreateImageView(ctx.device(), &vci, nullptr, &out.view);
      r != VK_SUCCESS) {
    destroy(ctx.device(), out);
    return fail(ErrorCode::Internal, "cannot create a post-process view: " + describe(r));
  }
  return {};
}

Result<VkShaderModule> makeShader(const VulkanContext& ctx,
                                  std::span<const std::uint8_t> spv) {
  VkShaderModuleCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  sci.codeSize = spv.size();
  sci.pCode = reinterpret_cast<const std::uint32_t*>(spv.data());
  VkShaderModule m = VK_NULL_HANDLE;
  if (const VkResult r = vkCreateShaderModule(ctx.device(), &sci, nullptr, &m);
      r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create shader module: " + describe(r));
  return m;
}

void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
             VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
             VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
  VkImageMemoryBarrier2 b{}; b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  b.srcStageMask = srcStage;
  b.srcAccessMask = srcAccess;
  b.dstStageMask = dstStage;
  b.dstAccessMask = dstAccess;
  b.oldLayout = from;
  b.newLayout = to;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkDependencyInfo dep{}; dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &dep);
}

constexpr auto kAttach = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr auto kAttachWrite = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
constexpr auto kAttachRW =
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
constexpr auto kSample = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
constexpr auto kSampleRead = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
}  // namespace

struct PostProcess::Impl {
  struct Slot {
    Gpu scene;
    Gpu msaa;
    std::vector<Gpu> levels;
    std::array<VkDescriptorSet, kSetsPerSlot> sets{};
    PostStats stats;
  };

  const VulkanContext* ctx{nullptr};
  VkFormat outputFormat{};
  VkShaderModule vert{}, downFrag{}, upFrag{}, compositeFrag{};
  VkSampler sampler{};
  VkDescriptorSetLayout setLayout{};
  VkDescriptorPool pool{};
  VkPipelineLayout layout{};
  VkPipeline down{}, up{}, composite{};
  std::array<Slot, kFrames> slots;

  ~Impl() {
    if (ctx == nullptr) return;
    const VkDevice d = ctx->device();
    for (Slot& s : slots) release(s);
    if (down != VK_NULL_HANDLE) vkDestroyPipeline(d, down, nullptr);
    if (up != VK_NULL_HANDLE) vkDestroyPipeline(d, up, nullptr);
    if (composite != VK_NULL_HANDLE) vkDestroyPipeline(d, composite, nullptr);
    if (layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(d, layout, nullptr);
    if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(d, pool, nullptr);
    if (setLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(d, setLayout, nullptr);
    if (sampler != VK_NULL_HANDLE) vkDestroySampler(d, sampler, nullptr);
    for (VkShaderModule m : {vert, downFrag, upFrag, compositeFrag})
      if (m != VK_NULL_HANDLE) vkDestroyShaderModule(d, m, nullptr);
  }

  void releaseBloom(Slot& s) {
    for (Gpu& g : s.levels) destroy(ctx->device(), g);
    s.levels.clear();
  }
  void release(Slot& s) {
    releaseBloom(s);
    destroy(ctx->device(), s.msaa);
    destroy(ctx->device(), s.scene);
  }

  void writeSet(VkDescriptorSet set, VkImageView a, VkImageView b) {
    VkDescriptorImageInfo info[2]{
        {sampler, a, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {sampler, b, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet w[2]{};
    for (int i = 0; i < 2; ++i) {
      w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w[i].dstSet = set;
      w[i].dstBinding = static_cast<std::uint32_t>(i);
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w[i].pImageInfo = &info[i];
    }
    vkUpdateDescriptorSets(ctx->device(), 2, w, 0, nullptr);
  }

  // Set layout per slot: [0] scene/scene, [1] scene/level0, [2 + i] level i / level i.
  void bindScene(Slot& s) {
    writeSet(s.sets[0], s.scene.view, s.scene.view);
    // Until bloom exists, the composite's second binding is the scene as well.
    writeSet(s.sets[1], s.scene.view, s.scene.view);
  }
  void bindBloom(Slot& s) {
    writeSet(s.sets[1], s.scene.view, s.levels[0].view);
    for (std::size_t i = 0; i < s.levels.size(); ++i)
      writeSet(s.sets[2 + i], s.levels[i].view, s.levels[i].view);
  }

  Result<VkPipeline> pipeline(VkShaderModule frag, VkFormat format, bool additive) {
    VkPipelineVertexInputStateCreateInfo vi{}; vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{}; ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{}; vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{}; rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{}; ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{}; ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (additive) {
      blend.blendEnable = VK_TRUE;
      blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
      blend.colorBlendOp = VK_BLEND_OP_ADD;
      blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      blend.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{}; cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    const VkDynamicState dynamics[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{}; dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dynamics;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkPipelineRenderingCreateInfo rendering{}; rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo gpi{}; gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpi.pNext = &rendering;
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pDepthStencilState = &ds;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &dy;
    gpi.layout = layout;
    VkPipeline p = VK_NULL_HANDLE;
    if (const VkResult r =
            vkCreateGraphicsPipelines(ctx->device(), VK_NULL_HANDLE, 1, &gpi, nullptr, &p);
        r != VK_SUCCESS)
      return fail(ErrorCode::Internal, "cannot create a post pipeline: " + describe(r));
    return p;
  }
};

bool PostProcess::supported(const VulkanContext& ctx) noexcept {
  VkFormatProperties fp{};
  vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice(), kSceneFormat, &fp);
  constexpr VkFormatFeatureFlags need =
      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  if ((fp.optimalTilingFeatures & need) != need) return false;
  VkImageFormatProperties ip{};
  if (vkGetPhysicalDeviceImageFormatProperties(
          ctx.physicalDevice(), kSceneFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0, &ip) != VK_SUCCESS)
    return false;
  return (ip.sampleCounts & OffscreenTarget::kSampleCount) != 0;
}

PostProcess::PostProcess() = default;
PostProcess::~PostProcess() = default;
PostProcess::PostProcess(PostProcess&&) noexcept = default;
PostProcess& PostProcess::operator=(PostProcess&&) noexcept = default;

Result<PostProcess> PostProcess::create(const VulkanContext& ctx, VkFormat outputFormat) {
  if (!supported(ctx))
    return fail(ErrorCode::Unsupported,
                "the device cannot filter, blend and multisample 16-bit float colour");
  PostProcess p;
  p.impl_ = std::make_unique<Impl>();
  Impl& m = *p.impl_;
  m.ctx = &ctx;
  m.outputFormat = outputFormat;
  const VkDevice d = ctx.device();

  struct Src {
    VkShaderModule* out;
    std::span<const std::uint8_t> spv;
  };
  for (const Src& s : {Src{&m.vert, spv::blur_vert_spv_span()},
                       Src{&m.downFrag, spv::post_down_frag_spv_span()},
                       Src{&m.upFrag, spv::post_up_frag_spv_span()},
                       Src{&m.compositeFrag, spv::post_composite_frag_spv_span()}}) {
    auto sh = makeShader(ctx, s.spv);
    if (!sh.has_value()) return fail(sh.error().code, sh.error().message);
    *s.out = *sh;
  }

  VkSamplerCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
  sci.addressModeU = sci.addressModeV = sci.addressModeW =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (const VkResult r = vkCreateSampler(d, &sci, nullptr, &m.sampler); r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create the post sampler: " + describe(r));

  VkDescriptorSetLayoutBinding bindings[2]{};
  for (std::uint32_t i = 0; i < 2; ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  VkDescriptorSetLayoutCreateInfo dlci{}; dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dlci.bindingCount = 2;
  dlci.pBindings = bindings;
  if (const VkResult r = vkCreateDescriptorSetLayout(d, &dlci, nullptr, &m.setLayout);
      r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create the post set layout: " + describe(r));

  const std::uint32_t totalSets = kFrames * kSetsPerSlot;
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * totalSets};
  VkDescriptorPoolCreateInfo dpci{}; dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpci.maxSets = totalSets;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &size;
  if (const VkResult r = vkCreateDescriptorPool(d, &dpci, nullptr, &m.pool);
      r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create the post pool: " + describe(r));
  std::vector<VkDescriptorSetLayout> layouts(totalSets, m.setLayout);
  VkDescriptorSetAllocateInfo dsai{}; dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dsai.descriptorPool = m.pool;
  dsai.descriptorSetCount = totalSets;
  dsai.pSetLayouts = layouts.data();
  std::vector<VkDescriptorSet> sets(totalSets);
  if (const VkResult r = vkAllocateDescriptorSets(d, &dsai, sets.data()); r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot allocate the post sets: " + describe(r));
  for (std::uint32_t f = 0; f < kFrames; ++f)
    for (std::uint32_t i = 0; i < kSetsPerSlot; ++i)
      m.slots[f].sets[i] = sets[f * kSetsPerSlot + i];

  VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush)};
  VkPipelineLayoutCreateInfo plci{}; plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &m.setLayout;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &push;
  if (const VkResult r = vkCreatePipelineLayout(d, &plci, nullptr, &m.layout);
      r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create the post layout: " + describe(r));

  auto down = m.pipeline(m.downFrag, kSceneFormat, false);
  if (!down.has_value()) return fail(down.error().code, down.error().message);
  m.down = *down;
  auto up = m.pipeline(m.upFrag, kSceneFormat, true);
  if (!up.has_value()) return fail(up.error().code, up.error().message);
  m.up = *up;
  auto comp = m.pipeline(m.compositeFrag, outputFormat, false);
  if (!comp.has_value()) return fail(comp.error().code, comp.error().message);
  m.composite = *comp;
  return p;
}

Result<void> PostProcess::prepare(std::uint32_t width, std::uint32_t height,
                                  std::uint32_t slot, bool bloom) {
  if (impl_ == nullptr || slot >= kFrames)
    return fail(ErrorCode::InvalidArgument, "post-process slot out of range");
  if (width == 0 || height == 0)
    return fail(ErrorCode::InvalidArgument, "post-process needs a nonzero size");
  Impl& m = *impl_;
  Impl::Slot& s = m.slots[slot];
  if (s.scene.image != VK_NULL_HANDLE && (s.scene.width != width || s.scene.height != height))
    m.release(s);
  if (s.scene.image == VK_NULL_HANDLE) {
    if (auto r = makeGpu(*m.ctx, width, height,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_SAMPLE_COUNT_1_BIT, s.scene);
        !r.has_value())
      return r;
    if (auto r = makeGpu(*m.ctx, width, height, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                         OffscreenTarget::kSampleCount, s.msaa);
        !r.has_value()) {
      m.release(s);
      return r;
    }
    s.stats.allocations += 2;
    m.bindScene(s);
  }
  if (bloom && s.levels.empty()) {
    const std::uint32_t n = bloomLevelCount(width, height);
    s.levels.resize(n);
    for (std::uint32_t i = 0; i < n; ++i) {
      if (auto r = makeGpu(*m.ctx, std::max(1u, width >> (i + 1)),
                           std::max(1u, height >> (i + 1)),
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_SAMPLE_COUNT_1_BIT, s.levels[i]);
          !r.has_value()) {
        m.releaseBloom(s);
        return r;
      }
      ++s.stats.allocations;
    }
    m.bindBloom(s);
  }
  return {};
}

VkImage PostProcess::sceneImage(std::uint32_t slot) const { return impl_->slots[slot].scene.image; }
VkImageView PostProcess::sceneView(std::uint32_t slot) const { return impl_->slots[slot].scene.view; }
VkImage PostProcess::msaaImage(std::uint32_t slot) const { return impl_->slots[slot].msaa.image; }
VkImageView PostProcess::msaaView(std::uint32_t slot) const { return impl_->slots[slot].msaa.view; }
PostStats PostProcess::stats(std::uint32_t slot) const { return impl_->slots[slot].stats; }

void PostProcess::record(VkCommandBuffer cmd, const OffscreenTarget& target,
                         const view::Theme& theme, float bloomStrength, float intensity,
                         double seconds, std::uint32_t slot) {
  Impl& m = *impl_;
  Impl::Slot& s = m.slots[slot];
  s.stats.bloomPasses = 0;
  const bool bloom = bloomStrength > 0 && !s.levels.empty();

  const auto pass = [&](VkImageView into, std::uint32_t w, std::uint32_t h,
                        VkAttachmentLoadOp load, VkPipeline pipe, VkDescriptorSet set,
                        const PostPush& pc) {
    VkRenderingAttachmentInfo att{}; att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    att.imageView = into;
    att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att.loadOp = load;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info{}; info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea = {{0, 0}, {w, h}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &att;
    const VkViewport vp{0, 0, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {w, h}};
    vkCmdBeginRendering(cmd, &info);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m.layout, 0, 1, &set, 0,
                            nullptr);
    vkCmdPushConstants(cmd, m.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
  };

  // The scene resolve was written as an attachment; everything below samples it.
  barrier(cmd, s.scene.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAttach, kAttachWrite, kSample,
          kSampleRead);

  if (bloom) {
    const std::size_t n = s.levels.size();
    // Down: scene -> level 0 (thresholded, Karis) -> ... -> level n-1.
    for (std::size_t i = 0; i < n; ++i) {
      const Gpu& src = i == 0 ? s.scene : s.levels[i - 1];
      Gpu& dst = s.levels[i];
      barrier(cmd, dst.image, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
              VK_ACCESS_2_MEMORY_READ_BIT, kAttach, kAttachWrite);
      PostPush pc;
      pc.texel[0] = 1.0f / static_cast<float>(src.width);
      pc.texel[1] = 1.0f / static_cast<float>(src.height);
      pc.first = i == 0 ? 1u : 0u;
      pass(dst.view, dst.width, dst.height, VK_ATTACHMENT_LOAD_OP_DONT_CARE, m.down,
           i == 0 ? s.sets[0] : s.sets[2 + i - 1], pc);
      ++s.stats.bloomPasses;
      barrier(cmd, dst.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAttach, kAttachWrite, kSample,
              kSampleRead);
    }
    // Up: level i+1 is tent-filtered and added onto level i, which keeps its down contents.
    for (std::size_t i = n - 1; i-- > 0;) {
      const Gpu& src = s.levels[i + 1];
      Gpu& dst = s.levels[i];
      barrier(cmd, dst.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, kSample, kSampleRead, kAttach,
              kAttachRW);
      PostPush pc;
      pc.texel[0] = 1.0f / static_cast<float>(src.width);
      pc.texel[1] = 1.0f / static_cast<float>(src.height);
      pass(dst.view, dst.width, dst.height, VK_ATTACHMENT_LOAD_OP_LOAD, m.up,
           s.sets[2 + i + 1], pc);
      ++s.stats.bloomPasses;
      barrier(cmd, dst.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, kAttach, kAttachWrite, kSample,
              kSampleRead);
    }
  }

  // Composite into the output, discarding whatever it held.
  barrier(cmd, target.colorImage(), VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
          VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, kAttach, kAttachWrite);
  PostPush pc;
  pc.strength = bloom ? bloomStrength : 0.0f;
  pc.grain = 0.012f * std::clamp(theme.grainAmount, 0.0f, 4.0f) * std::clamp(intensity, 0.0f, 1.0f);
  pc.seed = postFrameSeed(seconds);
  pc.vignette = kPostVignette;
  pass(target.colorView(), target.width(), target.height(), VK_ATTACHMENT_LOAD_OP_DONT_CARE,
       m.composite, bloom ? s.sets[1] : s.sets[0], pc);
}

}  // namespace cb::render
