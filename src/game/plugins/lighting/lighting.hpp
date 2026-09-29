#pragma once
#include <Kuru.h>
#include "entt/entity/fwd.hpp"
#include <entt/entt.hpp>

using namespace KR;

constexpr uint32_t MAX_LIGHTS = 4;
struct DirectionalLight {
  // xyz: direction. w unused.
  glm::vec4 direction{glm::normalize(glm::vec3(-1.0f, 0.4f, 0.05f)), 0.0f};
  glm::vec4 color{1.7f, 1.5f, 1.2f, 0.0f};
};
struct PointLight {
  // xyz: world position. w: radius, the distance the light fades out by.
  glm::vec4 position{0.0f, 0.0f, 0.0f, 1.0f};
  glm::vec4 color{1.7f, 1.5f, 1.2f, 0.0f};
};
struct SpotLight {
  // xyz: world position. w: near_radius, the full-bright inner cone.
  glm::vec4 position{0.0f, 0.0f, 0.0f, 1.0f};
  // xyz: beam direction. w: far_radius, the outer cone it fades out by.
  glm::vec4 direction{glm::normalize(glm::vec3(-1.0f, 0.4f, 0.05f)), 10.0f};
  glm::vec4 color{1.7f, 1.5f, 1.2f, 0.0f};
};

struct LightingPlugin : public Plugin {
  void init(entt::registry &reg) override;
  void update(entt::registry &reg) override;
  void UI(entt::registry &reg);
};
