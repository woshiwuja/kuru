#pragma once
#include "../common/common.hpp"
#include <vulkan/vulkan_raii.hpp>

namespace KR {
struct Graphics {
  vk::raii::SurfaceKHR surface = nullptr;
  vk::raii::SwapchainKHR swapChain = nullptr;
  std::vector<vk::Image> swapChainImages;
  vk::SurfaceFormatKHR swapChainSurfaceFormat;
  vk::Extent2D swapChainExtent;
  std::vector<vk::raii::ImageView> swapChainImageViews;
  uint32_t swapMinImageCount = 0;
  vk::SurfaceCapabilitiesKHR capabilities;
  vk::raii::Image depthImage = nullptr;
  vk::raii::DeviceMemory depthImageMemory = nullptr;
  vk::raii::ImageView depthImageView = nullptr;
  // Multisampled colour target, resolved into the swapchain image at end of
  // pass. Only populated when msaaSamples > e1; at 1x these stay null and the
  // swapchain image is rendered to directly.
  vk::raii::Image colorImage = nullptr;
  vk::raii::DeviceMemory colorImageMemory = nullptr;
  vk::raii::ImageView colorImageView = nullptr;
  // The knob. Set requestedMsaa before initVulkan; chooseMsaaSamples() clamps it
  // to what the device actually supports and stores that in msaaSamples.
  uint32_t requestedMsaa = 4;
  vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
  // Normal + view-distance G-buffer for the outline pass
  // (assets/shaders/outline.slang). Single-sampled and separate from the main
  // target: RenderPlugin fills it in a prepass before the main pass begins,
  // then samples it inside that pass, which an attachment of the pass itself
  // could not be.
  vk::raii::Image normalImage = nullptr;
  vk::raii::DeviceMemory normalImageMemory = nullptr;
  vk::raii::ImageView normalImageView = nullptr;
  vk::raii::Image normalDepthImage = nullptr;
  vk::raii::DeviceMemory normalDepthImageMemory = nullptr;
  vk::raii::ImageView normalDepthImageView = nullptr;
  static constexpr vk::Format normalFormat = vk::Format::eR16G16B16A16Sfloat;
  vk::raii::Sampler sampler = nullptr;
  // Clamped, unfiltered sampler for the G-buffer: the outline taps neighbouring
  // texels directly, so repeat wrapping would wrap edges around the screen and
  // linear filtering would blur the discontinuities it is looking for.
  vk::raii::Sampler gbufferSampler = nullptr;
  uint32_t framesInFlight = MAX_FRAMES_IN_FLIGHT;
  bool framebufferResized = false;
  void init();
  void createSurface();
  void chooseSwapExtent();
  void cleanupSwapChain();
  void createSwapChain();
  void recreateSwapChain();
  void createImageViews();
  void chooseSwapMinImageCount();
  void chooseSwapSurfaceFormat(
      const std::vector<vk::SurfaceFormatKHR> &availableFormats);
  void createDepthResources();
  void createNormalResources();
  void createColorResources();
  void chooseMsaaSamples();
  vk::Format findSupportedFormat(const std::vector<vk::Format> &candidates,
                                 vk::ImageTiling tiling,
                                 vk::FormatFeatureFlags features) const;
  [[nodiscard]] vk::Format findDepthFormat() const;
  static bool hasStencilComponent(vk::Format format);
  void createImage(uint32_t width, uint32_t height, vk::Format format,
                   vk::ImageTiling tiling, vk::ImageUsageFlags usage,
                   vk::MemoryPropertyFlags properties, vk::raii::Image &image,
                   vk::raii::DeviceMemory &imageMemory,
                   vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);
  vk::raii::ImageView createImageView(vk::raii::Image &image, vk::Format format,
                                      vk::ImageAspectFlags aspectFlags);
  void createTextureSampler();
  void createGbufferSampler();
};
} // namespace KR
