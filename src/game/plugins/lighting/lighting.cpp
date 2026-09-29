#include <Kuru.h>
#include "lighting.hpp"
#include "imgui.h"
#include <format>

using namespace KR;

void LightingPlugin::init(entt::registry &reg) {
   registerComponent<DirectionalLight>();
   registerComponent<PointLight>();
   registerComponent<SpotLight>();
}

void LightingPlugin::update(entt::registry &reg) {
  for (auto [e, light] : reg.view<DirectionalLight>().each()) {
    light.direction = glm::vec4(glm::normalize(glm::vec3(light.direction)), 0.0f);
  };
  for (auto [e, light] : reg.view<SpotLight>().each()) {
    // w carries far_radius, so only xyz gets normalized.
    light.direction =
        glm::vec4(glm::normalize(glm::vec3(light.direction)), light.direction.w);
  };

  UI(reg);
}

void LightingPlugin::UI(entt::registry &reg) {
  using namespace ImGui;
  Begin("Lights");

  for (auto [e, light] : reg.view<DirectionalLight>().each()) {
    PushID(std::format("Light {}", entt::to_integral(e)).c_str());
    Text("directional");
    if (DragFloat3("direction", &light.direction.x, 0.01f, -1.0f, 1.0f)) {
      light.direction = glm::vec4(glm::normalize(glm::vec3(light.direction)), 0.0f);
    }
    ColorEdit3("color", &light.color.x,
               ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

    if(Button("X")){
      reg.destroy(e);
    }
    PopID();
  }
  for (auto [e, light] : reg.view<PointLight>().each()) {
    PushID(std::format("Point {}", entt::to_integral(e)).c_str());
    Text("point");
    DragFloat3("position", &light.position.x, 0.1f);
    DragFloat("radius", &light.position.w, 0.1f, 0.01f, 1000.0f);
    ColorEdit3("color", &light.color.x,
               ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    if(Button("X")){
      reg.destroy(e);
    }
    PopID();
  }
  for (auto [e, light] : reg.view<SpotLight>().each()) {
    PushID(std::format("Spot {}", entt::to_integral(e)).c_str());
    Text("spot");
    DragFloat3("position", &light.position.x, 0.1f);
    if (DragFloat3("direction", &light.direction.x, 0.01f, -1.0f, 1.0f)) {
      light.direction =
          glm::vec4(glm::normalize(glm::vec3(light.direction)), light.direction.w);
    }
    DragFloat("near_radius", &light.position.w, 0.05f, 0.01f, light.direction.w);
    DragFloat("far_radius", &light.direction.w, 0.05f, light.position.w, 100.0f);
    ColorEdit3("color", &light.color.x,
               ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    if(Button("X")){
      reg.destroy(e);
    }
    PopID();
  }

  if (Button("Add directional")) {
    reg.emplace<DirectionalLight>(reg.create());
  }
  SameLine();
  if (Button("Add point")) {
    reg.emplace<PointLight>(reg.create());
  }
  SameLine();
  if (Button("Add spot")) {
    reg.emplace<SpotLight>(reg.create());
  }
  End();
}
