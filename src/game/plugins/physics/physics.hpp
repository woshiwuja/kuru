#pragma once
#include <Kuru.h>
#include <Jolt/Jolt.h>
#include "../transform/transform.hpp"
#include "../render/render.hpp"
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
#include <unordered_map>

using namespace KR;

inline void syncBodyToTransform(JPH::BodyInterface &bodies, JPH::BodyID id,
                                const Transform &t) {
  const JPH::RVec3 p(t.position.x, t.position.y, t.position.z);
  const glm::quat r =
      glm::normalize(t.rotation);
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

// Shape triangles in center-of-mass space, scale included: the entity drawing
// them only needs the body's COM transform. Null for shapes without triangles.
inline std::shared_ptr<Mesh> bodyWireMesh(const JPH::Shape &shape,
                                          glm::vec3 color) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // Only leaf shapes have triangles. Unwrap the ScaledShape syncShapeScale adds.
  const JPH::Shape *leaf = &shape;
  JPH::Vec3 scale = JPH::Vec3::sOne();
  if (shape.GetSubType() == JPH::EShapeSubType::Scaled) {
    const auto *scaled = static_cast<const JPH::ScaledShape *>(&shape);
    leaf = scaled->GetInnerShape();
    scale = scaled->GetScale();
  }
  // ponytail: compounds/other decorators get no wire; walk them with
  // CollectTransformedShapes if they ever show up.
  if (leaf->GetType() == JPH::EShapeType::Compound ||
      leaf->GetType() == JPH::EShapeType::Decorated)
    return nullptr;
  JPH::Shape::GetTrianglesContext ctx;
  leaf->GetTrianglesStart(ctx, JPH::AABox::sBiggest(), JPH::Vec3::sZero(),
                          JPH::Quat::sIdentity(), scale);
  constexpr int batch = JPH::Shape::cGetTrianglesMinTrianglesRequested;
  JPH::Float3 tris[3 * batch];
  while (int n = leaf->GetTrianglesNext(ctx, batch, tris)) {
    for (int i = 0; i < 3 * n; i++) {
      indices.push_back(static_cast<uint32_t>(vertices.size()));
      vertices.push_back(Vertex{.pos = {tris[i].x, tris[i].y, tris[i].z},
                                .color = color,
                                .texCoord = {0.0f, 0.0f},
                                .normal = {0.0f, 1.0f, 0.0f}});
    }
  }
  if (indices.empty())
    return nullptr;
  auto mesh = std::make_shared<Mesh>();
  mesh->upload(vertices, indices);
  return mesh;
}

struct PhysicsPlugin : public Plugin {
  PhysicsPlugin() { registerComponent<JPH::BodyCreationSettings>(); }

  glm::vec3 gravity{0, -9.81f, 0};

  bool drawBodies = false;
  glm::vec3 wireColor{0.2f, 1.0f, 0.3f};
  struct BodyWire {
    entt::entity entity = entt::null; // null: shape had no triangles
    JPH::RefConst<JPH::Shape> shape;
  };
  std::unordered_map<entt::entity, BodyWire> wires;
  void init(entt::registry &reg) override {
    auto &s = Core::get()->physicsManager.get()->system;
    auto &b = Core::get()->physicsManager.get()->bodies();
    s.SetGravity(glmVecToJPH(gravity));
  }

  // Spawning/despawning renderables only here: in update() the frame's
  // command buffer already references them (see NavigationPlugin::start).
  void start(entt::registry &reg) override {
    auto &bodies = Core::get()->physicsManager.get()->bodies();
    auto &render = renderer(reg);
    for (auto it = wires.begin(); it != wires.end();) {
      const auto *id =
          reg.valid(it->first) ? reg.try_get<JPH::BodyID>(it->first) : nullptr;
      if (drawBodies && id && bodies.GetShape(*id) == it->second.shape) {
        ++it;
        continue;
      }
      if (reg.valid(it->second.entity))
        render.despawn(reg, it->second.entity);
      it = wires.erase(it);
    }
    if (!drawBodies)
      return;
    for (auto [e, id] : reg.view<JPH::BodyID>().each()) {
      if (wires.contains(e))
        continue;
      BodyWire wire{.shape = bodies.GetShape(id)};
      // ponytail: one mesh per body, rebuilt on shape change. Many identical
      // bodies would want a per-shape cache.
      if (auto mesh = bodyWireMesh(*wire.shape, wireColor)) {
        wire.entity = reg.create();
        render.spawn(reg, wire.entity, std::move(mesh), getTexture(reg, ""),
                     {2.0f, 1.0f, 0.0f, 0.0f});
        reg.emplace<DebugMesh>(wire.entity);
        reg.emplace<DebugWire>(wire.entity);
      }
      wires.emplace(e, std::move(wire));
    }
  }

