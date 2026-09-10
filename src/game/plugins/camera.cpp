#include "camera.hpp"
#include "../../lib/core/core.hpp"
#include "entt/entity/fwd.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace {
constexpr glm::vec3 WORLD_UP = {0.0f, 1.0f, 0.0f};
}

glm::vec3 Camera::front() const {
  const float yawRad = glm::radians(yaw);
  const float pitchRad = glm::radians(pitch);
  return glm::normalize(glm::vec3{std::cos(yawRad) * std::cos(pitchRad),
                                  std::sin(pitchRad),
                                  std::sin(yawRad) * std::cos(pitchRad)});
}

glm::vec3 Camera::position() const { return pivot - front() * distance; }

void CameraPlugin::init(entt::registry &reg) {
  reg.emplace<MainCamera>(reg.create());
}

void CameraPlugin::update(entt::registry &reg) {
  auto &frame = reg.ctx().get<FrameContext>();
  auto view = reg.view<MainCamera>();
  if (view.begin() == view.end()) {
    return;
  }
  Camera &camera = view.get<MainCamera>(*view.begin());
  camera.control(frame);
  UI(reg);
}

void Camera::control(FrameContext frame){
    const auto *core = Core::get();
    const auto &input = *core->eventManager;
    const float deltaTime = core->deltaTime;
    if (input.down(SDL_BUTTON_MIDDLE) && !frame.uiCapturesMouse) {
      yaw += input.mouseDeltaX * orbitSensitivity;
      pitch -= input.mouseDeltaY * orbitSensitivity;
    }
    const bool keyboardFree = !frame.uiCapturesKeyboard;
    if (keyboardFree && input.down(SDL_SCANCODE_Q)) {
      yaw -= turnSpeed * deltaTime;
    }
    if (keyboardFree && input.down(SDL_SCANCODE_E)) {
      yaw += turnSpeed * deltaTime;
    }
    pitch = std::clamp(pitch, -89.0f, 89.0f);

    if (!frame.uiCapturesMouse) {
      distance *= 1.0f - input.wheel * zoomSpeed;
    }
    distance =
        std::clamp(distance, minDistance, maxDistance);

    const glm::vec3 front = this->front();
    const glm::vec3 flatFront =
        glm::normalize(glm::vec3{front.x, 0.0f, front.z});
    const glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, WORLD_UP));

    const float step = panSpeed * deltaTime;
    if (keyboardFree && input.down(SDL_SCANCODE_W)) pivot += flatFront * step;
    if (keyboardFree && input.down(SDL_SCANCODE_S)) pivot -= flatFront * step;
    if (keyboardFree && input.down(SDL_SCANCODE_D)) pivot += flatRight * step;
    if (keyboardFree && input.down(SDL_SCANCODE_A)) pivot -= flatRight * step;
    pivot.y = groundHeight;

    frame.view = glm::lookAt(position(), pivot, WORLD_UP);
    frame.proj = glm::perspective(
        glm::radians(fov),
        static_cast<float>(frame.extent.width) /
            static_cast<float>(frame.extent.height),
        nearPlane, farPlane);
    frame.proj[1][1] *= -1; // glm is GL-handed, Vulkan's Y points the other way
    // sky_clouds.slang unprojects assuming this (pre-reversal) NDC convention.
    frame.skyRayProj = frame.proj;

    // Reversed-Z: near maps to depth 1, far to depth 0. A standard depth buffer
    // spends almost all of its precision within the first few percent of
    // [nearPlane, farPlane]; this flip (needs GLM_FORCE_DEPTH_ZERO_TO_ONE, set
    // in CMakeLists.txt) redistributes it evenly in 1/z instead, so far terrain
    // stops z-fighting long before farPlane needs to come down to hide it.
    static const glm::mat4 REVERSE_Z(1, 0, 0, 0,
                                     0, 1, 0, 0,
                                     0, 0, -1, 0,
                                     0, 0, 1, 1);
    frame.proj = REVERSE_Z * frame.proj;
};
void CameraPlugin::UI(entt::registry &reg){
    using namespace ImGui;
    if (Begin("Cameras")) {
      for (auto [entity, camera] : reg.view<Camera>().each()) {
        DragFloat3("pivot", &camera.pivot.x, 0.05f);
        DragFloat("ground height", &camera.groundHeight, 0.05f);
        DragFloat("yaw", &camera.yaw, 0.5f);
        SliderFloat("pitch", &camera.pitch, -89.0f, 89.0f);
        SliderFloat("distance", &camera.distance, camera.minDistance,
                           camera.maxDistance);
        if (Button("back away")) {
          // Blind-safe escape for when the camera ends up inside the mesh:
          // snap to the origin at max range rather than fumbling sliders with
          // nothing visible to judge them by.
          camera.pivot = {0.0f, 0.0f, 0.0f};
          camera.distance = camera.maxDistance;
        }
        SeparatorText("range");
        DragFloat("min distance", &camera.minDistance, 0.1f, 0.01f,
                         camera.maxDistance);
        DragFloat("max distance", &camera.maxDistance, 1.0f,
                         camera.minDistance, 1e6f);
        DragFloat("near plane", &camera.nearPlane, 0.01f, 0.001f,
                         camera.farPlane);
        DragFloat("far plane", &camera.farPlane, 1.0f, camera.nearPlane,
                         1e6f);
        SeparatorText("feel");
        DragFloat("pan speed", &camera.panSpeed, 0.1f, 0.0f, 100.0f);
        DragFloat("turn speed", &camera.turnSpeed, 1.0f, 0.0f, 720.0f);
        DragFloat("orbit sensitivity", &camera.orbitSensitivity, 0.01f,
                         0.0f, 5.0f);
        DragFloat("zoom speed", &camera.zoomSpeed, 0.01f, 0.0f, 1.0f);
        SliderFloat("fov", &camera.fov, 10.0f, 120.0f);
      }
    }
   End();
}
