#include <Kuru.h>
#include "lighting.hpp"
#include "imgui.h"
#include <format>
#include "../transform/transform.hpp"
#include <glm/gtx/quaternion.hpp> // glm::rotation

using namespace KR;

LightingPlugin::LightingPlugin(){
   registerComponent<DirectionalLight>();
   registerComponent<PointLight>();
   registerComponent<SpotLight>();
}
void LightingPlugin::init(entt::registry &reg) {
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
  syncToTransform(reg);
  UI(reg);
}

// A light with a Transform takes its position and direction from it. The
// unrotated light points along local +Y; a point light's radius is scale.x.
static const glm::vec3 UP{0.0f, 1.0f, 0.0f};

void syncToTransform(entt::registry &reg){
  for (auto [e, light, t] : reg.view<DirectionalLight, Transform>().each()) {
    light.direction = glm::vec4(t.rotation * UP, 0.0f);
  };
  for (auto [e, light, t] : reg.view<SpotLight, Transform>().each()) {
    // w (near/far radius) stays the light's own.
    light.direction = glm::vec4(t.rotation * UP, light.direction.w);
    light.position = glm::vec4(glm::vec3(t.position), light.position.w);
  };
  for (auto [e, light, t] : reg.view<PointLight, Transform>().each()) {
    light.position = glm::vec4(glm::vec3(t.position), t.scale.x);
  };
}

void LightingPlugin::UI(entt::registry &reg) {

  using namespace ImGui;
  Begin("Lights");

  for (auto [e, light,t ] : reg.view<DirectionalLight, Transform>().each()) {
    PushID(std::format("Light {}", entt::to_integral(e)).c_str());
    Text("Directional Light %d", entt::to_integral(e));
    dragRotation("rotation", t.rotation);
    ColorEdit3("color", &light.color.x,
               ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    if(Button("X")){
      reg.destroy(e);
    }
    PopID();
  }
  for (auto [e, light,t] : reg.view<PointLight, Transform>().each()) {
    PushID(std::format("Point {}", entt::to_integral(e)).c_str());
    Text("point");
    dragVec3("position", t.position, 0.1f);
    DragFloat("radius", &t.scale.x, 0.1f, 0.01f, 1000.0f);
    ColorEdit3("color", &light.color.x,
               ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    DragFloat("intensity", &light.color.w, 0.5f, 0.0f, 10000.0f);
    if(Button("X")){
      reg.destroy(e);
    }
    PopID();
  }
  for (auto [e, light,t] : reg.view<SpotLight,Transform>().each()) {
    PushID(std::format("Spot {}", entt::to_integral(e)).c_str());
    Text("spot");
    dragVec3("position", t.position, 0.1f);
    dragRotation("rotation", t.rotation);
    DragFloat("near_radius", &light.position.w, 0.05f, 0.01f, light.direction.w);
    DragFloat("far_radius", &light.direction.w, 0.05f, light.position.w, 100.0f);
    ColorEdit3("color", &light.color.x,
               ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    DragFloat("intensity", &light.color.w, 0.5f, 0.0f, 10000.0f);
    if(Button("X")){
      reg.destroy(e);
    }
    PopID();
  }

  if (Button("Add directional")) {
    auto dir = reg.create();
    auto &light = reg.emplace<DirectionalLight>(dir);
    reg.emplace<Transform>(dir, Transform{
        .rotation = glm::rotation(UP, glm::vec3(light.direction))});
    reg.emplace<Selected>(dir);
  }
  SameLine();
  if (Button("Add point")) {
    auto point = reg.create();
    auto &light = reg.emplace<PointLight>(point);
    reg.emplace<Transform>(point, Transform{
        .position = {light.position.x, light.position.y, light.position.z},
        .scale = glm::vec3(light.position.w)});
    reg.emplace<Selected>(point);
  }
  SameLine();
  if (Button("Add spot")) {
    auto spot = reg.create();
    auto &light = reg.emplace<SpotLight>(spot);
    reg.emplace<Transform>(spot, Transform{
        .position = {light.position.x, light.position.y, light.position.z},
        .rotation = glm::rotation(UP, glm::vec3(light.direction))});
    reg.emplace<Selected>(spot);
  }
  End();
}
