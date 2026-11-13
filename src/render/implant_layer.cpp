// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/implant_layer.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "shaders.hpp"
#include "view/implant_fx.hpp"

namespace cb::render {
namespace {
constexpr std::uint32_t kFrames = 2;
// std140: only vec4s, including counts (converted to integers in the shader).
struct GpuParams {
  std::array<ImplantParams::Panel, 16> panels;
  std::array<std::array<float, 4>, 4> tools;
  float timing[4];  // time, intensity, globalSync, heartbeat
  float screen[4];  // width, height, dip, pixelScale
  float counts[4];  // panelCount, toolCount, afterimageMax, reserved
  float warm[4];
  float body[4];
};
static_assert(sizeof(ImplantParams::Panel) == 32);
static_assert(offsetof(GpuParams, timing) == 576);
static_assert(sizeof(GpuParams) == 656);
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
                     VkImageUsageFlags usage, VkFormat format, Gpu& out) {
  out = {};
  out.width = w;
  out.height = h;
  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = format;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (const VkResult r = vkCreateImage(ctx.device(), &ici, nullptr, &out.image);
      r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create a implant image: " + describe(r));
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(ctx.device(), out.image, &req);
  const auto type =
      ctx.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!type.has_value()) {
    destroy(ctx.device(), out);
    return fail(type.error().code, type.error().message);
  }
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = *type;
  if (const VkResult r = vkAllocateMemory(ctx.device(), &mai, nullptr, &out.memory);
      r != VK_SUCCESS) {
    destroy(ctx.device(), out);
    return fail(ErrorCode::Internal, "cannot allocate implant memory: " + describe(r));
  }
  if (const VkResult r = vkBindImageMemory(ctx.device(), out.image, out.memory, 0);
      r != VK_SUCCESS) {
    destroy(ctx.device(), out);
    return fail(ErrorCode::Internal, "cannot bind implant memory: " + describe(r));
  }
  VkImageViewCreateInfo vci{};
  vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.image = out.image;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = format;
  vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (const VkResult r = vkCreateImageView(ctx.device(), &vci, nullptr, &out.view);
      r != VK_SUCCESS) {
    destroy(ctx.device(), out);
    return fail(ErrorCode::Internal, "cannot create a implant view: " + describe(r));
  }
  return {};
}

Result<VkShaderModule> makeShader(const VulkanContext& ctx,
                                  std::span<const std::uint8_t> spv) {
  VkShaderModuleCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
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
  VkImageMemoryBarrier2 b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  b.srcStageMask = srcStage;
  b.srcAccessMask = srcAccess;
  b.dstStageMask = dstStage;
  b.dstAccessMask = dstAccess;
  b.oldLayout = from;
  b.newLayout = to;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkDependencyInfo dep{};
  dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &dep);
}

}  // namespace

