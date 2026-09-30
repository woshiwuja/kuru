#pragma once
#include <Kuru.h>
#include <Jolt/Jolt.h>
#include "../transform/transform.hpp"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/BodyID.h"
#include "Jolt/Physics/Body/BodyInterface.h"
#include "Jolt/Physics/Body/BodyLock.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/CylinderShape.h"
#include "Jolt/Physics/Collision/Shape/ScaledShape.h"
#include "Jolt/Physics/EActivation.h"
#include "Jolt/Physics/SoftBody/SoftBodyCreationSettings.h"
#include "entt/entity/fwd.hpp"
#include "imgui.h"
#include "plugin/plugin.hpp"

using namespace KR;

// Position/rotation live on the body, so a changed Transform is a teleport.
// Only written when it actually differs: this runs every frame for every
// inactive body and each call costs a broadphase update.
inline void syncBodyToTransform(JPH::BodyInterface &bodies, JPH::BodyID id,
                                const Transform &t) {
  const JPH::RVec3 p(t.position.x, t.position.y, t.position.z);
  const glm::quat r =
      glm::normalize(t.rotation); // SetPositionAndRotation asserts on this
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
    shape =
        scaled->GetInnerShape(); // rescale the original, don't nest wrappers
  }
  const JPH::Vec3 wanted = glmVecToJPH(scale);
  if (current.IsClose(wanted))
    return;
  // Returns the inner shape unwrapped at scale 1, a ScaledShape otherwise.
  JPH::Shape::ShapeResult result = shape->ScaleShape(wanted);
  if (result.HasError())
    return; // scale this shape can't represent (mirrored, zero, ...)
  bodies.SetShape(id, result.Get(), true, JPH::EActivation::Activate);
  {
    JPH::AABox b = bodies.GetShape(id)->GetLocalBounds();
    printf("SCALECHECK body=%u scale=%.2f bounds=(%.1f %.1f %.1f)-(%.1f %.1f "
           "%.1f)\n",
           id.GetIndex(), scale.x, b.mMin.GetX(), b.mMin.GetY(), b.mMin.GetZ(),
           b.mMax.GetX(), b.mMax.GetY(), b.mMax.GetZ());
    fflush(stdout);
  }
}

struct PhysicsPlugin : public Plugin {

  glm::vec3 gravity{0, -9.81f, 0};
  void init(entt::registry &reg) override {
    auto &s = Core::get()->physicsManager.get()->system;
    auto &b = Core::get()->physicsManager.get()->bodies();
    s.SetGravity(glmVecToJPH(gravity));
    registerComponent<JPH::BodyCreationSettings>();}
  void update(entt::registry &reg) override {
    auto *physics = Core::get()->physicsManager.get();
    auto &bodies = physics->bodies();

    for (auto [e, settings] : reg.view<JPH::BodyCreationSettings>().each()) {
      // Inspector "add" default-constructs these: no shape, and Jolt derefs
      // the null shape computing mass. Give it one; the inspector can swap it.
      if (!settings.GetShape())
        settings.SetShape(new JPH::SphereShape(1.0f));
      // Re-adding settings to an entity that already has a body replaces it.
      if (auto *old = reg.try_get<JPH::BodyID>(e)) {
        bodies.RemoveBody(*old);
        bodies.DestroyBody(*old);
      }
      reg.emplace_or_replace<JPH::BodyID>(
          e, bodies.CreateAndAddBody(settings, JPH::EActivation::Activate));
      reg.erase<JPH::BodyCreationSettings>(e);
    }
    if (!Core::get()->paused) {
      physics->update(Core::get()->deltaTime());
    }
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
    UI(reg);
  }
  void UI(entt::registry &r) {
    using namespace ImGui;
    auto &s = Core::get()->physicsManager.get()->system;
    auto &b = Core::get()->physicsManager.get()->bodies();
    Begin("Physics");
    if (DragFloat3("Gravity", &gravity.x, 0.1, -100, 100)) {
      s.SetGravity(glmVecToJPH(gravity));
    }
    for (auto [e, id] : r.view<JPH::BodyID>().each()) {
      PushID(static_cast<int>(entt::to_integral(e)));
      if (TreeNode("Body", "Body %u", id.GetIndex()))
        bodyUI(b, id);
      PopID();
    }
    End();
  }