  void update(entt::registry &reg) override {
    auto *physics = Core::get()->physicsManager.get();
    auto &bodies = physics->bodies();

    for (auto [e, settings] : reg.view<JPH::BodyCreationSettings>().each()) {
      // Inspector "add" default-constructs these: no shape, and Jolt derefs
      // the null shape computing mass. Give it one; the inspector can swap it.
      if (!settings.GetShape())
        settings.SetShape(new JPH::SphereShape(1.0f));
      // The Transform owns position/rotation (see below), so the body starts
      // there, not wherever the settings say (origin for inspector "add").
      // Scale is applied after creation by syncShapeScale.
      if (const auto *t = reg.try_get<Transform>(e)) {
        const glm::quat r = glm::normalize(t->rotation);
        settings.mPosition = JPH::RVec3(t->position.x, t->position.y, t->position.z);
        settings.mRotation = JPH::Quat(r.x, r.y, r.z, r.w);
      }
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
    for (auto &[e, wire] : wires) {
      const auto *id = reg.valid(e) ? reg.try_get<JPH::BodyID>(e) : nullptr;
      auto *t = reg.valid(wire.entity) ? reg.try_get<Transform>(wire.entity)
                                       : nullptr;
      if (!id || !t)
        continue;
      const JPH::RVec3 p = bodies.GetCenterOfMassPosition(*id);
      const JPH::Quat q = bodies.GetRotation(*id);
      t->position = {p.GetX(), p.GetY(), p.GetZ()};
      t->rotation = glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
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
    Checkbox("Draw Bodies", &drawBodies);
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

      // Jolt shapes are immutable: an edit builds a replacement. 0.1 floor
      // keeps sizes above the default convex radius (0.05), which Jolt asserts on.
      auto size = [](const char *label, float &v) {
        return DragFloat(label, &v, 0.01f, 0.1f, 1000.0f, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp);
      };
      JPH::Ref<JPH::Shape> s;
      switch (sub) {
      case JPH::EShapeSubType::Sphere: {
        float r = static_cast<const JPH::SphereShape *>(shape.GetPtr())->GetRadius();
        if (size("Radius", r))
          s = new JPH::SphereShape(r);
        break;
      }
      case JPH::EShapeSubType::Box: {
        JPH::Vec3 h = static_cast<const JPH::BoxShape *>(shape.GetPtr())->GetHalfExtent();
        float f[3] = {h.GetX(), h.GetY(), h.GetZ()};
        if (DragFloat3("Half Extent", f, 0.01f, 0.1f, 1000.0f, "%.3f",
                       ImGuiSliderFlags_AlwaysClamp))
          s = new JPH::BoxShape(JPH::Vec3(f[0], f[1], f[2]));
        break;
      }
      case JPH::EShapeSubType::Capsule: {
        const auto *c = static_cast<const JPH::CapsuleShape *>(shape.GetPtr());
        float hh = c->GetHalfHeightOfCylinder(), r = c->GetRadius();
        if (size("Half Height", hh) | size("Radius", r))
          s = new JPH::CapsuleShape(hh, r);
        break;
      }
      case JPH::EShapeSubType::Cylinder: {
        const auto *c = static_cast<const JPH::CylinderShape *>(shape.GetPtr());
        float hh = c->GetHalfHeight(), r = c->GetRadius();
        if (size("Half Height", hh) | size("Radius", r))
          s = new JPH::CylinderShape(hh, r);
        break;
      }
      default:
        TextDisabled("(no editable parameters)");
        break;
      }
      // syncShapeScale re-wraps it with the Transform scale next frame.
      if (s)
        b.SetShape(id, s, true, JPH::EActivation::Activate);
    }

    int layer = b.GetObjectLayer(id);
    static const char *layers[] = {"NON_MOVING", "MOVING"};
    if (Combo("Object Layer", &layer, layers, NUM_LAYERS))
      b.SetObjectLayer(id, JPH::ObjectLayer(layer));

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