struct ImplantLayer::Impl {
  struct Slot {
    Gpu image;
    VkBuffer uniform{};
    VkDeviceMemory memory{};
    void* mapped{};
    VkDescriptorSet set{};
  };
  const VulkanContext* ctx{};
  VkFormat format{};
  VkShaderModule vert{}, frag{};
  VkSampler sampler{};
  VkDescriptorSetLayout setLayout{};
  VkDescriptorPool pool{};
  VkPipelineLayout layout{};
  VkPipeline composite{};
  std::array<Slot, kFrames> slots{};
  ~Impl() {
    if (!ctx) return;
    const auto d = ctx->device();
    for (auto& slot : slots) {
      destroy(d, slot.image);
      if (slot.mapped) vkUnmapMemory(d, slot.memory);
      if (slot.uniform) vkDestroyBuffer(d, slot.uniform, nullptr);
      if (slot.memory) vkFreeMemory(d, slot.memory, nullptr);
    }
    if (composite) vkDestroyPipeline(d, composite, nullptr);
    if (layout) vkDestroyPipelineLayout(d, layout, nullptr);
    if (pool) vkDestroyDescriptorPool(d, pool, nullptr);
    if (setLayout) vkDestroyDescriptorSetLayout(d, setLayout, nullptr);
    if (sampler) vkDestroySampler(d, sampler, nullptr);
    if (vert) vkDestroyShaderModule(d, vert, nullptr);
    if (frag) vkDestroyShaderModule(d, frag, nullptr);
  }
  Result<VkPipeline> pipeline(VkFormat targetFormat) {
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    {
      blend.blendEnable = VK_TRUE;
      blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      blend.colorBlendOp = VK_BLEND_OP_ADD;
      blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      blend.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    const VkDynamicState dynamics[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{};
    dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dynamics;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &targetFormat;
    VkGraphicsPipelineCreateInfo gpi{};
    gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
    if (const VkResult r = vkCreateGraphicsPipelines(ctx->device(), VK_NULL_HANDLE, 1,
                                                     &gpi, nullptr, &p);
        r != VK_SUCCESS)
      return fail(ErrorCode::Internal,
                  "cannot create a implant pipeline: " + describe(r));
    return p;
  }
};

ImplantLayer::ImplantLayer() = default;
ImplantLayer::~ImplantLayer() = default;
ImplantLayer::ImplantLayer(ImplantLayer&&) noexcept = default;
ImplantLayer& ImplantLayer::operator=(ImplantLayer&&) noexcept = default;

Result<ImplantLayer> ImplantLayer::create(const VulkanContext& ctx,
                                          VkFormat targetFormat) {
  ImplantLayer p;
  p.impl_ = std::make_unique<Impl>();
  auto& m = *p.impl_;
  m.ctx = &ctx;
  m.format = targetFormat;
  const auto d = ctx.device();
  auto vert = makeShader(ctx, spv::blur_vert_spv_span());
  if (!vert) return fail(vert.error().code, vert.error().message);
  m.vert = *vert;
  auto frag = makeShader(ctx, spv::implant_frag_spv_span());
  if (!frag) return fail(frag.error().code, frag.error().message);
  m.frag = *frag;
  VkSamplerCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
  sci.addressModeU = sci.addressModeV = sci.addressModeW =
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (auto r = vkCreateSampler(d, &sci, nullptr, &m.sampler); r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create implant sampler: " + describe(r));
  VkDescriptorSetLayoutBinding bindings[2]{};
  bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                 VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  bindings[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 nullptr};
  VkDescriptorSetLayoutCreateInfo dl{};
  dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dl.bindingCount = 2;
  dl.pBindings = bindings;
  if (auto r = vkCreateDescriptorSetLayout(d, &dl, nullptr, &m.setLayout);
      r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create implant set layout: " + describe(r));
  const VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFrames},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFrames}};
  VkDescriptorPoolCreateInfo dp{};
  dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dp.maxSets = kFrames;
  dp.poolSizeCount = 2;
  dp.pPoolSizes = sizes;
  if (auto r = vkCreateDescriptorPool(d, &dp, nullptr, &m.pool); r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot create implant pool: " + describe(r));
  const VkDescriptorSetLayout layouts[] = {m.setLayout, m.setLayout};
  VkDescriptorSet sets[kFrames]{};
  VkDescriptorSetAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  ai.descriptorPool = m.pool;
  ai.descriptorSetCount = kFrames;
  ai.pSetLayouts = layouts;
  if (auto r = vkAllocateDescriptorSets(d, &ai, sets); r != VK_SUCCESS)
    return fail(ErrorCode::Internal, "cannot allocate implant sets: " + describe(r));
  for (std::uint32_t i = 0; i < kFrames; ++i) {
    auto& slot = m.slots[i];
    slot.set = sets[i];
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = sizeof(GpuParams);
    bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (auto r = vkCreateBuffer(d, &bi, nullptr, &slot.uniform); r != VK_SUCCESS)
      return fail(ErrorCode::Internal, "cannot create implant uniforms: " + describe(r));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(d, slot.uniform, &req);
    auto type =
        ctx.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!type) return fail(type.error().code, type.error().message);
    VkMemoryAllocateInfo mi{};
    mi.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mi.allocationSize = req.size;
    mi.memoryTypeIndex = *type;
    if (auto r = vkAllocateMemory(d, &mi, nullptr, &slot.memory); r != VK_SUCCESS)
      return fail(ErrorCode::Internal,
                  "cannot allocate implant uniforms: " + describe(r));
    if (auto r = vkBindBufferMemory(d, slot.uniform, slot.memory, 0); r != VK_SUCCESS)
      return fail(ErrorCode::Internal, "cannot bind implant uniforms: " + describe(r));
    if (auto r = vkMapMemory(d, slot.memory, 0, sizeof(GpuParams), 0, &slot.mapped);
        r != VK_SUCCESS)
      return fail(ErrorCode::Internal, "cannot map implant uniforms: " + describe(r));
  }
  VkPipelineLayoutCreateInfo pl{};
  pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pl.setLayoutCount = 1;
  pl.pSetLayouts = &m.setLayout;
  if (auto r = vkCreatePipelineLayout(d, &pl, nullptr, &m.layout); r != VK_SUCCESS)
    return fail(ErrorCode::Internal,
                "cannot create implant pipeline layout: " + describe(r));
  auto pipeline = m.pipeline(targetFormat);
  if (!pipeline) return fail(pipeline.error().code, pipeline.error().message);
  m.composite = *pipeline;
  return p;
}

Result<void> ImplantLayer::prepare(std::uint32_t width, std::uint32_t height,
                                   std::uint32_t slot) {
  if (!impl_ || slot >= kFrames || width == 0 || height == 0)
    return fail(ErrorCode::InvalidArgument,
                "implant needs a valid slot and nonzero size");
  auto& m = *impl_;
  auto& s = m.slots[slot];
  if (s.image.image && s.image.width == width && s.image.height == height) return {};
  destroy(m.ctx->device(), s.image);
  if (auto r = makeGpu(*m.ctx, width, height,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       m.format, s.image);
      !r)
    return r;
  VkDescriptorImageInfo image{m.sampler, s.image.view,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorBufferInfo buffer{s.uniform, 0, sizeof(GpuParams)};
  VkWriteDescriptorSet writes[2]{};
  for (std::uint32_t i = 0; i < 2; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = s.set;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
  }
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[0].pImageInfo = &image;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  writes[1].pBufferInfo = &buffer;
  vkUpdateDescriptorSets(m.ctx->device(), 2, writes, 0, nullptr);
  return {};
}

void ImplantLayer::beginLayer(VkCommandBuffer cmd, std::uint32_t slot) {
  const auto& s = impl_->slots[slot];
  barrier(cmd, s.image.image, VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
          VK_ACCESS_2_MEMORY_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  VkRenderingAttachmentInfo att{};
  att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  att.imageView = s.image.view;
  att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att.clearValue.color = {{0, 0, 0, 0}};
  VkRenderingInfo ri{};
  ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  ri.renderArea = {{0, 0}, {s.image.width, s.image.height}};
  ri.layerCount = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments = &att;
  vkCmdBeginRendering(cmd, &ri);
}
void ImplantLayer::endLayer(VkCommandBuffer cmd, std::uint32_t slot) {
  vkCmdEndRendering(cmd);
  barrier(cmd, impl_->slots[slot].image.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}
void ImplantLayer::composite(VkCommandBuffer cmd, const OffscreenTarget& target,
                             const ImplantParams& params, std::uint32_t slot) {
  auto& m = *impl_;
  auto& s = m.slots[slot];
  const GpuParams p{params.panels,
                    params.toolRects,
                    {params.time, params.intensity, params.globalSync, params.heartbeat},
                    {static_cast<float>(target.width()),
                     static_cast<float>(target.height()), params.dip, params.pixelScale},
                    {static_cast<float>(std::min(params.panelCount, 16u)),
                     static_cast<float>(std::min(params.toolCount, 4u)),
                     view::kImplantAfterimageLevel, 0},
                    {params.warm[0], params.warm[1], params.warm[2], params.warm[3]},
                    {params.body[0], params.body[1], params.body[2], params.body[3]}};
  std::memcpy(s.mapped, &p, sizeof(p));
  // The previous scene/blur pass writes the destination; this pass loads and blends it.
  barrier(cmd, target.colorImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  VkRenderingAttachmentInfo att{};
  att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  att.imageView = target.colorView();
  att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  const VkRect2D scissor{{0, 0}, {target.width(), target.height()}};
  VkRenderingInfo ri{};
  ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  ri.renderArea = scissor;
  ri.layerCount = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments = &att;
  vkCmdBeginRendering(cmd, &ri);
  const VkViewport viewport{
      0, 0, static_cast<float>(target.width()), static_cast<float>(target.height()),
      0, 1};
  vkCmdSetViewport(cmd, 0, 1, &viewport);
  vkCmdSetScissor(cmd, 0, 1, &scissor);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m.composite);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m.layout, 0, 1, &s.set, 0,
                          nullptr);
  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRendering(cmd);
}
}  // namespace cb::render
