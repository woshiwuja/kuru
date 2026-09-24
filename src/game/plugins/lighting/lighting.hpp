#pragma once
#include <Kuru.h>
#include "entt/entity/fwd.hpp"
#include <entt/entt.hpp>

using namespace KR;

struct DirectionalLight {
  glm::vec3 direction = glm::normalize(glm::vec3(-1.0f, 0.4f, 0.05f));
  glm::vec3 color = {1.7f, 1.5f, 1.2f};
};

struct LightingPlugin : public Plugin {
  void init(entt::registry &reg) override;
  void update(entt::registry &reg) override;
  void UI(entt::registry &reg);
};
