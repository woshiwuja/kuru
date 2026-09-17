#include "ui.hpp"
#include <Kuru.h>
#include "camera.hpp"
#include "entt/entity/fwd.hpp"
#include "lighting.hpp"
#include "render.hpp"

using namespace KR;


void UIPlugin::init(entt::registry &reg){}
void UIPlugin::start(entt::registry &reg) {
  for (const SDL_Event &event : Core::get()->eventManager->events) {
    ImGui_ImplSDL3_ProcessEvent(&event);
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                               ImGuiDockNodeFlags_PassthruCentralNode);
}

void UIPlugin::end(entt::registry &reg) {
  auto &frame = reg.ctx().get<FrameContext>();

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), **frame.commandBuffer);

  const ImGuiIO &io = ImGui::GetIO();
  frame.uiCapturesMouse = io.WantCaptureMouse;
  frame.uiCapturesKeyboard = io.WantCaptureKeyboard;
}

void UIPlugin::update(entt::registry &reg) {
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
