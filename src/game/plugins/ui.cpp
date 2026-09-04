#include "ui.hpp"
#include "camera.hpp"
#include "render.hpp"
#include "../../lib/core/core.hpp"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

namespace {
void checkVkResult(VkResult result) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error("imgui vulkan backend: VkResult " +
                             std::to_string(static_cast<int>(result)));
  }
}
} // namespace

void UiPlugin::init(entt::registry &reg) {
  auto *core = Core::get();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForVulkan(core->window->window);

  // The renderer uses dynamic rendering, so the backend needs the attachment
  // formats instead of a render pass.
  VkFormat colorFormat =
      static_cast<VkFormat>(core->graphics->swapChainSurfaceFormat.format);
  VkPipelineRenderingCreateInfo renderingInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &colorFormat,
      .depthAttachmentFormat =
          static_cast<VkFormat>(core->graphics->findDepthFormat())};

  ImGui_ImplVulkan_InitInfo info{};
  info.ApiVersion = VK_API_VERSION_1_3;
  info.Instance = *core->instance;
  info.PhysicalDevice = *core->device->physicalDevice;
  info.Device = *core->device->device;
  info.QueueFamily = core->device->queueIndex;
  info.Queue = *core->device->queue;
  // Non-zero means the backend allocates and owns its own pool.
  info.DescriptorPoolSize = 16;
  info.MinImageCount = core->graphics->swapMinImageCount;
  info.ImageCount =
      static_cast<uint32_t>(core->graphics->swapChainImages.size());
  info.UseDynamicRendering = true;
  info.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
  info.CheckVkResultFn = checkVkResult;

  if (!ImGui_ImplVulkan_Init(&info)) {
    throw std::runtime_error("failed to initialise the imgui vulkan backend");
  }
  started = true;
}

UiPlugin::~UiPlugin() {
  if (!started) {
    return;
  }
  // Core::cleanup() waits on the device and destroys the plugins before the
  // device itself, so the backend still has something to release into.
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

void UiPlugin::run(entt::registry &reg) {
  auto &frame = reg.ctx().get<FrameContext>();
  auto *core = Core::get();

  // The SDL queue was already drained this frame, so replay it here: the
  // backend needs every event, not the digest the camera reads.
  for (const SDL_Event &event : core->eventManager->events) {
    ImGui_ImplSDL3_ProcessEvent(&event);
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  // Transparent central node: the scene shows through, panels dock around it.
  ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                               ImGuiDockNodeFlags_PassthruCentralNode);
  drawPanels(reg);

  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                  **frame.commandBuffer);

  const ImGuiIO &io = ImGui::GetIO();
  frame.uiCapturesMouse = io.WantCaptureMouse;
  frame.uiCapturesKeyboard = io.WantCaptureKeyboard;
}

void UiPlugin::drawPanels(entt::registry &reg) {
  const auto *core = Core::get();

  if (ImGui::Begin("Scene")) {
    ImGui::Text("%.1f fps (%.2f ms)", 1.0f / std::max(core->deltaTime, 1e-6f),
                core->deltaTime * 1000.0f);
    ImGui::Text("drawables: %zu", reg.view<MeshRef>().size());
    ImGui::Text("swapchain: %ux%u", core->graphics->swapChainExtent.width,
                core->graphics->swapChainExtent.height);
  }
  ImGui::End();

  if (ImGui::Begin("Camera")) {
    for (auto [entity, camera] : reg.view<Camera>().each()) {
      ImGui::DragFloat3("pivot", &camera.pivot.x, 0.05f);
      ImGui::DragFloat("ground height", &camera.groundHeight, 0.05f);
      ImGui::DragFloat("yaw", &camera.yaw, 0.5f);
      ImGui::SliderFloat("pitch", &camera.pitch, -89.0f, 89.0f);
      ImGui::SliderFloat("distance", &camera.distance, camera.minDistance,
                         camera.maxDistance);
      ImGui::SeparatorText("feel");
      ImGui::DragFloat("pan speed", &camera.panSpeed, 0.1f, 0.0f, 100.0f);
      ImGui::DragFloat("turn speed", &camera.turnSpeed, 1.0f, 0.0f, 720.0f);
      ImGui::DragFloat("orbit sensitivity", &camera.orbitSensitivity, 0.01f,
                       0.0f, 5.0f);
      ImGui::DragFloat("zoom speed", &camera.zoomSpeed, 0.01f, 0.0f, 1.0f);
      ImGui::SliderFloat("fov", &camera.fov, 10.0f, 120.0f);
      break; // ponytail: first camera only, same as CameraPlugin
    }
  }
  ImGui::End();
}
