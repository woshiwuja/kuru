#pragma once
#include <Kuru.h>
#include <Jolt/Jolt.h>
#include "../transform/transform.hpp"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyID.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "Jolt/Physics/Collision/Shape/ScaledShape.h"
#include "Jolt/Physics/EActivation.h"
#include "entt/entity/fwd.hpp"
#include "imgui.h"

using namespace KR;

// Position/rotation live on the body, so a changed Transform is a teleport.
// Only written when it actually differs: this runs every frame for every
// inactive body and each call costs a broadphase update.
inline void syncBodyToTransform(JPH::BodyInterface &bodies, JPH::BodyID id,
                                const Transform &t) {
  const JPH::RVec3 p(t.position.x, t.position.y, t.position.z);
  const glm::quat r = glm::normalize(t.rotation); // SetPositionAndRotation asserts on this
  const JPH::Quat q(r.x, r.y, r.z, r.w);
  if (p == bodies.GetPosition(id) && q == bodies.GetRotation(id))
    return;
  bodies.SetPositionAndRotation(id, p, q, JPH::EActivation::Activate);
}

// Scale isn't a body property in Jolt - it lives on the shape, so a changed
// Transform.scale means swapping in a rescaled copy of the shape.
inline void syncShapeScale(JPH::BodyInterface &bodies, JPH::BodyID id,
                           glm::vec3 scale) {
  JPH::RefConst<JPH::Shape> shape = bodies.GetShape(id);
  JPH::Vec3 current = JPH::Vec3::sOne();
  if (shape->GetSubType() == JPH::EShapeSubType::Scaled) {
    const auto *scaled = static_cast<const JPH::ScaledShape *>(shape.GetPtr());
    current = scaled->GetScale();
    shape = scaled->GetInnerShape(); // rescale the original, don't nest wrappers
  }
  const JPH::Vec3 wanted = glmVecToJPH(scale);
  if (current.IsClose(wanted))
    return;
  // Returns the inner shape unwrapped at scale 1, a ScaledShape otherwise.
  JPH::Shape::ShapeResult result = shape->ScaleShape(wanted);
  if (result.HasError())
    return; // scale this shape can't represent (mirrored, zero, ...)
  bodies.SetShape(id, result.Get(), true, JPH::EActivation::Activate);
  { JPH::AABox b = bodies.GetShape(id)->GetLocalBounds();
    printf("SCALECHECK body=%u scale=%.2f bounds=(%.1f %.1f %.1f)-(%.1f %.1f %.1f)\n",
      id.GetIndex(), scale.x, b.mMin.GetX(), b.mMin.GetY(), b.mMin.GetZ(),
      b.mMax.GetX(), b.mMax.GetY(), b.mMax.GetZ()); fflush(stdout); }
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
      if (bodies.IsActive(id)) {
        const JPH::RVec3 p = bodies.GetPosition(id);
        const JPH::Quat q = bodies.GetRotation(id);
        t.position = {p.GetX(), p.GetY(), p.GetZ()};
        t.rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
      } else {
        // Nobody is simulating this body, so the Transform owns it and edits
        // (inspector drags, save loads) have to go the other way. Static
        // bodies like the map never activate, so this is their only path.
        syncBodyToTransform(bodies, id, t);
      }
      syncShapeScale(bodies, id, t.scale);
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
