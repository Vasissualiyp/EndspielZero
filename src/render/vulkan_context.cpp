// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/vulkan_context.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace cb::render {
namespace {

/// Validation output is collected globally because Vulkan's callback has no useful
/// per-context user pointer until the messenger exists, and because a message emitted
/// during instance teardown must still be visible to a test.
std::mutex g_logMutex;
std::vector<std::string> g_messages;
std::size_t g_errors = 0;

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  const std::lock_guard<std::mutex> lock(g_logMutex);
  std::string line;
  if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
    line = "ERROR: ";
    ++g_errors;
  } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
    line = "WARNING: ";
    ++g_errors;  // a warning is a bug we have not understood yet; treat it as one
  } else {
    line = "info: ";
  }
  line += data->pMessage != nullptr ? data->pMessage : "(no message)";
  g_messages.push_back(std::move(line));
  return VK_FALSE;
}

bool layerAvailable(const char* name) {
  std::uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());
  return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& l) {
    return std::strcmp(l.layerName, name) == 0;
  });
}

bool extensionAvailable(const char* name) {
  std::uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> exts(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
  return std::any_of(exts.begin(), exts.end(), [&](const VkExtensionProperties& e) {
    return std::strcmp(e.extensionName, name) == 0;
  });
}

}  // namespace

std::string describe(VkResult r) {
  switch (r) {
    case VK_SUCCESS:
      return "success";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      return "out of host memory";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return "out of device memory";
    case VK_ERROR_INITIALIZATION_FAILED:
      return "initialization failed";
    case VK_ERROR_LAYER_NOT_PRESENT:
      return "layer not present";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      return "extension not present";
    case VK_ERROR_FEATURE_NOT_PRESENT:
      return "feature not present";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      return "incompatible driver";
    case VK_ERROR_DEVICE_LOST:
      return "device lost";
    default:
      return "VkResult " + std::to_string(static_cast<int>(r));
  }
}

std::vector<std::string> VulkanContext::takeValidationMessages() const {
  const std::lock_guard<std::mutex> lock(g_logMutex);
  auto out = g_messages;
  g_messages.clear();
  g_errors = 0;
  return out;
}

std::size_t VulkanContext::validationErrorCount() const {
  const std::lock_guard<std::mutex> lock(g_logMutex);
  return g_errors;
}