  // ponytail: position/rotation/scale left out on purpose - the Transform owns
  // them (see update()), edits here would be overwritten next frame.
  void bodyUI(JPH::BodyInterface &b, JPH::BodyID id) {
    using namespace ImGui;
    auto vec3 = [&](const char *label, JPH::Vec3 v, auto set) {
      float f[3] = {v.GetX(), v.GetY(), v.GetZ()};
      if (DragFloat3(label, f, 0.1f))
        set(JPH::Vec3(f[0], f[1], f[2]));
    };

    // Shape: unwrap the scale wrapper, syncShapeScale reapplies it next frame.
    {
      JPH::RefConst<JPH::Shape> shape = b.GetShape(id);
      if (shape->GetSubType() == JPH::EShapeSubType::Scaled)
        shape =
            static_cast<const JPH::ScaledShape *>(shape.GetPtr())->GetInnerShape();
      static constexpr JPH::EShapeSubType types[] = {
          JPH::EShapeSubType::Sphere, JPH::EShapeSubType::Box,
          JPH::EShapeSubType::Capsule, JPH::EShapeSubType::Cylinder};
      const auto sub = shape->GetSubType();
      if (BeginCombo("Shape", JPH::sSubShapeTypeNames[int(sub)])) {
        for (auto t : types) {
          if (Selectable(JPH::sSubShapeTypeNames[int(t)], t == sub) && t != sub) {
            // Size the new primitive to fit the old one's bounds.
            JPH::Vec3 h = shape->GetLocalBounds().GetExtent();
            float r = std::max(0.1f, std::max(h.GetX(), h.GetZ()));
            float hh = std::max(0.1f, h.GetY());
            JPH::Ref<JPH::Shape> s;
            switch (t) {
            case JPH::EShapeSubType::Sphere:
              s = new JPH::SphereShape(std::max(r, hh));
              break;
            case JPH::EShapeSubType::Box:
              s = new JPH::BoxShape(JPH::Vec3::sMax(h, JPH::Vec3::sReplicate(0.1f)));
              break;
            case JPH::EShapeSubType::Capsule:
              s = new JPH::CapsuleShape(hh, r);
              break;
            default:
              s = new JPH::CylinderShape(hh, r);
              break;
            }
            b.SetShape(id, s, true, JPH::EActivation::Activate);
          }
        }
        EndCombo();
      }
    }

    int layer = b.GetObjectLayer(id);
    static const char *layers[] = {"NON_MOVING", "MOVING"};
    if (Combo("Object Layer", &layer, layers, NUM_LAYERS))
      b.SetObjectLayer(id, JPH::ObjectLayer(layer));

    // Static bodies created without mAllowDynamicOrKinematic have no motion
    // properties, and Jolt asserts on switching them to anything else.
    bool canMove;
    {
      JPH::BodyLockRead lock(
          Core::get()->physicsManager.get()->system.GetBodyLockInterface(), id);
      canMove = lock.Succeeded() && lock.GetBody().CanBeKinematicOrDynamic();
    }
    int motion = int(b.GetMotionType(id));
    static const char *motions[] = {"Static", "Kinematic", "Dynamic"};
    BeginDisabled(!canMove);
    if (Combo("Motion Type", &motion, motions, 3))
      b.SetMotionType(id, JPH::EMotionType(motion), JPH::EActivation::Activate);
    EndDisabled();

    bool active = b.IsActive(id);
    if (Checkbox("Active", &active))
      active ? b.ActivateBody(id) : b.DeactivateBody(id);
    bool sensor = b.IsSensor(id);
    if (Checkbox("Sensor", &sensor))
      b.SetIsSensor(id, sensor);
    bool manifold = b.GetUseManifoldReduction(id);
    if (Checkbox("Manifold Reduction", &manifold))
      b.SetUseManifoldReduction(id, manifold);

    float friction = b.GetFriction(id);
    if (DragFloat("Friction", &friction, 0.01f, 0, 10))
      b.SetFriction(id, friction);
    float restitution = b.GetRestitution(id);
    if (DragFloat("Restitution", &restitution, 0.01f, 0, 1))
      b.SetRestitution(id, restitution);

    if (motion != int(JPH::EMotionType::Static)) {
      int quality = int(b.GetMotionQuality(id));
      static const char *qualities[] = {"Discrete", "LinearCast"};
      if (Combo("Motion Quality", &quality, qualities, 2))
        b.SetMotionQuality(id, JPH::EMotionQuality(quality));
      float gf = b.GetGravityFactor(id);
      if (DragFloat("Gravity Factor", &gf, 0.01f, -10, 10))
        b.SetGravityFactor(id, gf);
      float maxLin = b.GetMaxLinearVelocity(id);
      if (DragFloat("Max Linear Velocity", &maxLin, 1, 0, 10000))
        b.SetMaxLinearVelocity(id, maxLin);
      float maxAng = b.GetMaxAngularVelocity(id);
      if (DragFloat("Max Angular Velocity", &maxAng, 1, 0, 10000))
        b.SetMaxAngularVelocity(id, maxAng);
      vec3("Linear Velocity", b.GetLinearVelocity(id),
           [&](JPH::Vec3 v) { b.SetLinearVelocity(id, v); });
      vec3("Angular Velocity", b.GetAngularVelocity(id),
           [&](JPH::Vec3 v) { b.SetAngularVelocity(id, v); });
    }
    TreePop();
  }
};
