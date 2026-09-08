#pragma once
#include "../../lib/core/core.hpp"
#include "../../lib/plugin/plugin.hpp"
#include "Jolt/Math/Vec3.h"
#include "glm/ext/vector_float3.hpp"
#include "transform.hpp"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyID.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "Jolt/Physics/EActivation.h"
#include "entt/entity/fwd.hpp"
#include "imgui.h"

JPH::Vec3 glmVecToJPH(glm::vec3 v){
    return JPH::Vec3(v.x,v.y,v.z);
}
struct PhysicsPlugin : public Plugin {

  glm::vec3 gravity{0, -9.81f, 0};
  void init(entt::registry &reg) override {
    auto &s = Core::get()->physicsManager.get()->system;
    auto &b = Core::get()->physicsManager.get()->bodies();
    s.SetGravity(glmVecToJPH(gravity));
  }
  void update(entt::registry &reg) override {
    auto *physics = Core::get()->physicsManager.get();
    auto &bodies = physics->bodies();

    for (auto [e, settings] : reg.view<JPH::BodyCreationSettings>().each()) {
      reg.emplace<JPH::BodyID>(
          e, bodies.CreateAndAddBody(settings, JPH::EActivation::Activate));
      reg.erase<JPH::BodyCreationSettings>(e);
    }
    physics->update(Core::get()->deltaTime);
    for (auto [e, t, id] : reg.view<Transform, JPH::BodyID>().each()) {
      if (!bodies.IsActive(id))
        continue;
      const JPH::RVec3 p = bodies.GetPosition(id);
      const JPH::Quat q = bodies.GetRotation(id);
      t.position = {p.GetX(), p.GetY(), p.GetZ()};
      t.rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }
    UI();
  }
  void UI() {
    using namespace ImGui;
    auto &s = Core::get()->physicsManager.get()->system;
    Begin("Physics");
    if(DragFloat3("Gravity", &gravity.x,0.1,-100,100)){
        s.SetGravity(glmVecToJPH(gravity));
    }
    End();
  }
};
