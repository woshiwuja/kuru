#pragma once
#include <Kuru.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "character/character.hpp"
#include "plugins.hpp"
#include "character/stats.hpp"
#include "transform/transform.hpp"
#include <format>
#include <string>

using namespace KR;

// The entity the inspector is looking at.
struct Selected {};

struct DefaultPlugin : public Plugin {
  void init(entt::registry &reg) override {
    entt::entity e = reg.create();
    reg.emplace<Character>(e);
    reg.emplace<Name>(e, Name{.fname="coglione"});
    reg.emplace<Strenght>(e,Stat{.level = 1, .currentExperience = 0,  .experienceToNext = calculateRequiredXP(1, 2)});
    reg.emplace<Vitality>(e,Stat{.level = 1, .currentExperience = 0,  .experienceToNext = calculateRequiredXP(1, 2)});
    auto &t = reg.emplace<Transform>(e);
    t.position = {0, 100, 0};
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
    auto &mapT = reg.emplace<Transform>(mapEnt, Transform{
        .position = {0, 2000, 0},
        .rotation = glm::vec3{0.0, 0.0, 0.0},
        .scale = glm::vec3{1, 1, 1},
    });
    reg.emplace<Name>(mapEnt,Name{.fname = "ASS"});
  };
  void update(entt::registry &reg) override {
    using namespace ImGui;
    ImGuiStorage *state = GetStateStorage();
    Begin("Characters");
    for (auto [e, name] : reg.view<Name>().each()) {
      Text("%s %s", name.fname.c_str(), name.lname.c_str());
      PushID(static_cast<int>(entt::to_integral(e)));
      auto openId = GetID("inspector open");
      bool open = state->GetBool(openId, false);
      if (Button("Inspect")) {
        open = true;
      }
      if (open) {
        Begin(std::format("{} details###details{}", name.fname.c_str(),
                          entt::to_integral(e))
                  .c_str(),
              &open);
        InputText("Name", &name.fname);
        auto t = reg.try_get<Transform>(e);
        if (t != nullptr) {
          dragVec3("Position", t->position, 5.0f);
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
