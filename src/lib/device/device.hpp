#pragma once
#include <memory>
#include <vulkan/vulkan_raii.hpp>
struct Device {
  vk::raii::PhysicalDevice physicalDevice = nullptr;
  vk::raii::Device device = nullptr;
  uint32_t queueIndex = ~0;
  vk::raii::Queue queue = nullptr;
  std::vector<const char *> requiredDeviceExtension = {
      vk::KHRSwapchainExtensionName, vk::KHRCreateRenderpass2ExtensionName,
      // core in 1.3, but imgui's Vulkan backend refuses dynamic rendering
      // unless the extension is named
      vk::KHRDynamicRenderingExtensionName};
  void pickPhysicalDevice();
  void createLogicalDevice();
  bool isDeviceSuitable(vk::raii::PhysicalDevice const &physicalDevice);
  void wait();

  // The command pool hangs off the queue family this Device picked, and every
  // one-shot upload goes through the same queue: they belong here, not on Core.
  vk::raii::CommandPool commandPool = nullptr;
  void createCommandPool();
  std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();
  void endSingleTimeCommands(const vk::raii::CommandBuffer &commandBuffer) const;
  void copyBuffer(vk::raii::Buffer &srcBuffer, vk::raii::Buffer &dstBuffer,
                  vk::DeviceSize size);
};
