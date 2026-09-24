#include "lighting.hpp"
#include "imgui.h"
#include <format>

using namespace KR;

void LightingPlugin::init(entt::registry &reg) {
  reg.emplace<DirectionalLight>(reg.create());
}

void LightingPlugin::update(entt::registry &reg) {
  auto lightView = reg.view<DirectionalLight>();
  for (auto [e, light] : lightView.each()) {
    light.direction = glm::normalize(light.direction);
  };
  UI(reg);
}
void LightingPlugin::UI(entt::registry &reg) {
  using namespace ImGui;
  Begin("Lights");
  for (auto [e, light] : reg.view<DirectionalLight>().each()) {
    PushID(std::format("Light {}", entt::to_integral(e)).c_str());
    if (DragFloat3("direction", &light.direction.x, 0.01f, -1.0f, 1.0f)) {
      light.direction = glm::normalize(light.direction);
    }
    ColorEdit3("color", &light.color.x,
               ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    PopID();
  }
  if (Button("Add light")) {
    auto new_light = reg.create();
    reg.emplace<DirectionalLight>(new_light);
  }
  End();
}
