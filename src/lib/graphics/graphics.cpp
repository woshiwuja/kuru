#include "graphics.hpp"
#include "../core/core.hpp"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include <vulkan/vulkan_core.h>

namespace KR {
void Graphics::init() { createSurface(); }
void Graphics::createSurface() {
  const auto& core = Core::Core::get();
  VkSurfaceKHR s{};
  if (!SDL_Vulkan_CreateSurface(core->window->window, *core->instance, nullptr, &s))
    throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError());
  surface = vk::raii::SurfaceKHR(core->instance, s);
}
void Graphics::chooseSwapExtent() {
    auto core = Core::Core::get();
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    swapChainExtent = capabilities.currentExtent;
  }
  int width, height;
  SDL_GetWindowSizeInPixels(core->window->window, &width, &height);
  swapChainExtent = vk::Extent2D{
      std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                           capabilities.maxImageExtent.width),
      std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                           capabilities.maxImageExtent.height)};
}
void Graphics::createSwapChain(){
      auto core = Core::Core::get();
      auto pd = core->device->physicalDevice;
      capabilities =
          pd.getSurfaceCapabilitiesKHR(*surface);
      chooseSwapExtent();
      chooseSwapMinImageCount();

      std::vector<vk::SurfaceFormatKHR> availableFormats =
          pd.getSurfaceFormatsKHR(*surface);
      chooseSwapSurfaceFormat(availableFormats);

      vk::SwapchainCreateInfoKHR swapChainCreateInfo{
          .surface = *surface,
          .minImageCount = swapMinImageCount,
          .imageFormat = swapChainSurfaceFormat.format,
          .imageColorSpace = swapChainSurfaceFormat.colorSpace,
          .imageExtent = swapChainExtent,
          .imageArrayLayers = 1,
          .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
          .imageSharingMode = vk::SharingMode::eExclusive,
          .preTransform = capabilities.currentTransform,
          .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
          // vsync: FIFO blocks on the refresh instead of throwing frames away
          // like mailbox did, and it's the one mode the spec guarantees exists.
          .presentMode = vk::PresentModeKHR::eFifo,
          .clipped = true};

      swapChain = vk::raii::SwapchainKHR(core->device->device, swapChainCreateInfo);
      swapChainImages = swapChain.getImages();
}
void Graphics::cleanupSwapChain() {
    swapChainImageViews.clear();
    swapChain = nullptr;
}

void Graphics::recreateSwapChain() {
    const auto& core = Core::Core::get();
    auto pd = core->device->physicalDevice;
    // SDL_GetWindowSizeInPixels reports the size the window will be restored
    // to while minimized, not 0x0, so it can't detect this case - poll the
    // surface capabilities themselves (what the driver reports as currentExtent
    // for a minimized/zero-area window) and pump events so the OS doesn't
    // consider the app hung and so a restore/quit while waiting is seen.
    auto caps = pd.getSurfaceCapabilitiesKHR(*surface);
    while (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
      core->eventManager->pump();
      if (core->eventManager->quit) return;
      caps = pd.getSurfaceCapabilitiesKHR(*surface);
    }
    core->device->wait();
    cleanupSwapChain();
    createSwapChain();
    createImageViews();
    createColorResources();
    createDepthResources();
    createNormalResources();
}

