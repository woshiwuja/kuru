#include "ui.hpp"
#include "../../lib/core/core.hpp"
#include "camera.hpp"
#include "lighting.hpp"
#include "render.hpp"
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

void UiPlugin::start(entt::registry &reg) {
  // The SDL queue was already drained this frame, so replay it here: the
  // backend needs every event, not the digest the camera reads.
  for (const SDL_Event &event : Core::get()->eventManager->events) {
    ImGui_ImplSDL3_ProcessEvent(&event);
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();

  // Transparent central node: the scene shows through, panels dock around it.
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

// This plugin's own windows. Any other plugin can add its own from update():
// the frame is open from start() to end().
void UiPlugin::update(entt::registry &reg) {
  const auto *core = Core::get();
  if (ImGui::Begin("Scene")) {
    bool demo = true;
    // ImGui::ShowStyleEditor();
    ImGui::ShowDemoWindow(&demo);
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
      if (ImGui::Button("back away")) {
        // Blind-safe escape for when the camera ends up inside the mesh:
        // snap to the origin at max range rather than fumbling sliders with
        // nothing visible to judge them by.
        camera.pivot = {0.0f, 0.0f, 0.0f};
        camera.distance = camera.maxDistance;
      }
      ImGui::SeparatorText("range");
      ImGui::DragFloat("min distance", &camera.minDistance, 0.1f, 0.01f,
                       camera.maxDistance);
      ImGui::DragFloat("max distance", &camera.maxDistance, 1.0f,
                       camera.minDistance, 1e6f);
      ImGui::DragFloat("near plane", &camera.nearPlane, 0.01f, 0.001f,
                       camera.farPlane);
      ImGui::DragFloat("far plane", &camera.farPlane, 1.0f, camera.nearPlane,
                       1e6f);
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

  if (ImGui::Begin("Transforms")) {
    for (auto [entity, transform] : reg.view<Transform>().each()) {
      ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
      if (ImGui::CollapsingHeader(
              ("entity " + std::to_string(entt::to_integral(entity)))
                  .c_str())) {
        ImGui::DragFloat3("position", &transform.position.x, 5.0f);
        ImGui::DragFloat3("rotation", &transform.rotation.x, .1f);
        ImGui::DragFloat3("scale", &transform.scale.x, 0.01f, 0.001f, 1000.0f);
        if (ImGui::Button("center at origin")) {
          transform.position = {0.0f, 0.0f, 0.0f};
        }
      }
      ImGui::PopID();
    }
  }
  ImGui::End();


}
