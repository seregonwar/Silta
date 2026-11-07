// Vulkan renderer, based on Dear ImGui's example_glfw_vulkan (MIT).
#include "renderer.hpp"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace silta::detail {
namespace {
void check(VkResult err) {
  if (err == VK_SUCCESS) return;
  std::fprintf(stderr, "silta: Vulkan error %d\n", int(err));
}

bool has_extension(const ImVector<VkExtensionProperties> &props, const char *name) {
  for (const VkExtensionProperties &p : props)
    if (std::strcmp(p.extensionName, name) == 0) return true;
  return false;
}

class VulkanRenderer final : public Renderer {
  GLFWwindow *window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = uint32_t(-1);
  VkQueue queue_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  ImGui_ImplVulkanH_Window wd_;
  uint32_t min_images_ = 2;
  bool rebuild_ = false;
  bool backend_ready_ = false;

  bool create_device() {
    ImVector<const char *> extensions;
    uint32_t count = 0;
    const char **glfw_ext = glfwGetRequiredInstanceExtensions(&count);
    for (uint32_t i = 0; i < count; ++i) extensions.push_back(glfw_ext[i]);

    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    uint32_t prop_count = 0;
    ImVector<VkExtensionProperties> props;
    vkEnumerateInstanceExtensionProperties(nullptr, &prop_count, nullptr);
    props.resize(int(prop_count));
    vkEnumerateInstanceExtensionProperties(nullptr, &prop_count, props.Data);
    if (has_extension(props, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
      extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (has_extension(props, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
      extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif
    const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
    if (std::getenv("SILTA_VULKAN_VALIDATION")) {
      ici.enabledLayerCount = 1;
      ici.ppEnabledLayerNames = layers;
    }
    ici.enabledExtensionCount = uint32_t(extensions.Size);
    ici.ppEnabledExtensionNames = extensions.Data;
    if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) return false;

    physical_ = ImGui_ImplVulkanH_SelectPhysicalDevice(instance_);
    if (physical_ == VK_NULL_HANDLE) return false;
    queue_family_ = ImGui_ImplVulkanH_SelectQueueFamilyIndex(physical_);
    if (queue_family_ == uint32_t(-1)) return false;

    ImVector<const char *> device_ext;
    device_ext.push_back("VK_KHR_swapchain");
    uint32_t dev_count = 0;
    ImVector<VkExtensionProperties> dev_props;
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &dev_count, nullptr);
    dev_props.resize(int(dev_count));
    vkEnumerateDeviceExtensionProperties(physical_, nullptr, &dev_count, dev_props.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
    if (has_extension(dev_props, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
      device_ext.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif
    const float priority = 1.f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_family_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = uint32_t(device_ext.Size);
    dci.ppEnabledExtensionNames = device_ext.Data;
    if (vkCreateDevice(physical_, &dci, nullptr, &device_) != VK_SUCCESS) return false;
    vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
        {VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
    };
    VkDescriptorPoolCreateInfo pci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    for (const VkDescriptorPoolSize &s : sizes) pci.maxSets += s.descriptorCount;
    pci.poolSizeCount = uint32_t(IM_COUNTOF(sizes));
    pci.pPoolSizes = sizes;
    return vkCreateDescriptorPool(device_, &pci, nullptr, &pool_) == VK_SUCCESS;
  }

  bool create_window() {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface) != VK_SUCCESS) return false;
    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_, queue_family_, surface, &supported);
    if (!supported) {
      vkDestroySurfaceKHR(instance_, surface, nullptr);
      return false;
    }
    wd_ = ImGui_ImplVulkanH_Window();
    wd_.Surface = surface;
    const VkFormat formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM,
                                VK_FORMAT_R8G8B8_UNORM};
    wd_.SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(physical_, surface, formats, IM_COUNTOF(formats),
                                                              VK_COLORSPACE_SRGB_NONLINEAR_KHR);
    VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR};
    wd_.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physical_, surface, modes, IM_COUNTOF(modes));
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    ImGui_ImplVulkanH_CreateOrResizeWindow(instance_, physical_, device_, &wd_, queue_family_, nullptr, w, h,
                                           min_images_, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    return true;
  }

public:
  ~VulkanRenderer() override { shutdown(); }

  bool init(GLFWwindow *window) override {
    window_ = window;
    if (!glfwVulkanSupported() || !create_device() || !create_window()) {
      std::fprintf(stderr, "silta: Vulkan is not available\n");
      shutdown();
      return false;
    }
    ImGui_ImplVulkan_InitInfo info = {};
    info.Instance = instance_;
    info.PhysicalDevice = physical_;
    info.Device = device_;
    info.QueueFamily = queue_family_;
    info.Queue = queue_;
    info.DescriptorPool = pool_;
    info.MinImageCount = min_images_;
    info.ImageCount = wd_.ImageCount;
    info.PipelineInfoMain.RenderPass = wd_.RenderPass;
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = check;
    backend_ready_ = ImGui_ImplVulkan_Init(&info);
    return backend_ready_;
  }

