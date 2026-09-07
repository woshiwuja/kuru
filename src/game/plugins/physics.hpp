#pragma once
#include "../../lib/core/core.hpp"
#include "../../lib/plugin/plugin.hpp"
#include "transform.hpp"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyID.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "Jolt/Physics/EActivation.h"
#include "entt/entity/fwd.hpp"

// The bridge between the ECS and the Jolt world, which lives in lib/physics.
struct PhysicsPlugin : public Plugin {
  void update(entt::registry &reg) override {
    auto *physics = Core::get()->physicsManager.get();
    auto &bodies = physics->bodies();

    // An entity carrying BodyCreationSettings is a spawn request: turn it into
    // a body once, then drop the request. Erasing the component of the entity
    // being iterated is allowed, and `settings` is read before it happens.
    for (auto [e, settings] : reg.view<JPH::BodyCreationSettings>().each()) {
      reg.emplace<JPH::BodyID>(
          e, bodies.CreateAndAddBody(settings, JPH::EActivation::Activate));
      reg.erase<JPH::BodyCreationSettings>(e);
    }

    physics->update(Core::get()->deltaTime);

    // Jolt owns the pose of anything that has a body, so it overwrites
    // Transform rather than reading it. Registered before RenderPlugin so the
    // new poses land in the same frame.
    for (auto [e, t, id] : reg.view<Transform, JPH::BodyID>().each()) {
      if (!bodies.IsActive(id)) continue; // dorme o è statico: posa invariata
      const JPH::RVec3 p = bodies.GetPosition(id);
      const JPH::Quat q = bodies.GetRotation(id);
      t.position = {p.GetX(), p.GetY(), p.GetZ()};
      // glm::quat takes w first, JPH::Quat stores x, y, z, w
      t.rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
    }
  }
};
