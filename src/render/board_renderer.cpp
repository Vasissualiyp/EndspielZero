// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/board_renderer.hpp"

#include <algorithm>
#include <cstring>
#include <map>

#include "shaders.hpp"
#ifdef CB_HAVE_IMGUI
#include "render/play_surface.hpp"  // the board as its shape (M17)
#endif

namespace cb::render {
namespace {

struct PushConstants {
  view::Mat4 viewProj{};
  float lightDir[4]{-0.42f, -0.55f, -0.72f, 0.0f};
  /// The camera position, so a fragment can turn its normal toward the eye. A
  /// non-orientable shape (a Klein bottle) cannot have a globally consistent outward
  /// normal, so shading by its sign would show the unavoidable flip as a seam; facing
  /// the normal at the viewer makes the flip invisible.
  float eyePos[4]{0.0f, 0.0f, 0.0f, 1.0f};
};
static_assert(sizeof(PushConstants) == 96);

struct BackdropPush {
  float inner[4]{};
  float outer[4]{};
  float params[4]{0.5f, 0.34f, 1.25f, 0.62f};
};
static_assert(sizeof(BackdropPush) == 48);

struct BlurPush {
  float direction[2]{};
  float strength{0};
  float dim{0};
  float ground[4]{};
};
static_assert(sizeof(BlurPush) == 32);

// The contact shadow (M18.2). The disc mesh's radius is half its extent, so an instance
// scale of 0.68 wants a world radius of 0.34: a touch wider than a piece's own foot
// (about 0.27) so the contact shows around the base, and still inside the cell (half
// extent 0.46). Lifted a hair above the board so it never z-fights the square it grounds.
constexpr float kShadowScale = 0.68f;
constexpr float kShadowLift = 0.004f;

void toFloat4(const view::Rgba& c, float out[4]) {
  out[0] = c.r;
  out[1] = c.g;
  out[2] = c.b;
  out[3] = c.a;
}

view::Rgba mix(const view::Rgba& a, const view::Rgba& b, float t) {
  return view::Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                    a.a + (b.a - a.a) * t};
}

Result<void> makeBuffer(const VulkanContext& ctx, VkDeviceSize size,
                        VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                        VkBuffer& buffer, VkDeviceMemory& memory) {
  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = size;
  bci.usage = usage;
  if (const VkResult r = vkCreateBuffer(ctx.device(), &bci, nullptr, &buffer);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create buffer: " + describe(r));
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(ctx.device(), buffer, &req);
  const auto type = ctx.findMemoryType(req.memoryTypeBits, props);
  if (!type.has_value()) return fail(type.error().code, type.error().message);
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = *type;
  if (const VkResult r = vkAllocateMemory(ctx.device(), &mai, nullptr, &memory);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot allocate buffer memory: " + describe(r));
  }
  vkBindBufferMemory(ctx.device(), buffer, memory, 0);
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
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create shader module: " + describe(r));
  }
  return m;
}

}  // namespace

view::Vec3 BoardRenderer::cellHalfExtent() {
  return view::Vec3{0.46f, 0.46f, 0.055f};
}

Result<BoardRenderer> BoardRenderer::create(const VulkanContext& ctx,
                                            VkFormat colorFormat) {
  BoardRenderer r;
  r.ctx_ = &ctx;
  r.colorFormat_ = colorFormat;
  r.meshes_ = MeshLibrary::build();
  if (auto ok = r.buildPipeline(); !ok.has_value())
    return fail(ok.error().code, ok.error().message);
  if (auto ok = r.buildBlurPipeline(); !ok.has_value())
    return fail(ok.error().code, ok.error().message);
  if (auto ok = r.uploadGeometry(); !ok.has_value())
    return fail(ok.error().code, ok.error().message);
  return r;
}

