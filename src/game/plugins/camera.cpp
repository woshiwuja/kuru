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

  // Orbit. Screen space drives it directly: horizontal drag swings around the
  // pivot, vertical drag raises and lowers the eye over it.
  if (input.middleDown) {
    camera.yaw += input.mouseDeltaX * camera.orbitSensitivity;
    camera.pitch -= input.mouseDeltaY * camera.orbitSensitivity;
  }
  if (input.down(SDL_SCANCODE_Q)) {
    camera.yaw -= camera.turnSpeed * deltaTime;
  }
  if (input.down(SDL_SCANCODE_E)) {
    camera.yaw += camera.turnSpeed * deltaTime;
  }
  // Straight up or down would collapse the flattened basis below, and the
  // orbit would gimbal.
  camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);

  // Zoom is the orbit radius, scaled by itself so it stays usable close in and
  // far out alike.
  camera.distance *= 1.0f - input.wheel * camera.zoomSpeed;
  camera.distance =
      std::clamp(camera.distance, camera.minDistance, camera.maxDistance);

  // Pan slides the pivot across the ground plane, and the camera rides along.
  const glm::vec3 front = camera.front();
  const glm::vec3 flatFront =
      glm::normalize(glm::vec3{front.x, 0.0f, front.z});
  const glm::vec3 flatRight = glm::normalize(glm::cross(flatFront, WORLD_UP));

  const float step = camera.panSpeed * deltaTime;
  if (input.down(SDL_SCANCODE_W)) camera.pivot += flatFront * step;
  if (input.down(SDL_SCANCODE_S)) camera.pivot -= flatFront * step;
  if (input.down(SDL_SCANCODE_D)) camera.pivot += flatRight * step;
  if (input.down(SDL_SCANCODE_A)) camera.pivot -= flatRight * step;
  camera.pivot.y = camera.groundHeight;

  frame.view = glm::lookAt(camera.position(), camera.pivot, WORLD_UP);
  frame.proj = glm::perspective(
      glm::radians(camera.fov),
      static_cast<float>(frame.extent.width) /
          static_cast<float>(frame.extent.height),
      camera.nearPlane, camera.farPlane);
  frame.proj[1][1] *= -1; // glm is GL-handed, Vulkan's Y points the other way
}
