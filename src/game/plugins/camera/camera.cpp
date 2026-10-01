#include "camera.hpp"
#include <Kuru.h>
#include "SDL3/SDL_mouse.h"
#include "entt/entity/fwd.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include "ImGuizmo.h"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <iostream>
#include "../physics/physics.hpp" // glmVecToJPH

using namespace KR;

namespace KR {
constexpr glm::vec3 WORLD_UP = {0.0f, 1.0f, 0.0f};

glm::vec3 Camera::front() const {
  const float yawRad = glm::radians(yaw);
  const float pitchRad = glm::radians(pitch);
  return glm::normalize(glm::vec3{std::cos(yawRad) * std::cos(pitchRad),
                                  std::sin(pitchRad),
                                  std::sin(yawRad) * std::cos(pitchRad)});
}

glm::vec3 Camera::position() const { return pivot - front() * distance; }

void CameraPlugin::init(entt::registry &reg) {
  const entt::entity camera = reg.create();
  reg.emplace<Camera>(camera);
  reg.emplace<MainCamera>(camera);
}

void CameraPlugin::update(entt::registry &reg) {
  auto &frame = reg.ctx().get<FrameContext>();
  auto view = reg.view<Camera, MainCamera>();
  if (view.begin() == view.end()) {
    return;
  }
  Camera &camera = view.get<Camera>(*view.begin());
  camera.control(frame);
  UI(reg);
}

void Camera::control(FrameContext &frame){
    const auto *core = Core::get();
    const auto &input = *core->eventManager;
    const float dt = core->deltaTime();
    const bool orbiting =
        input.down(SDL_BUTTON_MIDDLE) && !frame.uiCapturesMouse;
    if (orbiting) {
      yaw += input.mouseDeltaX * orbitSensitivity;
      pitch -= input.mouseDeltaY * orbitSensitivity;
    }
    // Relative mode rather than SDL_HideCursor: imgui's SDL3 backend re-shows
    // the cursor from its NewFrame every time its own cursor shape changes, and
    // winning that fight only on some frames is what made the pointer blink
    // back mid-orbit. This also pins the pointer, so a long drag no longer
    // walks it off to the screen edge.
    SDL_SetWindowRelativeMouseMode(core->window->window, orbiting);
    const bool keyboardFree = !frame.uiCapturesKeyboard;
    if (keyboardFree && input.down(SDL_SCANCODE_Q)) {
      yaw -= turnSpeed * dt;
    }
    if (keyboardFree && input.down(SDL_SCANCODE_E)) {
      yaw += turnSpeed * dt;
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

    const float step = panSpeed * dt;
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

    viewCube(frame);

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

void Camera::viewCube(FrameContext &frame) {
    constexpr float size = 128.0f;
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    glm::mat4 glProj = frame.skyRayProj;
    glProj[1][1] *= -1;
    glm::mat4 identity(1.0f);
    glm::mat4 view = frame.view;
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::ViewManipulate(&view[0][0], &glProj[0][0], ImGuizmo::TRANSLATE,
                             ImGuizmo::WORLD, &identity[0][0],
                             distance, {vp->Pos.x + vp->Size.x - size, vp->Pos.y},
                             {size, size}, 0x10101010);
    if (ImGuizmo::IsViewManipulateHovered() || ImGuizmo::IsUsingViewManipulate())
      frame.uiCapturesMouse = true;
    if (view == frame.view)
      return;
    const glm::vec3 f = -glm::vec3(glm::inverse(view)[2]);
    if (f.x * f.x + f.z * f.z > 1e-6f)
      yaw = glm::degrees(std::atan2(f.z, f.x));
    pitch = std::clamp(glm::degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f))),
                       -89.0f, 89.0f);
    frame.view = glm::lookAt(position(), pivot, WORLD_UP);
}

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
        DragFloat("fov", &camera.fov, 1.0f,1.0f, 120.0f);
      }
    }
   End();
}
}
