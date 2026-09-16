#include "ui.hpp"
#include <Kuru.h>
#include "camera.hpp"
#include "lighting.hpp"
#include "render.hpp"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

using namespace KR;


void UiPlugin::start(entt::registry &reg) {
  for (const SDL_Event &event : Core::get()->eventManager->events) {
    ImGui_ImplSDL3_ProcessEvent(&event);
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                               ImGuiDockNodeFlags_PassthruCentralNode);
}

void UiPlugin::end(entt::registry &reg) {
  auto &frame = reg.ctx().get<FrameContext>();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), **frame.commandBuffer);

  const ImGuiIO &io = ImGui::GetIO();
  frame.uiCapturesMouse = io.WantCaptureMouse;
  frame.uiCapturesKeyboard = io.WantCaptureKeyboard;
}

void UiPlugin::update(entt::registry &reg) {
  const auto *core = Core::get();
  ImGui::ShowDemoWindow();
  //ImGui::ShowStyleEditor();
  if (ImGui::Begin("Scene")) {
    ImGui::Text("%.1f fps (%.2f ms)", 1.0f / std::max(core->deltaTime, 1e-6f),
                core->deltaTime * 1000.0f);
    ImGui::Text("drawables: %zu", reg.view<MeshRef>().size());
    ImGui::Text("swapchain: %ux%u", core->graphics->swapChainExtent.width,
                core->graphics->swapChainExtent.height);
  }
  ImGui::End();
}
