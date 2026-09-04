#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <vector>
struct Sync {
    std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
    std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
    std::vector<vk::raii::Fence> inFlightFences;
    uint32_t frameIndex = 0;
    void init();
};
