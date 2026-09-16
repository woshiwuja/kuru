#pragma once
#include "imgui.h"
#include <vulkan/vulkan_raii.hpp>

namespace KR {
VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData, void *);

inline void GuiErrorMessage(const std::exception &exception){
    ImGui::Begin("Error");
        ImGui::Text("Exception: %s",exception.what());
    ImGui::End();
};
inline void GuiErrorMessage(const int errorCode){
    ImGui::Begin("Error");
        ImGui::Text("code: %d", errorCode);
    ImGui::End();
};
} // namespace KR
