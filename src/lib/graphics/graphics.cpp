#include "graphics.hpp"
#include "../core/core.hpp"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include <vulkan/vulkan_core.h>
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

      std::vector<vk::PresentModeKHR> availablePresentModes =
          pd.getSurfacePresentModesKHR(*surface);
      vk::PresentModeKHR presentMode =
          chooseSwapPresentMode(availablePresentModes);

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
          .presentMode = presentMode,
          .clipped = true};

      swapChain = vk::raii::SwapchainKHR(core->device->device, swapChainCreateInfo);
      swapChainImages = swapChain.getImages();
}
void Graphics::cleanupSwapChain() {
    swapChainImageViews.clear();
    swapChain = nullptr;
}

void Graphics::recreateSwapChain() {
    int width = 0, height = 0;
    const auto& core = Core::Core::get();
    SDL_GetWindowSizeInPixels(core->window->window, &width, &height);
    while (width == 0 || height == 0) {
      SDL_GetWindowSizeInPixels(core->window->window, &width, &height);
    }
    core->device->wait();
    cleanupSwapChain();
    createSwapChain();
    createImageViews();
    createDepthResources();
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
    auto minImageCount = std::max(3u, capabilities.minImageCount);
    if ((0 < capabilities.maxImageCount) &&
        (capabilities.maxImageCount < minImageCount)) {
      minImageCount = capabilities.maxImageCount;
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

vk::PresentModeKHR Graphics::chooseSwapPresentMode(
      std::vector<vk::PresentModeKHR> const &availablePresentModes) {
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
      return presentMode == vk::PresentModeKHR::eFifo;
    }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) {
                                 return vk::PresentModeKHR::eMailbox == value;
                               })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
}

void Graphics::createDepthResources() {
    vk::Format depthFormat = findDepthFormat();

    createImage(swapChainExtent.width, swapChainExtent.height, depthFormat,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment,
                vk::MemoryPropertyFlagBits::eDeviceLocal, depthImage,
                depthImageMemory);
    depthImageView = createImageView(depthImage, depthFormat,
                                     vk::ImageAspectFlagBits::eDepth);
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
                   vk::raii::DeviceMemory &imageMemory) {
    const auto& core = Core::Core::get();
    vk::ImageCreateInfo imageInfo{.imageType = vk::ImageType::e2D,
                                  .format = format,
                                  .extent = {width, height, 1},
                                  .mipLevels = 1,
                                  .arrayLayers = 1,
                                  .samples = vk::SampleCountFlagBits::e1,
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

bool Graphics::hasStencilComponent(vk::Format format) {
  return format == vk::Format::eD32SfloatS8Uint ||
         format == vk::Format::eD24UnormS8Uint;
}
