#pragma once
#include "entt/entity/fwd.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "imgui.h"
#include "../../lib/plugin/plugin.hpp"


struct Transform {
	glm::vec3 position = {0.0f, 0.0f, 0.0f};
	glm::quat rotation = {1.0f,0.0f, 0.0f, 0.0f};
	glm::vec3 scale    = {1.0f, 1.0f, 1.0f};
	glm::mat4 matrix() const;
};

inline bool dragRotation(const char *label, glm::quat &rotation) {
  glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
  if (!ImGui::DragFloat3(label, &euler.x, 1.0f)) return false;
  rotation = glm::quat(glm::radians(euler));
  return true;
}

struct GlobalTransform: public Transform{};
struct LocalTransform: public Transform{};


struct TransformPlugin : public Plugin {
    void init(entt::registry &r) override {
        r.ctx().emplace<GlobalTransform>();
        r.ctx().emplace<LocalTransform>();
    };
    void update(entt::registry &r) override {
        UI(r);
    };
    void UI(entt::registry &r){
        if (ImGui::Begin("Transforms")) {
          for (auto [entity, transform] : r.view<Transform>().each()) {
            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
            if (ImGui::CollapsingHeader(
                    ("entity " + std::to_string(entt::to_integral(entity)))
                        .c_str())) {
              ImGui::DragFloat3("position", &transform.position.x, 5.0f);
              dragRotation("rotation", transform.rotation);
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
};
