#pragma once
#include "../../lib/plugin/plugin.hpp"
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "glm/ext/vector_float3.hpp"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h" // ImGui::InputText overload for std::string
#include "plugins.hpp"
#include "physics.hpp"
#include "render.hpp"
#include <format>
#include <string>

struct Character {};
struct Name {
  std::string text;
};
struct DefaultPlugin : public Plugin {
  void init(entt::registry &reg) override {
    entt::entity e = reg.create();
    reg.emplace<Character>(e);
    reg.emplace<Name>(e).text =
        std::format("Insectman {}", entt::to_integral(e));
    auto &t = reg.emplace<Transform>(e);
    t.position = glm::vec3{0, 0, 0};
    t.rotation = glm::quat(glm::vec3{0.7, 0.7, 0}); // da euler radianti
    t.scale = glm::vec3{1, 1, 1};
    // _r vive in JPH::literals, opt-in apposta (Jolt/Math/Real.h:37)
    using namespace JPH::literals;
    // Il componente È la richiesta di spawn: PhysicsPlugin lo trasforma in un
    // corpo e poi lo cancella. Da locale non sarebbe mai arrivato a Jolt.
    reg.emplace<JPH::BodyCreationSettings>(
        e, new JPH::SphereShape(0.5f), JPH::RVec3(0.0_r, 2.0_r, 0.0_r),
        JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, kr::MOVING);
    // spawn(reg, e, "models/insectman.glb", "");

    entt::entity mapEnt = reg.create();
    reg.emplace<Character>(mapEnt);
    reg.emplace<Name>(mapEnt).text =
        std::format("Testmap {}", entt::to_integral(e));
    auto &mapT = reg.emplace<Transform>(mapEnt);
    mapT.position = glm::vec3{0, 0, 0};
    mapT.rotation = glm::vec3{0.0, 0.0, 0.0};
    mapT.scale = glm::vec3{1, 1, 1};
    //spawn(reg, mapEnt, "models/sanctuary.glb", "");
  };
  void update(entt::registry &reg) override {
    using namespace ImGui;
    // Per-entity "is the details window open" flag, keyed by ImGui ID instead
    // of living on a component: PushID(entity) scopes GetID("inspector open")
    // to that entity, and GetStateStorage() is ImGui's own persistent-by-ID
    // storage, so the flag survives across frames without us owning it.
    ImGuiStorage *state = GetStateStorage();
    Begin("Characters");
    for (auto [e, name] : reg.view<Name>().each()) {
      Text("%s", name.text.c_str());
      PushID(static_cast<int>(entt::to_integral(e)));
      auto openId = GetID("inspector open");
      bool open = state->GetBool(openId, false);
      if (Button("Inspect")) {
        open = true;
      }
      if (open) {
        Begin(std::format("{} details###details{}", name.text,
                          entt::to_integral(e))
                  .c_str(),
              &open);
        ImGui::InputText("Name", &name.text);
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
