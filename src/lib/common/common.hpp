#pragma once
#include "vertex.hpp"
#include <vulkan/vulkan_raii.hpp>

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
constexpr uint32_t MAX_OBJECTS = 64; // descriptor pool ceiling, not a live count

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

uint32_t findMemoryType(vk::PhysicalDeviceMemoryProperties props,
                        uint32_t typeFilter, vk::MemoryPropertyFlags memProps);
void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags memProps, vk::raii::Buffer &buffer,
                  vk::raii::DeviceMemory &bufferMemory);

std::vector<char> readFile(const std::string &filename);