Result<VulkanContext> VulkanContext::create(const ContextOptions& opts) {
  VulkanContext ctx;

  // ---- instance ----------------------------------------------------------
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = opts.applicationName.c_str();
  app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app.pEngineName = "EndspielZero";
  app.apiVersion = VK_API_VERSION_1_3;

  std::vector<const char*> layers;
  std::vector<const char*> extensions;
  const bool wantValidation = opts.validation &&
                              layerAvailable("VK_LAYER_KHRONOS_validation") &&
                              extensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  if (wantValidation) {
    layers.push_back("VK_LAYER_KHRONOS_validation");
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  for (const std::string& e : opts.instanceExtensions) extensions.push_back(e.c_str());

  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  ici.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
  ici.ppEnabledLayerNames = layers.data();
  ici.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  ici.ppEnabledExtensionNames = extensions.data();

  if (const VkResult r = vkCreateInstance(&ici, nullptr, &ctx.instance_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Unsupported,
                "cannot create a Vulkan instance (" + describe(r) +
                    "). A Vulkan 1.3 loader and an ICD are required; on a machine with "
                    "no GPU, a software implementation such as llvmpipe is enough.");
  }

  if (wantValidation) {
    VkDebugUtilsMessengerCreateInfoEXT dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dci.pfnUserCallback = debugCallback;
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(ctx.instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (fn != nullptr) (void)fn(ctx.instance_, &dci, nullptr, &ctx.debugMessenger_);
  }

  // ---- physical device ---------------------------------------------------
  std::uint32_t count = 0;
  vkEnumeratePhysicalDevices(ctx.instance_, &count, nullptr);
  if (count == 0) {
    return fail(ErrorCode::Unsupported, "no Vulkan physical device is available");
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(ctx.instance_, &count, devices.data());

  int bestScore = -1;
  for (VkPhysicalDevice candidate : devices) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(candidate, &props);

    // A graphics queue is the only hard requirement; no surface support is needed,
    // because nothing here presents.
    std::uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qCount, families.data());
    int family = -1;
    for (std::uint32_t i = 0; i < qCount; ++i) {
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
        family = static_cast<int>(i);
        break;
      }
    }
    if (family < 0) continue;
    if (props.apiVersion < VK_API_VERSION_1_3) continue;

    int score = 1;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
      score = opts.preferDiscrete ? 100 : 10;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
      score = 50;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
      score = 5;

    if (score > bestScore) {
      bestScore = score;
      ctx.physical_ = candidate;
      ctx.queueFamily_ = static_cast<std::uint32_t>(family);
      ctx.deviceName_ = props.deviceName;
    }
  }
  if (ctx.physical_ == VK_NULL_HANDLE) {
    return fail(
        ErrorCode::Unsupported,
        "no Vulkan 1.3 device with a graphics queue was found; the renderer needs "
        "dynamic rendering and synchronization2, both core in 1.3");
  }

  // ---- logical device ---------------------------------------------------
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = ctx.queueFamily_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;

  // Dynamic rendering means no VkRenderPass and no VkFramebuffer to keep in sync with
  // attachments; synchronization2 gives the barrier API that is actually readable.
  VkPhysicalDeviceVulkan13Features f13{};
  f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  f13.dynamicRendering = VK_TRUE;
  f13.synchronization2 = VK_TRUE;
  f13.shaderDemoteToHelperInvocation = VK_TRUE;  // `discard` in the board shader (M23.10)

  VkPhysicalDeviceFeatures2 features{};
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features.pNext = &f13;

  std::vector<const char*> deviceExtensions;
  if (opts.needSwapchain) deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.pNext = &features;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
  dci.ppEnabledExtensionNames = deviceExtensions.data();

  if (const VkResult r = vkCreateDevice(ctx.physical_, &dci, nullptr, &ctx.device_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Unsupported,
                "cannot create a Vulkan device on '" + ctx.deviceName_ + "' (" +
                    describe(r) +
                    "); dynamicRendering and synchronization2 are required");
  }
  vkGetDeviceQueue(ctx.device_, ctx.queueFamily_, 0, &ctx.queue_);

  VkCommandPoolCreateInfo pci{};
  pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pci.queueFamilyIndex = ctx.queueFamily_;
  if (const VkResult r = vkCreateCommandPool(ctx.device_, &pci, nullptr, &ctx.pool_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create a command pool: " + describe(r));
  }

  return ctx;
}

VulkanContext& VulkanContext::operator=(VulkanContext&& other) noexcept {
  if (this == &other) return *this;
  std::swap(instance_, other.instance_);
  std::swap(debugMessenger_, other.debugMessenger_);
  std::swap(physical_, other.physical_);
  std::swap(device_, other.device_);
  std::swap(queue_, other.queue_);
  std::swap(pool_, other.pool_);
  std::swap(queueFamily_, other.queueFamily_);
  std::swap(deviceName_, other.deviceName_);
  return *this;
}

VulkanContext::~VulkanContext() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    if (pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) {
    if (debugMessenger_ != VK_NULL_HANDLE) {
      auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
      if (fn != nullptr) fn(instance_, debugMessenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
  }
}

Result<std::uint32_t> VulkanContext::findMemoryType(std::uint32_t typeBits,
                                                    VkMemoryPropertyFlags props) const {
  VkPhysicalDeviceMemoryProperties mem{};
  vkGetPhysicalDeviceMemoryProperties(physical_, &mem);
  for (std::uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) == 0) continue;
    if ((mem.memoryTypes[i].propertyFlags & props) == props) return i;
  }
  return fail(ErrorCode::Unsupported,
              "no memory type satisfies the requested properties");
}

Result<void> VulkanContext::submitAndWait(
    const std::function<void(VkCommandBuffer)>& record) const {
  VkCommandBufferAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = pool_;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  if (const VkResult r = vkAllocateCommandBuffers(device_, &ai, &cmd); r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot allocate a command buffer: " + describe(r));
  }

  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);
  record(cmd);
  vkEndCommandBuffer(cmd);

  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  vkCreateFence(device_, &fci, nullptr, &fence);

  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  const VkResult submitted = vkQueueSubmit(queue_, 1, &si, fence);
  if (submitted == VK_SUCCESS) {
    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
  }
  vkDestroyFence(device_, fence, nullptr);
  vkFreeCommandBuffers(device_, pool_, 1, &cmd);
  if (submitted != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "queue submit failed: " + describe(submitted));
  }
  return {};
}

}  // namespace cb::render
