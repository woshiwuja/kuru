#pragma once
#include "common/material.hpp"
#include <Kuru.h>
using namespace KR;

void materialEditor(Material &m);
struct MaterialRef {
  std::shared_ptr<Material> material = std::make_shared<Material>();
  glm::vec4 params{0.0f};
};
struct MaterialPlugin : public Plugin{
    MaterialPlugin() {
        registerComponent<MaterialRef>();
        registerComponent<Material>();
    }
    void update(entt::registry &r ) override;
    void UI(Material &m);
};
