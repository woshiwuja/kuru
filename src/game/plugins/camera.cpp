#include "camera.hpp"
#include "../../lib/core/core.hpp"
#include <glm/gtc/matrix_transform.hpp>

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
  reg.emplace<Camera>(reg.create());
}

void CameraPlugin::update(entt::registry &reg) {
  auto &frame = reg.ctx().get<FrameContext>();
  const auto *core = Core::get();
  const auto &input = *core->eventManager;
  const float deltaTime = core->deltaTime;

  auto view = reg.view<Camera>();
  if (view.begin() == view.end()) {
    return;
  }
  Camera &camera = view.get<Camera>(*view.begin());

  if (input.middleDown && !frame.uiCapturesMouse) {
    camera.yaw += input.mouseDeltaX * camera.orbitSensitivity;
    camera.pitch -= input.mouseDeltaY * camera.orbitSensitivity;
  }
  const bool keyboardFree = !frame.uiCapturesKeyboard;
  if (keyboardFree && input.down(SDL_SCANCODE_Q)) {
    camera.yaw -= camera.turnSpeed * deltaTime;
  }
  if (keyboardFree && input.down(SDL_SCANCODE_E)) {
    camera.yaw += camera.turnSpeed * deltaTime;
  }
  camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);

  if (!frame.uiCapturesMouse) {
    camera.distance *= 1.0f - input.wheel * camera.zoomSpeed;
  }
  camera.distance =
      std::clamp(camera.distance, camera.minDistance, camera.maxDistance);

  // Pan slides the pivot across the ground plane, and the camera rides along.
  const glm::vec3 front = camera.front();
  const glm::vec3 flatFront =
      glm::normalize(glm::vec3{front.x, 0.0f, front.z});
  const glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, WORLD_UP));

  const float step = camera.panSpeed * deltaTime;
  if (keyboardFree && input.down(SDL_SCANCODE_W)) camera.pivot += flatFront * step;
  if (keyboardFree && input.down(SDL_SCANCODE_S)) camera.pivot -= flatFront * step;
  if (keyboardFree && input.down(SDL_SCANCODE_D)) camera.pivot += flatRight * step;
  if (keyboardFree && input.down(SDL_SCANCODE_A)) camera.pivot -= flatRight * step;
  camera.pivot.y = camera.groundHeight;

  frame.view = glm::lookAt(camera.position(), camera.pivot, WORLD_UP);
  frame.proj = glm::perspective(
      glm::radians(camera.fov),
      static_cast<float>(frame.extent.width) /
          static_cast<float>(frame.extent.height),
      camera.nearPlane, camera.farPlane);
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
}
