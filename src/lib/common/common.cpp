#include "../core/core.hpp"
#include <SDL3/SDL_filesystem.h>
#include <fstream>
uint32_t findMemoryType(vk::PhysicalDeviceMemoryProperties props,
                        uint32_t typeFilter,
                        vk::MemoryPropertyFlags memProps) {
  auto len = props.memoryTypeCount;
  for (uint32_t i = 0; i < len; i++) {
    if ((typeFilter & (1 << i)) && (props.memoryTypes[i].propertyFlags &
        memProps) == memProps) {
      return i;
    }
  }
  throw std::runtime_error("failed to find suitable memory type!");
}

void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags memProps, vk::raii::Buffer &buffer,
                  vk::raii::DeviceMemory &bufferMemory) {
  auto core = Core::get();
  auto props = core->device->physicalDevice.getMemoryProperties();
  vk::BufferCreateInfo bufferInfo{
      .size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive};
  buffer = vk::raii::Buffer(core->device->device, bufferInfo);
  vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
  vk::MemoryAllocateInfo allocInfo{
      .allocationSize = memRequirements.size,
      .memoryTypeIndex =
          findMemoryType(props,memRequirements.memoryTypeBits, memProps)};
  bufferMemory = vk::raii::DeviceMemory(core->device->device, allocInfo);
  buffer.bindMemory(*bufferMemory, 0);
}

std::string assetPath(const std::string &relative) {
  const char *base = SDL_GetBasePath(); // owned by SDL, valid after SDL_Init
  return base != nullptr ? base + relative : relative;
}

std::vector<char> readFile(const std::string &filename) {
  const std::string resolved = assetPath(filename);
  std::ifstream file(resolved, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + resolved);
  }

  size_t fileSize = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();
  return buffer;
}