Result<void> BoardRenderer::buildPipeline() {
  const auto vs = makeShader(*ctx_, spv::board_vert_spv_span());
  if (!vs.has_value()) return fail(vs.error().code, vs.error().message);
  vert_ = *vs;
  const auto fs = makeShader(*ctx_, spv::board_frag_spv_span());
  if (!fs.has_value()) return fail(fs.error().code, fs.error().message);
  frag_ = *fs;

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push.size = sizeof(PushConstants);
  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &push;
  if (const VkResult r = vkCreatePipelineLayout(ctx_->device(), &plci, nullptr, &layout_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create pipeline layout: " + describe(r));
  }

  const VkVertexInputBindingDescription bindings[2]{
      {0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE}};
  const VkVertexInputAttributeDescription attrs[12]{
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, pos)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
      {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(MeshVertex, height)},
      {11, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(MeshVertex, color)},
      {3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, center)},
      {4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, scale)},
      {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, color)},
      {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, edge)},
      {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, edgeMask)},
      {8, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, roll)},
      {9, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, metal)},
      {10, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, quat)}};

  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 2;
  vi.pVertexBindingDescriptions = bindings;
  vi.vertexAttributeDescriptionCount = 12;
  vi.pVertexAttributeDescriptions = attrs;

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
  // The generated meshes are not consistently wound, and a piece with a missing face is
  // a far worse bug than the handful of triangles culling would have saved.
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  // Matches the target's multisampled attachment (M4.8, MSAA); the blur stays single.
  ms.rasterizationSamples = OffscreenTarget::kSampleCount;

  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag_;
  stages[1].pName = "main";

  const VkFormat colorFormat = colorFormat_;
  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = OffscreenTarget::kDepthFormat;

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
  gpi.layout = layout_;
  if (const VkResult r = vkCreateGraphicsPipelines(ctx_->device(), VK_NULL_HANDLE, 1,
                                                   &gpi, nullptr, &pipeline_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the graphics pipeline: " + describe(r));
  }

  // The geometry view's ghost board (M17.10): the same shaders and vertex input, with
  // alpha blending on and depth writes off. Drawn after the opaque pieces, it lets the
  // far side of the shape show through the near side; the pieces stay solid, which is
  // the point of seeing through.
  VkPipelineColorBlendAttachmentState blendOn{};
  blendOn.blendEnable = VK_TRUE;
  blendOn.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blendOn.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendOn.colorBlendOp = VK_BLEND_OP_ADD;
  blendOn.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  blendOn.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blendOn.alphaBlendOp = VK_BLEND_OP_ADD;
  blendOn.colorWriteMask = blend.colorWriteMask;
  VkPipelineColorBlendStateCreateInfo cbOn = cb;
  cbOn.pAttachments = &blendOn;
  VkPipelineDepthStencilStateCreateInfo dsNoWrite = ds;
  dsNoWrite.depthWriteEnable = VK_FALSE;
  VkGraphicsPipelineCreateInfo gpiOn = gpi;
  gpiOn.pColorBlendState = &cbOn;
  gpiOn.pDepthStencilState = &dsNoWrite;
  if (const VkResult r = vkCreateGraphicsPipelines(
          ctx_->device(), VK_NULL_HANDLE, 1, &gpiOn, nullptr, &surfaceBlendPipeline_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the ghost board pipeline: " + describe(r));
  }
  return {};
}

Result<void> BoardRenderer::buildBlurPipeline() {
  const auto vs = makeShader(*ctx_, spv::blur_vert_spv_span());
  if (!vs.has_value()) return fail(vs.error().code, vs.error().message);
  blurVert_ = *vs;
  const auto fs = makeShader(*ctx_, spv::blur_frag_spv_span());
  if (!fs.has_value()) return fail(fs.error().code, fs.error().message);
  blurFrag_ = *fs;

  VkSamplerCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sci.magFilter = VK_FILTER_LINEAR;
  sci.minFilter = VK_FILTER_LINEAR;
  // Clamped, not wrapped: a blur that samples past the edge must not fetch the far side
  // of the frame and smear it back in.
  sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
  if (const VkResult r = vkCreateSampler(ctx_->device(), &sci, nullptr, &blurSampler_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the blur sampler: " + describe(r));
  }

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo dlci{};
  dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dlci.bindingCount = 1;
  dlci.pBindings = &binding;
  if (const VkResult r =
          vkCreateDescriptorSetLayout(ctx_->device(), &dlci, nullptr, &blurSetLayout_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the blur set layout: " + describe(r));
  }

  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            2 * kFramesInFlight};
  VkDescriptorPoolCreateInfo dpci{};
  dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpci.maxSets = 2 * kFramesInFlight;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &size;
  if (const VkResult r =
          vkCreateDescriptorPool(ctx_->device(), &dpci, nullptr, &blurPool_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the blur pool: " + describe(r));
  }

  // Two sets per frame in flight: a frame that samples the colour image and one that
  // samples the scratch, each rewritten only by its own slot.
  std::array<VkDescriptorSetLayout, 2 * kFramesInFlight> layouts{};
  layouts.fill(blurSetLayout_);
  VkDescriptorSetAllocateInfo dsai{};
  dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dsai.descriptorPool = blurPool_;
  dsai.descriptorSetCount = static_cast<std::uint32_t>(layouts.size());
  dsai.pSetLayouts = layouts.data();
  std::array<VkDescriptorSet, 2 * kFramesInFlight> sets{};
  if (const VkResult r = vkAllocateDescriptorSets(ctx_->device(), &dsai, sets.data());
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot allocate the blur sets: " + describe(r));
  }
  for (std::uint32_t f = 0; f < kFramesInFlight; ++f) {
    blurFromColor_[f] = sets[2 * f];
    blurFromScratch_[f] = sets[2 * f + 1];
  }

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push.size = sizeof(BlurPush);
  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &blurSetLayout_;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &push;
  if (const VkResult r =
          vkCreatePipelineLayout(ctx_->device(), &plci, nullptr, &blurLayout_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the blur layout: " + describe(r));
  }

  // No vertex input at all: the shader builds a full-screen triangle from its index.
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
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = blurVert_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = blurFrag_;
  stages[1].pName = "main";

  const VkFormat colorFormat = colorFormat_;
  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;

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
  gpi.layout = blurLayout_;
  if (const VkResult r = vkCreateGraphicsPipelines(ctx_->device(), VK_NULL_HANDLE, 1,
                                                   &gpi, nullptr, &blurPipeline_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the blur pipeline: " + describe(r));
  }
  return {};
}

Result<void> BoardRenderer::bindBlurTarget(const OffscreenTarget& target,
                                           std::uint32_t frame) {
  if (blurBoundColor_[frame] == target.colorView()) return {};

  VkDescriptorImageInfo fromColor{blurSampler_, target.colorView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo fromScratch{blurSampler_, target.scratchView(),
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet writes[2]{};
  for (int i = 0; i < 2; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = i == 0 ? blurFromColor_[frame] : blurFromScratch_[frame];
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[i].pImageInfo = i == 0 ? &fromColor : &fromScratch;
  }
  // No device wait: the caller only records a frame after the slot's fence has signalled
  // (or, on the synchronous path, after its own submit finished), so nothing can still be
  // reading these sets.
  vkUpdateDescriptorSets(ctx_->device(), 2, writes, 0, nullptr);
  blurBoundColor_[frame] = target.colorView();
  return {};
}

Result<void> BoardRenderer::buildBackdropPipeline() {
  const auto vs = makeShader(*ctx_, spv::backdrop_vert_spv_span());
  if (!vs.has_value()) return fail(vs.error().code, vs.error().message);
  backdropVert_ = *vs;
  const auto fs = makeShader(*ctx_, spv::backdrop_frag_spv_span());
  if (!fs.has_value()) return fail(fs.error().code, fs.error().message);
  backdropFrag_ = *fs;

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push.size = sizeof(BackdropPush);
  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &push;
  if (const VkResult r =
          vkCreatePipelineLayout(ctx_->device(), &plci, nullptr, &backdropLayout_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the backdrop layout: " + describe(r));
  }

  // No vertex input at all: the shader derives a full-screen triangle from the index.
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
  // The backdrop fills the same multisampled attachment the board does.
  ms.rasterizationSamples = OffscreenTarget::kSampleCount;
  // It is behind everything, so it neither tests nor writes depth.
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = backdropVert_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = backdropFrag_;
  stages[1].pName = "main";

  const VkFormat colorFormat = colorFormat_;
  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = OffscreenTarget::kDepthFormat;

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
  gpi.layout = backdropLayout_;
  if (const VkResult r = vkCreateGraphicsPipelines(ctx_->device(), VK_NULL_HANDLE, 1,
                                                   &gpi, nullptr, &backdropPipeline_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the backdrop pipeline: " + describe(r));
  }
  return {};
}

Result<void> BoardRenderer::uploadGeometry() {
  const VkDeviceSize vbytes = meshes_.vertices.size() * sizeof(MeshVertex);
  const VkDeviceSize ibytes = meshes_.indices.size() * sizeof(std::uint16_t);
  const auto hostProps =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  if (auto r = makeBuffer(*ctx_, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostProps,
                          vertexBuffer_, vertexMem_);
      !r.has_value()) {
    return r;
  }
  if (auto r = makeBuffer(*ctx_, ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, hostProps,
                          indexBuffer_, indexMem_);
      !r.has_value()) {
    return r;
  }

  void* mapped = nullptr;
  vkMapMemory(ctx_->device(), vertexMem_, 0, vbytes, 0, &mapped);
  std::memcpy(mapped, meshes_.vertices.data(), vbytes);
  vkUnmapMemory(ctx_->device(), vertexMem_);
  vkMapMemory(ctx_->device(), indexMem_, 0, ibytes, 0, &mapped);
  std::memcpy(mapped, meshes_.indices.data(), ibytes);
  vkUnmapMemory(ctx_->device(), indexMem_);
  return {};
}

Result<void> BoardRenderer::ensureInstanceCapacity(std::uint32_t frame,
                                                   std::size_t count) {
  if (count <= instanceCapacity_[frame]) return {};
  if (instanceBuffer_[frame] != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(ctx_->device());
    vkDestroyBuffer(ctx_->device(), instanceBuffer_[frame], nullptr);
    vkFreeMemory(ctx_->device(), instanceMem_[frame], nullptr);
    instanceBuffer_[frame] = VK_NULL_HANDLE;
    instanceMem_[frame] = VK_NULL_HANDLE;
  }
  const std::size_t capacity = std::max<std::size_t>(count * 2, 4096);
  if (auto r = makeBuffer(
          *ctx_, capacity * sizeof(Instance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          instanceBuffer_[frame], instanceMem_[frame]);
      !r.has_value()) {
    return r;
  }
  instanceCapacity_[frame] = capacity;
  return {};
}

Result<void> BoardRenderer::ensureSurfaceCapacity(std::uint32_t frame,
                                                  std::size_t vertices,
                                                  std::size_t indices) {
  const auto grow = [&](std::size_t want, std::size_t unit, VkBufferUsageFlags usage,
                        VkBuffer& buf, VkDeviceMemory& mem,
                        std::size_t& cap) -> Result<void> {
    if (want <= cap) return Result<void>{};
    if (buf != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(ctx_->device());
      vkDestroyBuffer(ctx_->device(), buf, nullptr);
      vkFreeMemory(ctx_->device(), mem, nullptr);
      buf = VK_NULL_HANDLE;
      mem = VK_NULL_HANDLE;
    }
    const std::size_t capacity = std::max<std::size_t>(want * 2, 4096);
    if (auto r = makeBuffer(
            *ctx_, capacity * unit, usage,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buf, mem);
        !r.has_value()) {
      return r;
    }
    cap = capacity;
    return Result<void>{};
  };
  if (auto r = grow(vertices, sizeof(MeshVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    surfaceVertexBuffer_[frame], surfaceVertexMem_[frame],
                    surfaceVertexCapacity_[frame]);
      !r.has_value()) {
    return r;
  }
  return grow(indices, sizeof(std::uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
              surfaceIndexBuffer_[frame], surfaceIndexMem_[frame],
              surfaceIndexCapacity_[frame]);
}

namespace {

/// A fingerprint of the bodies and icons a variant's pieces carry; zero for none. Not a
/// security hash - a change detector, so a body edited in the designer reaches the screen.
std::uint64_t bodiesSignature(const VariantSpec& v) {
  std::uint64_t h = 1469598103934665603ull;
  bool any = false;
  const auto mix = [&](std::uint64_t x) {
    h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    h *= 1099511628211ull;
  };
  const auto outline = [&](const assets::Outline& o) {
    mix(o.points.size());
    for (const assets::ModelPoint& p : o.points) {
      mix(static_cast<std::uint64_t>(static_cast<std::uint16_t>(p.x)) << 16 |
          static_cast<std::uint16_t>(p.y));
    }
  };
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    const PieceTypeDef& p = v.pieces[i];
    if (!p.body.has_value() && !p.icon.has_value()) continue;
    any = true;
    mix(i);
    if (p.body.has_value()) {
      mix(1);
      mix(p.body->segments);
      mix(static_cast<std::uint64_t>(p.body->symmetry.kind));
      mix(p.body->symmetry.k);
      outline(p.body->profile);
      for (const assets::Element& e : p.body->elements) {
        outline(e.outline);
        mix(static_cast<std::uint64_t>(static_cast<std::uint16_t>(e.radius)) << 32 |
            static_cast<std::uint64_t>(static_cast<std::uint16_t>(e.height)) << 16 |
            static_cast<std::uint16_t>(e.lateral));
        mix(static_cast<std::uint64_t>(e.thickness) << 1 | (e.tangent ? 1u : 0u));
      }
    }
    if (p.icon.has_value()) {
      mix(2);
      for (const assets::Outline& o : p.icon->fills) outline(o);
      mix(3);
      for (const assets::Outline& o : p.icon->cuts) outline(o);
    }
  }
  return any ? (h | 1ull) : 0ull;
}

}  // namespace

Result<void> BoardRenderer::syncBodies(const VariantSpec& v) {
  const std::uint64_t sig = bodiesSignature(v);
  if (sig == bodiesSig_) return {};
  std::vector<CustomBody> custom;
  customSlot_.assign(v.pieces.size(), -1);
  std::size_t slot = 0;
  for (std::size_t i = 1; i < v.pieces.size() && slot < kCustomShapes; ++i) {
    // A piece with an icon but no body keeps its sculpted archetype; one with a body
    // takes a slot, and its icon (if any) rides along.
    if (!v.pieces[i].body.has_value()) continue;
    custom.push_back(
        CustomBody{slot, archetypeFor(v.pieces[i]), *v.pieces[i].body, v.pieces[i].icon});
    customSlot_[i] = static_cast<int>(slot);
    ++slot;
  }
  meshes_ = MeshLibrary::build(custom);
  vkDeviceWaitIdle(ctx_->device());
  if (vertexBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(ctx_->device(), vertexBuffer_, nullptr);
  if (vertexMem_ != VK_NULL_HANDLE) vkFreeMemory(ctx_->device(), vertexMem_, nullptr);
  if (indexBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(ctx_->device(), indexBuffer_, nullptr);
  if (indexMem_ != VK_NULL_HANDLE) vkFreeMemory(ctx_->device(), indexMem_, nullptr);
  vertexBuffer_ = indexBuffer_ = VK_NULL_HANDLE;
  vertexMem_ = indexMem_ = VK_NULL_HANDLE;
  if (auto r = uploadGeometry(); !r.has_value()) return r;
  bodiesSig_ = sig;
  return {};
}

InstanceSet BoardRenderer::buildInstances(
    const view::PositionView& p, const view::ViewConfig& cfg, const view::SeamMap* seams,
    const view::MoveAnimation* anim, const std::function<bool(CellId)>& visible,
    const std::function<bool(CellId)>& present,
    const std::vector<view::TimelineLink>& links) const {
  const VariantSpec& v = p.variant();
  // Custom bodies are used only while they are the ones synced for *this* variant.
  const bool useCustom = bodiesSig_ != 0 && bodiesSignature(v) == bodiesSig_;
#ifdef CB_HAVE_IMGUI
  // The geometry view: the board *is* the surface. Each square is built as a patch of
  // that surface - a curved quad with a little thickness, its corners the surface's own
  // points - and the whole board goes down as one mesh with its colours in its vertices.
  // Not instances: an instance is one shape repeated, and no two squares on a curved
  // board are the same shape. The pieces stay instanced, because a piece *is* an object
  // standing on the surface. `PlaySurface` owns every placement and the picker reads the
  // same one, which is what makes a click land on the square under the cursor (M17).
  // `!options_.flat` is the M18.6 resolution of the two view modes: a shape has no 2D
  // rendering, so if a script or settings file sets both, the flat board wins.
  if (options_.surface && !options_.flat && hasPlaySurface(v)) {
    InstanceSet out;
    SurfacePose pose;
    pose.slideU = options_.surfaceSlideU;
    pose.slideV = options_.surfaceSlideV;
    pose.hyperSpinU = options_.surfaceHyperSpinU;
    pose.hyperSpinV = options_.surfaceHyperSpinV;
    pose.evert = options_.surfaceEvert;
    pose.twist = options_.surfaceTwist;
    pose.openness = options_.surfaceOpenness;
    pose.thickness = options_.surfaceThickness;
    const float nx = static_cast<float>(v.dims.extent(0));
    pose.collapsePhase = nx > 0.0f ? options_.surfaceCollapseSquares / nx : 0.0f;
    pose.formed = options_.surfaceFormed;
    pose.shellSpread = options_.surfaceShellSpread;
    const PlaySurface surf = PlaySurface::build(v, pose);

    std::vector<Archetype> shape(v.pieces.size(), Archetype::Tower);
    std::vector<float> height(v.pieces.size(), 1.0f);
    for (std::size_t i = 1; i < v.pieces.size(); ++i) {
      shape[i] = useCustom && i < customSlot_.size() && customSlot_[i] >= 0
                   ? customShape(static_cast<std::size_t>(customSlot_[i]))
                   : archetypeFor(v.pieces[i]);
      height[i] = heightFor(v.pieces[i]);
    }
    const auto highlighted = p.highlighted();
    const auto isHighlighted = [&](CellId c) {
      return std::find(highlighted.begin(), highlighted.end(), c) != highlighted.end();
    };

    // The square's colour comes from its lattice parity, exactly as on the flat board.
    const auto fillFor = [&](CellId cell) {
      const Coord co = v.dims.toCoord(cell);
      constexpr std::uint8_t kPlane[2] = {0, 1};
      view::Rgba fill = (squareParity(co, kPlane, 2, options_.swapLayerColors) % 2 == 0)
                            ? theme_.boardDark
                            : theme_.boardLight;
      if (cell == p.selected()) return theme_.ember;
      if (options_.showLegalMoves && isHighlighted(cell)) {
        return p.at(cell).empty() ? mix(fill, theme_.moss, 0.72f)
                                  : mix(fill, theme_.blood, 0.62f);
      }
      if (options_.showLastMove && (cell == lastFrom_ || cell == lastTo_)) {
        return mix(fill, theme_.ember, 0.22f);
      }
      return fill;
    };

    constexpr int kSide = PlaySurface::kSubdiv + 1;
    constexpr float kHalf = PlaySurface::kThickness * 0.5f;
    const auto push = [&](const view::Vec3& at, const view::Vec3& n, float h,
                          const float rgba[4]) {
      MeshVertex mv{};
      mv.pos[0] = at.x;
      mv.pos[1] = at.y;
      mv.pos[2] = at.z;
      mv.normal[0] = n.x;
      mv.normal[1] = n.y;
      mv.normal[2] = n.z;
      mv.height = h;
      for (int c = 0; c < 4; ++c) mv.color[c] = rgba[c];
      out.surfaceVertices.push_back(mv);
      return static_cast<std::uint32_t>(out.surfaceVertices.size() - 1);
    };
    const auto quad = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c,
                          std::uint32_t d) {
      for (const std::uint32_t i : {a, b, c, a, c, d}) out.surfaceIndices.push_back(i);
    };

    for (const SurfacePatch& patch : surf.patches()) {
      const view::Rgba fill = fillFor(patch.cell);
      // The seam rails (M17.21): a closed shape has no edge to draw a rim on the way the
      // flat board does, so the wrap ring is tinted into the patch's own vertices
      // instead. The colour is the flat view's own seam colour at the same cell, so the
      // two views cannot disagree about where the seam is or what it looks like. This is
      // colour, not shape: it never touches the picker or `blocked`, and it rides the
      // same vertex colour array the ghost's alpha does. A stacked D >= 3 shape has no
      // continuous ring to key off, so it gets none (M17.22).
      const bool rails = seams != nullptr && options_.showSeams && !surf.stacked();
      // With the levels collapsed onto one ring (`shellSpread` 0) the stacked shape's
      // levels are coincident and their transparencies multiply: four ghosts at `a` read
      // as `1 - (1-a)^4`, much darker than the plain torus the shape has just been. Give
      // each level the alpha that composites to `a` when they coincide, and ease back to
      // `a` each as they spread into their own nested shells.
      float ghostAlpha = options_.surfaceGhost;
      if (surf.stacked() && v.dims.dims() >= 3 && ghostAlpha < 0.99f) {
        const float levels = static_cast<float>(std::max<int>(1, v.dims.extent(2)));
        const float single = 1.0f - std::pow(1.0f - ghostAlpha, 1.0f / levels);
        const float spread = std::clamp(options_.surfaceShellSpread, 0.0f, 1.0f);
        ghostAlpha = single + (ghostAlpha - single) * spread;
      }
      const auto tinted = [&](view::Rgba base, int i, int j) {
        if (rails) {
          const SurfaceRail ru = surfaceRail(*seams, patch.cell, 0, i, j);
          const SurfaceRail rv = surfaceRail(*seams, patch.cell, 1, i, j);
          if (ru.weight > 0.0f && rv.weight > 0.0f) {
            // The one corner where both rings cross: average them so neither wins.
            base = mix(base, mix(ru.color, rv.color, 0.5f), 1.0f);
          } else if (ru.weight > 0.0f) {
            base = mix(base, ru.color, ru.weight);
          } else if (rv.weight > 0.0f) {
            base = mix(base, rv.color, rv.weight);
          }
        }
        // The ghost alpha rides in the vertex colour (M17.10); the pieces are separate
        // instances and stay opaque.
        base.a *= ghostAlpha;
        return base;
      };
      // The square's *sides* take the board's rim colour: that is what keeps the gap
      // between two squares a line rather than a seamless smear of chequerboard. The
      // underside is the square's own colour, the same as its face, so a board turned
      // over - or a shape seen from inside, as on a Klein bottle - still reads as the
      // same board rather than a uniform grey shell.
      const view::Rgba grooveBase = mix(fill, theme_.boardRim, 0.62f);
      const std::uint32_t base = static_cast<std::uint32_t>(out.surfaceVertices.size());
      // The face a player looks at, then the same grid pushed in along its own normals.
      // A square has thickness for the same reason the flat board's cells do: an edge you
      // can see from a low angle is what makes the board an object rather than a stain.
      for (int i = 0; i < kSide; ++i) {
        for (int j = 0; j < kSide; ++j) {
          const std::size_t k = static_cast<std::size_t>(i * kSide + j);
          float rgba[4]{};
          toFloat4(tinted(fill, i, j), rgba);
          push(patch.pos[k] + patch.normal[k] * kHalf, patch.normal[k], 1.0f, rgba);
        }
      }
      for (int i = 0; i < kSide; ++i) {
        for (int j = 0; j < kSide; ++j) {
          const std::size_t k = static_cast<std::size_t>(i * kSide + j);
          float rgba[4]{};
          toFloat4(tinted(fill, i, j), rgba);
          push(patch.pos[k] - patch.normal[k] * kHalf,
               view::Vec3{-patch.normal[k].x, -patch.normal[k].y, -patch.normal[k].z},
               0.80f, rgba);
        }
      }
      const std::uint32_t backBase = base + kSide * kSide;
      const auto top = [&](int i, int j) {
        return base + static_cast<std::uint32_t>(i * kSide + j);
      };
      const auto bot = [&](int i, int j) {
        return backBase + static_cast<std::uint32_t>(i * kSide + j);
      };
      for (int i = 0; i + 1 < kSide; ++i) {
        for (int j = 0; j + 1 < kSide; ++j) {
          quad(top(i, j), top(i + 1, j), top(i + 1, j + 1), top(i, j + 1));
          quad(bot(i, j), bot(i, j + 1), bot(i + 1, j + 1), bot(i + 1, j));
        }
      }
      // The rim, one band of quads round the patch's border. Its own vertices, so the
      // edge stays a crease instead of smearing the face's shading round the corner. On a
      // wrap edge the rim carries the rail colour end to end, which is the line a player
      // reads as the seam.
      const auto rim = [&](int i0, int j0, int i1, int j1) {
        const std::size_t k0 = static_cast<std::size_t>(i0 * kSide + j0);
        const std::size_t k1 = static_cast<std::size_t>(i1 * kSide + j1);
        const view::Vec3 edge = patch.pos[k1] - patch.pos[k0];
        const view::Vec3 side = view::cross(edge, patch.normal[k0]);
        const view::Vec3 n =
            view::length(side) > 1e-6f ? view::normalize(side) : patch.normal[k0];
        float c0[4]{};
        float c1[4]{};
        toFloat4(tinted(grooveBase, i0, j0), c0);
        toFloat4(tinted(grooveBase, i1, j1), c1);
        const std::uint32_t a =
            push(patch.pos[k0] + patch.normal[k0] * kHalf, n, 1.0f, c0);
        const std::uint32_t b =
            push(patch.pos[k1] + patch.normal[k1] * kHalf, n, 1.0f, c1);
        const std::uint32_t c =
            push(patch.pos[k1] - patch.normal[k1] * kHalf, n, 0.80f, c1);
        const std::uint32_t d =
            push(patch.pos[k0] - patch.normal[k0] * kHalf, n, 0.80f, c0);
        quad(a, b, c, d);
      };
      for (int i = 0; i + 1 < kSide; ++i) {
        rim(i, 0, i + 1, 0);
        rim(i + 1, kSide - 1, i, kSide - 1);
      }
      for (int j = 0; j + 1 < kSide; ++j) {
        rim(0, j + 1, 0, j);
        rim(kSide - 1, j, kSide - 1, j + 1);
      }
    }

    std::vector<std::vector<Instance>> byShape(
        kShapeSlots);
    // The token discs (M17.26) live in their own groups and are concatenated ahead of the
    // icons in each archetype's batch, so `record` draws the first `discCounts[s]` from
    // the disc mesh and the rest from the icon mesh.
    std::vector<std::vector<Instance>> byShapeDisc(
        kShapeSlots);
    const auto emitSurfacePiece = [&](std::vector<std::vector<Instance>>& into,
                                      std::vector<std::vector<Instance>>& discInto,
                                      const Piece& piece, const view::Vec3& centre,
                                      const view::Vec3& normal,
                                      const std::array<float, 4>& quat, float fit,
                                      bool inCheck, float alpha = 1.0f,
                                      float scaleMul = 1.0f) {
      view::Rgba pc =
          piece.colorOf() == Color::White ? theme_.whitePiece : theme_.blackPiece;
      if (options_.showCheck && inCheck) pc = mix(pc, theme_.blood, 0.65f);
      Instance body{};
      // Standing on the square, not in it: the piece's foot is its own local z = 0. The
      // icon-faced puck (M17.25) is the exception - it is centred *on* the surface so it
      // protrudes on both sides, which is what lets a flipped seat show the same icon.
      const float lift = options_.shapePieceCylinders ? 0.0f : kHalf;
      body.center[0] = centre.x + normal.x * lift;
      body.center[1] = centre.y + normal.y * lift;
      body.center[2] = centre.z + normal.z * lift;
      const float hgt = 1.0f + (height[piece.type] - 1.0f) * options_.pieceHeightScale;
      // Never bigger than the flat board's piece, and smaller where the square is. The
      // measure is the square's mean side rather than its shorter one: an embedding
      // squeezes one axis and stretches the other - a Moebius square is four times longer
      // than it is wide - and a piece sized to the short side there is a speck.
      body.scale[0] = 0.8f * fit * scaleMul;
      body.scale[1] = 0.8f * fit * scaleMul;
      body.scale[2] = 0.8f * fit * hgt * scaleMul;
      body.quat[0] = quat[0];
      body.quat[1] = quat[1];
      body.quat[2] = quat[2];
      body.quat[3] = quat[3];
      toFloat4(pc, body.color);
      body.color[3] = alpha;
      const std::size_t sh = static_cast<std::size_t>(shape[piece.type]);
      // The disc is the token, in the theme's one shared furniture colour, read live so a
      // palette switch repaints it the same frame it repaints everything else. It carries
      // no check tint - only the figure does, exactly as in the flat view - but it does
      // carry GHOST/fade alpha, so a ghosted or captured piece fades as one object, disc
      // and figure together.
      if (options_.shapePieceCylinders) {
        Instance disc =
            body;  // same centre/scale/quat, so it sits exactly under the icon
        toFloat4(theme_.pieceToken, disc.color);
        disc.color[3] = alpha;
        discInto[sh].push_back(disc);
      }
      into[sh].push_back(body);
    };
    // M18.2: a soft decal under each occupied seat, lying on the surface and turned to
    // the seat's own frame, exactly where the piece stands. The travelling piece's shadow
    // is pinned to the surface under it - `ground`, never the hovered `position` - so the
    // contact stays true while the piece rides above the board.
    std::vector<std::vector<Instance>> byShapeShadow(
        kShapeSlots);
    const auto emitSurfaceShadow = [&](const view::Vec3& centre, const view::Vec3& normal,
                                       const std::array<float, 4>& quat, float fit) {
      Instance sh{};
      sh.center[0] = centre.x + normal.x * (kHalf + kShadowLift);
      sh.center[1] = centre.y + normal.y * (kHalf + kShadowLift);
      sh.center[2] = centre.z + normal.z * (kHalf + kShadowLift);
      const float s = kShadowScale * fit;
      sh.scale[0] = s;
      sh.scale[1] = s;
      sh.scale[2] = 1.0f;
      sh.quat[0] = quat[0];
      sh.quat[1] = quat[1];
      sh.quat[2] = quat[2];
      sh.quat[3] = quat[3];
      toFloat4(theme_.shadow, sh.color);
      byShapeShadow[static_cast<std::size_t>(Archetype::Disc)].push_back(sh);
    };
    for (const SurfaceSeat& seat : surf.seats()) {
      const Piece piece = p.at(seat.cell);
      if (piece.empty()) continue;
      // The travelling piece is drawn where it currently is, not where it arrived; skip
      // it here so it is not drawn twice (M17.15).
      if (anim != nullptr && anim->active() && seat.cell == anim->travellingTo())
        continue;
      const float fit = std::min(std::sqrt(seat.stepU * seat.stepV), 1.0f);
      emitSurfacePiece(byShape, byShapeDisc, piece, seat.centre, seat.normal, seat.quat,
                       fit, seat.cell == checkCell_);
      emitSurfaceShadow(seat.centre, seat.normal, seat.quat, fit);
    }
    // The mover, part-way along the surface between its start and its end.
    if (anim != nullptr && anim->active()) {
      const Piece moving = p.at(anim->travellingTo());
      if (!moving.empty()) {
        const SurfaceMoveSample s =
            surfaceMoveSample(anim->path(), surf, anim->progress());
        emitSurfacePiece(byShape, byShapeDisc, moving, s.position, s.normal, s.quat,
                         s.fit, anim->travellingTo() == checkCell_);
        emitSurfaceShadow(s.ground, s.normal, s.quat, s.fit);
      }
    }

    // M18.1: the same capture flourish as the flat board, on its own square of the
    // surface - the captured piece shrinking and fading, and a blood flash at its foot.
    std::vector<std::vector<Instance>> byShapeFlourish(
        kShapeSlots);
    std::vector<std::vector<Instance>> byShapeFlourishDisc(
        kShapeSlots);
    if (anim != nullptr && anim->active() && anim->captures()) {
      const float t = anim->progress();
      const float fade = view::captureFade(t);
      const float flash = view::captureFlash(t);
      for (const SurfaceSeat& seat : surf.seats()) {
        if (seat.cell != anim->captureCell()) continue;
        if (fade > 0.0f) {
          const float fit = std::min(std::sqrt(seat.stepU * seat.stepV), 1.0f);
          emitSurfacePiece(byShapeFlourish, byShapeFlourishDisc, anim->capturedPiece(),
                           seat.centre, seat.normal, seat.quat, fit, false, fade,
                           0.35f + 0.65f * fade);
        }
        if (flash > 0.0f) {
          Instance ring{};
          ring.center[0] = seat.centre.x + seat.normal.x * kHalf;
          ring.center[1] = seat.centre.y + seat.normal.y * kHalf;
          ring.center[2] = seat.centre.z + seat.normal.z * kHalf;
          const float grow = 0.45f + 0.75f * (1.0f - flash);
          ring.scale[0] = grow;
          ring.scale[1] = grow;
          ring.scale[2] = 0.14f;
          ring.quat[0] = seat.quat[0];
          ring.quat[1] = seat.quat[1];
          ring.quat[2] = seat.quat[2];
          ring.quat[3] = seat.quat[3];
          toFloat4(theme_.blood, ring.color);
          ring.color[3] = flash * 0.85f;
          byShapeFlourish[static_cast<std::size_t>(Archetype::Cell)].push_back(ring);
        }
        break;
      }
    }

    // M17.12 part D: the tesseract's own 16-corner, 32-edge wireframe scaffold, so the
    // eye has a small number of long, stable lines to read the fourth-dimension rotation
    // against. The corners are the board's *own* `playShapeAt` projection at the lattice
    // extremes - not a second rotation formula - so the scaffold turns in exact lockstep
    // with the cells. Gated on `showSeams`, the existing toggle for structural (non-
    // gameplay) markings, and specific to `hyper4`: the nested shells are legible
    // already. The bars are blended (a wire, not a strut), and they never enter picking -
    // the ray still tests the patches alone, exactly as before.
    if (v.name == "hyper4" && options_.showSeams) {
      constexpr float kScaffoldHalf = 0.03f;
      constexpr float kScaffoldAlpha = 0.5f;
      const view::Vec3 cellHalf = cellHalfExtent();
      const std::size_t cellGroup = static_cast<std::size_t>(Archetype::Cell);
      out.scaffoldBatches[cellGroup].first =
          static_cast<std::uint32_t>(out.scaffold.size());
      for (const ScaffoldEdge& e : hypercubeScaffoldEdges(v, pose)) {
        const view::Vec3 delta = e.b - e.a;
        const float len = view::length(delta);
        if (len < 1e-6f) continue;
        const view::Vec3 mid = (e.a + e.b) * 0.5f;
        Instance bar{};
        bar.center[0] = mid.x;
        bar.center[1] = mid.y;
        bar.center[2] = mid.z;
        // A bar mesh's long axis is its local +X, turned onto the edge by `quat`; the
        // other two axes set the wire's cross-section.
        bar.scale[0] = (0.5f * len) / cellHalf.x;
        bar.scale[1] = kScaffoldHalf / cellHalf.y;
        bar.scale[2] = kScaffoldHalf / cellHalf.z;
        for (std::size_t q = 0; q < 4; ++q) bar.quat[q] = e.quat[q];
        toFloat4(theme_.boardRim, bar.color);
        bar.color[3] = kScaffoldAlpha;
        out.scaffold.push_back(bar);
      }
      out.scaffoldBatches[cellGroup].count =
          static_cast<std::uint32_t>(out.scaffold.size()) -
          out.scaffoldBatches[cellGroup].first;
    }

    std::size_t total = 0;
    for (const auto& group : byShape) total += group.size();
    for (const auto& group : byShapeDisc) total += group.size();
    out.instances.reserve(total + 1);
    for (std::size_t sh = 0; sh < byShape.size(); ++sh) {
      out.batches[sh].first = static_cast<std::uint32_t>(out.instances.size());
      // The disc goes down first, so the batch's first `discCounts[s]` entries are discs
      // and the icons follow: two ranges, one contiguous span, one buffer (M17.26).
      out.discCounts[sh] = static_cast<std::uint32_t>(byShapeDisc[sh].size());
      out.instances.insert(out.instances.end(), byShapeDisc[sh].begin(),
                           byShapeDisc[sh].end());
      out.instances.insert(out.instances.end(), byShape[sh].begin(), byShape[sh].end());
      out.batches[sh].count =
          static_cast<std::uint32_t>(byShapeDisc[sh].size() + byShape[sh].size());
    }
    for (std::size_t sh = 0; sh < byShapeFlourish.size(); ++sh) {
      out.flourishBatches[sh].first = static_cast<std::uint32_t>(out.flourish.size());
      out.flourishDiscCounts[sh] =
          static_cast<std::uint32_t>(byShapeFlourishDisc[sh].size());
      out.flourish.insert(out.flourish.end(), byShapeFlourishDisc[sh].begin(),
                          byShapeFlourishDisc[sh].end());
      out.flourish.insert(out.flourish.end(), byShapeFlourish[sh].begin(),
                          byShapeFlourish[sh].end());
      out.flourishBatches[sh].count = static_cast<std::uint32_t>(
          byShapeFlourishDisc[sh].size() + byShapeFlourish[sh].size());
    }
    for (std::size_t sh = 0; sh < byShapeShadow.size(); ++sh) {
      out.shadowBatches[sh].first = static_cast<std::uint32_t>(out.shadows.size());
      out.shadowBatches[sh].count = static_cast<std::uint32_t>(byShapeShadow[sh].size());
      out.shadows.insert(out.shadows.end(), byShapeShadow[sh].begin(),
                         byShapeShadow[sh].end());
    }
    // The board mesh is already in world space and already coloured, so its instance is
    // the identity: no offset, unit scale, white, and no seam band.
    Instance identity{};
    identity.scale[0] = 1.0f;
    identity.scale[1] = 1.0f;
    identity.scale[2] = 1.0f;
    identity.color[0] = 1.0f;
    identity.color[1] = 1.0f;
    identity.color[2] = 1.0f;
    identity.color[3] = 1.0f;
    out.surfaceInstance = static_cast<std::uint32_t>(out.instances.size());
    out.instances.push_back(identity);
    return out;
  }
#endif
  auto placements = view::layout(v.dims, cfg);
  // A temporal variant's lattice is mostly boards that do not exist yet; `visible` says
  // which are real, so the opening board is one board rather than a grid of empty ones.
  if (visible) {
    placements.erase(
        std::remove_if(placements.begin(), placements.end(),
                       [&](const view::Placement& pl) { return !visible(pl.cell); }),
        placements.end());
  }

  // Appearance per piece type, resolved once: a variant may declare a shape and a
  // height, and anything it leaves out is derived from how the piece moves.
  std::vector<Archetype> shape(v.pieces.size(), Archetype::Tower);
  std::vector<float> height(v.pieces.size(), 1.0f);
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    shape[i] = useCustom && i < customSlot_.size() && customSlot_[i] >= 0
                   ? customShape(static_cast<std::size_t>(customSlot_[i]))
                   : archetypeFor(v.pieces[i]);
    height[i] = heightFor(v.pieces[i]);
  }

  const auto highlighted = p.highlighted();
  const auto isHighlighted = [&](CellId c) {
    return std::find(highlighted.begin(), highlighted.end(), c) != highlighted.end();
  };

  // Two passes so instances of the same shape are contiguous: one draw call per shape.
  std::vector<std::vector<Instance>> byShape(kShapeSlots);
  // M18.2: the contact shadows, kept apart from the opaque batches because they blend.
  // The flat (`options_.flat`) board draws its pieces in the interface, so the renderer
  // has no shadow to ground and emits none - the same cut the M18.1 flourish takes.
  std::vector<std::vector<Instance>> byShapeShadow(
      kShapeSlots);
  const view::Vec3 half = cellHalfExtent();

  // One soft decal on the cell under a piece, at the cell's own surface point.
  const auto emitShadow = [&](float x, float y, float z) {
    Instance sh{};
    sh.center[0] = x;
    sh.center[1] = y;
    sh.center[2] = z + half.z + kShadowLift;
    sh.scale[0] = kShadowScale;
    sh.scale[1] = kShadowScale;
    sh.scale[2] = 1.0f;
    toFloat4(theme_.shadow, sh.color);
    byShapeShadow[static_cast<std::size_t>(Archetype::Disc)].push_back(sh);
  };

  // One piece, wherever it happens to be this frame. A travelling piece is the same
  // call with a different position, which is what keeps the animation from being a
  // second, subtly different way of drawing a piece.
  const auto emitPiece = [&](Piece piece, float x, float y, float z, bool inCheck) {
    view::Rgba pieceColor =
        piece.colorOf() == Color::White ? theme_.whitePiece : theme_.blackPiece;
    if (options_.showCheck && inCheck) pieceColor = mix(pieceColor, theme_.blood, 0.65f);

    if (options_.flat) {
      // Seen from straight above, a model is a blob, so the flat board's pieces are
      // drawn entirely by the interface: a circle token with the piece's own icon, in
      // screen space, which is also the only layer that can draw a circle and follow the
      // move animation with it. The renderer contributes the board, not the pieces.
      return;
    }

    Instance body{};
    body.center[0] = x;
    body.center[1] = y;
    body.center[2] = z + half.z;
    // The archetype already gives the piece its own height; this blends in the extra
    // scaling that encodes value. At 0 every piece stands at its natural size - which is
    // still different per shape - and nothing ever ends up shorter than its own model.
    const float h = 1.0f + (height[piece.type] - 1.0f) * options_.pieceHeightScale;
    body.scale[0] = 0.8f;
    body.scale[1] = 0.8f;
    body.scale[2] = 0.8f * h;
    toFloat4(pieceColor, body.color);
    byShape[static_cast<std::size_t>(shape[piece.type])].push_back(body);
  };

  for (const view::Placement& pl : placements) {
    const Coord c = v.dims.toCoord(pl.cell);
    // Square colour comes from the drawn spatial axes only: a sub-board on another turn
    // or timeline is still a chessboard, and its light and dark squares must not swap as
    // the turn axis advances.
    std::uint8_t drawn[3] = {};
    std::size_t drawnCount = 0;
    for (std::uint8_t a : cfg.screenAxes) drawn[drawnCount++] = a;
    const int parity = squareParity(c, drawn, drawnCount, options_.swapLayerColors);

    Instance cell{};
    cell.center[0] = pl.x;
    cell.center[1] = pl.y;
    cell.center[2] = pl.z;
    cell.scale[0] = 1.0f;
    cell.scale[1] = 1.0f;
    cell.scale[2] = 1.0f;

    view::Rgba fill = (parity % 2 == 0) ? theme_.boardDark : theme_.boardLight;
    if (pl.cell == p.selected()) {
      fill = theme_.ember;
    } else if (options_.showLegalMoves && isHighlighted(pl.cell)) {
      // A legal destination that would take something is marked differently from an
      // empty one, so a capture never comes as a surprise.
      fill = p.at(pl.cell).empty() ? mix(fill, theme_.moss, 0.72f)
                                   : mix(fill, theme_.blood, 0.62f);
    } else if (options_.showLastMove && (pl.cell == lastFrom_ || pl.cell == lastTo_)) {
      fill = mix(fill, theme_.ember, 0.22f);
    }
    toFloat4(fill, cell.color);

    byShape[static_cast<std::size_t>(Archetype::Cell)].push_back(cell);

    // A seam is drawn as a coloured rail along the face, one instance per face, so the
    // two ends of one identification can carry the same colour. A single tint per cell
    // could not: a corner cell is on two different portals at once.
    if (options_.showSeams && seams != nullptr) {
      for (const view::SeamFace& f : seams->at(pl.cell)) {
        const float dir = f.side == Side::Min ? -1.0f : 1.0f;
        Instance rail{};
        rail.center[0] = pl.x + (f.screenAxis == 0 ? dir * 0.5f : 0.0f);
        rail.center[1] = pl.y + (f.screenAxis == 1 ? dir * 0.5f : 0.0f);
        rail.center[2] = pl.z + half.z * 0.6f;
        rail.scale[0] = (f.screenAxis == 0 ? 0.07f : 0.5f) / half.x;
        rail.scale[1] = (f.screenAxis == 1 ? 0.07f : 0.5f) / half.y;
        rail.scale[2] = 0.055f / half.z;
        toFloat4(f.color, rail.color);
        // A mirror is drawn as metal; a glued portal keeps the matte seam colour.
        rail.metal = f.kind == view::SeamKind::Mirror ? 1.0f : 0.0f;
        byShape[static_cast<std::size_t>(Archetype::Cell)].push_back(rail);
      }
    }

    const Piece piece = p.at(pl.cell);
    if (piece.empty()) continue;
    // The travelling piece is drawn where it currently is, not where it has already
    // arrived; skipping it here is what stops it being drawn twice.
    if (anim != nullptr && anim->active() && pl.cell == anim->travellingTo()) continue;

    emitPiece(piece, pl.x, pl.y, pl.z, pl.cell == checkCell_);
    if (!options_.flat) emitShadow(pl.x, pl.y, pl.z);
  }

  // Where each sub-board ended up. Measured once and used twice: by the plinths, and by
  // the rails that run under a timeline.
  struct Extent {
    float minX{0}, maxX{0}, minY{0}, maxY{0}, z{0};
    bool seen{false};
    bool present{false};
  };
  std::vector<Extent> extentsBySlice(view::enumerateSlices(v.dims, cfg).size());

  // A plinth under each sub-board, so the cells sit on something instead of floating in
  // the dark. One per slice, because a grid of sub-boards should read as separate boards.
  {
    std::vector<Extent>& extents = extentsBySlice;
    for (const view::Placement& pl : placements) {
      Extent& e = extents[pl.slice];
      if (present && present(pl.cell)) e.present = true;
      if (!e.seen) {
        e = Extent{pl.x, pl.x, pl.y, pl.y, pl.z, true, e.present};
        continue;
      }
      e.minX = std::min(e.minX, pl.x);
      e.maxX = std::max(e.maxX, pl.x);
      e.minY = std::min(e.minY, pl.y);
      e.maxY = std::max(e.maxY, pl.y);
      e.z = std::min(e.z, pl.z);
    }
    // The board is an object standing on the page, not a pattern printed on it, and the
    // rim under the cells is what says so. It comes from the palette rather than from a
    // blend of the ground, because on a light theme a blend of the ground is the ground.
    const view::Rgba wood = theme_.boardRim;
    for (const Extent& e : extents) {
      if (!e.seen) continue;
      Instance plinth{};
      plinth.center[0] = (e.minX + e.maxX) * 0.5f;
      plinth.center[1] = (e.minY + e.maxY) * 0.5f;
      plinth.center[2] = e.z - half.z - 0.13f;
      plinth.scale[0] = ((e.maxX - e.minX) * 0.5f + 0.78f) / half.x;
      plinth.scale[1] = ((e.maxY - e.minY) * 0.5f + 0.78f) / half.y;
      plinth.scale[2] = 0.13f / half.z;
      // A board in the present - one a move is expected on - stands on a lit plinth, so
      // the boards to answer on are visible at a glance.
      toFloat4(e.present ? mix(wood, theme_.emberDeep, 0.55f) : wood, plinth.color);
      // A warm rim around the edge of the table, which is what makes it read as an
      // object rather than as a darker rectangle.
      toFloat4(e.present ? theme_.ember : mix(theme_.boardRim, theme_.emberDeep, 0.35f),
               plinth.edge);
      plinth.edgeMask = 15.0f;
      byShape[static_cast<std::size_t>(Archetype::Cell)].push_back(plinth);
    }
  }

  // Time's arrow, drawn under each timeline.
  //
  // A grid of boards says nothing about *which way* the game runs through it. On a board
  // with a temporal axis that is the first thing a player needs: these boards are a
  // sequence, those ones beside them are a different history of the same game. So each
  // row of boards gets a rail beneath it with a head on the end, and the timeline the
  // game started on is drawn in the accent while the branches are dimmer.
  {
    int temporalGrid = -1;
    for (std::size_t i = 0; i < cfg.gridAxes.size(); ++i) {
      if (v.dims.kind(cfg.gridAxes[i]) == AxisKind::Temporal)
        temporalGrid = static_cast<int>(i);
    }
    if (temporalGrid >= 0) {
      const auto slices = view::enumerateSlices(v.dims, cfg);
      const bool alongX = (temporalGrid % 2 == 0) != cfg.gridVertical;

      // One rail per timeline: every slice that differs only in its turn coordinate.
      std::map<std::vector<std::int16_t>, std::pair<float, float>> rails;
      std::map<std::vector<std::int16_t>, bool> origin;
      // Where each timeline's rail sits, so a branch can be joined to its parent. Only
      // meaningful with a single multiverse axis; anything more and the links are
      // skipped.
      std::map<std::int16_t, float> acrossOf;
      std::map<std::int16_t, float> spanOf;
      std::map<std::int16_t, float> zOf;
      std::map<std::int16_t, float> zBoardOf;
      std::map<std::pair<std::int16_t, std::int16_t>, float> boardHi;
      for (std::uint32_t si = 0; si < slices.size(); ++si) {
        if (si >= extentsBySlice.size() || !extentsBySlice[si].seen) continue;
        std::vector<std::int16_t> key;
        for (std::size_t i = 0; i < slices[si].at.size(); ++i) {
          if (static_cast<int>(i) == temporalGrid) continue;
          key.push_back(slices[si].at[i]);
        }
        const auto turn = slices[si].at[static_cast<std::size_t>(temporalGrid)];
        const Extent& e = extentsBySlice[si];
        const float lo = alongX ? e.minX : e.minY;
        const float hi = alongX ? e.maxX : e.maxY;
        if (key.size() == 1) boardHi[{key[0], turn}] = hi;
        const auto it = rails.find(key);
        if (it == rails.end()) {
          rails.emplace(key, std::pair{lo, hi});
        } else {
          it->second.first = std::min(it->second.first, lo);
          it->second.second = std::max(it->second.second, hi);
        }
        // The timeline the game began on is the one with a board at turn 0 - not the one
        // at line 0, since the origin sits in the middle of the axis so both players have
        // room to branch.
        if (origin.find(key) == origin.end()) {
          origin[key] = turn == 0;
        } else {
          origin[key] = origin[key] || (turn == 0);
        }
      }

      // Where a branch's rail begins: exactly where its connector meets it, so the two
      // read as one path rather than leaving a gap. The origin timeline is unaffected.
      std::map<std::int16_t, float> branchStartOf;
      if (!links.empty()) {
        for (const view::TimelineLink& link : links) {
          const auto hi = boardHi.find({link.fromLine, link.atTurn});
          if (hi == boardHi.end()) continue;
          branchStartOf[link.toLine] = hi->second + cfg.gridGap * 0.5f + 0.5f;
        }
      }

      // One height for every rail, from the lowest board on the board, so the origin
      // timeline's rail and a branch's share a centreline instead of sitting at two
      // slightly different depths.
      float railFloorZ = 0.0f;
      {
        bool any = false;
        for (const Extent& e : extentsBySlice) {
          if (!e.seen) continue;
          railFloorZ = any ? std::min(railFloorZ, e.z) : e.z;
          any = true;
        }
      }

      for (const auto& [key, span] : rails) {
        float acrossMin = 0;
        float acrossMax = 0;
        float floorZ = 0;
        float boardSpan = 1.0f;
        bool seen = false;
        for (std::uint32_t si = 0; si < slices.size(); ++si) {
          if (si >= extentsBySlice.size() || !extentsBySlice[si].seen) continue;
          std::vector<std::int16_t> k;
          for (std::size_t i = 0; i < slices[si].at.size(); ++i) {
            if (static_cast<int>(i) == temporalGrid) continue;
            k.push_back(slices[si].at[i]);
          }
          if (k != key) continue;
          const Extent& e = extentsBySlice[si];
          boardSpan = alongX ? (e.maxX - e.minX) : (e.maxY - e.minY);
          const float edgeMin = alongX ? e.minY : e.minX;
          const float edgeMax = alongX ? e.maxY : e.maxX;
          if (!seen) {
            acrossMin = edgeMin;
            acrossMax = edgeMax;
          } else {
            acrossMin = std::min(acrossMin, edgeMin);
            acrossMax = std::max(acrossMax, edgeMax);
          }
          floorZ = seen ? std::min(floorZ, e.z) : e.z;
          seen = true;
        }
        if (!seen) continue;

        const bool isOrigin = origin[key];
        // The starting timeline is the accent at full strength; a branch is a clear tint
        // of it, not a fade toward the rule colour, so both read as the same kind of
        // thing while staying distinguishable.
        const view::Rgba tint =
            isOrigin ? theme_.ember : mix(theme_.ember, theme_.panel, 0.35f);
        // Centred under the row and dropped in z, so the rail runs beneath its boards
        // rather than beside them.
        const float across = (acrossMin + acrossMax) * 0.5f;
        const float z = railFloorZ - half.z - 0.34f;
        if (key.size() == 1) {
          acrossOf[key[0]] = across;
          spanOf[key[0]] = boardSpan;
          zOf[key[0]] = z;
          zBoardOf[key[0]] = floorZ;
        }
        const float head = 1.1f;
        // The origin timeline starts a little before its first board; a branch starts
        // exactly where its connector ends, so the connector and the rail are one line.
        float lo = span.first - 0.9f;
        if (!isOrigin && key.size() == 1) {
          const auto bs = branchStartOf.find(key[0]);
          if (bs != branchStartOf.end()) lo = bs->second;
        }
        // Run a board's width past the last board, so the rail reads as a direction
        // rather than as an underline that happens to stop.
        const float hi = span.second + 0.9f + boardSpan;

        // The starting timeline's rail carries the accent at full width; a branch is a
        // touch slimmer, so the two read as the same kind of thing, ranked. The origin
        // rail is also twice as thick in Z, so it reads as the spine.
        const float railHalf = isOrigin ? 0.26f : 0.20f;
        const float railThick = isOrigin ? 0.05f : 0.025f;
        Instance rail{};
        rail.center[0] = alongX ? (lo + hi - head) * 0.5f : across;
        rail.center[1] = alongX ? across : (lo + hi - head) * 0.5f;
        rail.center[2] = z;
        rail.scale[0] = (alongX ? (hi - lo - head) * 0.5f : railHalf) / half.x;
        rail.scale[1] = (alongX ? railHalf : (hi - lo - head) * 0.5f) / half.y;
        rail.scale[2] = railThick / half.z;
        toFloat4(tint, rail.color);
        byShape[static_cast<std::size_t>(Archetype::Cell)].push_back(rail);

        // The head: a solid triangular prism, twice as wide as the rail and a little
        // thicker, pointing the way the game runs.
        Instance point{};
        point.center[0] = alongX ? hi - head * 0.5f : across;
        point.center[1] = alongX ? across : hi - head * 0.5f;
        point.center[2] = z;
        point.scale[0] = (alongX ? head * 0.5f : 0.68f) / half.x;
        point.scale[1] = (alongX ? 0.68f : head * 0.5f) / half.y;
        // As thick as the rail it caps, not prouder: a head is a point in the plane, and
        // the plane is the board's.
        point.scale[2] = railThick / half.z;
        toFloat4(tint, point.color);
        byShape[static_cast<std::size_t>(Archetype::Arrow)].push_back(point);
      }

      // Join each branch to the board it came from: a bar running from the parent
      // timeline's rail to the child's, half a board past the turn it branched at.
      if (!links.empty()) {
        const view::Rgba tint = mix(theme_.ember, theme_.panel, 0.35f);
        for (const view::TimelineLink& link : links) {
          const auto ap = acrossOf.find(link.fromLine);
          const auto ac = acrossOf.find(link.toLine);
          const auto hi = boardHi.find({link.fromLine, link.atTurn});
          if (ap == acrossOf.end() || ac == acrossOf.end() || hi == boardHi.end()) {
            continue;
          }
          // Join the two rails in the gap just past the board the piece came from - two
          // board-squares, not two whole turn-columns, which land on an earlier board.
          const float along = hi->second + cfg.gridGap * 0.5f + 0.5f;
          const float y1 = ap->second;
          const float y2 = ac->second;
          const float run = std::max(0.08f, std::abs(y2 - y1) * 0.5f);
          // One bar, as thin as a branch rail and at the same height, so the run from a
          // branch into its parent's rail reads as a single bent body, not a stack of
          // bars.
          Instance conn{};
          if (alongX) {
            conn.center[0] = along;
            conn.center[1] = (y1 + y2) * 0.5f;
            conn.scale[0] = 0.20f / half.x;
            conn.scale[1] = run / half.y;
          } else {
            conn.center[1] = along;
            conn.center[0] = (y1 + y2) * 0.5f;
            conn.scale[1] = 0.20f / half.y;
            conn.scale[0] = run / half.x;
          }
          // Same height as the rails, so it joins them rather than floating over a board.
          conn.center[2] = zOf[link.fromLine];
          conn.scale[2] = 0.025f / half.z;
          toFloat4(tint, conn.color);
          byShape[static_cast<std::size_t>(Archetype::Cell)].push_back(conn);

          // The corner where the branch rail turns out of the connector is an elbow : the
          // two bars leave one quadrant empty. Sit a quarter round on that corner and
          // turn the mesh into the empty quadrant, so the notch fills to a smooth outer
          // bend instead of two rectangles meeting at a right angle.
          const float r = 0.20f;
          const float halfPi = 1.5707963f;
          Instance fil{};
          if (alongX) {
            // Rail runs +X, so the empty quadrant is -X and the way the connector does
            // not go.
            fil.center[0] = along;
            fil.center[1] = y2;
            fil.roll = y1 > y2 ? 2.0f * halfPi : halfPi;
          } else {
            // Rail runs +Y; the same reasoning with the axes swapped.
            fil.center[0] = y2;
            fil.center[1] = along;
            fil.roll = y1 > y2 ? 2.0f * halfPi : 3.0f * halfPi;
          }
          fil.center[2] = zOf[link.fromLine];
          fil.scale[0] = r / half.x;
          fil.scale[1] = r / half.y;
          fil.scale[2] = 0.025f / half.z;
          toFloat4(tint, fil.color);
          byShape[static_cast<std::size_t>(Archetype::Fillet)].push_back(fil);
        }
      }
    }
  }

  // The travelling piece and any portal it is passing through. Drawn last so they sit
  // above the board they are crossing.
  if (anim != nullptr && anim->active()) {
    const view::MoveAnimation::Sample at = anim->sample();
    const Piece moving = p.at(anim->travellingTo());
    if (at.moving && !moving.empty()) {
      emitPiece(moving, at.x, at.y, at.z + at.lift, false);
      // The shadow stays on the board under a piece that is lifting or leaping over a
      // seam; following it up would turn the contact into a lie (M18.2).
      if (!options_.flat) emitShadow(at.x, at.y, at.z);
    }
    for (const view::MoveAnimation::Portal& portal : anim->openPortals()) {
      // A doorway standing across the seam the piece crossed: thin on the axis the
      // portal faces, wide along the seam, and growing as it opens. It uses the cube
      // archetype, not the flat cell slab - a slab lies on the board however it is
      // scaled, which is why a portal on a vertical file seam used to come out flat.
      const float open = portal.intensity;
      const float ax = std::abs(portal.nx);
      const float ay = std::abs(portal.ny);
      const float az = std::abs(portal.nz);
      const float wide = 0.46f;
      float ex = wide;
      float ey = wide;
      float ez = 0.06f + 0.55f * open;
      if (ax >= ay && ax >= az) {
        ex = 0.05f;
      } else if (ay >= az) {
        ey = 0.05f;
      } else {
        ez = 0.05f;
      }
      Instance iris{};
      iris.center[0] = portal.x;
      iris.center[1] = portal.y;
      iris.center[2] = portal.z + ez;  // stand it on the cell rather than through it
      // The cube's own half-extent is 0.5, so the scale is the wanted half-extent
      // doubled.
      iris.scale[0] = ex * 2.0f;
      iris.scale[1] = ey * 2.0f;
      iris.scale[2] = ez * 2.0f;
      toFloat4(portal.color, iris.color);
      byShape[static_cast<std::size_t>(Archetype::Portal)].push_back(iris);
    }
  }

  // M18.1: the capture flourish. The position has already moved on, so the captured piece
  // is no longer in `placements` - the animation kept its own copy and the square it
  // stood on (`captureCell`, which for en passant is not the landing square). It shrinks
  // and fades out as a pure function of `t`, so a clip stays reproducible, and a blood
  // flash marks the square at the moment the pieces meet. Both are drawn blended further
  // down, which is why their alpha is the fade rather than a number nothing reads.
  std::vector<std::vector<Instance>> byShapeFlourish(
      kShapeSlots);
  if (anim != nullptr && anim->active() && anim->captures() && !options_.flat) {
    const float t = anim->progress();
    const float fade = view::captureFade(t);
    const float flash = view::captureFlash(t);
    const view::Placement* at = nullptr;
    for (const view::Placement& pl : placements) {
      if (pl.cell == anim->captureCell()) {
        at = &pl;
        break;
      }
    }
    if (at != nullptr) {
      if (fade > 0.0f) {
        const Piece taken = anim->capturedPiece();
        Instance body{};
        body.center[0] = at->x;
        body.center[1] = at->y;
        body.center[2] = at->z + half.z;
        const float h = 1.0f + (height[taken.type] - 1.0f) * options_.pieceHeightScale;
        const float s = 0.8f * fade;
        body.scale[0] = s;
        body.scale[1] = s;
        body.scale[2] = s * h;
        view::Rgba pc =
            taken.colorOf() == Color::White ? theme_.whitePiece : theme_.blackPiece;
        toFloat4(pc, body.color);
        body.color[3] = fade;
        byShapeFlourish[static_cast<std::size_t>(shape[taken.type])].push_back(body);
      }
      if (flash > 0.0f) {
        // The flash is the flat cell slab, scaled larger as it fades so it reads as a
        // pulse rather than a square that blinks out. It sits just above the cell so it
        // never z-fights the board underneath it.
        Instance ring{};
        ring.center[0] = at->x;
        ring.center[1] = at->y;
        ring.center[2] = at->z + half.z + 0.012f;
        const float grow = 0.45f + 0.75f * (1.0f - flash);
        ring.scale[0] = grow;
        ring.scale[1] = grow;
        ring.scale[2] = 0.14f;
        toFloat4(theme_.blood, ring.color);
        ring.color[3] = flash * 0.85f;
        byShapeFlourish[static_cast<std::size_t>(Archetype::Cell)].push_back(ring);
      }
    }
  }

  InstanceSet out;
  std::size_t total = 0;
  for (const auto& group : byShape) total += group.size();
  out.instances.reserve(total);
  for (std::size_t s = 0; s < byShape.size(); ++s) {
    out.batches[s].first = static_cast<std::uint32_t>(out.instances.size());
    out.batches[s].count = static_cast<std::uint32_t>(byShape[s].size());
    out.instances.insert(out.instances.end(), byShape[s].begin(), byShape[s].end());
  }
  for (std::size_t s = 0; s < byShapeFlourish.size(); ++s) {
    out.flourishBatches[s].first = static_cast<std::uint32_t>(out.flourish.size());
    out.flourishBatches[s].count = static_cast<std::uint32_t>(byShapeFlourish[s].size());
    out.flourish.insert(out.flourish.end(), byShapeFlourish[s].begin(),
                        byShapeFlourish[s].end());
  }
  for (std::size_t s = 0; s < byShapeShadow.size(); ++s) {
    out.shadowBatches[s].first = static_cast<std::uint32_t>(out.shadows.size());
    out.shadowBatches[s].count = static_cast<std::uint32_t>(byShapeShadow[s].size());
    out.shadows.insert(out.shadows.end(), byShapeShadow[s].begin(),
                       byShapeShadow[s].end());
  }
  return out;
}

Result<void> BoardRenderer::render(const OffscreenTarget& target, const InstanceSet& set,
                                   const view::OrbitCamera& camera,
                                   const std::function<void(VkCommandBuffer)>& overlay,
                                   const std::function<void(VkCommandBuffer)>& background,
                                   BoardRect boardRect) {
  // The synchronous path runs on frame slot 0: it submits and waits, so there is never a
  // second frame overlapping it.
  Result<void> outcome{};
  const auto submitted = ctx_->submitAndWait([&](VkCommandBuffer cmd) {
    if (auto r = record(cmd, target, set, camera, overlay, background, boardRect, 0);
        !r.has_value()) {
      outcome = fail(r.error().code, r.error().message);
      return;
    }
    // Leave the colour image where readPixels and the window's blit expect it.
    const auto barrier = [&](VkImage image, VkImageLayout from, VkImageLayout to,
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
    };
    barrier(target.colorImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
  });
  if (!submitted.has_value()) return submitted;
  return outcome;
}

Result<void> BoardRenderer::record(VkCommandBuffer cmd, const OffscreenTarget& target,
                                   const InstanceSet& set,
                                   const view::OrbitCamera& camera,
                                   const std::function<void(VkCommandBuffer)>& overlay,
                                   const std::function<void(VkCommandBuffer)>& background,
                                   BoardRect boardRect, std::uint32_t frame) {
  // The scaffold, the contact shadows and the flourish all ride the same buffer,
  // immediately after the opaque ones, so each blended pass is one more draw group rather
  // than a second buffer. Their order is the order they are drawn in: the scaffold first,
  // then the shadows it grounds, then the capture flourish on top.
  const std::size_t flourishBase = set.instances.size();
  const std::size_t scaffoldBase = flourishBase + set.flourish.size();
  const std::size_t shadowBase = scaffoldBase + set.scaffold.size();
  const std::size_t instanceCount = shadowBase + set.shadows.size();
  if (auto r = ensureInstanceCapacity(frame, instanceCount); !r.has_value()) return r;
  if (instanceCount > 0) {
    void* mapped = nullptr;
    vkMapMemory(ctx_->device(), instanceMem_[frame], 0, instanceCount * sizeof(Instance),
                0, &mapped);
    std::memcpy(mapped, set.instances.data(), set.instances.size() * sizeof(Instance));
    std::memcpy(static_cast<char*>(mapped) + flourishBase * sizeof(Instance),
                set.flourish.data(), set.flourish.size() * sizeof(Instance));
    std::memcpy(static_cast<char*>(mapped) + scaffoldBase * sizeof(Instance),
                set.scaffold.data(), set.scaffold.size() * sizeof(Instance));
    std::memcpy(static_cast<char*>(mapped) + shadowBase * sizeof(Instance),
                set.shadows.data(), set.shadows.size() * sizeof(Instance));
    vkUnmapMemory(ctx_->device(), instanceMem_[frame]);
  }

  if (auto r = ensureSurfaceCapacity(frame, set.surfaceVertices.size(),
                                     set.surfaceIndices.size());
      !r.has_value()) {
    return r;
  }
  if (!set.surfaceIndices.empty()) {
    void* mapped = nullptr;
    const std::size_t vbytes = set.surfaceVertices.size() * sizeof(MeshVertex);
    vkMapMemory(ctx_->device(), surfaceVertexMem_[frame], 0, vbytes, 0, &mapped);
    std::memcpy(mapped, set.surfaceVertices.data(), vbytes);
    vkUnmapMemory(ctx_->device(), surfaceVertexMem_[frame]);
    const std::size_t ibytes = set.surfaceIndices.size() * sizeof(std::uint32_t);
    vkMapMemory(ctx_->device(), surfaceIndexMem_[frame], 0, ibytes, 0, &mapped);
    std::memcpy(mapped, set.surfaceIndices.data(), ibytes);
    vkUnmapMemory(ctx_->device(), surfaceIndexMem_[frame]);
  }

  if (auto r = bindBlurTarget(target, frame); !r.has_value()) return r;

  const BoardRect board = boardRect.valid()
                              ? boardRect
                              : BoardRect{0, 0, static_cast<float>(target.width()),
                                          static_cast<float>(target.height())};
  PushConstants push{};
  push.viewProj = camera.viewProj(board.width / board.height);
  const view::Vec3 eye = camera.eye();
  push.eyePos[0] = eye.x;
  push.eyePos[1] = eye.y;
  push.eyePos[2] = eye.z;

  {
    const auto barrier = [&](VkImage image, VkImageAspectFlags aspectMask,
                             VkImageLayout from, VkImageLayout to,
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
      b.subresourceRange = {aspectMask, 0, 1, 0, 1};
      VkDependencyInfo dep{};
      dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
      dep.imageMemoryBarrierCount = 1;
      dep.pImageMemoryBarriers = &b;
      vkCmdPipelineBarrier2(cmd, &dep);
    };

    // The resolved colour is an attachment too - the multisample resolve writes it - so
    // it needs the layout transition whether or not MSAA is on.
    barrier(target.colorImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    if (OffscreenTarget::msaaEnabled()) {
      barrier(target.msaaColorImage(), VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    barrier(target.depthImage(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = target.msaaColorView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{theme_.ink.r, theme_.ink.g, theme_.ink.b, 1.0f}};
    if (OffscreenTarget::msaaEnabled()) {
      // Resolve the multisampled attachment into the one-sample image the blur and
      // readPixels expect, at the end of this pass.
      color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
      color.resolveImageView = target.colorView();
      color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = target.depthView();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea = {{0, 0}, {target.width(), target.height()}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;

    // The full frame, used both by the ambient background drawn first and by the late
    // overlay pass: both cover the whole target, while the board itself is confined to
    // `board`.
    const VkViewport full{0,
                          0,
                          static_cast<float>(target.width()),
                          static_cast<float>(target.height()),
                          0.0f,
                          1.0f};
    const VkRect2D fullScissor{{0, 0}, {target.width(), target.height()}};

    vkCmdBeginRendering(cmd, &ri);
    // Whatever is already out there is painted before the board, not over it. The ambient
    // field lands in the same pass, ahead of the board's own viewport and draws, so the
    // board's opaque geometry simply covers it where the board actually sits.
    if (background) {
      vkCmdSetViewport(cmd, 0, 1, &full);
      vkCmdSetScissor(cmd, 0, 1, &fullScissor);
      background(cmd);
    }
    // The board gets its own viewport - the area the interface leaves free - while the
    // overlay is drawn across the whole frame.
    VkViewport boardViewport{board.x, board.y, board.width, board.height, 0.0f, 1.0f};
    VkRect2D boardScissor{
        {static_cast<std::int32_t>(board.x), static_cast<std::int32_t>(board.y)},
        {static_cast<std::uint32_t>(board.width),
         static_cast<std::uint32_t>(board.height)}};
    vkCmdSetViewport(cmd, 0, 1, &boardViewport);
    vkCmdSetScissor(cmd, 0, 1, &boardScissor);

    // The shape view's icon-faced pucks (M17.25): one option for the whole frame, because
    // every surface piece switches together. Scoped to the surface path - the flat board
    // and the ordinary solid board keep the sculptural meshes whatever the setting says.
    const bool cylinderPieces = options_.shapePieceCylinders && options_.surface &&
                                !options_.flat && !set.surfaceIndices.empty();
    if (!set.instances.empty()) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
      vkCmdPushConstants(cmd, layout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(PushConstants), &push);
      const VkDeviceSize zero = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &zero);
      vkCmdBindVertexBuffers(cmd, 1, 1, &instanceBuffer_[frame], &zero);
      vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
      // One draw per shape, not per piece: eight calls for any board of any size.
      for (std::size_t s = 0; s < set.batches.size(); ++s) {
        const auto& batch = set.batches[s];
        if (batch.count == 0) continue;
        // M17.26: the puck board is a token disc under an icon decal. The disc is the
        // neutral furniture colour, drawn first from `discRanges`; the icon follows from
        // `cylinderRanges`. Every other board has `discCounts` zero and stays exactly one
        // draw with the sculptural mesh - which is what keeps the flourish's blood flash
        // (a Cell, with no disc) on its ordinary mesh too.
        const std::uint32_t discs =
            cylinderPieces ? std::min(set.discCounts[s], batch.count) : 0u;
        if (discs > 0) {
          const MeshRange& dr = meshes_.discRanges[s];
          if (dr.indexCount > 0) {
            vkCmdDrawIndexed(cmd, dr.indexCount, discs, dr.firstIndex, dr.vertexOffset,
                             batch.first);
          }
        }
        const std::uint32_t icons = batch.count - discs;
        if (icons > 0) {
          const MeshRange& ir = discs > 0 ? meshes_.cylinderRanges[s] : meshes_.ranges[s];
          if (ir.indexCount > 0) {
            vkCmdDrawIndexed(cmd, ir.indexCount, icons, ir.firstIndex, ir.vertexOffset,
                             batch.first + discs);
          }
        }
      }
      // And one more for the geometry view's board, which is a mesh rather than a shape
      // repeated: the same pipeline, a different vertex buffer, one identity instance.
      // A ghosted board takes the blended pipeline instead - depth writes off, so the far
      // side blends over the near one (M17.10).
      if (!set.surfaceIndices.empty()) {
        vkCmdBindPipeline(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            options_.surfaceGhost < 0.99f ? surfaceBlendPipeline_ : pipeline_);
        vkCmdBindVertexBuffers(cmd, 0, 1, &surfaceVertexBuffer_[frame], &zero);
        vkCmdBindIndexBuffer(cmd, surfaceIndexBuffer_[frame], 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, static_cast<std::uint32_t>(set.surfaceIndices.size()), 1, 0,
                         0, set.surfaceInstance);
      }
    }
    // The tesseract scaffold (M17.12 part D), blended so it reads as a wire: depth-tested
    // against the board but writing no depth, so an edge behind the patches is hidden and
    // one in front lays over them. Drawn before the shadows, which belong under pieces.
    if (!set.scaffold.empty()) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, surfaceBlendPipeline_);
      vkCmdPushConstants(cmd, layout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(PushConstants), &push);
      const VkDeviceSize zero = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &zero);
      vkCmdBindVertexBuffers(cmd, 1, 1, &instanceBuffer_[frame], &zero);
      vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
      const std::uint32_t base = static_cast<std::uint32_t>(scaffoldBase);
      for (std::size_t s = 0; s < set.scaffoldBatches.size(); ++s) {
        const auto& batch = set.scaffoldBatches[s];
        if (batch.count == 0) continue;
        const MeshRange& range = meshes_.ranges[s];
        vkCmdDrawIndexed(cmd, range.indexCount, batch.count, range.firstIndex,
                         range.vertexOffset, base + batch.first);
      }
    }
    // The contact shadows (M18.2), blended like the flourish: a soft decal under each
    // occupied cell. Depth writes are off but the test stays on, so a piece in front of a
    // shadow still hides it. Drawn before the flourish, which belongs on top.
    if (!set.shadows.empty()) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, surfaceBlendPipeline_);
      vkCmdPushConstants(cmd, layout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(PushConstants), &push);
      const VkDeviceSize zero = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &zero);
      vkCmdBindVertexBuffers(cmd, 1, 1, &instanceBuffer_[frame], &zero);
      vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
      const std::uint32_t base = static_cast<std::uint32_t>(shadowBase);
      for (std::size_t s = 0; s < set.shadowBatches.size(); ++s) {
        const auto& batch = set.shadowBatches[s];
        if (batch.count == 0) continue;
        const MeshRange& range = meshes_.ranges[s];
        vkCmdDrawIndexed(cmd, range.indexCount, batch.count, range.firstIndex,
                         range.vertexOffset, base + batch.first);
      }
    }
    // The capture flourish (M18.1), last and blended: alpha is the fade, so it cannot
    // ride the opaque per-shape batches above. Depth writes are off but the test stays
    // on, so a fading piece is still hidden by whatever is in front of it.
    if (!set.flourish.empty()) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, surfaceBlendPipeline_);
      vkCmdPushConstants(cmd, layout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(PushConstants), &push);
      const VkDeviceSize zero = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &zero);
      vkCmdBindVertexBuffers(cmd, 1, 1, &instanceBuffer_[frame], &zero);
      vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
      const std::uint32_t base = static_cast<std::uint32_t>(set.instances.size());
      for (std::size_t s = 0; s < set.flourishBatches.size(); ++s) {
        const auto& batch = set.flourishBatches[s];
        if (batch.count == 0) continue;
        // The captured piece flourishes in whatever style the board is drawn in, so on a
        // puck board it is a shrinking token disc under a shrinking icon decal (M17.25,
        // M17.26) rather than a mismatched sculpt.
        const std::uint32_t discs =
            cylinderPieces ? std::min(set.flourishDiscCounts[s], batch.count) : 0u;
        if (discs > 0) {
          const MeshRange& dr = meshes_.discRanges[s];
          if (dr.indexCount > 0) {
            vkCmdDrawIndexed(cmd, dr.indexCount, discs, dr.firstIndex, dr.vertexOffset,
                             base + batch.first);
          }
        }
        const std::uint32_t icons = batch.count - discs;
        if (icons > 0) {
          const MeshRange& ir = discs > 0 ? meshes_.cylinderRanges[s] : meshes_.ranges[s];
          if (ir.indexCount > 0) {
            vkCmdDrawIndexed(cmd, ir.indexCount, icons, ir.firstIndex, ir.vertexOffset,
                             base + batch.first + discs);
          }
        }
      }
    }
    vkCmdEndRendering(cmd);

    // Defocus, in two separable passes that ping-pong between the target's two colour
    // images and therefore leave the result back in the first one. Skipped entirely when
    // nothing is out of focus, so a game in play pays for none of this.
    if (blur_ > 0.001f) {
      const auto blurPass = [&](VkImageView into, VkDescriptorSet from, float dx,
                                float dy) {
        VkRenderingAttachmentInfo att{};
        att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        att.imageView = into;
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        info.renderArea = fullScissor;
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &att;

        BlurPush bp{};
        bp.direction[0] = dx;
        bp.direction[1] = dy;
        bp.strength = std::clamp(blur_, 0.0f, 1.0f);
        // Only the second pass dims: doing it in both would square the effect.
        bp.dim = dy != 0.0f ? 0.22f * bp.strength : 0.0f;
        toFloat4(theme_.ink, bp.ground);

        vkCmdBeginRendering(cmd, &info);
        vkCmdSetViewport(cmd, 0, 1, &full);
        vkCmdSetScissor(cmd, 0, 1, &fullScissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurLayout_, 0, 1,
                                &from, 0, nullptr);
        vkCmdPushConstants(cmd, blurLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(BlurPush), &bp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
      };

      // Across, into the scratch image.
      barrier(target.colorImage(), VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      barrier(target.scratchImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
      const float radius = 2.6f;
      blurPass(target.scratchView(), blurFromColor_[frame],
               radius / static_cast<float>(target.width()), 0.0f);

      // And down, back into the image everything else expects to find the frame in.
      barrier(target.scratchImage(), VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      barrier(target.colorImage(), VK_IMAGE_ASPECT_COLOR_BIT,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
              VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
              VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
      blurPass(target.colorView(), blurFromScratch_[frame], 0.0f,
               radius / static_cast<float>(target.height()));
    }

    // The interface is drawn last and never blurred: the menu in front of a defocused
    // board is the one thing on screen that has to stay sharp.
    if (overlay) {
      VkRenderingAttachmentInfo att{};
      att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
      att.imageView = target.colorView();
      att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      att.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      VkRenderingInfo info{};
      info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
      info.renderArea = fullScissor;
      info.layerCount = 1;
      info.colorAttachmentCount = 1;
      info.pColorAttachments = &att;
      vkCmdBeginRendering(cmd, &info);
      vkCmdSetViewport(cmd, 0, 1, &full);
      vkCmdSetScissor(cmd, 0, 1, &fullScissor);
      overlay(cmd);
      vkCmdEndRendering(cmd);
    }
  }
  return {};
}

Result<Image> BoardRenderer::renderToImage(const OffscreenTarget& target,
                                           const view::PositionView& p,
                                           const view::ViewConfig& cfg,
                                           const view::OrbitCamera& camera) {
  const InstanceSet set = buildInstances(p, cfg);
  if (auto r = render(target, set, camera); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  auto pixels = target.readPixels();
  if (!pixels.has_value()) return fail(pixels.error().code, pixels.error().message);
  Image img;
  img.width = target.width();
  img.height = target.height();
  img.rgba = std::move(*pixels);
  return img;
}

BoardRenderer& BoardRenderer::operator=(BoardRenderer&& o) noexcept {
  if (this == &o) return *this;
  std::swap(ctx_, o.ctx_);
  std::swap(colorFormat_, o.colorFormat_);
  std::swap(theme_, o.theme_);
  std::swap(meshes_, o.meshes_);
  std::swap(lastFrom_, o.lastFrom_);
  std::swap(lastTo_, o.lastTo_);
  std::swap(checkCell_, o.checkCell_);
  std::swap(vert_, o.vert_);
  std::swap(frag_, o.frag_);
  std::swap(layout_, o.layout_);
  std::swap(pipeline_, o.pipeline_);
  std::swap(surfaceBlendPipeline_, o.surfaceBlendPipeline_);
  std::swap(blurVert_, o.blurVert_);
  std::swap(blurFrag_, o.blurFrag_);
  std::swap(blurLayout_, o.blurLayout_);
  std::swap(blurPipeline_, o.blurPipeline_);
  std::swap(blurSetLayout_, o.blurSetLayout_);
  std::swap(blurPool_, o.blurPool_);
  std::swap(blurSampler_, o.blurSampler_);
  std::swap(blurFromColor_, o.blurFromColor_);
  std::swap(blurFromScratch_, o.blurFromScratch_);
  std::swap(blurBoundColor_, o.blurBoundColor_);
  std::swap(blur_, o.blur_);
  std::swap(backdropVert_, o.backdropVert_);
  std::swap(backdropFrag_, o.backdropFrag_);
  std::swap(backdropLayout_, o.backdropLayout_);
  std::swap(backdropPipeline_, o.backdropPipeline_);
  std::swap(vertexBuffer_, o.vertexBuffer_);
  std::swap(vertexMem_, o.vertexMem_);
  std::swap(indexBuffer_, o.indexBuffer_);
  std::swap(indexMem_, o.indexMem_);
  std::swap(instanceBuffer_, o.instanceBuffer_);
  std::swap(instanceMem_, o.instanceMem_);
  std::swap(instanceCapacity_, o.instanceCapacity_);
  std::swap(surfaceVertexBuffer_, o.surfaceVertexBuffer_);
  std::swap(surfaceVertexMem_, o.surfaceVertexMem_);
  std::swap(surfaceVertexCapacity_, o.surfaceVertexCapacity_);
  std::swap(surfaceIndexBuffer_, o.surfaceIndexBuffer_);
  std::swap(surfaceIndexMem_, o.surfaceIndexMem_);
  std::swap(surfaceIndexCapacity_, o.surfaceIndexCapacity_);
  return *this;
}

BoardRenderer::~BoardRenderer() {
  if (ctx_ == nullptr || ctx_->device() == VK_NULL_HANDLE) return;
  const VkDevice d = ctx_->device();
  if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(d, pipeline_, nullptr);
  if (surfaceBlendPipeline_ != VK_NULL_HANDLE)
    vkDestroyPipeline(d, surfaceBlendPipeline_, nullptr);
  if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(d, layout_, nullptr);
  if (backdropPipeline_ != VK_NULL_HANDLE)
    vkDestroyPipeline(d, backdropPipeline_, nullptr);
  if (backdropLayout_ != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(d, backdropLayout_, nullptr);
  if (blurPipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(d, blurPipeline_, nullptr);
  if (blurLayout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(d, blurLayout_, nullptr);
  if (blurPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(d, blurPool_, nullptr);
  if (blurSetLayout_ != VK_NULL_HANDLE)
    vkDestroyDescriptorSetLayout(d, blurSetLayout_, nullptr);
  if (blurSampler_ != VK_NULL_HANDLE) vkDestroySampler(d, blurSampler_, nullptr);
  if (blurVert_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, blurVert_, nullptr);
  if (blurFrag_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, blurFrag_, nullptr);
  if (backdropVert_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, backdropVert_, nullptr);
  if (backdropFrag_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, backdropFrag_, nullptr);
  if (vert_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, vert_, nullptr);
  if (frag_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, frag_, nullptr);
  const auto destroyBuffer = [&](VkBuffer buf, VkDeviceMemory mem) {
    if (buf != VK_NULL_HANDLE) vkDestroyBuffer(d, buf, nullptr);
    if (mem != VK_NULL_HANDLE) vkFreeMemory(d, mem, nullptr);
  };
  destroyBuffer(vertexBuffer_, vertexMem_);
  destroyBuffer(indexBuffer_, indexMem_);
  for (std::uint32_t f = 0; f < kFramesInFlight; ++f) {
    destroyBuffer(instanceBuffer_[f], instanceMem_[f]);
    destroyBuffer(surfaceVertexBuffer_[f], surfaceVertexMem_[f]);
    destroyBuffer(surfaceIndexBuffer_[f], surfaceIndexMem_[f]);
  }
}

}  // namespace cb::render
