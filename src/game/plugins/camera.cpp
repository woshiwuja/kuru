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

void CameraPlugin::init(entt::registry &reg) {
  reg.emplace<Camera>(reg.create());
}

void CameraPlugin::run(entt::registry &reg) {
  auto &frame = reg.ctx().get<FrameContext>();
  const auto *core = Core::get();
  const auto &input = *core->eventManager;
  const float deltaTime = core->deltaTime;
  // ponytail: first camera wins. Add an Active tag the day there are two.
  auto view = reg.view<Camera>();
  if (view.begin() == view.end()) {
    return;
  }
  Camera &camera = view.get<Camera>(*view.begin());

  // Look: Q/E on the keyboard, middle drag with the mouse.
  if (input.down(SDL_SCANCODE_Q)) {
    camera.yaw -= camera.turnSpeed * deltaTime;
  }
  if (input.down(SDL_SCANCODE_E)) {
    camera.yaw += camera.turnSpeed * deltaTime;
  }
  if (input.middleDown) {
    camera.yaw += input.mouseDeltaX * camera.lookSensitivity;
    camera.pitch -= input.mouseDeltaY * camera.lookSensitivity;
  }
  // Straight up or down would collapse the flattened basis below, and it looks
  // wrong anyway.
  camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);

  const glm::vec3 front = camera.front();
  // Movement stays on the ground plane: looking down must not sink the camera.
  const glm::vec3 flatFront =
      glm::normalize(glm::vec3{front.x, 0.0f, front.z});
  const glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, WORLD_UP));

  const float step = camera.moveSpeed * deltaTime;
  if (input.down(SDL_SCANCODE_W)) camera.position += flatFront * step;
  if (input.down(SDL_SCANCODE_S)) camera.position -= flatFront * step;
  if (input.down(SDL_SCANCODE_D)) camera.position += flatRight * step;
  if (input.down(SDL_SCANCODE_A)) camera.position -= flatRight * step;

  // Zoom dollies along the real view direction, so it climbs and dives.
  camera.position += front * (input.wheel * camera.zoomSpeed);

  frame.view = glm::lookAt(camera.position, camera.position + front, WORLD_UP);
  frame.proj = glm::perspective(
      glm::radians(camera.fov),
      static_cast<float>(frame.extent.width) /
          static_cast<float>(frame.extent.height),
      camera.nearPlane, camera.farPlane);
  frame.proj[1][1] *= -1; // glm is GL-handed, Vulkan's Y points the other way
}
