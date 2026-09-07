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
    };
};
