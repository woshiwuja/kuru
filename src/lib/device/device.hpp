#pragma once
#include <memory>
#include <vulkan/vulkan_raii.hpp>

namespace KR {
struct Device {
  vk::raii::PhysicalDevice physicalDevice = nullptr;
  vk::raii::Device device = nullptr;
  uint32_t queueIndex = ~0;
  vk::raii::Queue queue = nullptr;
  std::vector<const char *> requiredDeviceExtension = {
      vk::KHRSwapchainExtensionName, vk::KHRCreateRenderpass2ExtensionName,
      vk::KHRDynamicRenderingExtensionName};
  void pickPhysicalDevice();
  void createLogicalDevice();
  bool isDeviceSuitable(vk::raii::PhysicalDevice const &physicalDevice);
  void wait();

  vk::raii::CommandPool commandPool = nullptr;
  void createCommandPool();
  std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();
  void
  endSingleTimeCommands(const vk::raii::CommandBuffer &commandBuffer) const;
  void copyBuffer(vk::raii::Buffer &srcBuffer, vk::raii::Buffer &dstBuffer,
                  vk::DeviceSize size);
};
} // namespace KR