  void new_frame() override {
    int w = 0, h = 0;
    glfwGetFramebufferSize(window_, &w, &h);
    if (w > 0 && h > 0 && (rebuild_ || wd_.Width != w || wd_.Height != h)) {
      ImGui_ImplVulkan_SetMinImageCount(min_images_);
      ImGui_ImplVulkanH_CreateOrResizeWindow(instance_, physical_, device_, &wd_, queue_family_, nullptr, w, h,
                                             min_images_, VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
      wd_.FrameIndex = 0;
      rebuild_ = false;
    }
    ImGui_ImplVulkan_NewFrame();
  }

  // Frame read-back: swapchain image -> host-visible buffer.
  struct Readback {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    int width = 0, height = 0;
  };

  Readback record_read_back(VkCommandBuffer cmd, VkImage image) {
    Readback rb;
    rb.width = wd_.Width, rb.height = wd_.Height;
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = VkDeviceSize(rb.width) * rb.height * 4;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(device_, &bci, nullptr, &rb.buffer) != VK_SUCCESS) return {};
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, rb.buffer, &req);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(physical_, &props);
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
      if ((req.memoryTypeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) {
        type = i;
        break;
      }
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (type == UINT32_MAX || vkAllocateMemory(device_, &mai, nullptr, &rb.memory) != VK_SUCCESS) {
      vkDestroyBuffer(device_, rb.buffer, nullptr);
      return {};
    }
    vkBindBufferMemory(device_, rb.buffer, rb.memory, 0);

    auto barrier = [&](VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst) {
      VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.oldLayout = from;
      b.newLayout = to;
      b.srcAccessMask = src;
      b.dstAccessMask = dst;
      b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = image;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                           0, nullptr, 1, &b);
    };
    barrier(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy region = {};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {uint32_t(rb.width), uint32_t(rb.height), 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buffer, 1, &region);
    barrier(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT, 0);
    return rb;
  }

  void finish_read_back(Readback &rb, VkFence fence, Capture &out) {
    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    void *mapped = nullptr;
    if (vkMapMemory(device_, rb.memory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS) {
      out.width = rb.width, out.height = rb.height;
      out.rgba.resize(size_t(rb.width) * rb.height * 4);
      const uint8_t *src = static_cast<const uint8_t *>(mapped);
      const bool bgr = wd_.SurfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM;
      for (size_t i = 0; i < out.rgba.size(); i += 4) {
        out.rgba[i + 0] = src[i + (bgr ? 2 : 0)];
        out.rgba[i + 1] = src[i + 1];
        out.rgba[i + 2] = src[i + (bgr ? 0 : 2)];
        out.rgba[i + 3] = 255;
      }
      vkUnmapMemory(device_, rb.memory);
    }
    vkDestroyBuffer(device_, rb.buffer, nullptr);
    vkFreeMemory(device_, rb.memory, nullptr);
  }

  void render(ImDrawData *data, Color clear, Capture *capture) override {
    if (data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0) return;
    wd_.ClearValue.color.float32[0] = clear.r * clear.a;
    wd_.ClearValue.color.float32[1] = clear.g * clear.a;
    wd_.ClearValue.color.float32[2] = clear.b * clear.a;
    wd_.ClearValue.color.float32[3] = clear.a;

    VkSemaphore acquired = wd_.FrameSemaphores[wd_.SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore done = wd_.FrameSemaphores[wd_.SemaphoreIndex].RenderCompleteSemaphore;
    VkResult err = vkAcquireNextImageKHR(device_, wd_.Swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &wd_.FrameIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) rebuild_ = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;

    ImGui_ImplVulkanH_Frame *fd = &wd_.Frames[wd_.FrameIndex];
    check(vkWaitForFences(device_, 1, &fd->Fence, VK_TRUE, UINT64_MAX));
    check(vkResetFences(device_, 1, &fd->Fence));
    check(vkResetCommandPool(device_, fd->CommandPool, 0));
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(fd->CommandBuffer, &bi));
    VkRenderPassBeginInfo rp = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = wd_.RenderPass;
    rp.framebuffer = fd->Framebuffer;
    rp.renderArea.extent.width = uint32_t(wd_.Width);
    rp.renderArea.extent.height = uint32_t(wd_.Height);
    rp.clearValueCount = 1;
    rp.pClearValues = &wd_.ClearValue;
    vkCmdBeginRenderPass(fd->CommandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(data, fd->CommandBuffer);
    vkCmdEndRenderPass(fd->CommandBuffer);
    Readback rb;
    if (capture) rb = record_read_back(fd->CommandBuffer, fd->Backbuffer);
    check(vkEndCommandBuffer(fd->CommandBuffer));

    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquired;
    si.pWaitDstStageMask = &stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &fd->CommandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &done;
    check(vkQueueSubmit(queue_, 1, &si, fd->Fence));
    if (rb.buffer) finish_read_back(rb, fd->Fence, *capture);

    if (rebuild_) return;
    VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &done;
    pi.swapchainCount = 1;
    pi.pSwapchains = &wd_.Swapchain;
    pi.pImageIndices = &wd_.FrameIndex;
    err = vkQueuePresentKHR(queue_, &pi);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) rebuild_ = true;
    if (err == VK_ERROR_OUT_OF_DATE_KHR) return;
    wd_.SemaphoreIndex = (wd_.SemaphoreIndex + 1) % wd_.SemaphoreCount;
  }

  void shutdown() override {
    if (device_) vkDeviceWaitIdle(device_);
    if (backend_ready_) ImGui_ImplVulkan_Shutdown();
    backend_ready_ = false;
    if (device_ && wd_.Swapchain) ImGui_ImplVulkanH_DestroyWindow(instance_, device_, &wd_, nullptr);
    if (instance_ && wd_.Surface) vkDestroySurfaceKHR(instance_, wd_.Surface, nullptr);
    wd_ = ImGui_ImplVulkanH_Window();
    if (pool_) vkDestroyDescriptorPool(device_, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    if (device_) vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    if (instance_) vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
};
} // namespace

std::unique_ptr<Renderer> make_vulkan_renderer() { return std::make_unique<VulkanRenderer>(); }

} // namespace silta::detail
