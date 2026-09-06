#pragma once
#include "../../lib/plugin/plugin.hpp"
#include "entt/entity/fwd.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

struct DirectionalLight {
	glm::vec3 direction = glm::normalize(glm::vec3(-1.0f, 0.4f, 0.05f));
	glm::vec3 color     = {1.7f, 1.5f, 1.2f};
};

struct LightingPlugin : public Plugin {
	void init(entt::registry &reg) override;
	void update(entt::registry &reg) override {
 if (ImGui::Begin("Lights")) {
    for (auto [e, light] : reg.view<DirectionalLight>().each()) {
      if (ImGui::DragFloat3("direction", &light.direction.x, 0.01f, -1.0f, 1.0f)) {
        light.direction = glm::normalize(light.direction);
      }
      ImGui::ColorEdit3("color", &light.color.x,
                        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    }
  }
  ImGui::End();
	};
};
