#pragma once
#include <Kuru.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "character.hpp"
#include "glm/ext/vector_float3.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h" // ImGui::InputText overload for std::string
#include "plugins.hpp"
#include "physics.hpp"
#include "render.hpp"
#include <format>
#include <string>

using namespace KR;

struct Name {
  std::string text;
};
struct DefaultPlugin : public Plugin {
  void init(entt::registry &reg) override {
    entt::entity e = reg.create();
    reg.emplace<Character>(e);
    reg.emplace<FirstName>(e, "coglione");
    reg.emplace<LastName>(e, "culone");
    auto &t = reg.emplace<Transform>(e);
    t.position = glm::vec3{0, 4000, 0};
    t.rotation = glm::quat(glm::vec3{0.7, 0.7, 0}); // da euler radianti
    t.scale = glm::vec3{1, 1, 1};
    using namespace JPH::literals;
    auto &bodySettings = reg.emplace<JPH::BodyCreationSettings>(
        e, new JPH::SphereShape(1.0f), JPH::RVec3(t.position.x,t.position.y,t.position.z),
        JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, MOVING);
    // Spawns 1500+ units above the terrain and free-falls into it; by impact
    // it's moving fast enough that discrete collision can miss the heightfield
    // between steps (tunnels through). LinearCast sweeps the shape instead.
    bodySettings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    spawn(reg, e, "models/spongebob.glb", "");

    entt::entity mapEnt = reg.create();
    reg.emplace<Character>(mapEnt);
    auto &mapT = reg.emplace<Transform>(mapEnt);
    mapT.position = glm::vec3{0, 2000, 0};
    mapT.rotation = glm::vec3{0.0, 0.0, 0.0};
    mapT.scale = glm::vec3{1, 1, 1};
    //spawn(reg, mapEnt, "models/sanctuary.glb", "");
  };
  void update(entt::registry &reg) override {
    using namespace ImGui;
    ImGuiStorage *state = GetStateStorage();
    Begin("Characters");
    for (auto [e, first_name, last_name] : reg.view<Character, FirstName, LastName>().each()) {
      Text("%s %s", first_name.c_str(), last_name.c_str());
      PushID(static_cast<int>(entt::to_integral(e)));
      auto openId = GetID("inspector open");
      bool open = state->GetBool(openId, false);
      if (Button("Inspect")) {
        open = true;
      }
      if (open) {
        Begin(std::format("{} details###details{}", first_name.c_str(),
                          entt::to_integral(e))
                  .c_str(),
              &open);
        ImGui::InputText("Name", &first_name);
        auto t = reg.try_get<Transform>(e);
        if (t != nullptr) {
          DragFloat3("Position", &t->position.x, 5.0f);
          dragRotation("Rotation", t->rotation);
          DragFloat3("Scale", &t->scale.x, 0.01f, 0.001f, 1000.0f);
        }
        End();
      }
      state->SetBool(openId, open);
      PopID();
    }
    End();
  }
};
