#include "sync.hpp"
#include "../core/core.hpp"

void Sync::init() {
      auto core = Core::Core::get();
      assert(presentCompleteSemaphores.empty() &&
             renderFinishedSemaphores.empty() && inFlightFences.empty());
      for (size_t i = 0; i < core->graphics->swapChainImages.size(); i++) {
        renderFinishedSemaphores.emplace_back(core->device->device, vk::SemaphoreCreateInfo());
      }

      for (size_t i = 0; i < core->graphics->framesInFlight; i++) {
        presentCompleteSemaphores.emplace_back(core->device->device, vk::SemaphoreCreateInfo());
        inFlightFences.emplace_back(
            core->device->device,
            vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
      }
}