void Graphics::createImageViews() {
    assert(swapChainImageViews.empty());
    vk::ImageViewCreateInfo imageViewCreateInfo{
        .viewType = vk::ImageViewType::e2D,
        .format = swapChainSurfaceFormat.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
    for (auto &image : swapChainImages) {
      imageViewCreateInfo.image = image;
      swapChainImageViews.emplace_back(Core::Core::get()->device->device,
                                       imageViewCreateInfo);
    }
}

void Graphics::chooseSwapMinImageCount() {
    swapMinImageCount = std::max(3u, capabilities.minImageCount);
    if ((0 < capabilities.maxImageCount) &&
        (capabilities.maxImageCount < swapMinImageCount)) {
      swapMinImageCount = capabilities.maxImageCount;
    }
}

void Graphics::chooseSwapSurfaceFormat(
      const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
    assert(!availableFormats.empty());
    const auto formatIt =
        std::ranges::find_if(availableFormats, [](const auto &format) {
          return format.format == vk::Format::eB8G8R8A8Srgb &&
                 format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    if (formatIt != availableFormats.end()) {
      swapChainSurfaceFormat = *formatIt;
    } else {
      swapChainSurfaceFormat = availableFormats[0];
    }
}

void Graphics::createDepthResources() {
    vk::Format depthFormat = findDepthFormat();

    // Depth has to match the colour target's sample count, and it's never
    // resolved: nothing reads it after the pass.
    createImage(swapChainExtent.width, swapChainExtent.height, depthFormat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, depthImage,
                depthImageMemory, msaaSamples);
    depthImageView = createImageView(depthImage, depthFormat,
                                     vk::ImageAspectFlagBits::eDepth);
}

void Graphics::createNormalResources() {
    // Written by RenderPlugin's prepass, sampled by the outline pass. 1x: it is
    // read as a texture, and resolving it would average normals across a
    // silhouette, blunting exactly the discontinuity the outline looks for.
    createImage(swapChainExtent.width, swapChainExtent.height, normalFormat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal, normalImage,
                normalImageMemory, vk::SampleCountFlagBits::e1);
    normalImageView = createImageView(normalImage, normalFormat,
                                      vk::ImageAspectFlagBits::eColor);
    // The prepass needs its own depth: the main one is multisampled and so
    // can't pair with this 1x colour target.
    vk::Format depthFormat = findDepthFormat();
    createImage(swapChainExtent.width, swapChainExtent.height, depthFormat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, normalDepthImage,
                normalDepthImageMemory, vk::SampleCountFlagBits::e1);
    normalDepthImageView = createImageView(normalDepthImage, depthFormat,
                                           vk::ImageAspectFlagBits::eDepth);
}

void Graphics::createColorResources() {
    if (msaaSamples == vk::SampleCountFlagBits::e1) {
      // Nothing to resolve from; the pass renders straight into the swapchain.
      colorImageView = nullptr;
      colorImage = nullptr;
      colorImageMemory = nullptr;
      return;
    }
    createImage(swapChainExtent.width, swapChainExtent.height,
                swapChainSurfaceFormat.format, vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eColorAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, colorImage,
                colorImageMemory, msaaSamples);
    colorImageView = createImageView(colorImage, swapChainSurfaceFormat.format,
                                     vk::ImageAspectFlagBits::eColor);
}

void Graphics::chooseMsaaSamples() {
    const auto &core = Core::Core::get();
    const vk::PhysicalDeviceLimits &limits =
        core->device->physicalDevice.getProperties().limits;
    // Colour and depth are attachments in the same pass, so only counts both
    // support are usable.
    const vk::SampleCountFlags supported = limits.framebufferColorSampleCounts &
                                           limits.framebufferDepthSampleCounts;

    msaaSamples = vk::SampleCountFlagBits::e1;
    for (const auto bit : {vk::SampleCountFlagBits::e2,
                           vk::SampleCountFlagBits::e4,
                           vk::SampleCountFlagBits::e8,
                           vk::SampleCountFlagBits::e16}) {
      const auto count = static_cast<uint32_t>(bit);
      if (count <= requestedMsaa && (supported & bit)) {
        msaaSamples = bit;
      }
    }
}

vk::Format Graphics::findSupportedFormat(const std::vector<vk::Format> &candidates,
                                 vk::ImageTiling tiling,
                                 vk::FormatFeatureFlags features) const {
    const auto& core = Core::Core::get();
    for (const auto format : candidates) {
      vk::FormatProperties props =
          core->device->physicalDevice.getFormatProperties(format);

      if (tiling == vk::ImageTiling::eLinear &&
          (props.linearTilingFeatures & features) == features) {
        return format;
      }
      if (tiling == vk::ImageTiling::eOptimal &&
          (props.optimalTilingFeatures & features) == features) {
        return format;
      }
    }

    throw std::runtime_error("failed to find supported format!");
}

vk::Format Graphics::findDepthFormat() const {
    return findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
         vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

void Graphics::createImage(uint32_t width, uint32_t height, vk::Format format,
                   vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                   vk::MemoryPropertyFlags properties, vk::raii::Image &image,
                   vk::raii::DeviceMemory &imageMemory,
                   vk::SampleCountFlagBits samples) {
    const auto& core = Core::Core::get();
    // Defaults to e1 on purpose: every texture goes through here, and a
    // multisampled image can be neither copied into nor sampled by a shader.
    // Only the attachments this file creates ever ask for more.
    vk::ImageCreateInfo imageInfo{.imageType = vk::ImageType::e2D,
                                  .format = format,
                                  .extent = {width, height, 1},
                                  .mipLevels = 1,
                                  .arrayLayers = 1,
                                  .samples = samples,
                                  .tiling = tiling,
                                  .usage = usage,
                                  .sharingMode = vk::SharingMode::eExclusive,
                                  .initialLayout = vk::ImageLayout::eUndefined};
    image = vk::raii::Image(core->device->device, imageInfo);

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memRequirements.size,
        .memoryTypeIndex =
            findMemoryType(core->device->physicalDevice.getMemoryProperties(),
                           memRequirements.memoryTypeBits, properties)};
    imageMemory = vk::raii::DeviceMemory(core->device->device, allocInfo);
    image.bindMemory(*imageMemory, 0);
}

vk::raii::ImageView Graphics::createImageView(vk::raii::Image &image, vk::Format format,
                                      vk::ImageAspectFlags aspectFlags) {
    const auto& core = Core::Core::get();
    vk::ImageViewCreateInfo viewInfo{
        .image = *image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {aspectFlags, 0, 1, 0, 1}};
    return vk::raii::ImageView(core->device->device, viewInfo);
}

void Graphics::createTextureSampler() {
    const auto &device = Core::Core::get()->device;
    vk::PhysicalDeviceProperties properties = device->physicalDevice.getProperties();
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0.0f,
        .anisotropyEnable = vk::True,
        .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways};
    sampler = vk::raii::Sampler(device->device, samplerInfo);
}

void Graphics::createGbufferSampler() {
    const auto &device = Core::Core::get()->device;
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .anisotropyEnable = vk::False,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways};
    gbufferSampler = vk::raii::Sampler(device->device, samplerInfo);
}

bool Graphics::hasStencilComponent(vk::Format format) {
  return format == vk::Format::eD32SfloatS8Uint ||
         format == vk::Format::eD24UnormS8Uint;
}
} // namespace KR
