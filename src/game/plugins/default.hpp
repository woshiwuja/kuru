#pragma once
#include <Kuru.h>
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "entt/entity/entity.hpp"
#include "entt/entity/fwd.hpp"
#include "character/character.hpp"
#include "render/render.hpp"
#include "character/stats.hpp"
#include "transform/transform.hpp"
#include "picking/picking.hpp"
#include <format>
#include <string>

using namespace KR;

// The entity the inspector is looking at.

struct DefaultPlugin : public Plugin {
  void init(entt::registry &reg) override {

    auto light =reg.create();
    reg.emplace<DirectionalLight>(light);
    reg.emplace<Transform>(light);
    entt::entity e = reg.create();
    reg.emplace<Character>(e);
    reg.emplace<Name>(e, Name{.fname="coglione"});
    auto &t = reg.emplace<Transform>(e);
    t.position = {0, 100, 0};
    t.rotation = glm::quat(glm::vec3{0.7, 0.7, 0}); // da euler radianti
    t.scale = glm::vec3{1, 1, 1};
    using namespace JPH::literals;
    auto &bodySettings = reg.emplace<JPH::BodyCreationSettings>(
        e, new JPH::CapsuleShape(1.0f, 0.5f), JPH::RVec3(t.position.x,t.position.y,t.position.z),
        JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, MOVING);
    bodySettings.mMotionQuality = JPH::EMotionQuality::LinearCast;
    spawn(reg, e, "models/spongebob.glb", "");
    reg.emplace<Pickable>(e);
    for (auto i = 0; i<=100; i++ ){
        for (auto j = 0; j<=100; j++ ){
        auto prop = reg.create();
        spawnProp(reg, prop, "primitive:cube", "",
                  Transform{.position = {3.0+i+.5, 20, 3.0+j+.5},.scale= {1,1,1}});
        reg.emplace<Pickable>(prop);
        }
    }
  };
  void update(entt::registry &reg) override {}
};
